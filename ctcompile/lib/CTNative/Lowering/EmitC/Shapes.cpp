// EmitC/Shapes.cpp - native lowering implementation.
#include "../Admission/Admission.h"
#include "Emitter.h"

namespace ctcompile::ctnative::lowering_detail {

// The C++ spelling of an admitted field carrier. Scalars retain their tags;
// a checked method-table slot carries the existing owning shared pointer.
// A template argument needs text where a field's type is an mlir::Type.
std::string lowering::spelled(mlir::Type type) {
    if (isNullableCarrier(type)) { return "ctnative::nullable_scalar"; }
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

// THE TYPE OF ONE SITE. A family that agrees everywhere is spelled by its
// name alone; one that does not is that name with an argument for each
// position it disagrees on, in field order.
std::string lowering::spelling(const siteShape & site) const {
    const family & f = families[site.family];
    if (!f.varies.any()) { return f.name; }
    std::string out = f.name + "<";
    for (unsigned i = 0, written = 0; i < f.fields.size(); ++i) {
        if (!f.varies[i]) { continue; }
        if (written++ != 0) { out += ", "; }
        out += spelled(site.types[i]);
    }
    return out + ">";
}

mlir::Type lowering::classType(const siteShape & site) {
    return ec::LValueType::get(ec::OpaqueType::get(context, spelling(site)));
}

// THE RECEIVER CARRIER, AND WHY IT IS A POINTER RATHER THAN A `&`.
//
// The answer this slice wanted is `ctn_x & self`, and EmitC has no spelling
// for it. `emitc.func` REJECTS an lvalue argument type outright - "cannot
// have lvalue type as argument" is in the dialect, and it is the check that
// makes a C++ reference parameter unrepresentable - so a parameter is
// always a value type. `emitc.member` on a value-typed operand gives a
// value-typed result, which can be read and not assigned, so `this.x = 5`
// would have no lowering. `emitc.dereference` does give an lvalue, but the
// emitter caches it as the string `*v0` and `emitc.member` then prints
// `*v0.x` - the wrong expression, with no diagnostic anywhere.
//
// `emitc.member_of_ptr` on an lvalue HOLDING a pointer is the one member
// access that survives a parameter, so the receiver is `ctn_x *`, the
// method opens with one `ctn_x * self;` `self = v0;` pair, and every
// access is `self->x`. That is a REFERENCE in every sense this tier cares
// about - non-owning, no allocation, the caller's frame, dead after the
// call - and `-O2` emits the same instructions. It is a pointer only in
// the spelling, and the spelling is the backend's, not this slice's.
mlir::Type lowering::receiverType(const siteShape & site) {
    return ec::PointerType::get(ec::OpaqueType::get(context, spelling(site)));
}

mlir::Type lowering::receiverLocalType(const siteShape & site) {
    return ec::LValueType::get(receiverType(site));
}

// THE MEMBER NAME OF ONE ACCESS, and a named fatal rather than
// `accessKey.at(o)`. `at` on a key that is not there THROWS, and this
// process cannot catch it: an access whose object never went through the
// shape census would have been an uncaught exception with no message
// naming the invariant. The invariant does hold - fieldsOf() records every
// get and set on a literal that passed the closed-shape proof, and
// admission refuses a property access on anything else - but it held by an
// argument and not by a check, which is the difference this makes.
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

const lowering::siteShape & lowering::shapeAt(mlir::Value object) const {
    const auto entry = shapeOf.find(object);
    if (entry == shapeOf.end()) {
        llvm::report_fatal_error("ctnative lowering: a closed object literal that the shape "
                                 "census never saw - censusShapes() runs over the whole "
                                 "accepted set before any function is lowered");
    }
    return entry->second;
}

// Every field is sorted by name and joins the carriers of its writes and
// reads. Mixed scalar stores use the tagged carrier. An optional read widens
// even definite stores because it can precede them.
// A field only ever read therefore preserves undefined. Collect both sides
// before choosing so SSA use-list ordering cannot change a shape's key.
//
// AND IT READS THE LATTICE, NOT THE IR. This ran inside retype(), after
// every value in the function had already taken its carrier, so it could
// read `get.getResult().getType()`. The census runs before any of that and
// asks the solver the question retype() would have asked.
llvm::SmallVector<std::pair<std::string, mlir::Type>> lowering::fieldsOf(mlir::Value object) {
    const auto carried = [&](mlir::Value v) {
        auto type = typeOf(v);
        if (auto table = llvm::dyn_cast_or_null<MethodTableType>(type)) {
            return methodTableCarrierType(table);
        }
        return carrierType(context, carrierOf(type));
    };
    llvm::StringMap<mlir::Type> stored;
    llvm::StringMap<mlir::Type> read;
    // OVER THE GROUP. A lifted method's `this.x` is a read of this shape
    // made through a different value in a different function, and a field
    // only that method touches is still a field. Collecting them here is
    // also what records the member name for the access, so replace() can
    // spell `self->x` without asking the census a second time.
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
                auto [at, fresh] =
                    read.try_emplace(admission::keyOf(key), carried(get.getResult()));
                if (!fresh && at->second != carried(get.getResult())) {
                    at->second = carrierType(context, carrier::nullable);
                }
            } else if (auto set = llvm::dyn_cast<ctjs::SetPropertyOp>(user)) {
                if (set.getObject() != alias) { continue; }
                key = set.getKey();
                auto [at, fresh] =
                    stored.try_emplace(admission::keyOf(key), carried(set.getValue()));
                if (!fresh && at->second != carried(set.getValue())) {
                    at->second = carrierType(context, carrier::nullable);
                }
            }
            if (key) { accessKey[user] = admission::keyOf(key).str(); }
        }
    }
    llvm::SmallVector<std::pair<std::string, mlir::Type>> fields;
    for (const auto & entry : stored) {
        const auto observed = read.lookup(entry.getKey());
        fields.emplace_back(entry.getKey().str(), observed && observed != entry.getValue()
                                                      ? carrierType(context, carrier::nullable)
                                                      : entry.getValue());
    }
    for (const auto & entry : read) {
        if (!stored.contains(entry.getKey())) {
            fields.emplace_back(entry.getKey().str(), entry.getValue());
        }
    }
    needsNullable |=
        llvm::any_of(fields, [](const auto & field) { return isNullableCarrier(field.second); });
    for (mlir::Value alias : aliasesOf(groups, object)) {
        for (mlir::Operation * user : alias.getUsers()) {
            const auto key = accessKey.find(user);
            if (key == accessKey.end()) { continue; }
            for (const auto & field : fields) {
                if (field.first == key->second) { accessType[user] = field.second; }
            }
        }
    }
    llvm::sort(fields, [](const auto & a, const auto & b) { return a.first < b.first; });
    return fields;
}

