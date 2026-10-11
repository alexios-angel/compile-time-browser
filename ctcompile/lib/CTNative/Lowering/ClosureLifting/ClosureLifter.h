//===- ClosureLifting/ClosureLifter.h - Closure lifting for native code ---===//
//
// Declares closureLifter, the rewrite that turns JavaScript closures into
// plain functions the native (C++) lowering can emit: captures become
// leading parameters and calls become ctjs.call_direct. The numbered rules
// below (one-write bindings, shared cells by pointer, hoisted `undefined`,
// local function bindings) are referred to by number from the .cpp files in
// this directory.
//
//===----------------------------------------------------------------------===//

#pragma once

#include "../LoweringSupport.h"

namespace ctcompile::ctnative {
struct HostContract;
}

namespace ctcompile::ctnative::lowering_detail {

/// What the rewrite did, for the `report` remark. Pass statistics are compiled
/// out of the LLVM package this builds against, so a counter that a test
/// asserts has to be printed.
struct liftReport {
    unsigned functions = 0;          // ctjs.funcs whose captures became parameters
    unsigned closures = 0;           // ctjs.create_closures that now lower to nothing
    unsigned captures = 0;           // capture operands turned into arguments
    unsigned calls = 0;              // ctjs.calls rewritten to ctjs.call_direct
    unsigned cells = 0;              // ctjs.create_cells proved constant and unboxed
    unsigned methods = 0;            // method fields whose closure became a free function
    unsigned receivers = 0;          // of those, the ones whose `this` became a parameter
    unsigned objects = 0;            // parameters that became a pointer to a closed shape
    unsigned constructors = 0;       // `new X(...)` sites turned into a frame-scope struct
    unsigned carried = 0;            // capture slots that became a pointer to a frame-local cell
    unsigned locals = 0;             // ctjs.create_cells that became a frame-local variable
    unsigned bindings = 0;           // local bindings that held a function and lowered to nothing
    unsigned bound = 0;              // of `calls`, the ones made direct from another frame
    unsigned callbackCalls = 0;      // calls through a callback parameter made direct
    unsigned callbackParameters = 0; // call-only function parameters erased
    unsigned statics = 0;            // boxes of run-once functions made static variables

    /// Adds `other`'s counts to these.
    void add(const liftReport & other) {
        functions += other.functions;
        closures += other.closures;
        captures += other.captures;
        calls += other.calls;
        cells += other.cells;
        methods += other.methods;
        receivers += other.receivers;
        objects += other.objects;
        constructors += other.constructors;
        carried += other.carried;
        locals += other.locals;
        bindings += other.bindings;
        bound += other.bound;
        callbackCalls += other.callbackCalls;
        callbackParameters += other.callbackParameters;
        statics += other.statics;
    }
};

/// What one use of a `ctjs.create_cell` is. The censuses and the rewrites all
/// ask the same questions of a cell's use list; a cell_set at any operand but
/// 0 stores the box into something rather than writing it.
enum class cellUse {
    read,
    write,
    storedInto,
    capture,
    other
};
inline cellUse classify(mlir::OpOperand & use) {
    mlir::Operation * user = use.getOwner();
    const unsigned n = use.getOperandNumber();
    if (llvm::isa<ctjs::CellGetOp>(user) && n == 0) { return cellUse::read; }
    if (llvm::isa<ctjs::CellSetOp>(user)) { return n == 0 ? cellUse::write : cellUse::storedInto; }
    if (llvm::isa<ctjs::CreateClosureOp>(user) && n >= kFirstCapture) { return cellUse::capture; }
    return cellUse::other;
}

/// Rewrites closures so the native lowering can treat them as plain functions.
///
/// A closure whose every use is a call is "lifted": its captures become
/// leading parameters, each call becomes a ctjs.call_direct, and the
/// ctjs.create_closure lowers to nothing. The same machinery lifts methods of
/// closed-shape object literals, `new X(...)` of a local constructor, local
/// bindings that hold a function, and mutable cells shared by pointer. Every
/// refusal records a reason for the diagnostic. run() is the entry point.
struct closureLifter {
    mlir::ModuleOp module;
    mlir::MLIRContext * context;

    /// Every ctjs.func by its function index, the number a
    /// ctjs.create_closure names its target by (see targetOf()).
    llvm::DenseMap<unsigned, ctjs::FuncOp> byIndex;
    /// Functions that may write one of their own upvalue slots, directly or
    /// through the closures they make. `ctjs.store_upvalue %arg2[j]` inside G
    /// writes the cell that G's creator put in slot j, so a cell captured into
    /// any such G is not constant. A cell captured into G and re-captured by an
    /// H that writes it is not constant either, which is why this set is
    /// computed as a fixpoint.
    llvm::DenseSet<mlir::Operation *> mutatesUpvalue;
    /// Functions in which a call may not become a ctjs.call_direct: op::call
    /// pushes its frame with the pending new.target, and call_direct
    /// materialises undefined for it. ResolveGlobals refuses the same shape.
    llvm::DenseSet<mlir::Operation *> passesNewTarget;
    // No guard refuses a target that a ctjs.call_direct already names. No
    // program reaches that shape: --ctjs-resolve-globals only names a
    // top-level declaration closure, which isDeclarationClosure exempts, and
    // running the pass twice is idempotent (Closures/refusals.mlir checks
    // this). If this is ever wrong, CallDirectOp::verifySymbolUses fails on an
    // operand count rather than producing a wrong answer.

    llvm::SmallVector<ctjs::CreateClosureOp> closures;
    llvm::DenseMap<mlir::Operation *, ctjs::CreateClosureOp> uniqueClosureByTarget;
    struct boundCaptureCall {
        ctjs::CallDirectOp call;
        ctjs::CreateClosureOp holder;
    };
    llvm::DenseMap<mlir::Operation *, llvm::SmallVector<boundCaptureCall>> boundCaptureCalls;
    llvm::DenseSet<mlir::Operation *> siblingBindingTargets;
    bool allowSiblingCaptures = false;

