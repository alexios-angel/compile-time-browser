// Global identity, reflection and opaque-body effect checks.
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

#include <cassert>
#include <cstdint>
#include <optional>
#include <string>

namespace ctcompile::ctjs::globals_detail {

// The function index the importer put after the last `$` of the symbol.
std::optional<std::uint32_t> function_index_of(FuncOp function) {
    const llvm::StringRef name = function.getSymName();
    const std::size_t dollar = name.rfind('$');
    if (dollar == llvm::StringRef::npos) { return std::nullopt; }
    std::uint32_t index = 0;
    if (name.substr(dollar + 1).getAsInteger(10, index)) { return std::nullopt; }
    return index;
}

// THE NAMES THE SHELL BINDS THE WINDOW OBJECT TO, AND `self` IS ONE OF THEM.
//
// the end of install_window in lib/Shell/bindings/window/window.cpp is three define_global calls of
// the same proxy - `window`, `globalThis` AND `self` - and this list held two.
// hands_back_the_global_object() below already lists `self` among the reads
// that alias, and its own comment names all three, so the omission was a
// soundness hole rather than a policy: `self.x = 1` reaches the proxy's `set`
// trap and therefore define_global("x", 1), through a value this walk never
// marked. The idiom is not exotic - testharness.js is
// `(function (global_scope) { ... }(self))`, because a library that may run in
// a worker never writes `window` at all.
bool names_global_object(llvm::StringRef name) {
    return name == "globalThis" || name == "window" || name == "self";
}

// A global whose value compiles source text at run time.
bool names_eval(llvm::StringRef name) {
    return name == "Function" || name == "eval";
}

std::string describe(mlir::Operation * op) {
    std::string text = op->getName().getStringRef().str();
    if (auto file = mlir::dyn_cast<mlir::FileLineColLoc>(op->getLoc())) {
        text += " at " + file.getFilename().str() + ":" + std::to_string(file.getLine());
    } else if (auto fused = mlir::dyn_cast<mlir::FusedLoc>(op->getLoc())) {
        for (const mlir::Location part : fused.getLocations()) {
            if (auto inner = mlir::dyn_cast<mlir::FileLineColLoc>(part)) {
                text += " at " + inner.getFilename().str() + ":" + std::to_string(inner.getLine());
                break;
            }
        }
    }
    return text;
}

// The constant string key of a property access, or empty.
llvm::StringRef constant_key(mlir::Value key) {
    auto constant = key.getDefiningOp<ConstantOp>();
    if (!constant) { return {}; }
    auto text = llvm::dyn_cast<StringAttr>(constant.getValue());
    return text ? text.getValue() : llvm::StringRef{};
}

// CAN THIS VALUE BE A FUNCTION OBJECT?
//
// THE QUESTION THE `.constructor` CLAUSE FORGOT TO ASK. `o.constructor` is
// `Function` only when `o` is one: `constructor` is an own property of every
// `X.prototype` table and holds the function that owns that table (compile/
// classes.cpp writes it for a class, vm/call.cpp:502 for an ordinary function,
// builtins/internal.hpp:635 for the natives), and `Function.prototype`'s copy
// is the one holding `Function`. Every function object reaches that table,
// which is why `(function(){}).constructor` IS the compiler; a plain object
// reaches `Object.prototype` first and gets `Object`.
//
// THE ANSWER IS "YES" UNLESS THE DEFINING OPERATION SAYS OTHERWISE, so a block
// argument, a load, a call result and any operation not listed here stay
// tainted. The list is only the shapes that CONSTRUCT something non-callable.
//
// AND `Object` IS ITSELF A FUNCTION. `({}).constructor` is untainted by this -
// the receiver is a literal - and its RESULT is `Object`, which is callable and
// whose own `.constructor` IS `Function`. That second read's receiver is a
// ctjs.get_property, which is NOT in the list, so `({}).constructor.constructor`
// is still tainted. Listing the results as non-functions too is the mistake
// this paragraph exists to stop.
bool may_be_function(mlir::Value value) {
    mlir::Operation * definition = value.getDefiningOp();
    if (definition == nullptr) { return true; } // a block argument: unknown

    // A PRIMITIVE, OR A FRESH NON-CALLABLE. ctjs.constant carries undefined,
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

    // THE `super(...)` DESUGARING, AND IT IS 17 OF BOOTSTRAP'S 38 CONSTRUCTOR
    // READS.
    //
    // `super(t, e)` is not a property read anybody wrote. compile/expressions
    // .cpp:640 and :685 compile it to `load_home; get_proto; get_prop
    // "constructor"` - the parent constructor, reached through the class's own
    // prototype chain - and the taint answered "a value that may be the
    // run-time compiler escapes into ctjs.call", because the very next thing
    // `super(...)` does is call it.
    //
    // WHY THAT VALUE IS NOT `Function`:
    //   * `__home` inside a constructor is the class's own `prototype` table,
    //     an op::new_object (compile/classes.cpp builds it and installs it);
    //   * ctjs.get_proto of that table is the link `extends` installed, which
    //     is the PARENT's `prototype` table (classes.cpp: get_prop "prototype"
    //     then set_proto);
    //   * a `prototype` table's own `constructor` is the function that owns
    //     it, never `Function` - unless that table IS `Function.prototype`,
    //     which means the class extends `Function`;
    //   * and naming `Function` needs ctjs.load_global "Function", which this
    //     clause refuses module-wide, or another `.constructor` read whose
    //     value escapes into the `extends` clause, which this same walk
    //     refuses.
    //
    // THE ONE PREMISE THE SSA WALK CANNOT SEE IS CHECKED, in
    // prototype_replaced(): step two assumes nothing put a CALLABLE where that
    // link points, and `X.prototype = <a function>` would.
    // `Object.setPrototypeOf(X.prototype, f)` would too and is a call into a
    // native - invisible here, exactly as a global object obtained without
    // naming it (`(function(){ return this })()`) is invisible to the rest of
    // this clause. Said rather than papered over.
    if (auto link = mlir::dyn_cast<GetProtoOp>(definition)) {
        if (link.getObject().getDefiningOp<LoadHomeOp>() != nullptr) { return false; }
    }
    return true;
}

// THE PREMISE OF THE SUPER CLAUSE, CHECKED RATHER THAN ASSUMED.
//
// A class's `prototype` table is written by the class machinery itself -
// compile/classes.cpp emits `set_prop dst, "prototype", <the new_object>` - so
// the ordinary case is a ctjs.create_object and provably not callable. A
// `X.prototype = <anything this pass cannot rule out>` can point some class's
// parent link at a function, whose `.constructor` IS `Function`. One such
// write anywhere retires the super clause for the whole module. It does not
// REFUSE the module: the clause it retires is a relaxation, so withdrawing it
// only restores the older, coarser answer.
bool prototype_replaced(mlir::ModuleOp module) {
    bool replaced = false;
    module.walk([&](SetPropertyOp set) {
        if (constant_key(set.getKey()) == "prototype" && may_be_function(set.getValue())) {
            replaced = true;
            return mlir::WalkResult::interrupt();
        }
        return mlir::WalkResult::advance();
    });
    return replaced;
}

// FROM THE GLOBAL OBJECT, WHICH NAMED READS HAND IT BACK?
//
// THE SAME QUESTION AS THE ONE BELOW, ASKED OF THE OTHER WATCHED KIND, AND ON
// BOOTSTRAP IT IS THE ONE THAT STILL BINDS AFTER THE `.constructor` CLAUSE IS
// FIXED. The walk propagated through EVERY read - "a read through it yields
// another value that may BE it (`window.window`)" - so
// `window.getComputedStyle(el)` marked the getter and then answered "the
// global object escapes into ctjs.call". Bootstrap reads eight names off the
// window (getComputedStyle, CSS, jQuery, innerWidth, document, escape,
// PointerEvent, DOMParser) and calls or returns every one of them; not one is
// the window.
//
// THE ALIAS SET IS THE HOST'S AND IT IS CLOSED. lib/Shell/bindings/window/window.cpp
// binds `window`, `globalThis` and `self` to the proxy as GLOBALS (install_window)
// and sets `parent` and `top` on the target to the same proxy (975-976);
// `frames` is listed because it is the standard fifth and costs nothing.
// Reading any other name off the window yields whatever global has that name,
// and for one of THOSE to be the window the program would have to have stored
// the window into a global - which is a ctjs.store_global of the marked value,
// an escape, and already the answer. So either this walk has already refused
// the module, or the five names below are the only reads that alias.
//
// WHAT IT DOES NOT CLOSE, and did not before either: `document.defaultView` is
// the window and `document` is an ordinary global this walk never marks, so
// `document.defaultView.x = 1` was invisible before this change and is
// invisible after it. The hole belongs to "which values are watched", not to
// "how far a watched value propagates".
bool hands_back_the_global_object(llvm::StringRef key) {
    return key.empty() || key == "window" || key == "globalThis" || key == "self" ||
           key == "parent" || key == "top" || key == "frames";
}

// FROM A VALUE THAT MAY BE `Function`, WHICH NAMED READS CAN HAND IT BACK?
//
// THE SECOND HALF OF THE PRECISION, AND ON THE CORPORA THE LARGER HALF. The
// taint propagates through every property read, which is right for
// `f.constructor.constructor` and absurd for `this.constructor.NAME` - a
// string - or for `this.constructor.eventName("show")`, which is bootstrap's
// commonest idiom and used to answer "escapes into ctjs.call". No named
// property of `Function` is `Function`, except:
//
//   constructor  `Function.constructor` is `Function`
//   prototype    and `Function.prototype.constructor` is too
//   __proto__    the same table under its other name
//   call/apply/bind  hand back something that CALLS `Function`
//
// A COMPUTED KEY IS UNKNOWN and keeps the taint - which is what an empty
// constant_key means here, and also what `o[""]` gets, harmlessly.
bool hands_back_the_compiler(llvm::StringRef key) {
    return key.empty() || key == "constructor" || key == "prototype" || key == "__proto__" ||
           key == "call" || key == "apply" || key == "bind";
}

// THE SKIPPED ROWS, READ FOR THE ONE THING THAT STILL REFUSES THE MODULE.
//
// A body the importer dropped is a body whose `ctjs.store_global`s are not
// here to be counted, and the first version of this clause therefore refused
// every name in the program the moment ONE function was skipped: 101 globals
// on p5 from 51 skips, 72 on phaser from TWO. Sound, and far too coarse.
//
// The importer now summarises each dropped body FROM ITS BYTECODE, which it
// still has: `stores` is the exact set of names the body's op::set_globals
// name - a static index into the function's own name pool, needing no lowering
// to read - and `opaque` says when that set does not bound the body. So only
// `opaque` is a module-wide answer; `stores` is refused per NAME below.
//
// A ROW WITH NO SUMMARY IS OPAQUE. Hand-written IR, and any translator
// predating the summary, produce rows without the two keys, and reading a
// missing key as "stores nothing" would resolve names a dropped body rebinds -
// the exact unsoundness this clause exists for. Absent means unknown.
std::optional<std::string> opaque_refusal(mlir::ModuleOp module) {
    auto skipped = module->getAttrOfType<mlir::ArrayAttr>("ctjs.skipped");
    if (!skipped) { return std::nullopt; }
    for (const mlir::Attribute row : skipped) {
        auto fields = llvm::dyn_cast<mlir::DictionaryAttr>(row);
        const auto opaque = fields ? fields.getAs<mlir::StringAttr>("opaque") : mlir::StringAttr{};
        const auto stores = fields ? fields.getAs<mlir::ArrayAttr>("stores") : mlir::ArrayAttr{};
        if (!opaque || !stores) {
            return "the importer refused " + std::to_string(skipped.size()) +
                   " function(s) (ctjs.skipped) and one of them carries no globals summary, so a "
                   "body this pass cannot read may store any global";
        }
        if (!opaque.getValue().empty()) { return opaque.getValue().str(); }
    }
    return std::nullopt;
}

// AND THE NAMES THOSE BODIES MAY STORE, which are refused one by one.
llvm::DenseSet<mlir::StringAttr> refused_store_names(mlir::ModuleOp module) {
    llvm::DenseSet<mlir::StringAttr> names;
    auto skipped = module->getAttrOfType<mlir::ArrayAttr>("ctjs.skipped");
    if (!skipped) { return names; }
    for (const mlir::Attribute row : skipped) {
        auto fields = llvm::dyn_cast<mlir::DictionaryAttr>(row);
        if (!fields) { continue; } // opaque_refusal has already refused the module
        auto stores = fields.getAs<mlir::ArrayAttr>("stores");
        if (!stores) { continue; }
        for (const mlir::Attribute one : stores) {
            if (auto text = llvm::dyn_cast<mlir::StringAttr>(one)) { names.insert(text); }
        }
    }
    return names;
}

// Whether every operation before `store` in its block belongs to the hoisting
// prologue: the frame, constants, and closure/store pairs.
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
