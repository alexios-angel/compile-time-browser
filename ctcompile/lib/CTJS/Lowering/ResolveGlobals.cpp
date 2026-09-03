// THE CLOSED WORLD - Phase 62½-A.
//
// A whole-program compile knows every call site, and the importer's IR says
// nothing of the kind. `function add(a, b) {...}` at the top level is
//
//     %6 = ctjs.create_closure %arg2[1] this %arg0
//     ctjs.store_global "add", %6
//
// and every `add(x, x)` anywhere in the program is
//
//     %f = ctjs.load_global "add"
//     %r = ctjs.call %f(%receiver, %x, %x)
//
// - a name, a table lookup, and a dispatch. No pass can type `add`'s
// parameters from that, because no pass can see who calls `add`. This one
// proves who does, per global NAME across the whole module, and writes the
// answer where MLIR reads it: a ctjs.call_direct @add$1 at every call site,
// and `private` on the function once every caller is one of those.
//
// WHAT IS PROVED, AND WHY EACH CLAUSE IS THERE.
//
//  1. THE NAME IS STORED EXACTLY ONCE IN THE PROGRAM. Two stores mean the
//     binding changes at run time and a call site cannot know which closure
//     it reaches (`function f(){}; f = 1;` - the negative case in the test).
//  2. THAT STORE IS OF A CLOSURE, DIRECTLY. The store's SSA operand is a
//     ctjs.create_closure result - not a load, not a call result, not a
//     block argument. The closure's function index F is the callee.
//  3. THE STORE IS IN THE TOP LEVEL'S HOISTING PROLOGUE: in the entry block of
//     the function with index 0, preceded only by frame_enter, constants and
//     other closure/store pairs. This is the clause the plan's sentence
//     ("bound exactly once ... never reassigned") does not spell and that
//     soundness needs: a single store that runs LATE - `f(); f = function(){}`
//     or a closure bound inside another function - leaves a window in which
//     the VM throws for an unbound name and a direct call would not. Function
//     declarations are hoisted to the start of the script by the compiler,
//     so every call in the program runs after the prologue.
//  4. THE CLOSURE VALUE HAS NO OTHER USE than the store, and F's own callee
//     argument (%arg2, the value `load_callee` reads) is used only to build
//     nested closures or read upvalues. Otherwise the closure escapes and the
//     VM may call F through a value this IR cannot see - which is why that
//     clause gates `private` rather than the rewrite: the rewrite is right
//     whenever the callee is F; `private` claims every caller is visible.
//  5. NOTHING WRITES THE GLOBALS TABLE DYNAMICALLY. In this VM that table is
//     context::globals_, written by op::set_global (run_loop.cpp,
//     VM_CASE(set_global)), its AOT helper, and the host's define_global.
//
//     AND `globalThis.add = 1` DOES REBIND `add` IN THIS ENGINE. This comment
//     said the opposite - that `window` and `globalThis` are the Shell's
//     window_view and "do NOT alias it", so the rule was "kept anyway,
//     conservatively... and the cost is nothing on the programs this MVP
//     targets". Both halves were wrong, and the line it cited is the proof of
//     the first: lib/Shell/bindings/window.cpp:911-923 is the PROXY's `set`
//     trap, and its else-arm is `c.define_global(name, args[2])`, which is
//     `globals_[name] = v` (script/vm.hpp:257). A write through the window
//     that does not hit an own property of the window target is a global
//     binding, exactly as the specification says. So this clause is
//     LOAD-BEARING, not spec-conformance politeness, and it may not be
//     relaxed on the grounds that this engine is simpler than a browser.
//
//     Any ctjs.load_global of those names whose value reaches an operation
//     that writes through it under a key this pass cannot name, escapes into a
//     call this pass cannot name, or reaches an operation this walk does not
//     know, resolves NOTHING in the module. A NAMED READ OFF IT DOES NOT
//     PROPAGATE unless the name is one of the host's own self-aliases
//     (hands_back_the_global_object), which is what keeps
//     `window.getComputedStyle(el)` from answering.
//
//     TWO OF THOSE CLAUSES USED TO BE COARSER THAN THEIR OWN REASONING.
//
//       * A CALL WAS AN ESCAPE, ALWAYS. `(function (g) { ... })(globalThis)`
//         is a closure defined in the same module and called two tokens later,
//         and the object does not leave this pass's sight at all - it arrives
//         at a parameter. The walk now continues there when known_callee()
//         proves which ctjs.func the call enters, by the SAME proof the census
//         uses for a name bound once to a create_closure. Unknown is still the
//         bail, and unknown is most of them.
//       * A WRITE THROUGH IT REFUSED THE MODULE, whatever the key.
//         `globalThis.x = 1` reaches the proxy's `set` trap, whose two arms
//         are store_property and `define_global(name, args[2])` - both about
//         the name written and neither about any other. So a CONSTANT key
//         binds that one name and refuses it in the census, exactly as clause
//         6 refuses the names a dropped body stores; a computed key,
//         `__proto__`, a delete, an accessor, a set_proto and a copy_props
//         still refuse everything.
//
//     NEITHER MOVED A VENDOR BUNDLE, AND CLAUSE 5 IS NOT WHERE ONE IS LOST.
//     Disabling this whole function - `return std::nullopt` before the walk -
//     leaves bootstrap at 0 of 37 resolved and 0 ctjs.call_direct, p5 at 0 of
//     101 and phaser at 0 of 72, with the rows reading "never stored in this
//     program - a host binding": 37 of 37 on bootstrap, 96 of 101 on p5 (4
//     bound outside the prologue, 1 to something other than a closure) and 72
//     of 72 on phaser. A BUNDLE DECLARES NOTHING AT GLOBAL SCOPE - each hands
//     a factory's result to one property of the window - so there is no NAME
//     for the census to bind and clause 1 refuses every row before clause 5 is
//     consulted. The escape reasons those rows carried were decorations.
//
//     That is not a licence to relax anything here: the reasons are still
//     sound and they are the ceiling on programs that DO declare functions
//     (differential.js resolves 56 of 72, launcher.js 9 of 10). It says where
//     a bundle is actually lost, which is the census's first clause.
//
//     Bootstrap's first escape is `window.scrollTo(...)` at
//     bootstrap.bundle.js:3062 - a native reached through a property read, and
//     genuinely unknown. THE OTHER HALF OF known_callee() IS THE LEVER NOBODY
//     HAS PULLED: a ctjs.call whose callee is a ctjs.create_closure result is
//     provably that function with no global name involved at all, and
//     rewriting those to ctjs.call_direct is the only path to a direct call
//     inside a bundle - 21 such sites in bootstrap, 42 in p5, 50 in phaser.
//
//     The eval-like case is real here too: `Function(...)` compiles and runs a
//     NEW program (builtins/objects.cpp, install_dynamic_function), whose body
//     can set_global anything, so a load of "Function" or "eval" also resolves
//     nothing, as does ctjs.dynamic_import. Measured on the corpora, that is
//     not hypothetical: it is the REASON PRINTED for all 72 of phaser's rows -
//     "a run-time compiler is reachable (ctjs.load_global at phaser.js:N)".
//     p5's 101 print clause 6's `opaque`, not this one. Both are first-past-
//     the-post: the paragraph above shows what is under them.
//
//  6. NO BODY THE IMPORTER REFUSED STORES THIS NAME. A `gave_up` function
//     emits no ctjs.func, so its `ctjs.store_global`s are not here to be
//     counted and a name stored once visibly and once inside such a body looks
//     singly bound. That used to refuse EVERY name in the program - 101
//     globals on p5 from 51 refusals, 72 on phaser from TWO - on the grounds
//     that "nothing says which names the missing bodies touch". Nothing in the
//     IR does. THE BYTECODE DOES: op::set_global names its target with
//     `proto.names[in.bx()]`, a static index into the function's own name
//     pool, so the importer reads the exact set off a body it is about to drop
//     and puts it on the ctjs.skipped row (`stores`). Only a body whose
//     summary cannot bound it - one whose pools name `Function`, `eval`,
//     `constructor`, `globalThis` or `window`, or whose code holds a dynamic
//     import - still refuses the module, and says so in `opaque`.
//
//     THE SUMMARY'S LIMIT IS THIS CLAUSE'S OWN LIMIT, deliberately. A
//     `constructor` key computed at run time is invisible to the summary
//     exactly as `constant_key` makes it invisible here, so the closed world
//     has ONE stated hole rather than a different one on each side of the
//     importer. An earlier summary refused every op::get_index in a body whose
//     string pool held the word, which bought nothing the IR walk was not
//     already giving away and cost p5 all 101 of its globals.
//
// AND THE `.constructor` CLAUSE OF 5 ASKS WHAT THE RECEIVER MAY BE. `Function`
// on every function's prototype is a real hazard and marking EVERY
// `get_property` whose constant key is "constructor" is not the way to say it:
// `super(t, e)` desugars to exactly that shape (compile/expressions.cpp:640) -
// 17 of bootstrap's 38 such reads - and `({}).constructor` is `Object`.
// may_be_function() answers which receivers can hand back the compiler, and
// hands_back_the_compiler() answers which named reads off such a value can:
// `this.constructor.NAME` is a string, and that idiom is the other 21.
//
// WHAT THE REWRITE PASSES. ctjs.call_direct's operands are the callee's entry
// block in order - receiver, new.target, callee, parameters (BytecodeImport
// .cpp, `implicit_arguments`). The receiver is the call's own; the callee is
// the load_global result, KEPT, because the boxed tier dispatches on it;
// new.target is a fresh `ctjs.constant #ctjs.undefined`, which is what the VM
// gives a plain call: VM_CASE(call) pushes `entered.new_target =
// pending_new_target_` and that root is undefined unless a pass_new_target or
// a construct set it (run_loop.cpp; context::call in call.cpp does the same
// two lines). A function containing ctjs.pass_new_target therefore has none
// of its calls rewritten - adjacency is an invariant nothing checks, and the
// pending flag could be live at any call in it.
//
// ARITY. Fewer arguments than parameters are padded with undefined, as
// VM_CASE(call) fills `registers_[new_base + i] = undefined` for i in
// [argc, param_count). MORE are not rewritten: the surplus lands in the
// callee's raw register window, where `arguments` and a rest parameter read
// it (type_record.cpp's note on context::call copying EVERY argument), and
// call_direct has no slot for a value the callee's block does not name. The
// native tier diagnoses such a site.
//
// ONE PROGRAM PER MODULE, ASSUMED AND SAID. A function index means nothing
// outside the program it was compiled in (ctjs.create_closure's own words),
// and the index is recovered from the symbol the importer built - `name$N`,
// BytecodeImport.cpp: `name += "$" + std::to_string(index)`. A module
// holding two programs would have two `$0`s and fail to verify long before
// this pass ran.
//
// C++ RATHER THAN DRR, per part 23 §1.2: this is a census across a module - a
// symbol table, a store count per name, an SSA reachability - and a Pat<>
// matches one operation's shape. The op it produces IS TableGen
// (CTJSOps.td, ctjs.call_direct); the pass declaration IS TableGen
// (Passes.td); only the analysis is here.
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

