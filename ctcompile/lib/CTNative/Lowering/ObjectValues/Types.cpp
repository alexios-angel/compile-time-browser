#include "../LoweringSupport.h"

namespace ctcompile::ctnative::lowering_detail {
namespace {
bool alternatives(mlir::Type type, bool & object) {
    if (!type) { return false; }
    if (llvm::isa<ObjectIdentityType>(type)) {
        object = true;
        return true;
    }
    if (llvm::isa<BottomType, NumType, BoolType>(type)) { return true; }
    if (auto opt = llvm::dyn_cast<OptType>(type)) {
        return alternatives(opt.getElementType(), object);
    }
    if (auto variant = llvm::dyn_cast<VariantType>(type)) {
        return llvm::all_of(variant.getAlternatives(),
                            [&](mlir::Type item) { return alternatives(item, object); });
    }
    return false;
}

bool spelling(mlir::Type type, llvm::StringRef name) {
    if (!type) { return false; }
    if (auto lvalue = llvm::dyn_cast<ec::LValueType>(type)) { type = lvalue.getValueType(); }
    auto opaque = llvm::dyn_cast<ec::OpaqueType>(type);
    return opaque && opaque.getValue() == name;
}
} // namespace

bool isObjectValueType(mlir::Type type) {
    bool object = false;
    return alternatives(type, object) && object;
}

bool mapNeedsObjectValues(MapType type) {
    if (isObjectValueType(type.getValueType())) { return true; }
    auto nested = llvm::dyn_cast<MapType>(type.getValueType());
    return nested && mapNeedsObjectValues(nested);
}

bool isObjectCarrier(carrier value) {
    return value == carrier::objectIdentity || value == carrier::objectValue;
}

bool isObjectValueCarrier(mlir::Type type) {
    return spelling(type, kObjectValueType);
}
bool isIdentityCarrier(mlir::Type type) {
    return spelling(type, kObjectIdentityType);
}

bool onlyAbsent(mlir::Type type) {
    auto opt = llvm::dyn_cast_or_null<OptType>(type);
    return opt && llvm::isa<BottomType>(opt.getElementType());
}

bool identityOrAbsent(mlir::Type type) {
    if (llvm::isa_and_nonnull<ObjectIdentityType>(type) || onlyAbsent(type)) { return true; }
    auto opt = llvm::dyn_cast_or_null<OptType>(type);
    return opt && llvm::isa<ObjectIdentityType>(opt.getElementType());
}
} // namespace ctcompile::ctnative::lowering_detail
