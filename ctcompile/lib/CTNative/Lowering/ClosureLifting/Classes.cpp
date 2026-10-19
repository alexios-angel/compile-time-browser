//===- ClosureLifting/Classes.cpp - The class census ----------------------===//
//
// Recognises the importer's spelling of a JavaScript `class` and records it as
// one static description, so the method and constructor rules can treat a
// class as a C++ class rather than as an object literal that happens to be
// called a prototype.
//
// The importer spells `class K extends P { constructor() {} m() {} static s()
// {} static get g() {} }` as:
//
//   %proto = ctjs.create_object
//   %K     = ctjs.create_closure ...            the constructor
//   ctjs.call __ctbrowser_class_heritage(%K, %P, %proto)   with `extends` only
//   ctjs.set_property %K["prototype"], %proto
//   ctjs.set_property %proto["constructor"], %K
//   ctjs.set_property %K["__home"], %proto
//   ctjs.set_property %proto["m"], %m           a prototype method
//   ctjs.set_property %m["__home"], %proto
//   ctjs.set_property %K["s"], %s               a static method
//   ctjs.set_property %s["__home"], %K
//   ctjs.define_accessor "g" on %K get %g set undefined
//   ctjs.set_property %g["__home"], %K
//   ctjs.call __ctbrowser_class_defined(%K)
//   ctjs.cell_set %binding, %K                  once or twice, the same %K
//
// The `prototype` attachment and the member definitions are the shape the
// constructor and method rules already prove for an ES5 prototype
// (immutablePrototype, methodCensus). The `constructor` and `__home` edges and
// the class_defined call are class setup: they lower to nothing once the class
// is proved, and those rules skip them. A class is proved when its prototype
// and constructor are touched by nothing else, and every member closure only
// by its definition and its `__home` edge. Static members become direct
// calls before this census runs (ClassStatics.cpp); a class whose static
// members that rewrite left in place is refused with its reason. A class
// that `extends` another class of the module has already been flattened into
// one that extends nothing (ClassInheritance.cpp, which also lowers
// `super`); one still extending another is refused with the reason that
// rewrite recorded. A class with a prototype accessor is refused for now.
// The class_defined call is the VM's engine-internal helper, which no script
// can replace (ctbrowser keeps it apart from the globals table; see
// ctcompile/CTJS/Import/EngineGlobals.h), so erasing it is exact. A refusal's
// reason is recorded on the constructor's closure (ctnative.class_reason).
//
//===----------------------------------------------------------------------===//

#include "ClassInheritance.h"
#include "ClosureLifter.h"
#include "ctcompile/Support/CppLiterals.hpp"

#include "llvm/ADT/SetVector.h"

