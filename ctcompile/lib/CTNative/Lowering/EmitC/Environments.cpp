#include "Emitter.h"

namespace ctcompile::ctnative::lowering_detail {

void lowering::censusEnvironments(llvm::ArrayRef<ctjs::FuncOp> accepted) {
    llvm::StringMap<unsigned> callables;
    for (ctjs::FuncOp fn : accepted) {
        fn.getBody().walk([&](ctjs::CreateClosureOp made) {
            if (environmentTarget(made).empty()) { return; }
            const bool stored = made->hasAttr(kNativeStoredCallable);
            if (stored || hasConcreteCallableSignature(made)) {
                // Admission has already proved ownership and every invocation.
                // Choose the existing callable ABI only after its concrete
                // signature is known; other admitted signatures keep tuples.
                made->setAttr(kNativeStoredCallable, mlir::UnitAttr::get(context));
                callables.try_emplace(environmentTarget(made),
                                      static_cast<unsigned>(made.getUpvalues().size()));
                censusStoredCallable(made, !stored);
                return;
            }
            const auto name = "ctn_env_" + cIdentifier(environmentTarget(made));
            std::string definition = "namespace ctnative {\nusing " + name + " = std::tuple<";
            for (auto [i, captured] : llvm::enumerate(made.getUpvalues())) {
                if (i != 0) { definition += ", "; }
                const auto type = typeOf(captured);
                switch (carrierOf(type)) {
                case carrier::nullable:
                    needsNullable = true;
                    definition += kNullableType;
                    break;
                case carrier::number: definition += "double"; break;
                case carrier::boolean: definition += "bool"; break;
                case carrier::string:
                    needsString = true;
                    definition += "std::string";
                    break;
                case carrier::objectValue:
                    needsObjectValue = true;
                    definition += kObjectValueType;
                    break;
                case carrier::objectIdentity:
                    needsObjectIdentity = true;
                    definition += kObjectIdentityType;
                    break;
                case carrier::map: {
                    needsMap = true;
                    const auto map = llvm::cast<MapType>(type);
                    needsString |= mapNeedsString(map);
                    needsObjectValue |= mapNeedsObjectValues(map);
                    definition += llvm::cast<ec::OpaqueType>(mapCarrierType(map)).getValue();
                    break;
                }
                default: llvm::report_fatal_error("native environment has an unowned capture");
                }
            }
            environments.push_back(definition + ">;\n}\n");
        });
    }
    for (ctjs::FuncOp fn : accepted) {
        fn.getBody().walk([&](mlir::Operation * op) {
            if (op->hasAttr(kNativeEnvironmentRead) && callables.contains(environmentTarget(op))) {
                // Keep inference's capture operands until the invocation is
                // replaced. The existing stored-read sweep removes them then.
                op->setAttr(kNativeStoredRead, mlir::UnitAttr::get(context));
            }
            auto call = llvm::dyn_cast<ctjs::CallDirectOp>(op);
            if (!call) { return; }
            const auto found = callables.find(call.getCallee());
            if (found == callables.end()) { return; }
            const auto type = llvm::dyn_cast_or_null<ClosureType>(typeOf(call.getCalleeValue()));
            if (!type || type.getTarget() != call.getCallee()) { return; }
            call->setAttr(
                kNativeStoredCall,
                mlir::IntegerAttr::get(mlir::IntegerType::get(context, 32), found->second));
        });
    }
}

bool lowering::replaceEnvironment(mlir::Operation * op) {
    if (op->hasAttr(kNativeStoredRead)) { return true; }
    const auto target = environmentTarget(op);
    if (target.empty()) { return false; }
    mlir::OpBuilder at(op);
    mlir::Value result;
    if (auto made = llvm::dyn_cast<ctjs::CreateClosureOp>(op)) {
        const std::string builder = made->hasAttr(kNativeStoredCallable)
                                        ? "ctn_bind_" + cIdentifier(target)
                                        : "std::make_tuple";
        result = callWithConstValueOperands(at, op->getLoc(),
                                            mlir::TypeRange{made.getResult().getType()},
                                            at.getStringAttr(builder), made.getUpvalues())
                     .getResult(0);
    } else if (auto read = llvm::dyn_cast<ctjs::LoadUpvalueOp>(op);
               read && op->hasAttr(kNativeEnvironmentRead)) {
        const auto callee = "std::get<" + std::to_string(read.getIndex()) + ">";
        result = callWithConstValueOperands(
                     at, op->getLoc(), mlir::TypeRange{read.getResult().getType()},
                     at.getStringAttr(callee), mlir::ValueRange{read.getClosure()})
                     .getResult(0);
    } else {
        return false;
    }
    op->getResult(0).replaceAllUsesWith(result);
    eraseIfUnused(op);
    return true;
}

} // namespace ctcompile::ctnative::lowering_detail