    // --- Rule 1: a binding with one dominating write ---------------------------
    //
    // `compiler_impl::predeclare_locals` hoists every `var`/`let`/`const` of a
    // body and boxes it up front holding `undefined`, so `var n = 5` is a
    // ctjs.create_cell of a constant undefined and a ctjs.cell_set into the box
    // that already exists. The basic constant-cell rule (isConstantCell) reads
    // that as a reassigned binding and refuses it. This is the most common
    // shape in real programs.
    //
    // A binding written once is constant after that write, so a capture taken
    // after it is exact, and "after" means dominance, not program order. The
    // cell is admitted when, and only when:
    //
    //   1. it has exactly one ctjs.cell_set naming it as the box;
    //   2. that store dominates every ctjs.cell_get of it;
    //   3. that store dominates every call of every closure that captures it
    //      (checked at the lift, by whyCapturesDoNotReach, not here);
    //   4. every other use is a read at operand 0, or a capture by a closure
    //      whose target writes no upvalue.
    //
    // Its value is then the store's operand rather than the cell's initial.
    // Conditions 2 and 3 make the initial unobservable: no read of the
    // binding, in this frame or in a closure over it, can happen on a path that
    // skips the store, so nothing can see the hoisted `undefined`. That is why
    // any initial is admitted, not only a constant undefined; a box initialised
    // from a parameter is admitted too.
    //
    // Condition 3 is about the call, not about the `ctjs.create_closure`. A
    // function declaration is hoisted: `function get() { return n; }` beside
    // `var n = 5` compiles to an `op::closure` in the prologue, before the
    // store, because `get` is callable on the first line of the body. Asking
    // the store to dominate the create_closure would refuse exactly this
    // shape. The lift prepends the captured value at each call, and a closure
    // reads the box when it runs (VM_CASE(get_upvalue) in
    // ctbrowser/lib/Script/vm/run_loop.cpp), so the point that has to be
    // dominated is the call site. A closure created before the store and
    // called after it reads the stored value in the interpreter and is handed
    // the stored value here.
    //
    // MLIR dominance handles loops correctly. `properlyDominates` normalises
    // the later operation into the earlier one's region, so a store inside a
    // loop body does not dominate a read after the loop (there is a path
    // around the body) and condition 2 refuses it, while a cell created inside
    // the body is a fresh box each iteration whose store, reads and captures
    // are all in that one region, and it lifts.
    //
    // Filled by census(), before any rewrite: every input is a use of the cell,
    // and the lift changes none of them.
    llvm::DenseMap<mlir::Operation *, ctjs::CellSetOp> writtenOnce;
    /// Why a cell that has a store is not in `writtenOnce`, for the diagnostic.
    /// Absent means the cell has no ctjs.cell_set at all. That cell is handled
    /// by isConstantCell, and any refusal is about a store_upvalue instead.
    llvm::DenseMap<mlir::Operation *, std::string> whyNotWrittenOnce;

    // --- Rule 2: a shared mutable cell, carried by pointer ---------------------
    //
    // The cells rule 1 cannot take: written twice, written on only one path,
    // or (most commonly) written by a closure with `ctjs.store_upvalue`, which
    // `mutatesUpvalue` marks and `isConstantCell` refuses. There is no single
    // value to copy into a parameter, so no by-value copy can be correct.
    //
    // So the box becomes an ordinary frame-local variable and the capture
    // becomes a pointer to it. `ctjs.create_cell` lowers to an
    // `emitc.variable` of the carrier, `ctjs.cell_get` to a load of it and
    // `ctjs.cell_set` to an assignment; a lifted target takes `double *` for
    // that slot instead of `double`, its `ctjs.load_upvalue` and
    // `ctjs.store_upvalue` become a cell_get and a cell_set through the
    // pointer, and each call site passes the variable's address. This is the
    // receiver lift's convention (`ctnative.receiver` emits `ctn_x * self`),
    // one operand along.
    //
    // Why the pointer cannot dangle:
    //
    //   1. The closure cannot outlive the frame. Condition 4 of whyNotLiftable
    //      admits a closure only when every use of its value is a call this
    //      rewrite lowers: a ctjs.call at operand 0, or a ctjs.call_direct at
    //      operand 2 that --ctjs-resolve-globals named. A closure that is
    //      stored, returned or passed is refused by name. A closure that can be
    //      reached from nowhere but a call in this frame cannot be called after
    //      the frame is gone.
    //   2. Every call is in the frame that owns the variable.
    //      whyCapturesDoNotReach compares the ctjs.func the captured value
    //      lives in with the ctjs.func the call sits in, and refuses when they
    //      differ (the METHODCAP case in test/CTNative/Lowering/Closures/
    //      refusals.mlir; a bare dominance query would answer "yes" there
    //      because builtin.module's body is a graph region). It then requires
    //      the cell to properly dominate the call, so the variable is in scope
    //      in the emitted C++ where its address is taken.
    //   3. The pointer goes nowhere else. After the lift, the only uses of the
    //      parameter are the cell_get and cell_set that replaced the upvalue
    //      read and write (whyUpvalueReadsDoNotLift refuses a target whose
    //      load or store names anything but `%arg2` at an index this rewrite
    //      carries), plus its re-appearance at a nested lifted call, which is
    //      the same three conditions one level in.
    //
    // The carrier is not proved here, and cannot be: this runs before type
    // inference. `admission::function` checks it on the parameter and
    // `admission::op` on the box, both after inference, and a cell whose type
    // has no C++ carrier is refused there, so it is never pointed at.
    llvm::DenseSet<mlir::Operation *> carriedCells;
    /// Why a cell is not carried either, for the diagnostic. Only the
    /// structural clause can fail here; the carrier is admission's question.
    llvm::DenseMap<mlir::Operation *, std::string> whyNotCarried;

