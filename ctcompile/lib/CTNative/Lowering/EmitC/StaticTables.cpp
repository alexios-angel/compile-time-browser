//===- StaticTables.cpp - Constant tables of run-once statics -------------===//
//
// Lowers a run-once static's constant table (Analysis/Inference/StaticTables.cpp)
// to one namespace-scope `const` struct:
//
//     struct static_nt_table_t : ctnative::static_table {
//         ctnative::js_num interval;
//         ctnative::js_string pause;
//     };
//     inline const static_nt_table_t static_nt_table{{}, ctnative::js_num{5000.0},
//                                                    ctnative::js_string("hover", 5)};
//
// The static variable is a `ctnative::static_table_ptr`, null until the
// run-once function stores `&static_nt_table` where it built the literal, and
// a field read is `ctnative::table_field<&static_nt_table_t::interval>(p)`.
//
//===----------------------------------------------------------------------===//
#include "ctcompile/CTNative/Analysis/StaticTables.h"
#include "../Admission/Admission.h"
#include "Emitter.h"
#include "ctcompile/Support/CppLiterals.hpp"

#include "llvm/ADT/StringExtras.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/raw_ostream.h"

namespace ctcompile::ctnative::lowering_detail {
namespace {

/// The struct type of the table held by static `name`.
std::string staticTableStruct(llvm::StringRef name) {
    return (name + "_table_t").str();
}

/// The C++ member for property `key`: the key itself when it is a plain C++
/// identifier, else `field_` and its hex bytes, as ObjectValues/Fields.cpp
/// spells identity fields. A key that starts with `field_` is hex-encoded too,
/// so two keys never share a member.
std::string tableMember(llvm::StringRef key) {
    if (admission::isCIdentifier(key) && !admission::isReservedInCpp(key) &&
        !key.starts_with("field_")) {
        return key.str();
    }
    return "field_" + llvm::toHex(key, /*LowerCase=*/true);
}

/// The C++ type of a field holding `constant`; the carrier its read gets.
llvm::StringRef memberType(mlir::Attribute constant) {
    if (llvm::isa<ctjs::NumberAttr>(constant)) { return kNumberType; }
    if (llvm::isa<ctjs::BooleanAttr>(constant)) { return kBooleanType; }
    if (llvm::isa<ctjs::StringAttr>(constant)) { return kStringType; }
    return kNullableType;
}

/// The C++ expression for `constant` as a value of memberType(constant).
std::string initializer(mlir::Attribute constant) {
    if (auto number = llvm::dyn_cast<ctjs::NumberAttr>(constant)) {
        // The proof admits finite numbers only. %.17g round-trips a double;
        // the `.0` keeps an integral one a double literal.
        std::string text;
        llvm::raw_string_ostream(text) << llvm::format("%.17g", number.getDouble());
        if (text.find_first_of(".e") == std::string::npos) { text += ".0"; }
        return "ctnative::js_num{" + text + "}";
    }
    if (auto boolean = llvm::dyn_cast<ctjs::BooleanAttr>(constant)) {
        return boolean.getValue() ? "ctnative::js_boolean_t{true}"
                                  : "ctnative::js_boolean_t{false}";
    }
    if (auto string = llvm::dyn_cast<ctjs::StringAttr>(constant)) {
        const llvm::StringRef value = string.getValue();
        return "ctnative::js_string(" +
               cpp::c_string_literal(std::string_view(value.data(), value.size())) + ", " +
               std::to_string(value.size()) + ")";
    }
    return "ctnative::nullable_scalar::null()";
}

/// The constant `value` holds; the proof made sure it is one.
mlir::Attribute constantOf(mlir::Value value) {
    return value.getDefiningOp<ctjs::ConstantOp>().getValue();
}

} // namespace

void lowering::censusStaticTables(llvm::ArrayRef<ctjs::FuncOp> accepted) {
    // A lowered read's key, while it is still a ctjs.constant.
    for (ctjs::FuncOp fn : accepted) {
        fn.walk([&](ctjs::GetPropertyOp read) {
            if (read->hasAttr(kNativeStaticTable)) {
                staticTableReads[read] = tableMember(ctjs::constantKey(read.getKey()));
                // The key itself, which a read of an unset table's error names.
                read->setAttr(
                    "ctnative.static_table_key",
                    mlir::StringAttr::get(read.getContext(), ctjs::constantKey(read.getKey())));
            }
        });
    }
    llvm::StringSet<> enumeratedTables;
    for (ctjs::FuncOp fn : accepted) {
        fn.walk([&](ctjs::CallOp call) {
            if (auto table = call->getAttrOfType<mlir::StringAttr>(kNativeStaticTableEnumeration)) {
                enumeratedTables.insert(table.getValue());
            }
        });
    }
    module.walk([&](ctjs::CreateObjectOp literal) {
        auto name = literal->getAttrOfType<mlir::StringAttr>(kNativeStaticTable);
        if (!name) { return; }
        // Members in the order the literal defines them, which is the order
        // the proof required before the store.
        llvm::SmallVector<ctjs::SetPropertyOp> stores;
        for (mlir::Operation * user : literal.getResult().getUsers()) {
            if (auto store = llvm::dyn_cast<ctjs::SetPropertyOp>(user)) { stores.push_back(store); }
        }
        llvm::sort(stores, [](ctjs::SetPropertyOp a, ctjs::SetPropertyOp b) {
            return a->isBeforeInBlock(b);
        });
        const std::string type = staticTableStruct(name.getValue());
        std::string members, values, keys, listed;
        for (ctjs::SetPropertyOp store : stores) {
            const llvm::StringRef key = ctjs::constantKey(store.getKey());
            std::string spelled, value;
            if (auto array = store.getValue().getDefiningOp<ctjs::CreateArrayOp>()) {
                // The literal's own elements, then its appends in program order.
                llvm::SmallVector<ctjs::AppendOp> appends;
                for (mlir::Operation * user : array.getResult().getUsers()) {
                    if (auto append = llvm::dyn_cast<ctjs::AppendOp>(user)) {
                        appends.push_back(append);
                    }
                }
                llvm::sort(appends, [](ctjs::AppendOp a, ctjs::AppendOp b) {
                    return a->isBeforeInBlock(b);
                });
                llvm::SmallVector<mlir::Value> elements(array.getElements());
                for (ctjs::AppendOp append : appends) { elements.push_back(append.getElement()); }
                spelled = ("std::array<" + memberType(constantOf(elements.front())) + ", " +
                           llvm::Twine(elements.size()) + ">")
                              .str();
                value = spelled + "{";
                for (auto [index, element] : llvm::enumerate(elements)) {
                    value += (index ? ", " : "") + initializer(constantOf(element));
                }
                value += "}";
            } else {
                spelled = memberType(constantOf(store.getValue())).str();
                value = initializer(constantOf(store.getValue()));
            }
            members += "    " + spelled + " " + tableMember(key) + ";\n";
            values += ", " + value;
            keys += (keys.empty() ? "" : ", ") +
                    cpp::c_string_literal(std::string_view(key.data(), key.size()));
            listed += (listed.empty() ? "" : ", ") + tableMember(key);
        }
        // An enumerated table (all Strings, proved by the table proof) names
        // its keys and values for Runtime/Object.hpp's object_table.
        if (enumeratedTables.contains(name.getValue())) {
            members += "    using value_type = " + kStringType.str() +
                       ";\n    static constexpr std::array<std::string_view, " +
                       std::to_string(stores.size()) + "> keys{" + keys +
                       "};\n    std::array<value_type, " + std::to_string(stores.size()) +
                       "> values() const { return {" + listed + "}; }\n";
        }
        staticTables[name.getValue().str()] = "struct " + type + " : ctnative::static_table {\n" +
                                              members + "};\ninline const " + type + " " +
                                              name.getValue().str() + "_table{{}" + values + "};\n";
    });
}

bool lowering::replaceStaticTable(mlir::Operation * o) {
    // Object.entries, keys or values of a proved table: object_table's
    // member of the same name, over the table's struct.
    if (auto call = llvm::dyn_cast<ctjs::CallOp>(o)) {
        auto table = o->getAttrOfType<mlir::StringAttr>(kNativeStaticTableEnumeration);
        if (!table) { return false; }
        mlir::OpBuilder at(o);
        const std::string callee = "ctnative::object_table<" + staticTableStruct(table.getValue()) +
                                   ">::" + ctjs::stringAttrOf(o, kNativeStaticTableMethod).str();
        // The answer is an element vector, which lands in a local that owns
        // it, as a built-in static's returned array does (EmitC/Vectors.cpp).
        mlir::Type type = call.getResult().getType();
        auto owner = llvm::dyn_cast<ec::LValueType>(type);
        mlir::Value value = callValue(at, o->getLoc(), owner ? owner.getValueType() : type, callee,
                                      call.getArgs().front());
        if (owner) {
            mlir::Value local =
                ec::VariableOp::create(at, o->getLoc(), owner, ec::OpaqueAttr::get(context, ""));
            ec::AssignOp::create(at, o->getLoc(), local, value);
            value = local;
        }
        call.getResult().replaceAllUsesWith(value);
        eraseIfUnused(o);
        return true;
    }
    // The owner's box: the variable starts as undefined, a null pointer.
    if (auto cell = llvm::dyn_cast<ctjs::CreateCellOp>(o);
        cell && !o->hasAttr(kNativeStaticReference) &&
        carrierOf(typeOf(cell.getResult())) == carrier::staticTable) {
        const auto name = o->getAttrOfType<mlir::StringAttr>(kNativeStaticCell);
        if (!name) { return false; }
        mlir::OpBuilder at(o);
        const auto type = llvm::cast<ec::LValueType>(cell.getResult().getType());
        statics.try_emplace(name.str(), type.getValueType());
        mlir::Value place = ec::GetGlobalOp::create(at, o->getLoc(), type,
                                                    mlir::FlatSymbolRefAttr::get(context, name));
        ec::AssignOp::create(at, o->getLoc(), place,
                             ec::LiteralOp::create(at, o->getLoc(), type.getValueType(),
                                                   at.getStringAttr("nullptr")));
        cell.getResult().replaceAllUsesWith(place);
        eraseIfUnused(o);
        return true;
    }
    auto name = o->getAttrOfType<mlir::StringAttr>(kNativeStaticTable);
    if (!name) { return false; }
    mlir::OpBuilder at(o);
    // The literal is the table's address; its fields are already spelled in
    // the namespace-scope definition.
    if (auto literal = llvm::dyn_cast<ctjs::CreateObjectOp>(o)) {
        mlir::Value address =
            ec::LiteralOp::create(at, o->getLoc(), literal.getResult().getType(),
                                  at.getStringAttr("&" + name.getValue() + "_table"));
        literal.getResult().replaceAllUsesWith(address);
        eraseIfUnused(o);
        return true;
    }
    if (auto read = llvm::dyn_cast<ctjs::GetPropertyOp>(o)) {
        const std::string member =
            "&" + staticTableStruct(name.getValue()) + "::" + staticTableReads.lookup(o);
        const mlir::Type type = read.getResult().getType();
        mlir::Value value;
        if (o->hasAttr(kNativeStaticTableSpread)) {
            // A spread's read names its result type, which also holds
            // undefined for a table not stored yet.
            value = callValue(at, o->getLoc(), type,
                              "ctnative::table_field_or_undefined<" + member + ", " +
                                  llvm::cast<ec::OpaqueType>(type).getValue().str() + ">",
                              read.getObject());
        } else {
            // The key, which the error of a read of an unset table names.
            const llvm::StringRef key =
                o->getAttrOfType<mlir::StringAttr>("ctnative.static_table_key").getValue();
            const mlir::Value named = ec::LiteralOp::create(
                at, o->getLoc(), ec::PointerType::get(ec::OpaqueType::get(context, "const char")),
                cpp::c_string_literal(std::string_view(key.data(), key.size())));
            value = callValue(at, o->getLoc(), type, "ctnative::table_field<" + member + ">",
                              mlir::ValueRange{read.getObject(), named});
        }
        read.getResult().replaceAllUsesWith(value);
        eraseIfUnused(o);
        return true;
    }
    // A field definition goes, and with it an array field's appends and the
    // array, which come before it and were left in place when visited.
    if (auto store = llvm::dyn_cast<ctjs::SetPropertyOp>(o)) {
        auto array = store.getValue().getDefiningOp<ctjs::CreateArrayOp>();
        eraseIfUnused(o);
        if (array) {
            for (mlir::Operation * user :
                 llvm::make_early_inc_range(array.getResult().getUsers())) {
                eraseIfUnused(user);
            }
            eraseIfUnused(array);
        }
        return true;
    }
    return llvm::isa<ctjs::CreateArrayOp, ctjs::AppendOp>(o);
}

} // namespace ctcompile::ctnative::lowering_detail