namespace ctcompile::ctnative::lowering_detail {

bool callsIntrinsic(ctjs::CallOp call, llvm::StringRef name) {
    auto load = call.getCallee().getDefiningOp<ctjs::LoadGlobalOp>();
    return load && load.getName() == name;
}

ctjs::CreateClosureOp boundConstructor(mlir::Value read) {
    auto get = read.getDefiningOp<ctjs::CellGetOp>();
    auto cell = get ? get.getCell().getDefiningOp<ctjs::CreateCellOp>() : ctjs::CreateCellOp{};
    if (!cell) { return read.getDefiningOp<ctjs::CreateClosureOp>(); }
    ctjs::CreateClosureOp held;
    for (mlir::Operation * user : cell.getResult().getUsers()) {
        auto set = llvm::dyn_cast<ctjs::CellSetOp>(user);
        if (!set || set.getCell() != cell.getResult()) { continue; }
        auto made = set.getValue().getDefiningOp<ctjs::CreateClosureOp>();
        if (!made || (held && held != made)) { return {}; }
        held = made;
    }
    return held;
}

/// Whether `op` is the `__home` edge of a member closure.
bool closureLifter::isHomeEdge(mlir::Operation * op) {
    auto set = llvm::dyn_cast<ctjs::SetPropertyOp>(op);
    return set && ctjs::constantKey(set.getKey()) == "__home" &&
           set.getObject().getDefiningOp<ctjs::CreateClosureOp>();
}

/// Records every class the module defines (see the file header) in
/// `jsClasses`, and marks the setup of each proved class with
/// `ctnative.class_setup`.
void closureLifter::classCensus() {
    jsClasses.clear();
    // The census runs more than once (the static-member rewrite runs it
    // first), so a mark from an earlier run must not outlive a class that
    // this run refuses.
    module.walk([&](mlir::Operation * op) { op->removeAttr(kNativeClassSetup); });
    module.walk([&](ctjs::CallOp call) {
        if (!callsIntrinsic(call, "__ctbrowser_class_defined") || call.getArgs().size() != 1) {
            return;
        }
        auto ctor = call.getArgs()[0].getDefiningOp<ctjs::CreateClosureOp>();
        if (!ctor) { return; }
        jsClass & made = jsClasses[ctor.getOperation()];
        made.constructor = ctor;
        made.setup.insert(call.getOperation());
    });
    // Why ClassStatics.cpp left a class's static members in place.
    const auto staticRefusal = [&](mlir::Operation * key) -> std::string {
        const auto found = staticRefusals.find(key);
        return found == staticRefusals.end() ? "static members are the next slice" : found->second;
    };
    for (auto & [key, cls] : jsClasses) {
        const auto refuse = [&](const llvm::Twine & why) {
            if (cls.reason.empty()) { cls.reason = why.str(); }
        };
        const mlir::Value ctor = cls.constructor.getResult();
        // The constructor's own uses.
        for (mlir::OpOperand & use : ctor.getUses()) {
            mlir::Operation * user = use.getOwner();
            if (cls.setup.contains(user) || cls.definitions.contains(user)) { continue; }
            if (auto set = llvm::dyn_cast<ctjs::SetPropertyOp>(user)) {
                const llvm::StringRef name = ctjs::constantKey(set.getKey());
                if (use.getOperandNumber() == 0 && name == "prototype") {
                    cls.prototype = set.getValue().getDefiningOp<ctjs::CreateObjectOp>();
                    if (!cls.prototype) { refuse("its prototype is not an object literal"); }
                    cls.definitions.insert(user);
                } else if (use.getOperandNumber() == 0 && name == "__home") {
                    cls.setup.insert(user);
                } else if (use.getOperandNumber() == 0 && !name.empty() &&
                           set.getValue().getDefiningOp<ctjs::CreateClosureOp>()) {
                    cls.statics[name] = set.getValue().getDefiningOp<ctjs::CreateClosureOp>();
                    cls.definitions.insert(user);
                    refuse("it has static method `" + name + "`; " + staticRefusal(key));
                } else if (use.getOperandNumber() == 2 && name == "constructor") {
                    cls.setup.insert(user);
                } else if (use.getOperandNumber() == 2 && name == "__home") {
                    cls.setup.insert(user);
                } else {
                    refuse("its constructor is written or stored by `" +
                           user->getName().getStringRef() + "`");
                }
                continue;
            }
            if (auto accessor = llvm::dyn_cast<ctjs::DefineAccessorOp>(user);
                accessor && use.getOperandNumber() == 0) {
                if (auto getter = accessor.getGetter().getDefiningOp<ctjs::CreateClosureOp>();
                    getter && ctjs::isUndefinedConstant(accessor.getSetter())) {
                    cls.staticGetters[accessor.getName()] = getter;
                    cls.definitions.insert(user);
                    refuse("it has static getter `" + accessor.getName() + "`; " +
                           staticRefusal(key));
                } else {
                    refuse("its static accessor `" + accessor.getName() + "` has a setter");
                }
                continue;
            }
            if (auto call = llvm::dyn_cast<ctjs::CallOp>(user);
                call && callsIntrinsic(call, "__ctbrowser_class_heritage")) {
                if (call.getArgs().size() == 3 && call.getArgs()[0] != ctor) {
                    refuse("a class that extends it was not flattened");
                    continue;
                }
                if (call.getArgs().size() != 3) {
                    refuse("its heritage call is not (class, parent, prototype)");
                    continue;
                }
                cls.heritage = call;
                cls.parent = boundConstructor(call.getArgs()[1]);
                if (!cls.parent) { refuse("its parent is not one class this module defines"); }
                cls.definitions.insert(user);
                // ClassInheritance.cpp flattens every class it can; one still
                // extending another carries its reason.
                const auto why =
                    cls.constructor->getAttrOfType<mlir::StringAttr>(kNativeInheritanceReason);
                refuse("it extends another class: " +
                       (why ? why.getValue() : llvm::StringRef("its chain was not flattened")));
                continue;
            }
            // Bindings, `new K(...)`, static reads and calls are left to the
            // rules that resolve them, which refuse what they cannot name.
        }
        if (!cls.prototype) {
            refuse("its prototype was not found");
            continue;
        }
        // The prototype's uses.
        for (mlir::OpOperand & use : cls.prototype.getResult().getUses()) {
            mlir::Operation * user = use.getOwner();
            if (cls.setup.contains(user) || isHomeEdge(user)) {
                cls.setup.insert(user);
                continue;
            }
            if (auto set = llvm::dyn_cast<ctjs::SetPropertyOp>(user);
                set && use.getOperandNumber() == 0) {
                const llvm::StringRef name = ctjs::constantKey(set.getKey());
                auto method = set.getValue().getDefiningOp<ctjs::CreateClosureOp>();
                if (name.empty() || name == "__proto__" || !method || cls.methods.count(name)) {
                    refuse("its prototype is written by `" + user->getName().getStringRef() +
                           "` other than as a method definition");
                    continue;
                }
                cls.methods[name] = method;
                cls.definitions.insert(user);
                continue;
            }
            if (auto accessor = llvm::dyn_cast<ctjs::DefineAccessorOp>(user);
                accessor && use.getOperandNumber() == 0) {
                auto getter = accessor.getGetter().getDefiningOp<ctjs::CreateClosureOp>();
                if (!getter || !ctjs::isUndefinedConstant(accessor.getSetter())) {
                    refuse("its accessor `" + accessor.getName() + "` has a setter");
                    continue;
                }
                cls.getters[accessor.getName()] = getter;
                cls.definitions.insert(user);
                refuse("it has getter `" + accessor.getName() + "`; accessors are the next slice");
                continue;
            }
            if (cls.definitions.contains(user)) { continue; }
            refuse("its prototype is used as a value by `" + user->getName().getStringRef() + "`");
        }
        // Each member closure: its member store and its `__home` edge, and
        // nothing else. A method read as a value, or called with `.call`, has
        // a receiver this census cannot name.
        const auto memberOnly = [&](llvm::StringRef kind, llvm::StringRef name,
                                    ctjs::CreateClosureOp member) {
            for (mlir::OpOperand & use : member.getResult().getUses()) {
                if (cls.setup.contains(use.getOwner()) ||
                    cls.definitions.contains(use.getOwner())) {
                    continue;
                }
                if (isHomeEdge(use.getOwner()) && use.getOperandNumber() == 0) {
                    cls.setup.insert(use.getOwner());
                    continue;
                }
                refuse(kind + " `" + name + "` is also used by `" +
                       use.getOwner()->getName().getStringRef() + "`");
            }
        };
        for (const auto & entry : cls.methods) {
            memberOnly("its method", entry.getKey(), entry.getValue());
        }
        for (const auto & entry : cls.getters) {
            memberOnly("its getter", entry.getKey(), entry.getValue());
        }
        for (const auto & entry : cls.statics) {
            memberOnly("its static method", entry.getKey(), entry.getValue());
        }
        for (const auto & entry : cls.staticGetters) {
            memberOnly("its static getter", entry.getKey(), entry.getValue());
        }
    }
    // A class is only as proved as its parent.
    for (bool changed = true; changed;) {
        changed = false;
        for (auto & [key, cls] : jsClasses) {
            (void)key;
            if (!cls.reason.empty() || !cls.parent) { continue; }
            const auto parent = jsClasses.find(cls.parent.getOperation());
            if (parent == jsClasses.end() || !parent->second.reason.empty()) {
                cls.reason = "its parent class is not proved";
                changed = true;
            }
        }
    }
    for (auto & [key, cls] : jsClasses) {
        // Why the static-member rewrite left this class's statics, even when
        // another refusal of the census comes first in ctnative.class_reason.
        if (const auto found = staticRefusals.find(key); found != staticRefusals.end()) {
            cls.constructor->setAttr("ctnative.static_reason",
                                     mlir::StringAttr::get(context, found->second));
        }
        if (!cls.reason.empty()) {
            cls.constructor->setAttr(kNativeClassReason,
                                     mlir::StringAttr::get(context, cls.reason));
            continue;
        }
        cls.constructor->removeAttr(kNativeClassReason);
        for (mlir::Operation * op : cls.setup) {
            op->setAttr(kNativeClassSetup, mlir::UnitAttr::get(context));
        }
    }
}

/// Erases a proved class's setup (the edges and the class_defined call) and
/// then the constants and intrinsic loads only it used, which would otherwise
/// be refused as values without a carrier.
void closureLifter::eraseClassSetup(jsClass & cls) {
    llvm::SetVector<mlir::Operation *> feeders;
    for (mlir::Operation * op : cls.setup) {
        if (!op->use_empty()) { continue; }
        for (mlir::Value operand : op->getOperands()) {
            if (mlir::Operation * made = operand.getDefiningOp();
                llvm::isa_and_nonnull<ctjs::ConstantOp, ctjs::LoadGlobalOp>(made)) {
                feeders.insert(made);
            }
        }
        op->erase();
    }
    cls.setup.clear();
    for (mlir::Operation * made : feeders) {
        if (made->use_empty()) { made->erase(); }
    }
}

/// The C++ name of the class whose constructor is `target`: the JavaScript
/// name, without the importer's `$N` suffix.
std::string closureLifter::classNameOf(ctjs::FuncOp target) {
    llvm::StringRef name = target.getSymName();
    name = name.substr(0, name.rfind('$'));
    std::string spelled;
    for (const char c : name) { spelled += cpp::is_identifier_char(c) ? c : '_'; }
    return spelled.empty() || (spelled.front() >= '0' && spelled.front() <= '9') ? "K" + spelled
                                                                                 : spelled;
}

} // namespace ctcompile::ctnative::lowering_detail
