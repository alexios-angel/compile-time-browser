#include "../Admission/Admission.h"
#include "../EmitC/Emitter.h"

namespace ctcompile::ctnative::lowering_detail {
namespace {
std::string fieldName(llvm::StringRef name) {
    constexpr char hex[] = "0123456789abcdef";
    std::string out = "field_";
    for (unsigned char byte : name.bytes()) {
        out += hex[byte >> 4];
        out += hex[byte & 15];
    }
    return out;
}
} // namespace

void lowering::censusIdentityFields(llvm::ArrayRef<ctjs::FuncOp> accepted) {
    for (ctjs::FuncOp fn : accepted) {
        fn.getBody().walk([&](mlir::Operation * op) {
            if (nativeObjectFieldGroup(op) < 0) { return; }
            const auto name = fieldName(admission::keyOf(op->getOperand(1)));
            identityFields.insert(name);
            identityAccess[op] = name;
            needsNullable = true;
            needsObjectIdentity = true;
        });
    }
}

std::string lowering::identityDefinition() const {
    if (identityFields.empty()) {
        return "namespace ctnative {\n// ctcompile: proved property-free object identity\n"
               "struct identity_object {};\n}\n";
    }
    std::string out =
        "namespace ctnative {\n// ctcompile: owning identity with proved scalar fields\n"
        "struct identity_object {\n";
    for (const std::string & name : identityFields) {
        out += "    nullable_scalar " + name + ";\n";
    }
    return out + "};\n}\n";
}

std::string lowering::identityFieldHelpers() const {
    std::string out =
        "namespace ctnative {\n"
        "inline identity_object & object_fields(const std::shared_ptr<identity_object> & value) {\n"
        "    return *value;\n}\n";
    if (needsObjectValue) {
        out += "inline identity_object & object_fields(const object_value & value) {\n"
               "    return *value.object;\n}\n";
    }
    for (const std::string & name : identityFields) {
        out += "template <class Owner> inline nullable_scalar object_get_" + name +
               "(const Owner & value) {\n    return object_fields(value)." + name + ";\n}\n";
        out += "template <class Owner> inline void object_set_" + name +
               "(const Owner & value, nullable_scalar field) {\n    object_fields(value)." + name +
               " = field;\n}\n";
    }
    return out + "}\n";
}

bool lowering::replaceIdentityField(mlir::Operation * op) {
    const auto found = identityAccess.find(op);
    if (found == identityAccess.end()) { return false; }
    mlir::OpBuilder b(op);
    const mlir::Location where = op->getLoc();
    const auto storage = carrierType(context, carrier::nullable);
    if (auto get = llvm::dyn_cast<ctjs::GetPropertyOp>(op)) {
        auto loaded =
            callWithConstValueOperands(b, where, mlir::TypeRange{storage},
                                       b.getStringAttr("ctnative::object_get_" + found->second),
                                       mlir::ValueRange{get.getObject()});
        get.getResult().replaceAllUsesWith(
            convertScalar(b, where, loaded.getResult(0), get.getResult().getType()));
    } else {
        auto set = llvm::cast<ctjs::SetPropertyOp>(op);
        callWithConstValueOperands(
            b, where, mlir::TypeRange{}, b.getStringAttr("ctnative::object_set_" + found->second),
            mlir::ValueRange{set.getObject(), convertScalar(b, where, set.getValue(), storage)});
    }
    eraseIfUnused(op);
    return true;
}

} // namespace ctcompile::ctnative::lowering_detail