    // --- Rule 3: dropping the hoisted `undefined` from a carried cell's type ---
    //
    // Rule 2 emits the box as a frame-scope variable assigned the initial, so
    // `TypeInference::cellTypeOf` joins that initial into the binding's type at
    // every read and a hoisted `var` is `opt<num>` however the program uses it.
    // That join is flow-insensitive: `opt<num>` carries undefined as NaN, and
    // at a `ctjs.store_global` (where a value becomes observable) the print
    // convention spells a Number `%.17g`, so such a binding would print `nan`
    // where the interpreter prints `undefined`.
    //
    // A write that dominates every read makes the initial unobservable, and
    // the type at every read is then the join of the writes alone.
    // `var n = 0; function tick() { n = n + 1; }`: the declaration is a write
    // dominating everything, so `n` is `num`. `var v; if (k > 0) { v = 5; }`:
    // no write dominates the read in `get`, so `v` stays `opt<num>` and every
    // use where the difference shows is refused.
    //
    // The two conditions are conditions 2 and 3 of rule 1, and
    // `writeReachesEveryRead` implements both rules. Rule 1 uses them to
    // replace a read with the store's operand; this rule uses them to drop the
    // box's initial from that read's type. Both rest on the same fact: no read
    // of the binding, in this frame or in a closure over it, can happen on a
    // path that skipped the store.
    //
    // Where the two rules differ: rule 1 also needs the stored value to
    // dominate each call (`byValueMissesACall`), because it copies that value
    // into a parameter. This rule copies nothing (the pointer is passed and the
    // callee loads through it), so only the store has to dominate. A binding
    // like `var t = k*2; if (k>0) { v = t; }`, whose value dominates every call
    // and whose store dominates none, is therefore carried by rule 2 but not
    // narrowed by this rule. Asking about the value here would have admitted
    // the same wrong answer rule 1 once gave (fixed by byValueMissesACall).
    //
    // No callee write is needed. In `function counter() { var n = 0; function
    // tick() { n = n + 1; } tick(); return n; }` the write inside `tick`
    // dominates the read in `counter` in no single CFG, and it does not have
    // to, because `var n = 0` already dominates both the frame's own read and
    // every call of `tick`. A callee's write can only add a value to the join;
    // it can never put the initial back. So this rule asks only about writes in
    // the box's own frame, which is exactly the set `ctjs.cell_set` names, and
    // the `ctjs.store_upvalue` a callee makes is left to the type join.
    llvm::DenseSet<mlir::Operation *> assignedBeforeRead;

    // --- Rule 4: a local binding that holds a function -------------------------
    //
    // `var f = function () { ... };` inside a function body is a hoisted local,
    // and it is boxed exactly when some nested function mentions the name:
    // `compiler_impl::declare_local` asks `is_captured` (capture.cpp) and
    // `predeclare_locals` emits `op::new_cell` only when the answer is yes. So
    // the shape this rule handles is a `ctjs.create_cell` of `undefined`, a
    // `ctjs.create_closure`, a `ctjs.cell_set` putting the second in the
    // first, and then a `ctjs.cell_get` in this frame or a `ctjs.load_upvalue`
    // in a nested one wherever the name is used. Without this rule the closure
    // is refused only because it reaches a `ctjs.cell_set`, which is a common
    // refusal in real bundles.
    //
    // A binding written once with a closure, and only ever called through, is
    // that function. This is the declaration case one scope in:
    // `isDeclarationClosure` already lowers a create_closure whose one use is a
    // `ctjs.store_global` to nothing, because --ctjs-resolve-globals names the
    // callee and every call of it is direct. The box adds nothing a call
    // needs, so the box, its store and its reads all lower to nothing and each
    // call through the name becomes a `ctjs.call_direct` of the target.
    //
    // The conditions, each a named refusal when it fails (`bindingClosures`
    // holds the admitted ones):
    //
    //  1. One store, holding a closure made here and used for nothing else.
    //     Two stores make the binding a variable again, and which function a
    //     call reaches would depend on the path to it; a closure with a second
    //     use is a value as well as a name, and the other rules handle that.
    //  2. Every other use of the box is a read or a capture. A box stored into
    //     an object, put in another box, returned or passed is held by
    //     something else, and a name that lowers to nothing cannot stand in
    //     for it.
    //  3. The store reaches every read (`writeReachesEveryRead`, shared with
    //     rules 1 and 3, with the same two clauses): it dominates every
    //     `ctjs.cell_get` of the box, and every call of every closure that
    //     captured it. A read before it yields the `undefined` the binding was
    //     hoisted with, which is not a function.
    //  4. Every read is a call the native lowering handles. In this frame that
    //     is a `ctjs.call` at operand 0 of a `ctjs.cell_get` result; in a
    //     nested function it is a `ctjs.call` at operand 0 of a
    //     `ctjs.load_upvalue` result. A read used for anything else (compared,
    //     stored, passed) is a function value, and this rule makes no value.
    //     The same question is asked at every depth the binding reaches: a
    //     closure made inside one of those nested functions can fill a slot of
    //     its own from the one holding the box (`enclosing_indices`), and then
    //     the same three questions are asked one frame further in.
    //  5. The captures reach the calls this rule writes itself. A call in
    //     another function has nowhere to prepend the captures (the METHODCAP
    //     case, one binding along), so a binding read from a nested function
    //     may only hold a closure whose every capture is itself a binding this
    //     rule erases, or the identical cell already captured by its sibling
    //     caller. The latter is passed through the caller's ordinary lifted
    //     capture parameter. This condition matters in practice: a bundle's
    //     helpers close over each other, and the recursive
    //     `var f = function () { ... f(); }` is exactly the case where the one
    //     capture is the binding being defined.
    //
    // Recursion is admitted, and the rule is built around it. `f` names
    // itself, so the closure captures the very box it is written into;
    // condition 5 sees a capture that is a binding this rule erases and takes
    // it, and the `ctjs.load_upvalue` inside the body becomes a
    // `ctjs.call_direct` of the target it sits in. For condition 3, the
    // closure's only use is the store, and a store is not a call, so it is
    // not asked to dominate itself: a read of the binding inside that function
    // happens when the function runs, and every call of it is one of the
    // reads this walk has already checked (see `writeReachesEveryRead`).
    //
    // The capture slot is removed, which is the only IR surgery here. A slot
    // holding a binding that lowers to nothing has no value to pass, so the
    // operand goes, `enclosing_indices` shortens with it, the target's
    // `upvalue_count` drops and every `ctjs.load_upvalue` index past the hole
    // is renumbered. A nested closure that names the removed slot through its
    // own `enclosing_indices` has its own slot removed too, deepest first.
    // This is safe because `makeBoundCallDirect` writes a `ctjs.call_direct`
    // in a frame the closure value cannot reach, so a read two frames in has
    // the same call site as a read one frame in.
    //
    // It runs before `census()`, and has to: the censuses record the uses of
    // every cell, and this erases cells, stores, reads and calls. It is the
    // same ordering rule the three censuses state about `lift()`.
    //
    // It is `--ctjs-resolve-globals` for locals. The resolver turns a global
    // bound once in the prologue to a closure into a symbol every call names;
    // this turns a local bound once in a frame into the same thing. What is
    // left for the ordinary lift is a `ctjs.create_closure` whose every use is
    // a call, which the basic closure lift already handles.
    //
    // Condition 3 is conservative about source order, which is where this rule
    // stops. `writeReachesEveryRead` requires the store to dominate every use
    // of every closure that captured the box, not every call of one: it cannot
    // ask about calls, because a closure whose own value goes into a box has
    // no call site at all until this rewrite makes one. That is sound, since a
    // call of a closure happens at or after some use of its value, so a store
    // dominating every use ran before any call could. It also refuses
    // `function g() { f(); } function f() {}` written in that order, because
    // g's own store comes first.

