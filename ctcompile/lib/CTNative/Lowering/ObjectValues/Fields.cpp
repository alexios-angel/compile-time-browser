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

llvm::StringMap<mlir::Type> identityFieldStoreTypes(mlir::DataFlowSolver & solver,
                                                    llvm::ArrayRef<ctjs::FuncOp> functions) {
    llvm::StringMap<mlir::Type> types;
    for (ctjs::FuncOp fn : functions) {
        fn.getBody().walk([&](mlir::Operation * op) {
            if (nativeObjectFieldGroup(op) < 0) { return; }
            const auto absent = OptType::get(op->getContext(), BottomType::get(op->getContext()));
            auto [position, inserted] =
                types.try_emplace(admission::keyOf(op->getOperand(1)), absent);
            (void)inserted;
            auto store = llvm::dyn_cast<ctjs::SetPropertyOp>(op);
            if (!store) { return; }
            const auto * lattice = solver.lookupState<TypeLattice>(store.getValue());
            const auto type = lattice ? lattice->getValue().getType() : mlir::Type{};
            // Missing/pending source information is never a storage choice.
            const auto actual = type ? type : BoxedType::get(store.getContext());
            position->second = meet(position->second, actual);
        });
    }
    return types;
}

void lowering::censusIdentityFields(llvm::ArrayRef<ctjs::FuncOp> accepted) {
    const auto types = identityFieldStoreTypes(solver, accepted);
    for (const auto & field : types) {
        const bool strings = isStringCarrier(carrierOf(field.second));
        identityFieldTypes[fieldName(field.first())] =
            carrierType(context, strings ? carrier::nullableString : carrier::nullable);
        needsNullableString |= strings;
    }
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
        const auto type = isNullableStringCarrier(identityFieldTypes.lookup(name))
                              ? "nullable_string"
                              : "nullable_scalar";
        out += "    " + std::string(type) + " " + name + ";\n";
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
    if (needsNullableString) {
        out += "inline nullable_scalar object_absent_field(const nullable_string & value) {\n"
               "    if (value.tag == nullable_string::kind::undefined) { return {}; }\n"
               "    if (value.tag == nullable_string::kind::null_value) {\n"
               "        return nullable_scalar::null();\n"
               "    }\n"
               "    std::terminate();\n}\n";
    }
    for (const std::string & name : identityFields) {
        const std::string type = isNullableStringCarrier(identityFieldTypes.lookup(name))
                                     ? "nullable_string"
                                     : "nullable_scalar";
        out += "template <class Owner> inline " + type + " object_get_" + name +
               "(const Owner & value) {\n    return object_fields(value)." + name + ";\n}\n";
        out += "template <class Owner> inline void object_set_" + name + "(const Owner & value, " +
               type + " field) {\n    object_fields(value)." + name + " = field;\n}\n";
    }
    return out + "}\n";
}

bool lowering::replaceIdentityField(mlir::Operation * op) {
    const auto found = identityAccess.find(op);
    if (found == identityAccess.end()) { return false; }
    mlir::OpBuilder b(op);
    const mlir::Location where = op->getLoc();
    const auto storage = identityFieldTypes.lookup(found->second);
    if (auto get = llvm::dyn_cast<ctjs::GetPropertyOp>(op)) {
        auto loaded =
            callWithConstValueOperands(b, where, mlir::TypeRange{storage},
                                       b.getStringAttr("ctnative::object_get_" + found->second),
                                       mlir::ValueRange{get.getObject()});
        const auto target = get.getResult().getType();
        mlir::Value value = loaded.getResult(0);
        // Shared member storage can be wider than this allocation's field.
        // Narrow with exact tag checks, never JavaScript string coercion.
        if (isNullableStringCarrier(storage) && target != storage) {
            const auto helper = isNullableCarrier(target) ? "ctnative::object_absent_field"
                                                          : "ctnative::global_string";
            value = callWithConstValueOperands(b, where, mlir::TypeRange{target},
                                               b.getStringAttr(helper), mlir::ValueRange{value})
                        .getResult(0);
        } else {
            value = convertScalar(b, where, value, target);
        }
        get.getResult().replaceAllUsesWith(value);
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
