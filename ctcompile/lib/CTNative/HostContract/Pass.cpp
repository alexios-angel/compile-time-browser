#include "ctcompile/CTNative/Analysis/HostContract.h"
#include "ctcompile/CTNative/Transforms/Passes.h"

#include "mlir/IR/Builders.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/FormatVariadic.h"
#include "llvm/Support/JSON.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/raw_ostream.h"

namespace ctcompile::ctnative {
#define GEN_PASS_DEF_CTNATIVEHOSTCONTRACT
#include "ctcompile/CTNative/Transforms/Passes.h.inc"
namespace {

struct CTNativeHostContractPass : impl::CTNativeHostContractBase<CTNativeHostContractPass> {
    using CTNativeHostContractBase::CTNativeHostContractBase;

    void runOnOperation() override {
        auto module = getOperation();
        clearHostContractReports(module);
        const std::string digest = hostContractFingerprint(module);
        if (fingerprint) {
            mlir::emitRemark(module.getLoc()) << "host-contract fingerprint: " << digest;
            return;
        }
        if (manifest.empty()) {
            mlir::emitError(module.getLoc())
                << "host contract requires an explicit driver manifest";
            signalPassFailure();
            return;
        }
        auto buffer = llvm::MemoryBuffer::getFile(manifest);
        if (!buffer) {
            mlir::emitError(module.getLoc())
                << "cannot read host contract: " << buffer.getError().message();
            signalPassFailure();
            return;
        }
        auto contract = parseHostContract((*buffer)->getBuffer());
        if (!contract) {
            mlir::emitError(module.getLoc()) << llvm::toString(contract.takeError());
            signalPassFailure();
            return;
        }
        HostContractAnalysis analysis(module, *contract, maxSteps);
        mlir::Builder builder(&getContext());
        llvm::SmallVector<mlir::Attribute> slots;
        llvm::json::Array jsonSlots;
        unsigned roots = 0, writes = 0, reads = 0, edges = 0;
        for (const HostSlotReport & slot : analysis.slots()) {
            roots += static_cast<bool>(slot.owner);
            writes += static_cast<unsigned>(slot.writes.size());
            reads += static_cast<unsigned>(slot.reads.size());
            edges += static_cast<unsigned>(slot.edges.size());
            const auto count = [&](std::size_t value) {
                return builder.getI64IntegerAttr(static_cast<std::int64_t>(value));
            };
            slots.push_back(builder.getDictionaryAttr(
                {builder.getNamedAttr("binding", builder.getStringAttr(slot.binding)),
                 builder.getNamedAttr("property", builder.getStringAttr(slot.property)),
                 builder.getNamedAttr("fresh_source_root",
                                      builder.getBoolAttr(static_cast<bool>(slot.owner))),
                 builder.getNamedAttr("source_writes", count(slot.writes.size())),
                 builder.getNamedAttr("source_reads", count(slot.reads.size())),
                 builder.getNamedAttr("candidate_edges", count(slot.edges.size())),
                 builder.getNamedAttr("proved_edges",
                                      count(analysis.proved() ? slot.edges.size() : 0)),
                 builder.getNamedAttr("reason", builder.getStringAttr(slot.reason))}));
            jsonSlots.push_back(llvm::json::Object{
                {"binding", slot.binding},
                {"property", slot.property},
                {"fresh_source_root", static_cast<bool>(slot.owner)},
                {"source_writes", static_cast<std::int64_t>(slot.writes.size())},
                {"source_reads", static_cast<std::int64_t>(slot.reads.size())},
                {"candidate_edges", static_cast<std::int64_t>(slot.edges.size())},
                {"proved_edges",
                 static_cast<std::int64_t>(analysis.proved() ? slot.edges.size() : 0)},
                {"reason", slot.reason}});
        }
        module->setAttr("ctnative.host_proved", builder.getBoolAttr(analysis.proved()));
        module->setAttr("ctnative.host_reason", builder.getStringAttr(analysis.reason()));
        module->setAttr("ctnative.host_slots", builder.getArrayAttr(slots));
        module->setAttr("ctnative.host_fingerprint", builder.getStringAttr(digest));
        if (!output.empty()) {
            std::error_code error;
            llvm::raw_fd_ostream stream(output, error, llvm::sys::fs::OF_Text);
            if (error) {
                mlir::emitError(module.getLoc()) << "cannot write host report: " << error.message();
                signalPassFailure();
                return;
            }
            llvm::json::Object document{
                {"provider", "closed-source-v1"},
                {"module_sha256", digest},
                {"entry", contract->entry},
                {"proved", analysis.proved()},
                {"reason", analysis.reason().str()},
                {"observation_stores", static_cast<std::int64_t>(analysis.observations().size())},
                {"slots", std::move(jsonSlots)}};
            stream << llvm::formatv("{0:2}\n", llvm::json::Value(std::move(document)));
        }
        if (report) {
            mlir::emitRemark(module.getLoc())
                << "host-contract: " << (analysis.proved() ? "proved" : "refused") << "; " << roots
                << " fresh source root(s), " << writes << " slot write(s), " << reads
                << " slot read(s), " << edges << " candidate flow edge(s)"
                << (analysis.proved() ? "" : "; ") << analysis.reason();
        }
        if (requireProof && !analysis.proved()) {
            mlir::emitError(module.getLoc()) << "host contract refused: " << analysis.reason();
            signalPassFailure();
        }
    }
};

} // namespace
} // namespace ctcompile::ctnative