    /// The closures the admitted bindings held. Condition 4 of `whyNotLiftable`
    /// requires something to call the closure, and for one of these the calls
    /// may all be `ctjs.call_direct`s in other frames that this rule wrote,
    /// which are not uses of the closure value at all.
    llvm::DenseSet<mlir::Operation *> bindingClosures;
    /// Why a box that holds a function is not admitted by rule 4, for the
    /// diagnostic. Written for every cell whose `ctjs.cell_set` stores a
    /// `ctjs.create_closure`, so that "it reaches `ctjs.cell_set`" is replaced
    /// by the clause that failed.
    llvm::DenseMap<mlir::Operation *, std::string> whyNotAFunctionBinding;

    /// One dominator tree per lifter. It stays valid across the
    /// classify-then-lift fixpoint: lift() inserts entry-block arguments and
    /// erases and creates operations, and neither changes the block structure
    /// the tree is built over. MLIR invalidates a block's operation-order cache
    /// itself on every insertion and removal, so the intra-block half stays
    /// correct too.
    mlir::DominanceInfo dominance{nullptr};

    // --- the receiver lift's census ---------------------------------------
    llvm::SmallVector<ctjs::CreateObjectOp> objects;
    llvm::SmallVector<ctjs::CallOp> allCalls;
    llvm::SmallVector<ctjs::CallDirectOp> allDirectCalls;
    /// The single closure a literal's key holds, per literal, per key.
    struct methodField {
        ctjs::SetPropertyOp store;
        ctjs::CreateClosureOp closure;
    };
    llvm::DenseMap<mlir::Value, llvm::StringMap<methodField>> methodsOf;
    /// Which closed literals a receiver value may name: a literal names itself,
    /// and a lifted method's `%arg0` names everything its call sites pass. The
    /// second part is a fixpoint, because `this.other()` inside a method is a
    /// call whose receiver is that `%arg0`.
    llvm::DenseMap<mlir::Value, llvm::SmallVector<mlir::Value, 2>> behind;
    /// One method call this rewrite makes direct.
    struct methodCall {
        ctjs::CallOp call;
        ctjs::GetPropertyOp load;
        mlir::Value receiver;
        ctjs::FuncOp target;
    };
    llvm::MapVector<mlir::Operation *, llvm::SmallVector<methodCall>> callsOfTarget;
    llvm::DenseSet<mlir::Operation *> methodClosures; // create_closures bound to a method field
    llvm::DenseSet<mlir::Operation *> lifted;         // closures lifted by this invocation

    // --- The constructor lift ----------------------------------------------
    //
    // Handles `new X(a, b)` where X is a ctjs.create_closure result. The rule
    // is a rewrite into two operations that already lower: the instance
    // becomes an empty ctjs.create_object (a frame-scope struct, not a heap
    // allocation) and the constructor becomes the receiver carrier's free
    // function over it, reached by the same ctjs.call_direct the method lift
    // emits. So `hasClosedShape`, `groupReceivers`, `fieldsOf`, `censusShapes`
    // and `replace` need no constructor case: after the rewrite there is no
    // constructor, only a literal and a call that takes its address.
    //
    // A local immutable prototype supplies scalar defaults and method bindings.
    // Its complete use census excludes observations of property ownership or
    // prototype identity. Mutable prototype chains are refused; there is no
    // immutability proof for them yet.
    llvm::SmallVector<ctjs::ConstructOp> allConstructs;
    llvm::MapVector<mlir::Operation *, llvm::SmallVector<ctjs::ConstructOp>> constructsOfTarget;
    llvm::DenseSet<mlir::Operation *> constructorClosures; // diagnostic routing, not admission

