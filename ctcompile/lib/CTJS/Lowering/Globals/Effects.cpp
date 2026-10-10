//===- Effects.cpp - Global-object and compiler effect checks ---*- C++ -*-===//
//
// Helpers for --ctjs-resolve-globals' clause 5 (nothing writes the globals
// table dynamically): which names and property reads can yield the global
// object or the run-time compiler (`Function`), which values may be functions,
// and what the importer's ctjs.skipped rows say about bodies it refused.
//
//===----------------------------------------------------------------------===//
#include "Effects.h"
#include "ctcompile/CTJS/IR/CTJSAttrs.h"
#include "ctcompile/CTJS/IR/CTJSDialect.h"
#include "ctcompile/CTJS/IR/CTJSOps.h"
#include "ctcompile/CTJS/Transforms/Passes.h"

#include "mlir/IR/Builders.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/SymbolTable.h"
#include "mlir/Interfaces/ControlFlowInterfaces.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/MapVector.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringSet.h"

#include <cassert>
#include <cstdint>
#include <optional>
#include <string>

namespace ctcompile::ctjs::globals_detail {

/// Whether \p name is one of the globals the shell binds to the window proxy:
/// install_window (ctbrowser/lib/Shell/bindings/window/window.cpp) defines
/// `window`, `globalThis` and `self`. Missing one is a soundness hole:
/// `self.x = 1` reaches the proxy's `set` trap and defines global `x`.
/// Libraries that may run in a worker (testharness.js) use only `self`.
bool names_global_object(llvm::StringRef name) {
    return name == "globalThis" || name == "window" || name == "self";
}

/// Whether global \p name compiles source text at run time.
bool names_eval(llvm::StringRef name) {
    return name == "Function" || name == "eval";
}

/// Describes \p op for a diagnostic: its name, then its own file location or
/// the first file part of a fused location.
std::string describe(mlir::Operation * op) {
    std::string text = op->getName().getStringRef().str();
    mlir::FileLineColLoc file = mlir::dyn_cast<mlir::FileLineColLoc>(op->getLoc());
    if (auto fused = mlir::dyn_cast<mlir::FusedLoc>(op->getLoc()); !file && fused) {
        for (const mlir::Location part : fused.getLocations()) {
            if ((file = mlir::dyn_cast<mlir::FileLineColLoc>(part))) { break; }
        }
    }
    if (file) { text += " at " + file.getFilename().str() + ":" + std::to_string(file.getLine()); }
    return text;
}

/// Whether \p value may be a function object.
///
/// This matters for `.constructor` reads: `o.constructor` is `Function` only
/// when `o` is a function. `constructor` is an own property of every
/// `X.prototype` table and holds the function that owns that table
/// (compile/classes.cpp writes it for a class, ensure_prototype in
/// vm/call/construct.cpp for an ordinary function, and the native-constructor
/// setup in ctbrowser/lib/Script/builtins/internal.hpp for the natives), and
/// `Function.prototype`'s copy holds `Function`. Every function object reaches
/// that table, which is why `(function(){}).constructor` is the compiler; a
/// plain object reaches `Object.prototype` first and gets `Object`.
///
/// The answer is "yes" unless the defining operation says otherwise, so a
/// block argument, a load, a call result and any unlisted operation stay
/// tainted. Only shapes that construct something non-callable are listed.
///
/// `Object` is itself a function. `({}).constructor` is untainted (the receiver
/// is a literal), but its result is `Object`, whose own `.constructor` is
/// `Function`. That second read's receiver is a ctjs.get_property, which is
/// not listed, so `({}).constructor.constructor` stays tainted. Do not list
/// property-read results as non-functions.
bool may_be_function(mlir::Value value) {
    mlir::Operation * definition = value.getDefiningOp();
    if (definition == nullptr) { return true; } // a block argument: unknown

    // A primitive, or a fresh non-callable. ctjs.constant carries undefined,
    // null, a string, a number or a boolean and nothing else; the arithmetic,
    // predicate and delete operations answer primitives; create_object,
    // create_array, create_regexp, own_keys and the two `arguments`
    // operations answer a fresh object or array.
    if (mlir::isa<ConstantOp, CreateObjectOp, CreateArrayOp, CreateRegExpOp, MakeArgumentsOp,
                  GatherRestOp, OwnKeysOp, BinaryOp, BinaryStaticOp, UnaryOp, CompareOp, TruthyOp,
                  FromBoolOp, InstanceOfOp, HasPropertyOp, DeletePropertyOp, DeleteNamedOp>(
            definition)) {
        return false;
    }

    // The `super(...)` desugaring, the most common `.constructor` read in real
    // class-based code.
    //
    // `super(t, e)` is not a property read anybody wrote:
    // compiler_impl::compile_call_target
    // (ctbrowser/lib/Script/compile/expression_calls.cpp) compiles it to
    // `load_home; get_proto; get_prop "constructor"`, the parent constructor
    // reached through the class's own prototype chain, and then calls it.
    //
    // Why that value is not `Function`:
    //   * `__home` inside a constructor is the class's own `prototype` table,
    //     an op::new_object (compile/classes.cpp builds and installs it);
    //   * ctjs.get_proto of that table is the link `extends` installed, which
    //     is the parent's `prototype` table (classes.cpp: get_prop "prototype"
    //     then set_proto);
    //   * a `prototype` table's own `constructor` is the function that owns
    //     it, never `Function`, unless that table is `Function.prototype`,
    //     which means the class extends `Function`;
    //   * and naming `Function` needs ctjs.load_global "Function", which this
    //     clause refuses module-wide, or another `.constructor` read whose
    //     value escapes into the `extends` clause, which this same walk
    //     refuses.
    //
    // The one premise the SSA walk cannot see is checked in
    // prototype_replaced(): the second bullet above assumes nothing put a
    // callable where that link points, and `X.prototype = <a function>` would.
    // `Object.setPrototypeOf(X.prototype, f)` would too and is a call into a
    // native, invisible here, just as a global object a native or the host
    // hands over (`document.defaultView`, `this` in a timer callback) is
    // invisible to the rest of this clause. This is a known hole.
    if (auto link = mlir::dyn_cast<GetProtoOp>(definition)) {
        if (link.getObject().getDefiningOp<LoadHomeOp>() != nullptr) { return false; }
    }
    return true;
}

/// Whether any `X.prototype = v` store in \p module may store a function.
///
/// This checks the premise of the `super(...)` case in may_be_function(). A
/// class's `prototype` table is written by the class machinery itself
/// (compile/classes.cpp emits `set_prop dst, "prototype", <the new_object>`),
/// so normally it is a ctjs.create_object and provably not callable. A store
/// this pass cannot rule out could point some class's parent link at a
/// function, whose `.constructor` is `Function`. One such write disables the
/// super case for the whole module. It does not refuse the module: the super
/// case is a relaxation, so withdrawing it only restores the coarser answer.
bool prototype_replaced(mlir::ModuleOp module) {
    bool replaced = false;
    module.walk([&](SetPropertyOp set) {
        if (constantKey(set.getKey()) == "prototype" && may_be_function(set.getValue())) {
            replaced = true;
            return mlir::WalkResult::interrupt();
        }
        return mlir::WalkResult::advance();
    });
    return replaced;
}

/// Whether reading property \p key off the global object can yield the global
/// object again. Only the host's aliases do: `window`, `globalThis` and `self`
/// (globals) and `parent`, `top` (set on the window; `frames` is the standard
/// fifth). Any other name yields whatever global has that name, and storing
/// the window into a global is already an escape. So `window.getComputedStyle`
/// does not keep the taint. An empty key (computed) is unknown and keeps it.
/// Not covered: `document.defaultView`, since `document` is never marked.
bool hands_back_the_global_object(llvm::StringRef key) {
    static const llvm::StringSet<> aliases{"window", "globalThis", "self",
                                           "parent", "top",        "frames"};
    // The length check is a cheap reject before hashing: "globalThis" (10)
    // is the longest alias.
    return key.empty() || (key.size() <= 10 && aliases.contains(key));
}

/// Whether reading property \p key off a value that may be `Function` can
/// yield `Function` (or something that calls it) again.
///
/// The taint must follow `f.constructor.constructor`, but not
/// `this.constructor.NAME` (a string) or `this.constructor.eventName("show")`,
/// a common Bootstrap idiom. No named property of `Function` is `Function`,
/// except:
///
///   constructor  `Function.constructor` is `Function`
///   prototype    and `Function.prototype.constructor` is too
///   __proto__    the same table under its other name
///   call/apply/bind  hand back something that calls `Function`
///
/// A computed key (an empty constantKey) is unknown and keeps the taint; a
/// literal `o[""]` gets the same answer, harmlessly.
bool hands_back_the_compiler(llvm::StringRef key) {
    static const llvm::StringSet<> aliases{"constructor", "prototype", "__proto__",
                                           "call",        "apply",     "bind"};
    // "constructor" (11) is the longest alias; see hands_back_the_global_object.
    return key.empty() || (key.size() <= 11 && aliases.contains(key));
}

/// Reads the module's `ctjs.skipped` rows: functions the importer did not
/// translate, so their `ctjs.store_global`s are not in the IR. Each row carries
/// `stores` (the global names the body's bytecode writes, read from its name
/// pool) and `opaque` (non-empty when that set does not bound the body). Names
/// in `stores` are refused one by one; an `opaque` row, or a row with no
/// summary at all (hand-written IR, older importers), refuses the whole module,
/// since a missing summary must mean "may store anything". The first such row
/// supplies the reason.
skipped_summary read_skipped(mlir::ModuleOp module) {
    skipped_summary summary;
    auto skipped = module->getAttrOfType<mlir::ArrayAttr>("ctjs.skipped");
    if (!skipped) { return summary; }
    for (const mlir::Attribute row : skipped) {
        auto fields = llvm::dyn_cast<mlir::DictionaryAttr>(row);
        const auto opaque = fields ? fields.getAs<mlir::StringAttr>("opaque") : mlir::StringAttr{};
        const auto stores = fields ? fields.getAs<mlir::ArrayAttr>("stores") : mlir::ArrayAttr{};
        if (!summary.opaque && (!opaque || !stores)) {
            summary.opaque =
                "the importer refused " + std::to_string(skipped.size()) +
                " function(s) (ctjs.skipped) and one of them carries no globals summary, so a "
                "body this pass cannot read may store any global";
        } else if (!summary.opaque && !opaque.getValue().empty()) {
            summary.opaque = opaque.getValue().str();
        }
        if (!stores) { continue; }
        for (const mlir::Attribute one : stores) {
            if (auto text = llvm::dyn_cast<mlir::StringAttr>(one)) { summary.stores.insert(text); }
        }
    }
    return summary;
}

/// Whether every operation before \p store in its block belongs to the
/// hoisting prologue: the frame, constants, and closure/store pairs.
bool in_prologue(StoreGlobalOp store) {
    for (mlir::Operation & before : *store->getBlock()) {
        if (&before == store.getOperation()) { return true; }
        if (!mlir::isa<FrameEnterOp, ConstantOp, CreateClosureOp, StoreGlobalOp>(before)) {
            return false;
        }
    }
    return false;
}

} // namespace ctcompile::ctjs::globals_detail