namespace ctcompile::ctjs {

#define GEN_PASS_DEF_CTJSRESOLVEGLOBALS
#include "ctcompile/CTJS/Transforms/Passes.h.inc"

namespace {

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
// lib/Shell/bindings/window.cpp:934-948 is three define_global calls of the
// same proxy - `window`, `globalThis` AND `self` - and this list held two.
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

// The two kinds of value this walk follows. They escape and are written
// through in the same places, but they are different objects and a diagnostic
// that names the wrong one sends a reader to the wrong line.
enum class watched {
    global_object, // globalThis / window, whose properties ARE the globals
    compiler,      // a value that may be `Function`, which compiles source
};

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
// THE ALIAS SET IS THE HOST'S AND IT IS CLOSED. lib/Shell/bindings/window.cpp
// binds `window`, `globalThis` and `self` to the proxy as GLOBALS (934-948)
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

struct binding {
    llvm::SmallVector<StoreGlobalOp> stores;
    llvm::SmallVector<LoadGlobalOp> loads;
};

// THE CENSUS AS ONE VALUE, because clause 5 now reads it. Following the global
// object into a callee asks precisely the question the per-name proof asks -
// "which one ctjs.func is this?" - and a second copy of that proof, written
// beside the first, is how a closed world quietly stops being closed.
struct census {
    llvm::MapVector<mlir::StringAttr, binding> bindings;
    llvm::DenseMap<std::uint32_t, FuncOp> by_index;
    llvm::DenseSet<mlir::StringAttr> refused; // clause 6, per name
    FuncOp top;                               // function index 0, or null
};

// CLAUSES 1-3 AND 6 FOR ONE NAME: everything the census proves that does not
// depend on clause 5's SSA walk. Answers the function, or the reason there is
// none.
//
// CLAUSE 5 IS DELIBERATELY ABSENT. It is a module-wide answer computed BY the
// walk, so asking for it here would be circular - the walk calls this to decide
// where a marked value goes next. That is sound in the direction that matters:
// this says only which function a name IS bound to, and clause 5's own bail
// still decides whether any name resolves at all.
struct verdict {
    FuncOp target;           // null unless the name is bound to exactly one closure
    CreateClosureOp closure; // the ctjs.create_closure that store bound, for clause 4
    std::string reason;      // why not, when the target is null
};

verdict bound_closure(const census & world, mlir::StringAttr name, const binding & facts) {
    const auto no = [](std::string because) {
        return verdict{FuncOp{}, CreateClosureOp{}, std::move(because)};
    };
    if (world.refused.contains(name)) {
        return no("a function the importer refused (ctjs.skipped) stores this name, so the "
                  "binding this pass can see is not the only one");
    }
    if (facts.stores.empty()) { return no("never stored in this program - a host binding"); }
    if (facts.stores.size() != 1) {
        return no("stored " + std::to_string(facts.stores.size()) + " times");
    }
    StoreGlobalOp store = facts.stores.front();
    auto made = store.getValue().getDefiningOp<CreateClosureOp>();
    if (!made) { return no("bound to something other than a closure"); }
    FuncOp top = world.top;
    if (!top) { return no("no top-level function (index 0) in the module"); }
    if (store->getParentOfType<FuncOp>() != top || store->getBlock() != &top.getBody().front() ||
        !in_prologue(store)) {
        return no("bound outside the top level's hoisting prologue, so a call may run "
                  "before the binding exists");
    }
    const auto index = static_cast<std::uint32_t>(made.getFunction());
    FuncOp target = world.by_index.lookup(index);
    if (!target) {
        return no("function " + std::to_string(index) +
                  " emitted no ctjs.func - refused by the importer (ctjs.skipped)");
    }
    if (target->hasAttr("ctjs.not_lowered")) {
        return no("function " + target.getSymName().str() + " was refused");
    }
    if (target.getBody().empty() || target.getBody().front().getNumArguments() < 3) {
        return no("function " + target.getSymName().str() + " has no body");
    }
    return verdict{target, made, {}};
}

// WHICH ctjs.func DOES THIS CALLEE VALUE PROVABLY ENTER? Two shapes, and the
// first is the one a bundle is made of.
//
//   * A ctjs.create_closure result. The closure names its function index
//     outright, and calling it enters that function - there is nothing to
//     prove beyond the index being in this module. This is the IIFE, which is
//     what a UMD header is: `(function (global) { ... })(globalThis)` calls a
//     closure defined two tokens earlier, and the global object it hands over
//     used to leave this pass's sight there.
//   * A ctjs.load_global of a name bound_closure() answers for. The SAME proof
//     the per-name census uses, called rather than re-typed.
//
// EVERYTHING ELSE IS UNKNOWN, and unknown is still the bail: a parameter, a
// property read (`window.scrollTo`), a call result, a native. The body behind
// such a value is not in this module and may set_global anything.
FuncOp known_callee(const census & world, mlir::Value callee) {
    if (auto made = callee.getDefiningOp<CreateClosureOp>()) {
        return world.by_index.lookup(static_cast<std::uint32_t>(made.getFunction()));
    }
    if (auto load = callee.getDefiningOp<LoadGlobalOp>()) {
        const auto row = world.bindings.find(load.getNameAttr());
        if (row == world.bindings.end()) { return FuncOp{}; }
        return bound_closure(world, load.getNameAttr(), row->second).target;
    }
    return FuncOp{};
}

// DOES THIS BODY READ ITS RAW ARGUMENT WINDOW? `arguments` and a rest
// parameter copy the FRAME's registers - every value the site passed, not the
// declared ones - and read them back under an index this walk cannot see
// (ctjs.make_arguments' own note: make_arguments_object copies the RAW
// window). A marked value handed to such a body is reachable there through a
// computed key, so a body holding one is not a known callee at all.
bool reads_raw_arguments(FuncOp function) {
    bool reads = false;
    function.walk([&](mlir::Operation * op) {
        if (mlir::isa<MakeArgumentsOp, GatherRestOp>(op)) {
            reads = true;
            return mlir::WalkResult::interrupt();
        }
        return mlir::WalkResult::advance();
    });
    return reads;
}

// PADDING A SHORT CALL IS VISIBLE TO `arguments`, AND IS THE ONE THING THE PAD
// MAY NOT CHANGE.
//
// The rewrite fills a call that passes fewer arguments than the callee declares
// with `ctjs.constant #ctjs.undefined`, which is exactly what VM_CASE(call)
// does to the callee's REGISTERS (run_loop.cpp: `registers_[new_base + i] =
// undefined` for i in [argc, param_count)). It is NOT what op::call does to the
// ARGUMENT WINDOW: make_arguments_object copies the raw window, whose length is
// argc, so `function f(a, b) { return arguments.length; } f(1)` is 1 in the VM.
// A padded ctjs.call_direct would make it 2, because the boxed tier parks
// `getArgs()` and hands ct_aot_call that window (CTJSToEmitC.cpp, the
// `dispatched_arguments` arm).
//
// AT EXACT ARITY THERE IS NO PAD AND NOTHING TO HIDE, which is why this asks
// about the PADDING and not about `arguments`: refusing every callee that
// mentions `arguments` would refuse calls that are already the right length and
// change nothing about them.
bool padding_hides_arguments(FuncOp target, std::size_t supplied, unsigned parameters) {
    return supplied < parameters && reads_raw_arguments(target);
}

// CLAUSE 5: can anything in the module write the globals table other than a
// ctjs.store_global this pass can count? Answers the reason if so.
//
// THE WALK NOW CROSSES A CALL IT CAN NAME, which is this file's change.
// `(function (g) { ... })(globalThis)` used to answer "the global object
// escapes into ctjs.call" and refuse every name in the program; the callee of
// that call is a closure defined in the same module, so the value does not
// leave this pass's sight - it arrives at a parameter, and the walk continues
// there. known_callee() says when that is provable, and three guards say when
// the parameter is the wrong place to continue at:
//
//   * MORE ARGUMENTS THAN PARAMETERS. The surplus lands in the callee's raw
//     register window, which `arguments` and a rest parameter read; there is
//     no block argument for it and no way to follow it.
//   * A BODY THAT READS THAT WINDOW AT ALL, even within arity, for the same
//     reason: ctjs.make_arguments copies every register and a computed index
//     off the result is invisible here.
//   * AN UNKNOWN CALLEE, which is most of them. `window.scrollTo(...)` is a
//     native reached through a property read - and it is bootstrap's FIRST
//     escape, at bootstrap.bundle.js:3062.
//
// FIXPOINT AND TERMINATION. Entering a callee marks a BLOCK ARGUMENT, and
// mark() enqueues a value only the first time it is seen, so a recursive or
// mutually recursive chain marks each parameter once and stops. The module has
// finitely many values and the walk visits each at most once.
//
// AND A NAMED WRITE THROUGH THE OBJECT BINDS ONE NAME, NOT THE MODULE. The
// proxy's `set` trap (lib/Shell/bindings/window.cpp:880-892) is a two-armed
// if: an own property of the window target gets store_property, everything
// else gets `define_global(name, args[2])`. Both arms touch exactly the name
// written, so `globalThis.x = 1` rebinds `x` and says nothing whatever about
// `add`. This was a whole-module refusal - the coarse answer clause 6 already
// had to be talked out of once - and is now per name, in the census, exactly
// as a refused body's stores are.
//
// A COMPUTED KEY STILL REFUSES EVERYTHING, because `globalThis[k] = 1` names
// no name; so does `__proto__`, which is the object's prototype link rather
// than a binding; so do a delete, an accessor definition, a ctjs.set_proto and
// a ctjs.copy_props (`{...globalThis}`'s sibling - every own key of a source
// this pass cannot enumerate).
std::optional<std::string> dynamic_global_writes(
    mlir::ModuleOp module, const census & world,
    llvm::MapVector<mlir::StringAttr, std::string> & bound_through) {
    std::optional<std::string> reason;
    // COMPUTED ONCE, because it is a question about the whole module and
    // may_be_function() is asked once per `.constructor` read.
    const bool super_is_open = prototype_replaced(module);
    llvm::DenseMap<mlir::Value, watched> marked;
    llvm::SmallVector<mlir::Value> work;
    llvm::DenseMap<mlir::Operation *, bool> raw_arguments;
    const auto mark = [&](mlir::Value value, watched kind) {
        if (marked.try_emplace(value, kind).second) { work.push_back(value); }
    };
    const auto name_of = [](watched kind) {
        return kind == watched::global_object ? "the global object"
                                              : "a value that may be the run-time compiler";
    };
    const auto reads_arguments = [&](FuncOp target) {
        const auto row = raw_arguments.try_emplace(target.getOperation(), false);
        if (row.second) { row.first->second = reads_raw_arguments(target); }
        return row.first->second;
    };

    // A FUNCTION THE IMPORTER REFUSED IS A FUNCTION THIS PASS CANNOT READ, and
    // its `ctjs.store_global`s are not in the module to be counted. The census
    // below would then see one store where the program has two and resolve a
    // name that is rebound at run time - a call compiled to the WRONG function
    // rather than a diagnostic.
    //
    // IT IS NO LONGER A WHOLE-MODULE BAIL, because "nothing says which names
    // the missing bodies touch" was not true: the BYTECODE says, and the
    // importer reads it. Only a body whose summary is `opaque` still refuses
    // everything; the rest refuse the names they name, in the census.
    if (const std::optional<std::string> refused = opaque_refusal(module)) { return refused; }

    module.walk([&](mlir::Operation * op) {
        if (reason) { return mlir::WalkResult::interrupt(); }
        if (auto load = mlir::dyn_cast<LoadGlobalOp>(op)) {
            if (names_global_object(load.getName())) {
                mark(load.getResult(), watched::global_object);
            }
            if (names_eval(load.getName())) {
                reason = "a run-time compiler is reachable (" + describe(op) +
                         ") and the program it builds can store any global";
            }
        }
        // `Function` IS NOT ONLY A GLOBAL NAME. It sits on every function's
        // prototype as `.constructor`, so `(function(){}).constructor` is the
        // compiler reached through a property read this pass would otherwise
        // never look at. Following the value rather than refusing the key is
        // what keeps `o.constructor === C` and `o.constructor.name` resolvable:
        // those end at a comparison, and only a call or an escape answers.
        //
        // AND THE RECEIVER IS ASKED FIRST. Marking EVERY `.constructor` read
        // made the clause answer for `super(t, e)` - which is one of these,
        // desugared - and for `({}).constructor`, neither of which can be the
        // compiler. may_be_function() says which receivers can.
        if (auto get = mlir::dyn_cast<GetPropertyOp>(op)) {
            if (constant_key(get.getKey()) == "constructor" &&
                (super_is_open || may_be_function(get.getObject()))) {
                mark(get.getResult(), watched::compiler);
            }
        }
        if (mlir::isa<DynamicImportOp>(op)) {
            reason = "a dynamic import (" + describe(op) +
                     ") loads code this module cannot see, and it can store any global";
        }
        return mlir::WalkResult::advance();
    });
    if (reason) { return reason; }

    // ENTER A KNOWN CALLEE AT ONE ARGUMENT POSITION, or answer why not. The
    // position is already in the CALLEE's entry-block order: receiver,
    // new.target, callee, then the declared parameters.
    const auto enter = [&](FuncOp target, std::size_t supplied, unsigned parameter,
                           mlir::Operation * at, watched kind) -> std::optional<std::string> {
        if (!target || target.getBody().empty() || target.getBody().front().getNumArguments() < 3) {
            return std::string{name_of(kind)} +
                   " escapes into a call whose callee this module does not name (" + describe(at) +
                   ")";
        }
        mlir::Block & entry = target.getBody().front();
        const unsigned parameters = entry.getNumArguments() - 3;
        if (supplied > parameters) {
            return std::string{name_of(kind)} + " is passed to " + target.getSymName().str() +
                   " beyond its " + std::to_string(parameters) +
                   " parameter(s), where the surplus keeps frame semantics (" + describe(at) + ")";
        }
        if (reads_arguments(target)) {
            return std::string{name_of(kind)} + " is passed to " + target.getSymName().str() +
                   ", which reads its raw argument window (" + describe(at) + ")";
        }
        // IN RANGE BY THE ARITY CLAUSE ABOVE: a receiver is 0, a callee value
        // is 2, and argument i of at most `parameters` is 3 + i.
        assert(parameter < entry.getNumArguments() && "a call operand with no block argument");
        mark(entry.getArgument(parameter), kind);
        return std::nullopt;
    };

    // FORWARD SSA REACHABILITY from the global object. A read through it
    // yields another value that may BE it (`window.window`), so reads
    // propagate; predicates and arithmetic answer primitives and stop; a
    // branch forwards into the successor's block argument; a call this module
    // can name forwards into the callee's parameter; a named write through it
    // binds that one name; and anything else is the answer.
    while (!work.empty()) {
        const mlir::Value value = work.pop_back_val();
        const watched kind = marked.find(value)->second;
        for (mlir::OpOperand & use : value.getUses()) {
            mlir::Operation * op = use.getOwner();
            if (auto branch = mlir::dyn_cast<mlir::BranchOpInterface>(op)) {
                if (const std::optional<mlir::BlockArgument> argument =
                        branch.getSuccessorBlockArgument(use.getOperandNumber())) {
                    mark(*argument, kind);
                }
                continue;
            }
            if (mlir::isa<TruthyOp, CompareOp, InstanceOfOp, HasPropertyOp, UnaryOp, BinaryOp,
                          BinaryStaticOp, RootOp>(op)) {
                continue; // a primitive comes out, never the object
            }
            if (auto get = mlir::dyn_cast<GetPropertyOp>(op)) {
                // A NAMED READ OFF A WATCHED VALUE IS NOT THAT VALUE, unless it
                // is one of the few names that hand it back. `Function.NAME` is
                // undefined and `window.getComputedStyle` is a function;
                // `this.constructor.eventName("show")` and
                // `window.getComputedStyle(el)` are bootstrap's two commonest
                // idioms and both used to answer here.
                //
                // ONLY WHEN THE TAINTED VALUE IS THE RECEIVER. As the KEY it is
                // being converted to a string, and the property that comes out
                // belongs to somebody else's object - marked conservatively,
                // because narrowing that is not what this clause is about.
                const llvm::StringRef key = constant_key(get.getKey());
                const bool hands_back = kind == watched::compiler
                                            ? hands_back_the_compiler(key)
                                            : hands_back_the_global_object(key);
                if (use.getOperandNumber() == 0 && !hands_back) { continue; }
                mark(get.getResult(), kind);
                continue;
            }
            if (mlir::isa<GetProtoOp, IterableOp, ConvertOp, CellGetOp, LoadUpvalueOp>(op)) {
                for (const mlir::Value result : op->getResults()) { mark(result, kind); }
                continue;
            }
            if (auto set = mlir::dyn_cast<SetPropertyOp>(op)) {
                // AS THE KEY OR THE VALUE IT IS NOT BEING WRITTEN THROUGH: it
                // is being converted to a string, or stored into somebody
                // else's object, and either way it leaves this walk's sight.
                if (use.getOperandNumber() != 0) {
                    return std::string{name_of(kind)} + " escapes into " + describe(op);
                }
                const llvm::StringRef key = constant_key(set.getKey());
                if (kind == watched::global_object && !key.empty() && key != "__proto__") {
                    bound_through.insert(
                        {mlir::StringAttr::get(module.getContext(), key), describe(op)});
                    continue;
                }
                return std::string{name_of(kind)} + " is written through (" + describe(op) + ")";
            }
            if (mlir::isa<DeletePropertyOp, DeleteNamedOp, DefineAccessorOp, SetProtoOp,
                          CopyPropsOp>(op)) {
                return std::string{name_of(kind)} + " is written through (" + describe(op) + ")";
            }
            if (auto call = mlir::dyn_cast<CallOp>(op)) {
                // THE OBJECT AS THE CALLEE ITSELF - `globalThis(...)` - names no
                // body at all, and this is also where `seed.constructor(src)`
                // answers.
                if (use.getOperandNumber() == 0) {
                    return std::string{name_of(kind)} + " escapes into " + describe(op);
                }
                // ctjs.call is (callee, receiver, args...) and the entry block
                // is (receiver, new.target, callee, parameters...) - the same
                // remapping ctjs.call_direct is built from.
                const unsigned parameter =
                    use.getOperandNumber() == 1 ? 0u : use.getOperandNumber() + 1u;
                if (const std::optional<std::string> refusal =
                        enter(known_callee(world, call.getCallee()), call.getArgs().size(),
                              parameter, op, kind)) {
                    return refusal;
                }
                continue;
            }
            if (auto direct = mlir::dyn_cast<CallDirectOp>(op)) {
                // AN EARLIER RUN OF THIS PASS ALREADY NAMED THIS ONE, and its
                // operands are the entry block in order, so the position needs
                // no remapping at all.
                if (const std::optional<std::string> refusal =
                        enter(mlir::SymbolTable::lookupNearestSymbolFrom<FuncOp>(
                                  direct, direct.getCalleeAttr()),
                              direct.getArgs().size(), use.getOperandNumber(), op, kind)) {
                    return refusal;
                }
                continue;
            }
            // Stored, captured, returned, thrown, constructed with, spread, or
            // an operation this walk does not know: the value is out of sight
            // and anything may write through it - or compile through it - from
            // here on.
            return std::string{name_of(kind)} + " escapes into " + describe(op);
        }
    }
    return std::nullopt;
}

// CLAUSE 4's second half: F's own closure, as its body sees it, feeds nothing
// but nested closures and upvalue reads. Anything else is an escape.
std::optional<std::string> own_closure_escapes(FuncOp function) {
    if (function.getBody().empty() || function.getBody().front().getNumArguments() < 3) {
        return "the function has no callee argument";
    }
    const mlir::BlockArgument callee = function.getBody().front().getArgument(2);
    for (mlir::OpOperand & use : callee.getUses()) {
        mlir::Operation * op = use.getOwner();
        if (auto made = mlir::dyn_cast<CreateClosureOp>(op); made && use.getOperandNumber() == 0) {
            continue; // the enclosing closure of a nested one
        }
        if (mlir::isa<LoadUpvalueOp, StoreUpvalueOp>(op) && use.getOperandNumber() == 0) {
            continue; // a cell read or write through it
        }
        return "the function's own closure escapes into " + describe(op);
    }
    return std::nullopt;
}

struct CTJSResolveGlobalsPass : impl::CTJSResolveGlobalsBase<CTJSResolveGlobalsPass> {
    using CTJSResolveGlobalsBase::CTJSResolveGlobalsBase;