    struct prototypeFields {
        ctjs::SetPropertyOp attachment;
        llvm::SmallVector<ctjs::SetPropertyOp> fields;
    };
    std::optional<prototypeFields> immutablePrototype(ctjs::CreateClosureOp c);

    // --- The open-literal census (a measurement, not a rule) --------------
    //
    // Widening `closedAfterLift` is only worth what real bundles contain, so
    // the pass can report what is blocking every method-bearing literal it
    // refuses: one bucket per blocking use, labelled by the operation and the
    // operand position so that no assumption about what "escapes" means shapes
    // the answer. `censusSole` is the number that matters: an object with two
    // kinds of blocking use is not unblocked by widening one of them.
    bool censusOn = false;
    llvm::StringMap<unsigned> censusUses;
    llvm::StringMap<unsigned> censusSole;
    llvm::StringMap<unsigned> censusSoleFields;
    llvm::StringMap<unsigned> censusRoot;
    llvm::StringMap<unsigned> censusDepth;
    /// One example per bucket, with its source location, so that a bucket can
    /// be checked against the JavaScript it came from.
    llvm::StringMap<mlir::Location> censusExample;
    unsigned censusOpenObjects = 0;
    unsigned censusMethodFields = 0;

    explicit closureLifter(mlir::ModuleOp m, bool doCensus = false)
        : module(m), context(m.getContext()), censusOn(doCensus) {}
    ctjs::FuncOp targetOf(ctjs::CreateClosureOp c);

    /// One call site of a closure, in either of the two shapes it can have.
    ///
    /// --ctjs-resolve-globals may rewrite a ctjs.call whose callee is a
    /// ctjs.create_closure result, so by the time this pass runs the same site
    /// is either a ctjs.call with the closure at operand 0 and its arguments
    /// from operand 2, or a ctjs.call_direct with the closure at operand 2
    /// (`$callee_value`) and its arguments from operand 3, because
    /// call_direct's operands are the callee's entry block in order.
    ///
    /// Every census asks through this helper. Casting to ctjs::CallOp alone
    /// silently treats a call_direct site as "not a call".
    struct closureCall {
        mlir::Operation * op = nullptr;
        mlir::ValueRange args;
        mlir::Value receiver;
        explicit operator bool() const { return op != nullptr; }
    };
    closureCall callSiteOf(mlir::OpOperand & use, ctjs::FuncOp target);

    ctjs::CreateClosureOp closureCalledBy(mlir::Operation * user);

    static mlir::ValueRange argsOfCallSite(mlir::Operation * user);

    void indexAndNewTargets();

    void specializeCallbacks(liftReport & out);

    /// Array-method callbacks (ArrayCallbacks.cpp): each closure whose one use
    /// is the callback argument of `a.forEach(f)`, `a.map(f)` and the like,
    /// with that call. Such a closure is judged by whyNotArrayCallback and,
    /// once lift() has made its captures parameters, its call is rewritten by
    /// rewriteArrayCallback instead of lift()'s call-site loop.
    llvm::DenseMap<mlir::Operation *, ctjs::CallOp> arrayCallbacks;
    void arrayCallbackCensus();
    std::optional<std::string> whyNotArrayCallback(ctjs::CreateClosureOp c);
    void rewriteArrayCallback(ctjs::CreateClosureOp c, ctjs::FuncOp target, liftReport & out);
    /// Replacement functions of `text.replace(/pattern/g, f)`
    /// (StringCallbacks.cpp), judged and rewritten as an Array callback is.
    llvm::DenseMap<mlir::Operation *, ctjs::CallOp> stringCallbacks;
    void stringCallbackCensus();
    std::optional<std::string> whyNotStringCallback(ctjs::CreateClosureOp c);
    void rewriteStringCallback(ctjs::CreateClosureOp c, ctjs::FuncOp target, liftReport & out);

    void census();

    void singleWriteCensus();

    void sharedCellCensus();

    ctjs::CellSetOp dominatingWriteOf(ctjs::CreateCellOp cell);

    bool writeReachesEveryRead(ctjs::CreateCellOp cell, ctjs::CellSetOp store);

    bool isAssignedBeforeRead(ctjs::CreateCellOp cell) const;

    // --- Rule 4 (local function bindings): the check and the rewrite ----------

    /// What the check found, so that it and the rewrite cannot disagree about
    /// which operations they mean. `examineFunctionBinding` fills it and
    /// `bindLocalFunctions` reads it; nothing else constructs one.
    struct functionBinding {
        ctjs::CreateCellOp cell;
        ctjs::CellSetOp store;
        ctjs::CreateClosureOp closure;
        ctjs::FuncOp target;
        ctjs::FuncOp owner;
        /// The reads in the frame that owns the box.
        llvm::SmallVector<ctjs::CellGetOp> reads;
        /// The closures of this frame that carry the binding one frame in, with
        /// the slot each one holds it at: the entries the cell's own use list
        /// names, and the roots of the walk below.
        llvm::SmallVector<std::pair<ctjs::CreateClosureOp, unsigned>> captured;
        /// Every slot the rewrite has to remove, including nested ones. A
        /// closure made inside one of those targets can fill a slot of its own
        /// from the enclosing closure's slot (the `enclosing_indices`
        /// attribute), which carries the binding one frame further in.
        /// `examineCapturedSlot` follows it and appends in post-order, so a
        /// slot always comes before the slot it is filled from.
        /// bindLocalFunctions (Bindings.cpp) orders the removals by how the
        /// closure targets nest.
        struct capturedSlot {
            ctjs::CreateClosureOp made;
            unsigned slot;
        };
        llvm::SmallVector<capturedSlot> slots;
        unsigned calls = 0;
    };

