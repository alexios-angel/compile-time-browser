//===- Shapes.cpp - Object-literal shapes -----------------------*- C++ -*-===//
//
// Object-literal shapes for the EmitC lowering: the census that groups
// closed-shape literals into C++ class families, their names and template
// parameters, and the C++ spelling of field and receiver types.
//
//===----------------------------------------------------------------------===//
#include "../Admission/Admission.h"
#include "Emitter.h"

namespace ctcompile::ctnative::lowering_detail {

/// The C++ spelling of an admitted field carrier. Scalars retain their tags;
/// a checked method-table slot carries the existing owning shared pointer.
/// A template argument needs text where a field's type is an mlir::Type.
std::string lowering::spelled(mlir::Type type) {
    if (auto pointer = llvm::dyn_cast_or_null<ec::PointerType>(type)) {
        return spelled(pointer.getPointee()) + " *";
    }
    if (llvm::isa<mlir::Float64Type>(type)) { return "double"; }
    if (auto integer = llvm::dyn_cast_or_null<mlir::IntegerType>(type);
        integer && integer.getWidth() == 1) {
        return "bool";
    }
    if (auto opaque = llvm::dyn_cast_or_null<ec::OpaqueType>(type)) {
        return opaque.getValue().str();
    }
    llvm::report_fatal_error("ctnative lowering: a struct field has no admitted C++ carrier");
}

/// The C++ type name of one object-literal site. A family whose sites agree on
/// every field type is spelled by its name alone; otherwise the name takes a
/// template argument for each position the sites disagree on, in field order.
std::string lowering::spelling(const siteShape & site) const {
    const family & f = families[site.family];
    if (!f.varies.any()) { return f.name; }
    llvm::SmallVector<std::string> arguments;
    for (unsigned i = 0; i < f.fields.size(); ++i) {
        if (f.varies[i]) { arguments.push_back(spelled(site.types[i])); }
    }
    return f.name + "<" + llvm::join(arguments, ", ") + ">";
}

/// The site's class type, as an lvalue (the type of an `emitc.variable`).
mlir::Type lowering::classType(const siteShape & site) {
    return ec::LValueType::get(ec::OpaqueType::get(context, spelling(site)));
}

/// The type of a lifted method's receiver parameter: a pointer to the site's
/// class (`ctn_x * self`), not a reference.
///
/// EmitC cannot express a reference parameter: `emitc.func` rejects lvalue
/// argument types ("cannot have lvalue type as argument"). `emitc.member` on a
/// value-typed parameter gives a read-only result, so `this.x = 5` would have
/// no lowering, and `emitc.dereference` prints as `*v0`, which `emitc.member`
/// then turns into the wrong expression `*v0.x` without a diagnostic.
/// `emitc.member_of_ptr` on an lvalue holding a pointer works, so each method
/// opens with `ctn_x * self; self = v0;` and every access is `self->x`. The
/// pointer is non-owning and does not outlive the call, so it behaves exactly
/// like a reference and compiles to the same code at -O2.
mlir::Type lowering::receiverType(const siteShape & site) {
    return ec::PointerType::get(ec::OpaqueType::get(context, spelling(site)));
}

/// The member name of one property access, recorded by fieldsOf(). A missing
/// entry is a fatal error that names the broken invariant, rather than
/// `accessKey.at()`, whose uncaught exception would say nothing: fieldsOf()
/// records every get and set on a closed-shape literal, and admission refuses
/// a property access on anything else.
llvm::StringRef lowering::memberName(mlir::Operation * access) const {
    const auto entry = accessKey.find(access);
    if (entry == accessKey.end()) {
        llvm::report_fatal_error(llvm::Twine("ctnative lowering: `") +
                                 access->getName().getStringRef() +
                                 "` has no recorded member name - it reads or writes an "
                                 "object the shape census never saw, and admission should "
                                 "have refused the function for a property access on "
                                 "something that is not a closed-shape literal");
    }
    return entry->second;
}

/// The shape censusShapes() recorded for `object`; fatal if there is none.
const lowering::siteShape & lowering::shapeAt(mlir::Value object) const {
    const auto entry = shapeOf.find(object);
    if (entry == shapeOf.end()) {
        llvm::report_fatal_error("ctnative lowering: a closed object literal that the shape "
                                 "census never saw - censusShapes() runs over the whole "
                                 "accepted set before any function is lowered");
    }
    return entry->second;
}

/// The fields of a closed object literal, sorted by name, each with the join
/// of the carriers of its writes and reads. Mixed scalar stores use the
/// tagged carrier. An optional read widens even definite stores because it
/// can precede them, so a field that is only ever read preserves undefined.
/// Both sides are collected before choosing, so SSA use-list ordering cannot
/// change a shape's key. Also records each access's member name (accessKey)
/// and field type (accessType).
///
/// Carriers come from the type lattice (typeOf), not from the IR's types: the
/// shape census runs before retype() has given any value its carrier.
llvm::SmallVector<std::pair<std::string, mlir::Type>> lowering::fieldsOf(mlir::Value object) {
    const auto carried = [&](mlir::Value v) {
        auto type = typeOf(v);
        if (auto table = llvm::dyn_cast_or_null<MethodTableType>(type)) { return tableType(table); }
        if (auto closure = llvm::dyn_cast_or_null<ClosureType>(type)) {
            return closureCarrierType(closure);
        }
        return carrierType(context, carrierOf(type));
    };
    llvm::StringMap<mlir::Type> stored;
    llvm::StringMap<mlir::Type> read;
    // A field seen with two different carriers is nullable.
    const auto join = [&](llvm::StringMap<mlir::Type> & into, llvm::StringRef key,
                          mlir::Type type) {
        auto [at, fresh] = into.try_emplace(key, type);
        if (!fresh && at->second != type) { at->second = carrierType(context, carrier::nullable); }
    };
    // Walk every alias in the group: a lifted method's `this.x` reads this
    // shape through a different value in a different function, and a field
    // only that method touches is still a field. This walk also records each
    // access's member name, so replace() can spell `self->x` directly.
    for (mlir::Value alias : aliasesOf(groups, object)) {
        for (mlir::Operation * user : alias.getUsers()) {
            // The method field takes no member: the closure it holds is a
            // free function and the store lowers to nothing. Its key can
            // also be used as string data, so only the access is erased;
            // the final sweep removes the key when nothing reads it.
            if (auto method = llvm::dyn_cast<ctjs::SetPropertyOp>(user);
                method && user->hasAttr("ctnative.method")) {
                continue;
            }
            mlir::Value key;
            if (auto get = llvm::dyn_cast<ctjs::GetPropertyOp>(user)) {
                if (get.getObject() != alias) { continue; }
                key = get.getKey();
                join(read, ctjs::constantKey(key), carried(get.getResult()));
            } else if (auto set = llvm::dyn_cast<ctjs::SetPropertyOp>(user)) {
                if (set.getObject() != alias) { continue; }
                key = set.getKey();
                join(stored, ctjs::constantKey(key), carried(set.getValue()));
            }
            if (key) { accessKey[user] = ctjs::constantKey(key).str(); }
        }
    }
    // Every field, read-only ones included, with its joined carrier.
    for (const auto & entry : read) { join(stored, entry.getKey(), entry.getValue()); }
    for (mlir::Value alias : aliasesOf(groups, object)) {
        for (mlir::Operation * user : alias.getUsers()) {
            const auto key = accessKey.find(user);
            if (key == accessKey.end()) { continue; }
            if (auto field = stored.find(key->second); field != stored.end()) {
                accessType[user] = field->second;
            }
        }
    }
    llvm::SmallVector<std::pair<std::string, mlir::Type>> fields;
    for (const auto & entry : stored) {
        fields.emplace_back(entry.getKey().str(), entry.getValue());
    }
    llvm::sort(fields, [](const auto & a, const auto & b) { return a.first < b.first; });
    return fields;
}

/// Groups every closed object literal in the accepted functions into shape
/// families, before any function is lowered. A site's type is something like
/// `ctn_at_hit<bool>`, and which positions are template parameters depends on
/// every site in the program, so no site can be spelled until all of them
/// have been seen.
void lowering::censusShapes(llvm::ArrayRef<ctjs::FuncOp> accepted) {
    std::vector<llvm::StringSet<>> keys; // per family, its distinct (name, type) keys
    for (ctjs::FuncOp fn : accepted) {
        fn.getBody().walk([&](ctjs::CreateObjectOp object) {
            if (carrierOf(typeOf(object.getResult())) == carrier::json) { return; }
            if (!methodTableName(object).empty() || object->hasAttr(kNativeObjectIdentity)) {
                return;
            }
            // A table is a dictionary, not a struct (EmitC/Dictionaries.cpp).
            if (isNativeDictionarySite(object)) { return; }
            const auto fields = fieldsOf(object.getResult());
            std::string nameKey; // the family key: just the names
            std::string typeKey; // the instantiation key: names AND types
            siteShape site;
            // An instance of a JavaScript class is its own family, named after
            // the class, however its fields compare with another shape's.
            const auto className = object->getAttrOfType<mlir::StringAttr>(kNativeClassName);
            if (className) {
                nameKey = "class " + className.getValue().str();
                nameKey.push_back('\0');
            }
            for (const auto & field : fields) {
                nameKey += field.first;
                nameKey.push_back('\0'); // no field name can contain one
                typeKey += field.first;
                typeKey += ':';
                typeKey += spelled(field.second);
                typeKey.push_back('\0');
                site.types.push_back(field.second);
            }
            const auto [entry, fresh] =
                familyIndex.try_emplace(nameKey, static_cast<unsigned>(families.size()));
            site.family = entry->second;
            if (fresh) {
                family made;
                if (className) { made.className = className.getValue().str(); }
                for (const auto & field : fields) { made.fields.push_back(field.first); }
                made.types = site.types;
                made.varies = llvm::BitVector(static_cast<unsigned>(fields.size()), false);
                families.push_back(std::move(made));
                keys.emplace_back();
            } else {
                family & f = families[site.family];
                for (unsigned i = 0; i < f.types.size(); ++i) {
                    if (f.types[i] != site.types[i]) { f.varies.set(i); }
                }
            }
            families[site.family].where.push_back(siteOf(object.getLoc()));
            keys[site.family].insert(typeKey);
            // Every value in the alias group gets the shape, which is what
            // makes a receiver parameter spellable: `%arg0` of a lifted method
            // has the class of the literal it is called on, because it is that
            // literal. Literals whose fields differ were already joined into
            // one shape by the group, so two objects sharing one method need
            // no separate check.
            for (mlir::Value alias : aliasesOf(groups, object.getResult())) {
                shapeOf[alias] = site;
            }
        });
    }
    for (unsigned i = 0; i < families.size(); ++i) {
        families[i].instantiations = static_cast<unsigned>(keys[i].size());
    }
    nameFamilies();
}

/// Names each family after its fields, so the same fields name the same type
/// wherever they are written: `{x, y}` is `ctn_x_y` in every function. Also
/// picks the template parameter names.
void lowering::nameFamilies() {
    llvm::StringSet<> taken;
    for (family & f : families) {
        std::string joined = "ctn";
        for (const std::string & field : f.fields) { joined += "_" + field; }
        // A literal with no fields still needs a name, and `ctn_` is not
        // one. `var e = {};` is admitted (hasClosedShape's loop over the uses
        // of a literal with none is vacuously true), so this arm is reachable.
        if (f.fields.empty()) { joined += "_empty"; }
        // A double underscore is reserved to the C++ implementation in every
        // scope, and a JavaScript field named `_x` would put one here.
        std::string squeezed;
        for (char ch : joined) {
            if (ch == '_' && !squeezed.empty() && squeezed.back() == '_') { continue; }
            squeezed.push_back(ch);
        }
        // Two families can still want one name: `{a_b}` and `{a, b}` both
        // join to `ctn_a_b` because the separator is also a legal field-name
        // character, and the squeeze above makes `{a, _b}` a third. The second
        // family to ask gets `_2`. Without this the module would hold two
        // `emitc.class @ctn_a_b` and the symbol-table verifier would reject it.
        // A class instance is spelled after its JavaScript class instead.
        if (!f.className.empty()) {
            squeezed = admission::isReservedInCpp(f.className) ? "js_" + f.className : f.className;
        }
        f.name = squeezed;
        for (unsigned n = 2; !taken.insert(f.name).second; ++n) {
            f.name = squeezed + "_" + std::to_string(n);
        }
        // Template parameters: one per position the family disagrees on, none
        // where it agrees. A parameter may not share a field's name:
        // `template <class T0> class C { double T0; };` is "declaration of
        // 'T0' shadows template parameter", and `{T0: 1}` is an ordinary
        // JavaScript object.
        f.parameters.assign(f.fields.size(), std::string{});
        for (unsigned i = 0, next = 0; i < f.fields.size(); ++i) {
            if (!f.varies[i]) { continue; }
            std::string parameter;
            do {
                parameter = "T" + std::to_string(next++);
            } while (llvm::is_contained(f.fields, parameter));
            f.parameters[i] = parameter;
        }
    }
}

/// Builds the provenance comment printed above a generated class. A shape
/// used at many object literals gets one class, so the comment names the first
/// three sites, says how many there are in total, and for a template how many
/// instantiations it has. A reader arriving from a C++ diagnostic gets a place
/// to start and learns that other sites exist.
std::string lowering::provenanceOf(const family & f) const {
    std::string out = f.className.empty() ? "object literal at "
                                          : "class " + f.className + ", instances made at ";
    const size_t shown = std::min<size_t>(3, f.where.size());
    out += llvm::join(llvm::ArrayRef(f.where).take_front(shown), ", ");
    if (f.where.size() > shown) {
        out += " and " + std::to_string(f.where.size() - shown) + " more";
    }
    if (f.where.size() > 1) {
        out += " (" + std::to_string(f.where.size()) + " sites";
        if (f.varies.any()) { out += ", " + std::to_string(f.instantiations) + " instantiations"; }
        out += ")";
    }
    return out;
}

/// Sorts the proved accesses of one dense array into length/index reads and
/// writes, before retyping. Keys are still lowered: a constant can also be
/// used as ordinary string data. A key used only by erased accesses is
/// removed by the final sweep.
void lowering::collectVector(mlir::Value array) {
    for (mlir::Operation * user : array.getUsers()) {
        if (auto set = llvm::dyn_cast<ctjs::SetPropertyOp>(user)) {
            if (ctjs::constantKey(set.getKey()) == "length") {
                vectorLengthWrites.insert(user);
            } else {
                vectorIndexWrites.insert(user);
            }
            continue;
        }
        auto get = llvm::dyn_cast<ctjs::GetPropertyOp>(user);
        if (!get) { continue; }
        if (ctjs::constantKey(get.getKey()) == "length") {
            vectorLengthReads.insert(user);
        } else {
            vectorIndexReads.insert(user);
        }
    }
}

} // namespace ctcompile::ctnative::lowering_detail
