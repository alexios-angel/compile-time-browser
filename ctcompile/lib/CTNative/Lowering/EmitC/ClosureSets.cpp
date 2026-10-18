//===- EmitC/ClosureSets.cpp - Closure-set lowering ------------*- C++ -*-===//
//
// Spells a value of the lattice's `closure_set` type, and each closure that
// makes one of its members, as one struct emitted per set. Its enum class tag
// says which member the value is, each member's captures are named fields,
// each member has a factory, and its call operator switches on the tag and
// calls the member's lifted function directly with that member's captures:
//
//   struct closure_set_fn_2_fn_3 {
//     enum class member : unsigned char { fn_2, fn_3 };
//     member tag{};
//     ctnative::js_num c0_0{};
//     static closure_set_fn_2_fn_3 make_fn_2(ctnative::js_num c0) { ... }
//     static closure_set_fn_2_fn_3 make_fn_3() { ... }
//     template <class... A> auto operator()(A &&... args) const {
//       switch (tag) {
//       case member::fn_2: return fn_2(c0_0, std::forward<A>(args)...);
//       case member::fn_3: return fn_3(std::forward<A>(args)...);
//       }
//       std::terminate();
//     }
//   };
//
// A member closure becomes `closure_set_fn_2_fn_3::make_fn_2(captures...)`. A
// call through the set is the borrowed call's `ctnative::invoke_callable`,
// which calls the struct (Environments.cpp). Admission proved that every
// member takes the same arguments and returns one type, so each case of the
// call returns the same C++ type. ClosureLifting/ClosureSets.cpp says which
// closures and calls these are.
//
//===----------------------------------------------------------------------===//

#include "Emitter.h"

#include "ctcompile/CTNative/Analysis/NativeClosure.h"

