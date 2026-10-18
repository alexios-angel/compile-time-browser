//===- TestClosureValues.cpp - Print the closures each call may enter -----===//
//
// The test-only pass ctjs-test-closure-values: it writes ClosureValues'
// answer for each call through a value onto the call, so lit tests can pin
// the analysis before a pass consumes it.
//
//===----------------------------------------------------------------------===//

#include "Globals/ClosureValues.h"
#include "ctcompile/CTJS/Transforms/Passes.h"

#include "mlir/IR/Builders.h"

namespace ctcompile::ctjs {

#define GEN_PASS_DEF_CTJSTESTCLOSUREVALUES
#include "ctcompile/CTJS/Transforms/Passes.h.inc"

namespace {

struct TestClosureValues final : impl::CTJSTestClosureValuesBase<TestClosureValues> {
    void runOnOperation() override {
        globals_detail::ClosureValues values(getOperation());
        mlir::Builder builder(&getContext());
        getOperation().walk([&](CallOp call) {
            if (!calleeGlobalName(call).empty()) { return; }
            const auto answer = values.closuresOf(globals_detail::ClosureValues::calledValue(call));
            if (!answer.unknown.empty()) {
                call->setAttr("ctjs.test_closures_unknown", builder.getStringAttr(answer.unknown));
                return;
            }
            llvm::SmallVector<llvm::StringRef> names;
            for (CreateClosureOp made : answer.closures) {
                FuncOp target = values.functionOf(made);
                names.push_back(target ? target.getSymName() : "?");
            }
            llvm::sort(names);
            call->setAttr("ctjs.test_closures", builder.getStrArrayAttr(names));
        });
    }
};

} // namespace
} // namespace ctcompile::ctjs