// THE SHAPE CENSUS, over the whole accepted set and BEFORE any function is
// lowered - for the same reason the global census in runOnOperation() runs
// there. A site's TYPE is `ctn_at_hit<bool>`, and WHICH positions are
// template parameters is a property of every site in the program, so no
// site can be spelled until all of them have been seen.
void lowering::censusShapes(llvm::ArrayRef<ctjs::FuncOp> accepted) {
    std::vector<std::set<std::string>> keys; // per family, its distinct (name, type) keys
    for (ctjs::FuncOp fn : accepted) {
        fn.getBody().walk([&](ctjs::CreateObjectOp object) {
            if (!methodTableName(object).empty() || object->hasAttr(kNativeObjectIdentity)) {
                return;
            }
            const auto fields = fieldsOf(object.getResult());
            std::string nameKey; // the family key: just the names
            std::string typeKey; // the instantiation key: names AND types
            siteShape site;
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
            // EVERY VALUE IN THE GROUP GETS THE SHAPE, which is what makes
            // a receiver parameter spellable: `%arg0` of the lifted method
            // has the same class as the literal it is called on, because
            // it IS that literal. Two literals of one shape calling one
            // method put the same answer here twice; a disagreement would
            // be two literals whose fields differ, and the group has
            // already joined those into ONE shape - which is correct, and
            // is why "two objects of the same shape share one method" needs
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

// THE NAME IS THE SHAPE, so the same fields name the same type wherever
// they are written: `{x, y}` is `ctn_x_y` in every function in the program.
void lowering::nameFamilies() {
    llvm::StringSet<> taken;
    for (family & f : families) {
        std::string joined = "ctn";
        for (const std::string & field : f.fields) { joined += "_" + field; }
        // A LITERAL WITH NO FIELDS STILL NEEDS A NAME, and `ctn_` is not
        // one. `var e = {};` is admitted today - hasClosedShape's loop over
        // the uses of a literal with none is vacuously true - so this arm
        // is reachable and is what keeps the empty shape a plain class.
        if (f.fields.empty()) { joined += "_empty"; }
        // A DOUBLE UNDERSCORE IS RESERVED TO THE IMPLEMENTATION IN EVERY
        // SCOPE, and a JavaScript field named `_x` would put one here.
        std::string squeezed;
        for (char ch : joined) {
            if (ch == '_' && !squeezed.empty() && squeezed.back() == '_') { continue; }
            squeezed.push_back(ch);
        }
        // TWO FAMILIES CAN STILL WANT ONE NAME. `{a_b}` and `{a, b}` both
        // join to `ctn_a_b`, because every character a field name may
        // contain is also the character the separator is; and the squeeze
        // above makes `{a, _b}` a third. The second family to ask gets
        // `_2`. Without this the module holds two `emitc.class @ctn_a_b`
        // and the symbol-table verifier rejects it - which is how the guard
        // is proved.
        f.name = squeezed;
        for (unsigned n = 2; !taken.insert(f.name).second; ++n) {
            f.name = squeezed + "_" + std::to_string(n);
        }
        // THE TEMPLATE PARAMETERS, one per position the family disagrees on
        // and none at all where it agrees. A parameter may NOT be named for
        // a field: `template <class T0> class C { double T0; };` is
        // "declaration of 'T0' shadows template parameter", and `{T0: 1}`
        // is a perfectly ordinary JavaScript object.
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

// PART 24 PHASE 63 STEP 7, WITH ONE DEFINITION FOR MANY SITES. Every
// generated definition sits under a comment naming its JavaScript site;
// a shape written at eleven places has eleven of them, so the comment
// names the first three, says how many there are, and says how many
// instantiations a template has. A reader who arrives at the class from a
// C++ diagnostic gets somewhere to start AND the fact that there are
// others - which a single site silently chosen from the eleven would hide.
std::string lowering::provenanceOf(const family & f) const {
    std::string out = "object literal at ";
    const size_t shown = std::min<size_t>(3, f.where.size());
    for (size_t i = 0; i < shown; ++i) {
        if (i != 0) { out += ", "; }
        out += f.where[i];
    }
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

// The reads of one dense array, sorted into `length` and index. Keys are
// still lowered: a constant can also be used as ordinary string data.
// A key used only by erased accesses is removed by the final sweep.
void lowering::collectVector(mlir::Value array) {
    needsVector = true;
    for (mlir::Operation * user : array.getUsers()) {
        auto get = llvm::dyn_cast<ctjs::GetPropertyOp>(user);
        if (!get) { continue; }
        if (admission::keyOf(get.getKey()) == "length") {
            vectorLengthReads.insert(user);
        } else {
            vectorIndexReads.insert(user);
        }
    }
}

} // namespace ctcompile::ctnative::lowering_detail
