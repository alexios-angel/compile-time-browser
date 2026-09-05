#include "Emitter.h"

namespace ctcompile::ctnative::lowering_detail {

void lowering::censusEnvironments(llvm::ArrayRef<ctjs::FuncOp> accepted) {
    for (ctjs::FuncOp fn : accepted) {
        fn.getBody().walk([&](ctjs::CreateClosureOp made) {
            if (environmentTarget(made).empty()) { return; }
            if (made->hasAttr(kNativeStoredCallable)) {
                censusStoredCallable(made);
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
                case carrier::map: {
                    needsMap = true;
                    const auto map = llvm::cast<MapType>(type);
                    needsString |= mapNeedsString(map);
                    definition += llvm::cast<ec::OpaqueType>(mapCarrierType(map)).getValue();
                    break;
                }
                default: llvm::report_fatal_error("native environment has an unowned capture");
                }
            }
            environments.push_back(definition + ">;\n}\n");
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
        result =
            ec::CallOpaqueOp::create(at, op->getLoc(), mlir::TypeRange{made.getResult().getType()},
                                     at.getStringAttr(builder), made.getUpvalues())
                .getResult(0);
    } else if (auto read = llvm::dyn_cast<ctjs::LoadUpvalueOp>(op);
               read && op->hasAttr(kNativeEnvironmentRead)) {
        const auto callee = "std::get<" + std::to_string(read.getIndex()) + ">";
        result =
            ec::CallOpaqueOp::create(at, op->getLoc(), mlir::TypeRange{read.getResult().getType()},
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