    /// Which slots of one closure are removed. One closure can hold slots from
    /// more than one binding, so the removals are collected per closure and
    /// applied in one rewrite: `removeCaptureSlots` renumbers, and a second
    /// rewrite of the same closure would renumber indices the first one
    /// already moved.
    struct slotRemoval {
        llvm::SmallVector<unsigned> slots;
    };
    static bool holdsAFunction(ctjs::CreateCellOp cell);

    std::optional<std::string> whyReadIsNotACall(mlir::Value read, ctjs::FuncOp target,
                                                 unsigned & calls);

    std::optional<std::string> examineCapturedSlot(
        functionBinding & plan, ctjs::CreateClosureOp made, unsigned slot,
        llvm::DenseSet<std::pair<mlir::Operation *, unsigned>> & examined);

    std::optional<std::string> examineFunctionBinding(ctjs::CreateCellOp cell,
                                                      functionBinding & plan);

    void makeBoundCallDirect(ctjs::CallOp call, ctjs::FuncOp target, ctjs::CreateClosureOp holder);

    static std::optional<unsigned> siblingCaptureSlot(ctjs::CreateClosureOp closure, unsigned index,
                                                      ctjs::CreateClosureOp holder);

    mlir::Value siblingCapturedValue(ctjs::CreateClosureOp closure, unsigned index,
                                     ctjs::CreateClosureOp holder);

    void removeCaptureSlots(ctjs::CreateClosureOp c, llvm::ArrayRef<unsigned> slots);

    // --- The class census (Classes.cpp) --------------------------------------

    /// One JavaScript class as the importer spells it (see Classes.cpp).
    struct jsClass {
        ctjs::CreateClosureOp constructor;
        ctjs::CreateObjectOp prototype;
        ctjs::CreateClosureOp parent; ///< The parent's constructor, or null.
        ctjs::CallOp heritage;        ///< The `extends` helper call, or null.
        llvm::StringMap<ctjs::CreateClosureOp> methods, getters, statics, staticGetters;
        /// Operations that lower to nothing once the class is proved: the
        /// `constructor` and `__home` edges and the class_defined call.
        llvm::SmallPtrSet<mlir::Operation *, 16> setup;
        /// The `prototype` attachment and each member definition, which the
        /// constructor and method rules consume as they do an ES5 prototype.
        llvm::SmallPtrSet<mlir::Operation *, 16> definitions;
        llvm::SmallVector<ctjs::CellSetOp> bindings;
        /// Uses of the constructor that are not setup (`new`, static reads).
        llvm::SmallVector<mlir::OpOperand *> uses;
        std::string reason; ///< Why the class is not proved; empty if it is.
    };
    /// Keyed by the constructor's ctjs.create_closure.
    llvm::MapVector<mlir::Operation *, jsClass> jsClasses;

    static bool isHomeEdge(mlir::Operation * op);

    void classCensus();

    ctjs::CreateClosureOp classMethod(const jsClass & cls, llvm::StringRef name);

    void eraseClassSetup(jsClass & cls);

    static std::string classNameOf(ctjs::FuncOp target);

    // --- Static cells of run-once functions (RunOnce.cpp) ------------------

    /// Set on the lifter that run() starts after a static rewrite, so that the
    /// rewrite is tried once per module.
    bool staticsDone = false;

    std::optional<std::string> callsThrough(mlir::Value value, ctjs::FuncOp target,
                                            llvm::SmallVectorImpl<mlir::Operation *> & sites,
                                            llvm::DenseSet<mlir::Value> & seen);

    std::optional<std::string> whyNotRunOnce(
        ctjs::FuncOp fn, const llvm::DenseSet<mlir::Operation *> & once,
        const llvm::DenseMap<mlir::Operation *, llvm::SmallVector<mlir::Operation *>> & symbolUsers,
        const llvm::DenseMap<mlir::Operation *, llvm::SmallVector<ctjs::CreateClosureOp>> &
            creators);

    /// Marks each ctjs.create_closure with its walk-order id for the trial
    /// lift of RunOnce.cpp; removed again before that function returns.
    static constexpr llvm::StringLiteral kTrialClosure = "ctnative.trial_closure";

    /// The same for each ctjs.create_cell, so that the trial can say which
    /// boxes bindLocalFunctions turned into direct calls.
    static constexpr llvm::StringLiteral kTrialCell = "ctnative.trial_cell";

    /// What a trial lift of a copy of the module did: the closures it left
    /// unlifted and the boxes it bound as local functions, by trial id.
    struct trialOutcome {
        llvm::DenseSet<int64_t> refused;
        llvm::DenseSet<int64_t> bound;
    };

    trialOutcome trialLift();

    /// The kTrialCell ids of the boxes bindLocalFunctions bound, filled when
    /// this lifter is a trial.
    llvm::DenseSet<int64_t> boundTrialCells;

    /// With `functions` false, boxes that hold a function are left alone, so
    /// that a second call can first see which of them the binding rule takes
    /// once the other boxes are static.
    bool makeRunOnceCellsStatic(liftReport & out, bool functions);

    bool bindLocalFunctions(liftReport & out, bool probeSiblings = false);

    /// Turns calls of methods of an object literal held in a static variable
    /// into direct calls when nothing else reads it (Singletons.cpp). Returns
    /// whether it changed the module.
    bool bindSingletonMethods(liftReport & out);

    std::string whyNotABoundFunction(ctjs::CellSetOp store);

    bool byValueMissesACall(ctjs::CreateCellOp cell);

    bool isCarried(ctjs::CreateCellOp cell) const;

    bool slotIsCarried(ctjs::CreateClosureOp c, unsigned i);

