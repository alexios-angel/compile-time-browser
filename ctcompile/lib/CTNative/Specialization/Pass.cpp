#include "Candidates.h"

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
            auto target = mlir::SymbolTable::lookupNearestSymbolFrom<ctjs::FuncOp>(
                call, call.getCalleeAttr());
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
        // Complete all proofs against the unchanged call graph. Redirecting a
        // declaration's call cannot change another candidate's eligibility.
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
            // Preserve one actual generic call, including its argument types.
            // An unused original function would lose native inference evidence.
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
                specialized = llvm::cast<ctjs::FuncOp>(candidate.function->clone());
                // Never use incoming proof/provenance annotations to authorize
                // a clone or its later native representation.
                specialized.walk([](mlir::Operation * op) {
                    llvm::SmallVector<mlir::StringAttr> remove;
                    for (auto attr : op->getAttrs()) {
                        if (attr.getName().strref().starts_with("ctnative.")) {
                            remove.push_back(attr.getName());
                        }
                    }
                    for (auto name : remove) { op->removeAttr(name); }
                });
                specialized.setSymName((candidate.function.getSymName() + "__specialized").str());
                symbols.insert(specialized);
                auto & entry = specialized.getBody().front();
                mlir::OpBuilder at(&entry, entry.begin());
                llvm::SmallVector<mlir::NamedAttribute> bindings;
                for (auto [i, literal] : llvm::enumerate(candidate.arguments)) {
                    if (!literal) { continue; }
                    auto value = ctjs::ConstantOp::create(at, specialized.getLoc(), literal);
                    entry.getArgument(static_cast<unsigned>(i))
                        .replaceAllUsesWith(value.getResult());
                    bindings.push_back(attributes.getNamedAttr(std::to_string(i), literal));
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