    void runOnOperation() override {
        mlir::ModuleOp module = getOperation();
        mlir::MLIRContext * context = &getContext();
        mlir::Builder builder(context);

        // THE COUNTERS THE REMARK PRINTS, BESIDE THE STATISTICS RATHER THAN
        // INSTEAD OF THEM.
        //
        // A mlir::Pass::Statistic IS NOT A NUMBER IN THIS RELEASE. The LLVM
        // package this builds against compiles statistics out, so the tracking
        // type has no value to read and streaming one into a diagnostic
        // printed an empty field - `resolved  global(s), rewrote  call(s)` -
        // which a lit test would have matched with a wildcard and a floor
        // would have read as zero. Counting locally is three lines and is the
        // only number here that exists in a release build.
        std::size_t resolved_here = 0;
        std::size_t rewritten_here = 0;
        std::size_t closed_here = 0;
        // THE SECOND SHAPE'S OWN NUMBER, counted apart from the per-name one so
        // that a test can pin it. `rewritten_here` is both shapes together
        // because tools/check/native-claims.py holds it equal to the count of
        // ctjs.call_direct in the IR, and that invariant is what stops a pass
        // reporting a rewrite it did not make.
        std::size_t closure_here = 0;

        // THE CENSUS. In first-seen order, so the attribute is stable.
        census world;
        llvm::DenseSet<mlir::Operation *> passes_new_target;
        module.walk([&](mlir::Operation * op) {
            if (auto store = mlir::dyn_cast<StoreGlobalOp>(op)) {
                world.bindings[store.getNameAttr()].stores.push_back(store);
            } else if (auto load = mlir::dyn_cast<LoadGlobalOp>(op)) {
                world.bindings[load.getNameAttr()].loads.push_back(load);
            } else if (auto function = mlir::dyn_cast<FuncOp>(op)) {
                if (const std::optional<std::uint32_t> index = function_index_of(function)) {
                    world.by_index.try_emplace(*index, function);
                }
            } else if (mlir::isa<PassNewTargetOp>(op)) {
                passes_new_target.insert(op->getParentOfType<FuncOp>());
            }
        });
        world.refused = refused_store_names(module);
        world.top = world.by_index.lookup(0);

        // CLAUSE 5, AND THE NAMES IT NOW HANDS BACK RATHER THAN REFUSING FOR.
        // `bound_through` is the set of names a `globalThis.NAME = v` binds -
        // one row each, refused for the same reason a second ctjs.store_global
        // would refuse them, and NOT a reason to refuse anything else.
        llvm::MapVector<mlir::StringAttr, std::string> bound_through;
        const std::optional<std::string> dynamic =
            dynamic_global_writes(module, world, bound_through);
        // A NAME BOUND ONLY THAT WAY HAS NEITHER A LOAD NOR A STORE, so it has
        // no census row yet - and a verdict nobody can read is not a verdict.
        // Skipped when the module is refused outright: those names would carry
        // the module-wide reason, which says nothing about them.
        if (!dynamic) {
            for (const auto & written : bound_through) { (void)world.bindings[written.first]; }
        }

        llvm::SmallVector<mlir::Attribute> rows;
        const auto record = [&](mlir::StringAttr name, std::size_t stores, FuncOp resolved,
                                const std::string & reason) {
            rows.push_back(builder.getDictionaryAttr({
                builder.getNamedAttr("name", name),
                builder.getNamedAttr("stores",
                                     builder.getI32IntegerAttr(static_cast<std::int32_t>(stores))),
                builder.getNamedAttr("resolved",
                                     resolved ? mlir::Attribute{mlir::FlatSymbolRefAttr::get(
                                                    resolved.getSymNameAttr())}
                                              : mlir::Attribute{builder.getStringAttr("none")}),
                builder.getNamedAttr("reason", builder.getStringAttr(reason)),
            }));
        };

        for (auto & [name, facts] : world.bindings) {
            // ---- clause 5 first: is the whole module refused? --------------
            if (dynamic) {
                record(name, facts.stores.size(), FuncOp{}, *dynamic);
                continue;
            }
            // AND THE NAMES CLAUSE 5 REFUSES ONE BY ONE. `globalThis.NAME = v`
            // is the proxy's `set` trap and therefore define_global(NAME, v),
            // so the binding this pass can see is not the only one - for THIS
            // name. Every other name in the program is untouched by it, which
            // is the difference between a precise clause and the whole-module
            // bail this used to be.
            if (const auto written = bound_through.find(name); written != bound_through.end()) {
                record(name, facts.stores.size(), FuncOp{},
                       "written through the global object (" + written->second +
                           "), which binds this name");
                continue;
            }
            // ---- clauses 1-3 and 6, which known_callee() also asks ---------
            const verdict answer = bound_closure(world, name, facts);
            if (!answer.target) {
                record(name, facts.stores.size(), FuncOp{}, answer.reason);
                continue;
            }
            FuncOp target = answer.target;
            ++resolvedGlobals;
            ++resolved_here;
            const unsigned parameters = target.getBody().front().getNumArguments() - 3;

            // ---- the rewrite, per load ----------------------------------------
            std::optional<std::string> open;
            CreateClosureOp closure = answer.closure;
            if (!closure.getResult().hasOneUse()) {
                open = "the closure value has a use other than the store";
            }
            std::size_t rewritten = 0;
            for (LoadGlobalOp load : facts.loads) {
                llvm::SmallVector<mlir::OpOperand *> uses;
                for (mlir::OpOperand & use : load.getResult().getUses()) { uses.push_back(&use); }
                for (mlir::OpOperand * use : uses) {
                    mlir::Operation * user = use->getOwner();
                    // Already resolved by an earlier run: the callee VALUE of a
                    // call_direct naming this very function.
                    if (auto direct = mlir::dyn_cast<CallDirectOp>(user);
                        direct && use->getOperandNumber() == 2 &&
                        direct.getCallee() == target.getSymName()) {
                        continue;
                    }
                    auto call = mlir::dyn_cast<CallOp>(user);
                    if (!call || use->getOperandNumber() != 0) {
                        if (!open) { open = "the binding is used by " + describe(user); }
                        continue;
                    }
                    if (call.getArgs().size() > parameters) {
                        if (!open) {
                            open = "a call passes " + std::to_string(call.getArgs().size()) +
                                   " argument(s) to " + std::to_string(parameters) +
                                   " parameter(s) - the surplus has frame semantics (" +
                                   describe(user) + ")";
                        }
                        continue;
                    }
                    if (passes_new_target.contains(call->getParentOfType<FuncOp>())) {
                        if (!open) {
                            open = "a call sits in a function that passes new.target (" +
                                   describe(user) + ")";
                        }
                        continue;
                    }
                    // AND THE PAD MAY NOT BE VISIBLE. This clause was missing:
                    // a short call into a body that reads its raw argument
                    // window was padded, and `arguments.length` came out as the
                    // parameter count instead of the argument count.
                    if (padding_hides_arguments(target, call.getArgs().size(), parameters)) {
                        if (!open) {
                            open = "a call passes " + std::to_string(call.getArgs().size()) +
                                   " argument(s) to " + std::to_string(parameters) +
                                   " parameter(s) and the callee reads its raw argument window, "
                                   "which the pad would lengthen (" +
                                   describe(user) + ")";
                        }
                        continue;
                    }

                    mlir::OpBuilder at(call);
                    const auto value_type = ValueType::get(context);
                    const mlir::Value undefined = ConstantOp::create(at, call.getLoc(), value_type,
                                                                     UndefinedAttr::get(context));
                    llvm::SmallVector<mlir::Value> arguments(call.getArgs());
                    while (arguments.size() < parameters) { arguments.push_back(undefined); }
                    auto direct = CallDirectOp::create(
                        at, call.getLoc(), value_type,
                        mlir::FlatSymbolRefAttr::get(target.getSymNameAttr()), call.getReceiver(),
                        undefined, load.getResult(), arguments,
                        /*arg_attrs=*/nullptr, /*res_attrs=*/nullptr);
                    call.getResult().replaceAllUsesWith(direct.getResult());
                    call.erase();
                    ++rewritten;
                    ++rewrittenCalls;
                    ++rewritten_here;
                }
            }

            // ---- clause 4: is the world closed around F? ----------------------
            if (!open) { open = own_closure_escapes(target); }
            if (open) {
                record(name, facts.stores.size(), target,
                       "open: " + *open + "; " + std::to_string(rewritten) + " call(s) rewritten");
                continue;
            }
            mlir::SymbolTable::setSymbolVisibility(target, mlir::SymbolTable::Visibility::Private);
            ++closedFunctions;
            ++closed_here;
            record(name, facts.stores.size(), target,
                   "closed: " + std::to_string(rewritten) + " call(s) rewritten");
        }

        // ---- THE SECOND SHAPE, AND THE ONLY ONE A VENDOR BUNDLE HAS --------
        //
        // A ctjs.call whose callee is a ctjs.create_closure RESULT is provably
        // that function with no global NAME involved at all: the closure names
        // its function index outright (ctjs.create_closure's own words) and
        // calling it enters that function. known_callee() already proves it -
        // clause 5's escape walk crosses exactly this shape - so this asks it
        // rather than re-deriving it.
        //
        // IT DEPENDS ON NO CLAUSE OF THE PER-NAME CENSUS, WHICH IS THE POINT.
        // Clauses 1-3 need a name bound once in the hoisting prologue, and a
        // BUNDLE BINDS NO GLOBALS AT ALL - each hands a factory's result to one
        // property of the window - so `resolved` is 0 on bootstrap, p5 and
        // phaser before any other clause is consulted. Nothing a program does
        // to the globals table can change which function a closure VALUE
        // enters, so clause 5's module-wide refusal is not a reason to refuse
        // this either: it runs even when `dynamic` refused every name above.
        //
        // MEASURED, BECAUSE THE ESTIMATE WAS WRONG ONCE. There are 21 such
        // ctjs.call sites in bootstrap, 42 in p5 and 50 in phaser - and
        // --ctnative-lower-to-emitc's closure lift ALREADY named some of them
        // later in the pipeline (3, 47 and 3 calls respectively). This is not
        // that rewrite: it is the same proof applied where the whole pipeline,
        // and tools/check/native-claims.py, can see it.
        llvm::SmallVector<CallOp> closure_calls;
        module.walk([&](CallOp call) {
            if (call.getCallee().getDefiningOp<CreateClosureOp>() != nullptr) {
                closure_calls.push_back(call);
            }
        });
        for (CallOp call : closure_calls) {
            FuncOp target = known_callee(world, call.getCallee());
            if (!target || target->hasAttr("ctjs.not_lowered")) { continue; }
            if (target.getBody().empty() || target.getBody().front().getNumArguments() < 3) {
                continue;
            }
            const unsigned parameters = target.getBody().front().getNumArguments() - 3;
            // ARITY IS A HARD VERIFIER FAILURE, NOT A REFUSAL THIS MAY LEAVE TO
            // SOMEBODY ELSE. CallDirectOp::verifySymbolUses holds the operand
            // count equal to the entry block's, so a surplus call must be
            // refused HERE; a short one is padded, as VM_CASE(call) pads.
            if (call.getArgs().size() > parameters) { continue; }
            if (padding_hides_arguments(target, call.getArgs().size(), parameters)) { continue; }
            // new.target is undefined for a plain call, and a function holding
            // a ctjs.pass_new_target may have the pending flag live at any call
            // in it - adjacency is an invariant nothing checks.
            if (passes_new_target.contains(call->getParentOfType<FuncOp>())) { continue; }

            mlir::OpBuilder at(call);
            const auto value_type = ValueType::get(context);
            const mlir::Value undefined =
                ConstantOp::create(at, call.getLoc(), value_type, UndefinedAttr::get(context));
            llvm::SmallVector<mlir::Value> arguments(call.getArgs());
            while (arguments.size() < parameters) { arguments.push_back(undefined); }
            auto direct =
                CallDirectOp::create(at, call.getLoc(), value_type,
                                     mlir::FlatSymbolRefAttr::get(target.getSymNameAttr()),
                                     call.getReceiver(), undefined, call.getCallee(), arguments,
                                     /*arg_attrs=*/nullptr, /*res_attrs=*/nullptr);
            call.getResult().replaceAllUsesWith(direct.getResult());
            call.erase();
            ++rewrittenCalls;
            ++rewritten_here;
            ++closure_here;
        }

        module->setAttr("ctjs.globals", builder.getArrayAttr(rows));

        // THE COUNTS, AS A REMARK, BECAUSE THE STATISTICS ARE INERT. The
        // release LLVM package this builds against compiles pass statistics
        // out, so --mlir-pass-statistics prints an empty report and a lit test
        // asserting one asserts nothing. This is the same escape hatch
        // --ctjs-lift-to-scf ships, for the same reason, and it is what lets a
        // test PIN a number rather than read the IR and hope.
        if (report) {
            module->emitRemark() << "resolved " << resolved_here << " global(s), rewrote "
                                 << rewritten_here << " call(s), closed " << closed_here
                                 << " function(s) over " << rows.size() << " name(s), named "
                                 << closure_here << " closure call(s)";
        }
    }
};

} // namespace

} // namespace ctcompile::ctjs
