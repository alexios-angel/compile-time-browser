//===- Pass.cpp - Clone functions per constant arguments ------------------===//
//
// --ctnative-specialize: for each closed ctjs.func with at least two direct
// callers, clones the function once per distinct set of constant arguments
// seen at a call (every call except the first, which keeps the generic body
// and its type evidence alive) and redirects those calls to the clone. Stops
// at maxVariants clones or maxClonedOps cloned operations; calls past the
// budget keep the generic callee and get a ctnative.specialization_reason.
//
//===----------------------------------------------------------------------===//

#include "Candidates.h"

#include "ctcompile/CTNative/Analysis/HostContract.h"

#include "ctcompile/CTNative/Transforms/Passes.h"
#include "mlir/IR/Builders.h"
#include "mlir/IR/SymbolTable.h"
#include "llvm/ADT/MapVector.h"
#include "llvm/ADT/STLExtras.h"

namespace ctcompile::ctnative {
#define GEN_PASS_DEF_CTNATIVESPECIALIZE
#include "ctcompile/CTNative/Transforms/Passes.h.inc"
namespace {
struct CTNativeSpecializePass : impl::CTNativeSpecializeBase<CTNativeSpecializePass> {
    using CTNativeSpecializeBase::CTNativeSpecializeBase;
    void runOnOperation() override {
        auto module = getOperation();
        module.walk([](mlir::Operation * op) {
            op->removeAttr("ctnative.specialization_reason");
            op->removeAttr("ctnative.specialization_summary");
        });
        llvm::MapVector<mlir::Operation *, llvm::SmallVector<ctjs::CallDirectOp>> callers;
        module.walk([&](ctjs::CallDirectOp call) {
            auto target = call.getTarget();
            if (target) { callers[target].push_back(call); }
        });
        struct candidate {
            ctjs::FuncOp function;
            ctjs::CallDirectOp call;
            llvm::SmallVector<mlir::Attribute> arguments;
            unsigned operations;
        };
        llvm::SmallVector<candidate> candidates;
        mlir::Builder attributes(&getContext());
        // Complete all proofs against the unchanged call graph first, so that
        // redirecting one call cannot change another candidate's eligibility.
        for (auto & [operation, calls] : callers) {
            auto function = llvm::cast<ctjs::FuncOp>(operation);
            const auto problem = specialization::refusal(function, module);
            if (!problem.empty()) {
                function->setAttr("ctnative.specialization_reason",
                                  attributes.getStringAttr(problem));
                continue;
            }
            if (calls.size() < 2) { continue; }
            const unsigned operations = specialization::bodySize(function);
            // Keep the first call on the generic function, with its argument
            // types. An original function with no callers would lose the
            // evidence native type inference needs.
            for (ctjs::CallDirectOp call : llvm::drop_begin(calls)) {
                auto known = specialization::staticArguments(call, function);
                if (llvm::any_of(known, [](mlir::Attribute value) { return bool(value); })) {
                    candidates.push_back({function, call, std::move(known), operations});
                }
            }
        }
        struct variant {
            ctjs::FuncOp source;
            ctjs::FuncOp function;
            llvm::SmallVector<mlir::Attribute> arguments;
        };
        llvm::SmallVector<variant> variants;
        mlir::SymbolTable symbols(module);
        unsigned redirected = 0, clonedOps = 0, limited = 0;
        for (auto & candidate : candidates) {
            ctjs::FuncOp specialized;
            for (const auto & existing : variants) {
                if (existing.source == candidate.function &&
                    existing.arguments == candidate.arguments) {
                    specialized = existing.function;
                    break;
                }
            }
            if (!specialized) {
                const unsigned constants = static_cast<unsigned>(llvm::count_if(
                    candidate.arguments, [](mlir::Attribute value) { return bool(value); }));
                const uint64_t cost = uint64_t(candidate.operations) + constants;
                if (variants.size() >= maxVariants || uint64_t(clonedOps) + cost > maxClonedOps) {
                    candidate.call->setAttr(
                        "ctnative.specialization_reason",
                        attributes.getStringAttr("specialization budget exhausted"));
                    ++limited;
                    continue;
                }
                specialized =
                    specialization::cloneWithBindings(candidate.function, candidate.arguments);
                specialized.setSymName((candidate.function.getSymName() + "__specialized").str());
                symbols.insert(specialized);
                llvm::SmallVector<mlir::NamedAttribute> bindings;
                for (auto [i, literal] : llvm::enumerate(candidate.arguments)) {
                    if (literal) {
                        bindings.push_back(attributes.getNamedAttr(std::to_string(i), literal));
                    }
                }
                specialized->setAttr("ctnative.specialized_from",
                                     attributes.getStringAttr(candidate.function.getSymName()));
                specialized->setAttr("ctnative.specialized_arguments",
                                     attributes.getDictionaryAttr(bindings));
                variants.push_back({candidate.function, specialized, candidate.arguments});
                clonedOps += static_cast<unsigned>(cost);
            }
            candidate.call.setCalleeAttr(
                mlir::FlatSymbolRefAttr::get(specialized.getSymNameAttr()));
            ++redirected;
        }
        module->setAttr(
            "ctnative.specialization_summary",
            attributes.getDictionaryAttr(
                {attributes.getNamedAttr("variants", attributes.getI64IntegerAttr(
                                                         static_cast<int64_t>(variants.size()))),
                 attributes.getNamedAttr("redirected_calls",
                                         attributes.getI64IntegerAttr(redirected)),
                 attributes.getNamedAttr("cloned_ops", attributes.getI64IntegerAttr(clonedOps)),
                 attributes.getNamedAttr("limited_calls", attributes.getI64IntegerAttr(limited))}));
        if (report) {
            module.emitRemark() << "specialization: " << variants.size() << " variant(s), "
                                << redirected << " redirected call(s), " << clonedOps
                                << " cloned operation(s), " << limited << " budget refusal(s)";
        }
    }
};
} // namespace
} // namespace ctcompile::ctnative
