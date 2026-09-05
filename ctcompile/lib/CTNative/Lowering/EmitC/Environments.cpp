#include "Emitter.h"

namespace ctcompile::ctnative::lowering_detail {

void lowering::censusEnvironments(llvm::ArrayRef<ctjs::FuncOp> accepted) {
    for (ctjs::FuncOp fn : accepted) {
        fn.getBody().walk([&](ctjs::CreateClosureOp made) {
            if (environmentTarget(made).empty()) { return; }
            const auto name =
                llvm::cast<ec::OpaqueType>(
                    closureCarrierType(ClosureType::get(context, environmentTarget(made))))
                    .getValue();
            std::string definition = "using " + name.str() + " = std::tuple<";
            for (auto [i, captured] : llvm::enumerate(made.getUpvalues())) {
                if (i != 0) { definition += ", "; }
                const auto type = typeOf(captured);
                switch (carrierOf(type)) {
                case carrier::number: definition += "double"; break;
                case carrier::boolean: definition += "bool"; break;
                case carrier::string:
                    needsString = true;
                    definition += "std::string";
                    break;
                case carrier::map: {
                    needsMap = true;
                    const auto map = llvm::cast<MapType>(type);
                    needsString |= llvm::isa<StrType>(map.getKeyType());
                    definition += llvm::cast<ec::OpaqueType>(mapCarrierType(map)).getValue();
                    break;
                }
                default: llvm::report_fatal_error("native environment has an unowned capture");
                }
            }
            environments.push_back(definition + ">;\n");
        });
    }
}

bool lowering::replaceEnvironment(mlir::Operation * op) {
    const auto target = environmentTarget(op);
    if (target.empty()) { return false; }
    mlir::OpBuilder at(op);
    mlir::Value result;
    if (auto made = llvm::dyn_cast<ctjs::CreateClosureOp>(op)) {
        result =
            ec::CallOpaqueOp::create(at, op->getLoc(), mlir::TypeRange{made.getResult().getType()},
                                     at.getStringAttr("std::make_tuple"), made.getUpvalues())
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