namespace ctcompile::ctnative::lowering_detail {

/// The member whose lifted function is `target`, read from the closure that
/// makes it: the function's C++ name and its captures' fields.
const lowering::closureSetMember & lowering::closureSetMemberOf(llvm::StringRef target) {
    if (const auto found = closureSetMembers.find(target); found != closureSetMembers.end()) {
        return found->second;
    }
    ctjs::CreateClosureOp made;
    module.walk([&](ctjs::CreateClosureOp c) {
        if (c->hasAttr(kNativeClosureSet) && borrowedTarget(c) == target) { made = c; }
    });
    if (!made) {
        llvm::report_fatal_error(llvm::Twine("ctnative lowering: the closure set member `") +
                                 target + "` has no closure that makes it");
    }
    const auto named = names.find(target);
    closureSetMember member;
    member.function = named == names.end() ? cIdentifier(target) : named->second;
    // A capture with no value carrier, such as the pointer an arrow's lexical
    // `this` is bound as (ArrowThis.cpp), is declared as the lifted function's
    // own parameter type (ctnative::parameter_t).
    member.receiver = !llvm::all_of(made.getUpvalues(), [&](mlir::Value captured) {
        return isCallableCarrier(carrierOf(typeOf(captured)));
    });
    for (auto [i, captured] : llvm::enumerate(made.getUpvalues())) {
        if (member.receiver) {
            member.captures.push_back(
                {"ctnative::parameter_t<" + member.function + ", " + std::to_string(i) + ">",
                 "{}"});
            continue;
        }
        // The fields of the members a value does not hold are initialized
        // too. An Element view has no default, so it starts unbound, which no
        // call reads.
        const bool element = carrierOf(typeOf(captured)) == carrier::domElement;
        member.captures.push_back({callableTypeSpelling(typeOf(captured)),
                                   element ? " = ctnative::js_element_t::unbound()" : "{}"});
    }
    return closureSetMembers.try_emplace(target, std::move(member)).first->second;
}

/// The struct of the set of `targets`, in the given order: its name, after
/// recording its definition for declareClosureSetMembers or
/// defineReceiverClosureSets to emit.
std::string lowering::closureSetSpelling(llvm::ArrayRef<llvm::StringRef> targets) {
    llvm::SmallVector<const closureSetMember *> members;
    llvm::SmallVector<llvm::StringRef> functions;
    for (llvm::StringRef target : targets) {
        members.push_back(&closureSetMemberOf(target));
        functions.push_back(members.back()->function);
    }
    const std::string name = "closure_set_" + llvm::join(functions, "_");
    if (closureSetDefinitions.count(name)) { return name; }
    std::string text;
    llvm::raw_string_ostream out(text);
    out << "// A value of the closure set {" << llvm::join(functions, ", ")
        << "}: which member it is, and each member's captures.\n"
        << "struct " << name << " {\n"
        << "  enum class member : unsigned char { " << llvm::join(functions, ", ") << " };\n"
        << "  member tag{};\n";
    bool receiver = false;
    for (auto [m, member] : llvm::enumerate(members)) {
        receiver |= member->receiver;
        for (auto [c, capture] : llvm::enumerate(member->captures)) {
            out << "  " << capture.first << " c" << m << "_" << c << capture.second << ";\n";
        }
    }
    for (auto [m, member] : llvm::enumerate(members)) {
        out << "  static " << name << " make_" << member->function << "(";
        for (auto [c, capture] : llvm::enumerate(member->captures)) {
            out << (c != 0 ? ", " : "") << capture.first << " c" << c;
        }
        out << ") {\n    return {.tag = member::" << member->function;
        for (unsigned c = 0; c < member->captures.size(); ++c) {
            out << ", .c" << m << "_" << c << " = std::move(c" << c << ")";
        }
        out << "};\n  }\n";
    }
    out << "  template <class... A> auto operator()(A &&... args) const {\n"
        << "    switch (tag) {\n";
    for (auto [m, member] : llvm::enumerate(members)) {
        out << "    case member::" << member->function << ": return " << member->function << "(";
        for (unsigned c = 0; c < member->captures.size(); ++c) {
            out << "c" << m << "_" << c << ", ";
        }
        out << "std::forward<A>(args)...);\n";
    }
    out << "    }\n    std::terminate();\n  }\n};\n";
    closureSetDefinitions[name] = {text, receiver};
    return name;
}

/// The struct of the set `v` belongs to: a member's closure, or a value (a
/// merge, a field read, a struct field's write) whose type is a set of
/// members. Null otherwise; a borrowed parameter that callers fill with
/// different callbacks has a `closure_set` type too, and stays the borrowed
/// function_ref.
mlir::Type lowering::closureSetCarrierOf(mlir::Value v) {
    // Gathered on the first query, from the shape census or the first value
    // lowered, before any member's closure has been replaced. Every value of
    // a set gets the struct of the whole set, which each member's closure
    // names: a merge of two members has a type naming only those two, and a
    // field holds every member any of its writes stores.
    if (!closureSetGroups) {
        closureSetGroups.emplace();
        module.walk([&](ctjs::CreateClosureOp c) {
            auto members = c->getAttrOfType<mlir::ArrayAttr>(kNativeClosureSet);
            if (!members) { return; }
            llvm::SmallVector<llvm::StringRef> names;
            for (auto symbol : members.getAsRange<mlir::FlatSymbolRefAttr>()) {
                names.push_back(symbol.getValue());
            }
            (*closureSetGroups)[borrowedTarget(c)] = closureSetSpelling(names);
        });
    }
    std::string spelling;
    mlir::Type type = typeOf(v);
    const bool absent = llvm::isa_and_nonnull<OptType>(type);
    if (absent) { type = llvm::cast<OptType>(type).getElementType(); }
    if (auto made = v.getDefiningOp<ctjs::CreateClosureOp>();
        made && made->hasAttr(kNativeClosureSet)) {
        spelling = closureSetGroups->lookup(borrowedTarget(made));
    } else if (auto set = llvm::dyn_cast_or_null<ClosureSetType>(type)) {
        for (mlir::StringAttr target : set.getTargets()) {
            const auto group = closureSetGroups->find(target.getValue());
            if (group == closureSetGroups->end() ||
                (!spelling.empty() && spelling != group->second)) {
                return {};
            }
            spelling = group->second;
        }
    }
    if (spelling.empty()) { return {}; }
    // A field read no write dominates may find the field absent.
    if (absent) { spelling = "std::optional<" + spelling + ">"; }
    return ec::OpaqueType::get(context, spelling);
}

/// The join of two field carriers when both are one closure set's struct,
/// possibly absent: the set itself, or std::optional of it. Null otherwise.
mlir::Type lowering::joinClosureSetCarriers(mlir::Type a, mlir::Type b) {
    auto left = llvm::dyn_cast<ec::OpaqueType>(a);
    auto right = llvm::dyn_cast<ec::OpaqueType>(b);
    if (!left || !right) { return {}; }
    const auto present = [](llvm::StringRef spelling) {
        spelling.consume_front("std::optional<") && spelling.consume_back(">");
        return spelling;
    };
    const llvm::StringRef set = present(left.getValue());
    if (!closureSetDefinitions.count(set.str()) || set != present(right.getValue())) { return {}; }
    return ec::OpaqueType::get(context, ("std::optional<" + set + ">").str());
}

/// A set's struct stored where the set may be absent becomes the engaged
/// std::optional of it. Null for any other conversion.
mlir::Value lowering::convertClosureSet(mlir::OpBuilder & b, mlir::Location where,
                                        mlir::Value value, mlir::Type target) {
    auto from = llvm::dyn_cast<ec::OpaqueType>(value.getType());
    auto to = llvm::dyn_cast<ec::OpaqueType>(target);
    if (!from || !to || !closureSetDefinitions.count(from.getValue().str()) ||
        to.getValue() != ("std::optional<" + from.getValue() + ">").str()) {
        return {};
    }
    return callValue(b, where, target, to.getValue(), value);
}

bool lowering::retypeClosureSet(mlir::Value v) {
    const mlir::Type set = closureSetCarrierOf(v);
    if (set) { v.setType(set); }
    return static_cast<bool>(set);
}

/// Declares each closure-set member's function ahead of the shape classes,
/// then defines the sets whose captures all have value carriers: a class
/// field of a set's struct names them, and the prototypes that follow the
/// classes come too late for it. A second prototype of the same function is
/// valid C++. A member bound to a receiver pointer is left to the prototypes
/// after the classes, since its signature names a class, and so is its set's
/// struct (defineReceiverClosureSets): such a set lives in the method's frame
/// (ClosureLifting/ClosureSets.cpp), so only function bodies spell it.
/// ponytail: a class field holding such a set would need the class declared
/// ahead; forward-declare the classes here if a set ever reaches one.
void lowering::declareClosureSetMembers(mlir::OpBuilder & b, llvm::ArrayRef<ec::FuncOp> lowered) {
    if (!closureSetGroups || closureSetGroups->empty()) { return; }
    llvm::StringSet<> members;
    for (const auto & target : *closureSetGroups) {
        const closureSetMember & member = closureSetMemberOf(target.getKey());
        if (!member.receiver) { members.insert(member.function); }
    }
    for (ec::FuncOp f : lowered) {
        if (!members.contains(f.getSymName())) { continue; }
        ec::DeclareFuncOp::create(b, f.getLoc(),
                                  mlir::FlatSymbolRefAttr::get(context, f.getSymName()));
    }
    for (const auto & [name, definition] : closureSetDefinitions) {
        if (!definition.second) { verbatim(b, definition.first); }
    }
}

/// Defines the closure sets a member of which binds a receiver pointer, after
/// the function prototypes that declare every member.
void lowering::defineReceiverClosureSets(mlir::OpBuilder & b) {
    for (const auto & [name, definition] : closureSetDefinitions) {
        if (definition.second) { verbatim(b, definition.first); }
    }
}

bool lowering::replaceClosureSetMember(mlir::Operation * op) {
    auto made = llvm::dyn_cast<ctjs::CreateClosureOp>(op);
    if (!made || !op->hasAttr(kNativeClosureSet)) { return false; }
    mlir::OpBuilder at(op);
    const mlir::Type set = made.getResult().getType();
    const std::string factory =
        (opaqueSpelling(set) + "::make_" + closureSetMemberOf(borrowedTarget(op)).function).str();
    made.getResult().replaceAllUsesWith(
        callValue(at, op->getLoc(), set, factory, made.getUpvalues()));
    eraseIfUnused(op);
    return true;
}

} // namespace ctcompile::ctnative::lowering_detail
