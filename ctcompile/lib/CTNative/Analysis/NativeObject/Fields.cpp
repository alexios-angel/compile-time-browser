#include "Fields.h"
#include "ctcompile/CTNative/Analysis/NativeObjectIdentity.h"
#include "llvm/ADT/StringSwitch.h"

namespace ctcompile::ctnative {

int64_t nativeObjectFieldGroup(mlir::Operation * op) {
    auto group = op->getAttrOfType<mlir::IntegerAttr>(kNativeObjectFieldGroup);
    return group ? group.getInt() : -1;
}

namespace object_detail {
namespace {
bool ordinaryKey(mlir::Value value) {
    auto constant = value.getDefiningOp<ctjs::ConstantOp>();
    auto key =
        constant ? llvm::dyn_cast<ctjs::StringAttr>(constant.getValue()) : ctjs::StringAttr{};
    if (!key) { return false; }
    // Missing ordinary fields are undefined only while the prototype world is
    // closed. These inherited names and the prototype setter are not fields.
    return !llvm::StringSwitch<bool>(key.getValue())
                .Cases({"__proto__", "prototype", "constructor"}, true)
                .Cases({"toString", "valueOf", "toLocaleString"}, true)
                .Cases({"hasOwnProperty", "isPrototypeOf", "propertyIsEnumerable"}, true)
                .Cases({"__defineGetter__", "__defineSetter__"}, true)
                .Cases({"__lookupGetter__", "__lookupSetter__"}, true)
                .Default(false);
}
} // namespace

bool scalarFieldEnvironment(mlir::ModuleOp module) {
    bool safe = true;
    // Native Map recognition already excludes unknown calls, constructors,
    // globals and module imports. Reject the direct prototype/accessor routes
    // too, including writes to an otherwise unrelated object's __proto__.
    module.walk([&](mlir::Operation * op) {
        if (llvm::isa<ctjs::GetProtoOp, ctjs::SetProtoOp, ctjs::DefineAccessorOp,
                      ctjs::DeleteNamedOp, ctjs::DeletePropertyOp, ctjs::LoadHomeOp,
                      ctjs::OwnKeysOp, ctjs::IterableOp, ctjs::WrapPromiseOp, ctjs::SuspendOp,
                      ctjs::ThrowOp, ctjs::ResumeThrowOp>(op)) {
            safe = false;
        }
        if (auto get = llvm::dyn_cast<ctjs::GetPropertyOp>(op)) {
            if (!ordinaryKey(get.getKey())) { safe = false; }
        }
        if (auto set = llvm::dyn_cast<ctjs::SetPropertyOp>(op)) {
            if (!ordinaryKey(set.getKey())) { safe = false; }
        }
    });
    return safe;
}

bool scalarFieldUse(mlir::OpOperand & use) {
    if (use.getOperandNumber() != 0) { return false; }
    if (auto get = llvm::dyn_cast<ctjs::GetPropertyOp>(use.getOwner())) {
        return ordinaryKey(get.getKey());
    }
    if (auto set = llvm::dyn_cast<ctjs::SetPropertyOp>(use.getOwner())) {
        return ordinaryKey(set.getKey());
    }
    return false;
}

} // namespace object_detail
} // namespace ctcompile::ctnative