    mlir::Value constantValueOf(ctjs::CreateCellOp cell);

    bool isConstantCell(ctjs::CreateCellOp cell);

    std::optional<std::string> whyTargetIsNotLiftable(ctjs::CreateClosureOp c);

    /// The entry-block argument that holds upvalue k of a lifted function.
    /// lift() establishes this layout (it inserts the captures at 3, after the
    /// three implicit arguments, and rewrites every ctjs.load_upvalue k to the
    /// argument here), and capturedValue() reads it to hand a nested closure
    /// the enclosing function's capture k without an operand to follow (the
    /// `enclosing_indices` attribute). Two places depend on this layout, so it
    /// is defined once.
    ///
    /// Both places must use this helper. If one re-derived the position and
    /// the layout changed, it would silently select a parameter instead of a
    /// capture, and the result would still compile.
    static constexpr unsigned captureArgument(unsigned k) { return 3 + k; }
    static std::int32_t enclosingIndex(ctjs::CreateClosureOp c, unsigned i);

    mlir::Value capturedValue(ctjs::CreateClosureOp c, unsigned i);

    mlir::Value liftedCapture(ctjs::CreateClosureOp c, unsigned i);
    llvm::SmallVector<mlir::Value> liftedCaptures(ctjs::CreateClosureOp c);
    /// A fresh `undefined` constant at `at`.
    mlir::Value undefinedAt(mlir::OpBuilder & at, mlir::Location where);
    /// Marks `c` lifted and updates the report counts.
    void markLifted(ctjs::CreateClosureOp c, liftReport & out, unsigned captures);
    /// True when `from` is in `at`'s ctjs.func and properly dominates it. The
    /// function is compared first: builtin.module's body is a graph region, in
    /// which a bare `properlyDominates` answers yes for every pair of
    /// operations.
    bool reachesInFrame(mlir::Value from, mlir::Operation * at);
    bool reachesInFrame(mlir::Operation * from, mlir::Operation * at);
    /// Builds a ctjs.call_direct of `target` at `at`: `prefix` then `args`,
    /// padded with `undefined` to `width`; its new.target is `undefined` too.
    ctjs::CallDirectOp directCall(mlir::OpBuilder & at, mlir::Location where, ctjs::FuncOp target,
                                  mlir::Value receiver, mlir::Value callee, mlir::Value undefined,
                                  llvm::ArrayRef<mlir::Value> prefix, mlir::ValueRange args,
                                  size_t width);

    std::optional<std::string> whyCapturesDoNotReach(ctjs::CreateClosureOp c, mlir::Operation * at);

    /// Closures refused because a capture is filled from an enclosing closure
    /// that is not lifted: `enclosing_indices` names an upvalue of a function
    /// that carries no `ctnative.captures`. Once the fixpoint has settled,
    /// run() appends the enclosing closure's own reason to the message, which
    /// cannot be known when the refusal is made.
    llvm::DenseMap<mlir::Operation *, ctjs::FuncOp> chainedThrough;
    std::optional<std::string> whyCapturesDoNotLift(ctjs::CreateClosureOp c);

    std::optional<std::string> whyOwnClosureEscapes(ctjs::FuncOp target);

    std::optional<std::string> whyUpvalueReadsDoNotLift(ctjs::CreateClosureOp c,
                                                        ctjs::FuncOp target);

    /// A lift's last checks: captures reach each site, the target's own closure
    /// does not escape, and its upvalue reads lift.
    template <class Sites>
    std::optional<std::string> whyLiftTailFails(ctjs::CreateClosureOp c, ctjs::FuncOp target,
                                                Sites && sites) {
        for (mlir::Operation * site : sites) {
            if (std::optional<std::string> why = whyCapturesDoNotReach(c, site)) { return why; }
        }
        if (std::optional<std::string> escapes = whyOwnClosureEscapes(target)) { return escapes; }
        return whyUpvalueReadsDoNotLift(c, target);
    }

    /// Refuses surplus arguments, or a caller that passes new.target, at a
    /// site described by `noun` ("a call" or "a `new`").
    std::optional<std::string> whyCallSiteDoesNotLift(mlir::Operation * site, size_t arguments,
                                                      size_t parameters, llvm::StringRef noun);

    std::optional<std::string> whyNotLiftable(ctjs::CreateClosureOp c);

    bool closedAfterLift(mlir::Value object);

    bool makesAnInstance(mlir::Value object);

    bool usesCloseTheShape(mlir::Value object);

    bool retainedByLocalMap(mlir::OpOperand & use,
                            llvm::DenseMap<mlir::Value, mlir::Value> * reads = nullptr);

    std::optional<methodCall> resolveMethod(ctjs::CallOp call);

    std::string whyNotAMethodField(ctjs::SetPropertyOp set);

    std::string blockingLabel(mlir::Value object, mlir::OpOperand & use);

    std::string argumentDetail(mlir::Operation * call, unsigned j);

    static bool onlyConstantKeyAccess(mlir::Value v);

