#include "ctcompile/CTNative/Analysis/BindingTime.h"
#include "ctcompile/CTNative/Transforms/Passes.h"
#include "mlir/IR/Builders.h"

namespace ctcompile::ctnative {
#define GEN_PASS_DEF_CTNATIVEBINDINGTIMEANALYSIS
#include "ctcompile/CTNative/Transforms/Passes.h.inc"
namespace {
struct CTNativeBindingTimeAnalysisPass
    : impl::CTNativeBindingTimeAnalysisBase<CTNativeBindingTimeAnalysisPass> {
    using CTNativeBindingTimeAnalysisBase::CTNativeBindingTimeAnalysisBase;
    void runOnOperation() override {
        auto module = getOperation();
        module.walk([](mlir::Operation * op) {
            for (llvm::StringRef name :
                 {"ctnative.binding_time", "ctnative.binding_time_reason",
                  "ctnative.result_binding_times", "ctnative.argument_binding_times",
                  "ctnative.binding_time_summary"}) {
                op->removeAttr(name);
            }
        });
        BindingTimeAnalysis analysis(module);
        mlir::Builder at(&getContext());
        unsigned totalStatic = 0, totalDynamic = 0, staticArguments = 0;
        module.walk([&](ctjs::FuncOp fn) {
            llvm::SmallVector<mlir::Attribute> arguments;
            if (!fn.getBody().empty()) {
                for (mlir::Value arg : fn.getBody().front().getArguments()) {
                    const auto time = analysis.get(arg);
                    arguments.push_back(at.getStringAttr(bindingTimeName(time)));
                    staticArguments += time == BindingTime::Static;
                }
            }
            fn->setAttr("ctnative.argument_binding_times", at.getArrayAttr(arguments));
            unsigned statics = 0, dynamics = 0;
            fn.getBody().walk([&](mlir::Operation * op) {
                const bool known = analysis.isStatic(op);
                statics += known;
                dynamics += !known;
                op->setAttr("ctnative.binding_time",
                            at.getStringAttr(known ? "static" : "dynamic"));
                op->setAttr("ctnative.binding_time_reason", at.getStringAttr(analysis.reason(op)));
                llvm::SmallVector<mlir::Attribute> results;
                for (mlir::Value result : op->getResults()) {
                    results.push_back(at.getStringAttr(bindingTimeName(analysis.get(result))));
                }
                op->setAttr("ctnative.result_binding_times", at.getArrayAttr(results));
            });
            fn->setAttr("ctnative.binding_time_summary",
                        at.getDictionaryAttr(
                            {at.getNamedAttr("static_ops", at.getI64IntegerAttr(statics)),
                             at.getNamedAttr("dynamic_ops", at.getI64IntegerAttr(dynamics))}));
            totalStatic += statics;
            totalDynamic += dynamics;
        });
        module->setAttr(
            "ctnative.binding_time_summary",
            at.getDictionaryAttr(
                {at.getNamedAttr("static_ops", at.getI64IntegerAttr(totalStatic)),
                 at.getNamedAttr("dynamic_ops", at.getI64IntegerAttr(totalDynamic)),
                 at.getNamedAttr("static_arguments", at.getI64IntegerAttr(staticArguments))}));
        if (report) {
            module.emitRemark() << "binding-time analysis: " << totalStatic
                                << " static operation(s), " << totalDynamic
                                << " dynamic operation(s), " << staticArguments
                                << " static argument(s)";
        }
    }
};
} // namespace
} // namespace ctcompile::ctnative
