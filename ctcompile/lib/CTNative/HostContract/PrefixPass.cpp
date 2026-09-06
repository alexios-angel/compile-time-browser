#include "ctcompile/CTNative/Analysis/HostPrefix.h"
#include "ctcompile/CTNative/Transforms/Passes.h"

#include "mlir/IR/Builders.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/FormatVariadic.h"
#include "llvm/Support/JSON.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/raw_ostream.h"

namespace ctcompile::ctnative {
#define GEN_PASS_DEF_CTNATIVESPECIALIZEHOSTPREFIX
#include "ctcompile/CTNative/Transforms/Passes.h.inc"
namespace {

bool selectable(HostPrefixBranch proof) {
    auto & region = proof.operation->getRegion(proof.selected ? 0u : 1u);
    if (region.empty()) { return proof.operation.getNumResults() == 0; }
    if (!llvm::hasSingleElement(region) || region.front().getNumArguments() != 0) { return false; }
    auto yield = llvm::dyn_cast<mlir::scf::YieldOp>(region.front().getTerminator());
    return yield && yield.getNumOperands() == proof.operation.getNumResults();
}

void select(HostPrefixBranch proof) {
    auto branch = proof.operation;
    auto & region = branch->getRegion(proof.selected ? 0u : 1u);
    if (!region.empty()) {
        auto yield = llvm::cast<mlir::scf::YieldOp>(region.front().getTerminator());
        for (auto [result, value] : llvm::zip(branch.getResults(), yield.getOperands())) {
            result.replaceAllUsesWith(value);
        }
        for (mlir::Operation & operation :
             llvm::make_early_inc_range(region.front().without_terminator())) {
            operation.moveBefore(branch);
        }
    }
    branch.erase();
}

struct CTNativeSpecializeHostPrefixPass
    : impl::CTNativeSpecializeHostPrefixBase<CTNativeSpecializeHostPrefixPass> {
    using CTNativeSpecializeHostPrefixBase::CTNativeSpecializeHostPrefixBase;

    void runOnOperation() override {
        auto module = getOperation();
        clearHostContractReports(module);
        auto buffer = llvm::MemoryBuffer::getFile(manifest);
        if (!buffer) {
            mlir::emitError(module.getLoc())
                << "cannot read host prefix contract: " << buffer.getError().message();
            signalPassFailure();
            return;
        }
        auto contract = parseHostContract((*buffer)->getBuffer());
        if (!contract) {
            mlir::emitError(module.getLoc()) << llvm::toString(contract.takeError());
            signalPassFailure();
            return;
        }
        HostEntryPrefixAnalysis analysis(module, *contract, maxSteps, followPublication);
        // Snapshot a checked plan before making the first semantic mutation.
        // No analysis query runs against partially rewritten IR.
        std::vector<HostPrefixBranch> branches(analysis.branches().begin(),
                                               analysis.branches().end());
        std::vector<HostPrefixCall> calls(analysis.calls().begin(), analysis.calls().end());
        if (!output.empty()) {
            std::error_code error;
            llvm::raw_fd_ostream stream(output, error, llvm::sys::fs::OF_Text);
            if (error) {
                mlir::emitError(module.getLoc())
                    << "cannot write host prefix report: " << error.message();
                signalPassFailure();
                return;
            }
            llvm::json::Array targets;
            for (auto proof : calls) { targets.push_back(proof.target.getSymName().str()); }
            llvm::json::Array factories, publications;
            std::int64_t resourceCount = 0, captureCount = 0;
            for (const auto & proof : analysis.factories()) {
                llvm::json::Array captures;
                llvm::SmallVector<ctjs::CreateCellOp> cells;
                for (auto edge : proof.captures) {
                    if (!llvm::is_contained(cells, edge.cell)) { cells.push_back(edge.cell); }
                    captures.push_back(llvm::json::Object{
                        {"property", edge.property},
                        {"closure_function", edge.closure.getFunction()},
                        {"capture_index", static_cast<std::int64_t>(edge.index)},
                        {"cell",
                         static_cast<std::int64_t>(llvm::find(cells, edge.cell) - cells.begin())},
                        {"resource",
                         static_cast<std::int64_t>(llvm::find(proof.resources, edge.resource) -
                                                   proof.resources.begin())}});
                }
                resourceCount += static_cast<std::int64_t>(proof.resources.size());
                captureCount += static_cast<std::int64_t>(proof.captures.size());
                auto target = proof.target;
                factories.push_back(llvm::json::Object{
                    {"target", target.getSymName().str()},
                    {"resource_allocations", static_cast<std::int64_t>(proof.resources.size())},
                    {"captures", std::move(captures)}});
            }
            for (const auto & proof : analysis.publications()) {
                const auto factory =
                    llvm::find_if(analysis.factories(), [&](const auto & candidate) {
                        return candidate.operation == proof.factory;
                    });
                auto target = factory->target;
                publications.push_back(
                    llvm::json::Object{{"binding", proof.binding},
                                       {"property", proof.property},
                                       {"factory_target", target.getSymName().str()}});
            }
            llvm::json::Object document{
                {"valid", analysis.valid()},
                {"reason", analysis.reason().str()},
                {"boundary", analysis.boundary().str()},
                {"source_module_sha256", contract->moduleSha256},
                {"selected_branches", static_cast<std::int64_t>(branches.size())},
                {"resolved_calls", static_cast<std::int64_t>(calls.size())},
                {"targets", std::move(targets)},
                {"summarized_factories", static_cast<std::int64_t>(analysis.factories().size())},
                {"runtime_provider_allocations", resourceCount},
                {"capture_edges", captureCount},
                {"publication_writes", static_cast<std::int64_t>(analysis.publications().size())},
                {"factories", std::move(factories)},
                {"publications", std::move(publications)},
                {"full_host_contract_claimed", false}};
            stream << llvm::formatv("{0:2}\n", llvm::json::Value(std::move(document)));
        }
        if (!analysis.valid()) {
            mlir::emitError(module.getLoc()) << "host prefix refused: " << analysis.reason();
            signalPassFailure();
            return;
        }
        if (!llvm::all_of(branches, selectable)) {
            mlir::emitError(module.getLoc()) << "host prefix selected an unsupported region shape";
            signalPassFailure();
            return;
        }
        // Splice only the proved arms. Generic SCF canonicalization can form
        // arith.select with !ctjs.value operands, which LLVM's scalar folder
        // does not support. Selected nested operations retain their identity.
        for (auto proof : branches) { select(proof); }
        for (auto proof : calls) {
            auto call = proof.operation;
            mlir::OpBuilder at(call);
            auto undefined = ctjs::ConstantOp::create(at, call.getLoc(), call.getType(),
                                                      ctjs::UndefinedAttr::get(&getContext()));
            auto direct = ctjs::CallDirectOp::create(
                at, call.getLoc(), call.getType(),
                mlir::FlatSymbolRefAttr::get(proof.target.getSymNameAttr()), call.getReceiver(),
                undefined, call.getCallee(), call.getArgs(), nullptr, nullptr);
            call.getResult().replaceAllUsesWith(direct.getResult());
            call.erase();
        }
        if (report) {
            mlir::emitRemark(module.getLoc())
                << "host-prefix: " << branches.size() << " selected branch(es), " << calls.size()
                << " resolved call(s); " << analysis.boundary();
        }
    }
};

} // namespace
} // namespace ctcompile::ctnative