    // --- Object arguments: a parameter that carries a closed-shape object ------
    //
    // `f(o)` is `o.m()` with the pointer in a different operand. The receiver
    // lift's content is that a closed-shape literal reaches a function as a
    // `ctn_x *` and its fields as `self->x`; nothing in that carrier is about
    // operand 0. So an argument gets it too, on the same proof and with the
    // same refusals, and the two share `receiverType`, `receiverArgs`,
    // `memberAccess` and the alias groups rather than duplicating them.
    //
    // The conditions, each a named refusal when it fails:
    //
    //  1. The callee is one closed function: a local closure used only as a
    //     callee, or a unique hoisted declaration with closed direct calls.
    //     Every symbolic caller participates in the same argument census.
    //  2. Every call passes a closed object or another proved borrow. One
    //     parameter is one C++ type; a position that is a literal at one site
    //     and a number at another has no single spelling, and a short call
    //     that omits it would pass the padding `undefined`.
    //  3. The parameter is used only through constant keys or proved borrows.
    //     An unread parameter is `-Wunused-parameter` under -Werror; one
    //     reached any other way needs an owner.
    //
    // Condition 2 is a fixpoint. Whether a literal is closed depends on
    // whether being passed here opens it, which depends on whether this slot
    // carries an object, which depends on whether the literals passed to it
    // are closed. `argumentCensus` starts with every candidate slot admitted
    // and drops the ones whose literals do not hold up, until nothing changes.
    // This is the greatest fixpoint, which is the right reading of "no use
    // opens it": two literals passed to one read-only parameter support each
    // other, and neither opens anything.
    llvm::DenseMap<mlir::Operation *, llvm::SmallVector<unsigned, 2>> objectSlotsOf;
    // One immutable symbol census, consumed only before the first lift.
    llvm::StringMap<llvm::SmallVector<mlir::Operation *, 2>> objectArgumentSymbolUsers;
    llvm::StringMap<ctjs::CreateClosureOp> objectArgumentDeclarations;
    llvm::DenseSet<mlir::Operation *> directObjectArgumentFunctions;
    bool completeObjectArgumentSymbols = false;
    llvm::SmallVector<closureCall> objectArgumentCalls(ctjs::CreateClosureOp c);
    mlir::Operation * objectArgumentCallee(mlir::Operation * call);
    bool slotIsACandidate(ctjs::CreateClosureOp c, unsigned j);

    bool slotCarriesAnObject(mlir::Operation * callable, unsigned j) const;
    /// True for a call's operand 2 + j, or a call_direct's 3 + j, whose slot
    /// carries an object.
    bool carriesObjectArgument(mlir::OpOperand & use);
    /// True for a call whose callee is a constant-key read of its receiver
    /// `object`.
    static bool methodCallOn(ctjs::CallOp call, mlir::Value object);

    void argumentCensus();

    std::string whyNotAnObjectArgument(mlir::Operation * call, unsigned j);

    void blockingLabelsOf(mlir::Value object, llvm::StringSet<> & into,
                          llvm::StringMap<unsigned> * tally);

    std::string rootBlockingLabel(mlir::Value object, unsigned & depthOut);

    void censusOpenLiteral(mlir::Value object);

    void methodCensus();

    std::optional<std::string> whyThisLeaks(ctjs::FuncOp target);

    std::optional<std::string> whyNotLiftableMethod(ctjs::CreateClosureOp c);

    void constructorCensus();

    struct returnedClosure {
        llvm::SmallVector<mlir::Operation *> calls;
        std::string reason;
        bool stored = false;
    };
    llvm::DenseMap<mlir::Operation *, returnedClosure> returnedClosures;
    void returnedClosureCensus();
    void returnedMethodTableCensus(const OwnedGlobalRoots * globals = nullptr);
    /// Removes the ctnative.* attributes a previous run left on the source, so
    /// a re-run proves everything again.
    void discardNativeSourceFacts();
    /// The host-contract path: lifts the method table of a proved owned global,
    /// and returns nullopt when it cannot.
    std::optional<liftReport> prepareOwnedGlobalMethodTables(const OwnedGlobalRoots & globals,
                                                             const HostContract & contract,
                                                             unsigned maxSteps);
    std::optional<std::string> whyNotReturnedClosure(ctjs::CreateClosureOp c);
    void liftReturnedClosure(ctjs::CreateClosureOp c, ctjs::FuncOp target, unsigned captures,
                             unsigned parameters, liftReport & out, bool preserveReceiver = false);

    static bool isNotObjectLike(mlir::Value v);

    std::optional<std::string> whyConstructorReturnsAnObject(ctjs::FuncOp target);

    std::optional<std::string> whyPrototypeIsTouched(ctjs::CreateClosureOp c);

    /// True when `use` is `new c(...)` naming `c` as both its callee and its
    /// new.target.
    static bool isSelfConstruct(mlir::OpOperand & use, ctjs::CreateClosureOp c);

    std::optional<std::string> whyNotLiftableConstructor(ctjs::CreateClosureOp c);
    std::optional<std::string> whyConstructorSetupDoesNotLift(ctjs::CreateClosureOp c);

    // --- Borrowed callbacks (BorrowedCallbacks.cpp) ----------------------------
    //
    // A closure passed to a parameter that its callee only calls, where every
    // caller of that callee passes one. Each such closure maps to all the
    // closures passed to the same parameter (including itself), because that
    // parameter has one C++ type for all of them. The census is retaken each
    // round, as lifting a callee changes its operand positions.
    llvm::DenseMap<mlir::Operation *, llvm::SmallVector<ctjs::CreateClosureOp, 2>>
        borrowedCallbacks;
    void borrowedCallbackCensus();
    std::optional<std::string> whyNotBorrowedMember(ctjs::CreateClosureOp c);
    std::optional<std::string> whyNotBorrowedCallback(ctjs::CreateClosureOp c);
    void rewriteBorrowedCallback(ctjs::CreateClosureOp c, ctjs::FuncOp target, liftReport & out);

    liftReport run();

    void lift(ctjs::FuncOp target, llvm::ArrayRef<ctjs::CreateClosureOp> made, liftReport & out,
              bool preserveReceiver = false);

    static bool namesACellArgument(mlir::Operation * call, mlir::OpOperand & use);

    std::optional<std::string> whyCarriedCellStaysABox(ctjs::CreateCellOp cell);

    void unboxCells(liftReport & out, bool localObjectsOnly = false);
    /// Erases every unboxed cell nothing reads any more.
    void eraseUnusedUnboxedCells();
};

} // namespace ctcompile::ctnative::lowering_detail
