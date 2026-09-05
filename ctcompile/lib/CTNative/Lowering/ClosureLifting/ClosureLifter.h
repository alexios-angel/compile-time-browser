#pragma once

#include "../LoweringSupport.h"

namespace ctcompile::ctnative::lowering_detail {

struct liftReport {
    unsigned functions = 0;    // ctjs.funcs whose captures became parameters
    unsigned closures = 0;     // ctjs.create_closures that now lower to nothing
    unsigned captures = 0;     // capture operands turned into arguments
    unsigned calls = 0;        // ctjs.calls rewritten to ctjs.call_direct
    unsigned cells = 0;        // ctjs.create_cells proved constant and unboxed
    unsigned methods = 0;      // method fields whose closure became a free function
    unsigned receivers = 0;    // of those, the ones whose `this` became a parameter
    unsigned objects = 0;      // parameters that became a pointer to a closed shape
    unsigned constructors = 0; // `new X(...)` sites turned into a frame-scope struct
    unsigned carried = 0;      // capture slots that became a pointer to a frame-local cell
    unsigned locals = 0;       // ctjs.create_cells that became a frame-local variable
    unsigned bindings = 0;     // local bindings that held a function and lowered to nothing
    unsigned bound = 0;        // of `calls`, the ones made direct from another frame
    unsigned callbackCalls = 0;
    unsigned callbackParameters = 0;
};

struct closureLifter {
    mlir::ModuleOp module;
    mlir::MLIRContext * context;

    llvm::DenseMap<unsigned, ctjs::FuncOp> byIndex;
    // A function that may write one of its OWN upvalue slots, transitively
    // through the closures it makes. `ctjs.store_upvalue %arg2[j]` inside G
    // writes the cell that G's creator put in slot j, so a cell captured into
    // any such G is not constant - and a cell captured into G and re-captured
    // by an H that writes it is not either, which is why this is a fixpoint
    // and not a one-line test.
    llvm::DenseSet<mlir::Operation *> mutatesUpvalue;
    // A call inside one of these may not become a ctjs.call_direct: op::call
    // pushes its frame with the PENDING new.target, and call_direct
    // materialises undefined for it. ResolveGlobals refuses the same shape.
    llvm::DenseSet<mlir::Operation *> passesNewTarget;
    // THERE IS NO "ALREADY CALLED BY SYMBOL" GUARD, and there was one until it
    // was tested. It refused to lift a target a ctjs.call_direct already
    // names, on the grounds that such a call passes exactly the entry block's
    // operands and inserting capture parameters would break it. No program
    // reaches it: --ctjs-resolve-globals resolves only a global bound in the
    // top level's prologue to a create_closure, and that closure's single use
    // is the store - which isDeclarationClosure exempts before this rewrite
    // looks at it - while a source function compiles to exactly one `closure`
    // opcode, so no ctjs.func is both. Running the pass TWICE, which is the
    // one shape that could, is already idempotent for two independent reasons
    // the double run in closure-refusals.mlir pins: `upvalue_count` is set to
    // 0 by the first lift, so the second sees a capture list that disagrees
    // with the descriptors, and a lifted closure's only remaining use is a
    // call_direct's callee value, which is not a call this rewrite lowers.
    // Removing the guard and running the lowering twice changed nothing, so it
    // was decoration and is gone. If the reasoning above is wrong the failure
    // is CallDirectOp::verifySymbolUses on an operand count - a hard verifier
    // error, not a wrong answer.

    llvm::SmallVector<ctjs::CreateClosureOp> closures;

    // --- PHASE 59 SLICE 2 STEP 1: A BINDING WITH ONE DOMINATING WRITE -------
    //
    // `compiler_impl::predeclare_locals` hoists every `var`/`let`/`const` of a
    // body and BOXES it up front holding `undefined`, so `var n = 5` is a
    // ctjs.create_cell of a constant undefined and a ctjs.cell_set into the box
    // that already exists. Slice 1's immutability proof reads that as a
    // reassigned binding and refuses it - which is most of a real program: the
    // shape is 250 of the 271 closures bootstrap refuses for a reassigned
    // capture, 37 of phaser's 40 reachable callees and 7 of p5's 26.
    //
    // A BINDING WRITTEN ONCE IS CONSTANT AFTER THAT WRITE, so a capture taken
    // after it is exact - and "after" is dominance, not program order. The cell
    // is admitted when, and only when:
    //
    //   1. it has exactly ONE ctjs.cell_set naming it as the box;
    //   2. that store DOMINATES every ctjs.cell_get of it;
    //   3. that store DOMINATES every CALL of every closure that captures it -
    //      asked at the lift, by whyCapturesDoNotReach, and not here;
    //   4. every other use is one slice 1 already admits - a read at operand 0,
    //      or a capture of a closure whose target writes no upvalue.
    //
    // and its value is then the STORE'S OPERAND rather than the cell's initial.
    // Conditions 2 and 3 are what make the initial unobservable: no read of the
    // binding, in this frame or in a closure over it, can happen on a path that
    // skips the store, so nothing can ever see the hoisted `undefined`. That is
    // why any initial is admitted here and not only a constant undefined - the
    // conservative rule the measurement suggested would have cost the shapes
    // whose hoisted box is initialised from a parameter, and bought nothing,
    // because condition 2 already carries the whole argument.
    //
    // CONDITION 3 IS ABOUT THE CALL AND NOT ABOUT THE `ctjs.create_closure`,
    // and the difference is the whole of this step. A FUNCTION DECLARATION IS
    // HOISTED: `function get() { return n; }` beside `var n = 5` compiles to an
    // `op::closure` in the PROLOGUE, before the store, because that is what
    // JavaScript does - `get` is callable on the first line of the body. So a
    // rule that asked the store to dominate the create_closure refused the
    // exact shape this step exists for; measured, it refused every one of them.
    // What the lift actually does is prepend the captured VALUE at each CALL,
    // and a closure reads the box when it RUNS - `run_loop.cpp`,
    // VM_CASE(get_upvalue) - so the point that has to be dominated is the call
    // site. A closure created before the store and called after it reads the
    // stored value in the interpreter and is handed the stored value here.
    //
    // MLIR'S DOMINANCE IS EXACTLY THE RIGHT INSTRUMENT, INCLUDING FOR LOOPS.
    // `properlyDominates` normalises the LATER operation into the earlier one's
    // region, so a store inside a loop body does NOT dominate a read after the
    // loop (there is a path around the body) and condition 2 refuses it, while
    // a cell created inside the body is a fresh box each iteration whose store,
    // reads and captures are all in that one region and lifts.
    //
    // Filled by census(), before any rewrite: every input is a use of the cell,
    // and the lift changes none of them.
    llvm::DenseMap<mlir::Operation *, ctjs::CellSetOp> writtenOnce;
    // Why a cell that HAS a store is not in the map, for the diagnostic. Absent
    // means the cell has no ctjs.cell_set at all, which is slice 1's shape and
    // whose refusal - if any - is about a store_upvalue somewhere instead.
    llvm::DenseMap<mlir::Operation *, std::string> whyNotWrittenOnce;

    // --- PHASE 59 SLICE 2 STEP 2: A SHARED MUTABLE CELL, CARRIED BY POINTER -
    //
    // The cells step 1 CANNOT take: written twice, written on only one path,
    // or - the shape that dominates the reachable set - written by a closure
    // with `ctjs.store_upvalue`, which `mutatesUpvalue` marks and
    // `isConstantCell` refuses. There is no single value to copy into a
    // parameter, so nothing this tier does by VALUE can be right.
    //
    // SO THE BOX BECOMES AN ORDINARY FRAME-LOCAL VARIABLE AND THE CAPTURE
    // BECOMES A POINTER TO IT. `ctjs.create_cell` lowers to an
    // `emitc.variable` of the carrier, `ctjs.cell_get` to a load of it and
    // `ctjs.cell_set` to an assign; a lifted target takes `double *` for that
    // slot instead of `double`, its `ctjs.load_upvalue` and
    // `ctjs.store_upvalue` become a cell_get and a cell_set THROUGH the
    // pointer, and each call site passes the variable's address. That is the
    // receiver lift's convention exactly (`ctnative.receiver` emits
    // `ctn_x * self`), one operand along.
    //
    // WHY THE POINTER CANNOT DANGLE, which is the only question worth asking:
    //
    //   1. THE CLOSURE CANNOT OUTLIVE THE FRAME. Condition 4 of whyNotLiftable
    //      admits a closure only when EVERY use of its value is a call this
    //      rewrite lowers - a ctjs.call at operand 0, or a ctjs.call_direct at
    //      operand 2 the closed world named. Stored, returned or passed, it is
    //      refused by name. A closure that can be reached from nowhere but a
    //      call in this frame cannot be called after the frame is gone.
    //   2. AND EVERY CALL IS IN THE FRAME THAT OWNS THE VARIABLE.
    //      whyCapturesDoNotReach compares the ctjs.func the captured value
    //      lives in with the ctjs.func the call sits in, and refuses when they
    //      differ - which is the METHODCAP program in closure-refusals.mlir,
    //      and the one case where a bare dominance question answers "yes"
    //      because builtin.module's body is a graph region. It then asks that
    //      the cell properly dominate the call, so the variable is in scope in
    //      the emitted C++ at the point its address is taken.
    //   3. AND THE POINTER GOES NOWHERE ELSE. After the lift, the only uses of
    //      the parameter are the cell_get and cell_set that replaced the
    //      upvalue read and write - whyUpvalueReadsDoNotLift refuses a target
    //      whose load or store names anything but `%arg2` at an index this
    //      rewrite carries - plus its re-appearance at a nested lifted call,
    //      which is the same three conditions one level in.
    //
    // WHAT IS NOT PROVED HERE IS THE CARRIER, and it cannot be: this runs
    // BEFORE the type solve. `admission::function` asks it of the parameter
    // and `admission::op` of the box, both after the solve, and a cell of a
    // type with no C++ carrier is refused there - never pointed at.
    llvm::DenseSet<mlir::Operation *> carriedCells;
    // Why a cell is not carried either, for the diagnostic. Only the
    // structural clause can fail here; the carrier is admission's question.
    llvm::DenseMap<mlir::Operation *, std::string> whyNotCarried;

    // --- PHASE 59 SLICE 2 STEP 3: PAST THE HOISTED `undefined` -------------
    //
    // Step 2 emits the box as a frame-scope variable ASSIGNED THE INITIAL, so
    // `TypeInference::cellTypeOf` joins that initial into the binding's type at
    // every read and a hoisted `var` is `opt<num>` however the program uses it.
    // That is flow-INSENSITIVE, and it is the whole cost of step 2: `opt<num>`
    // carries undefined as NaN, and at a `ctjs.store_global` - the one place a
    // value becomes an observable - the print convention spells a Number
    // `%.17g`, so such a binding prints `nan` where the interpreter prints
    // `undefined`.
    //
    // A WRITE THAT DOMINATES EVERY READ MAKES THE INITIAL UNOBSERVABLE, and
    // the type at every read is then the join of the WRITES alone.
    // `var n = 0; function tick() { n = n + 1; }` - the declaration is a write
    // dominating everything, so `n` is `num`. `var v; if (k > 0) { v = 5; }` -
    // no write dominates the read in `get`, so `v` stays `opt<num>` and every
    // use where the difference shows is refused.
    //
    // THE TWO CONDITIONS ARE CONDITIONS 2 AND 3 OF STEP 1, WORD FOR WORD, and
    // `writeReachesEveryRead` is where the two rules are one function rather
    // than two answers. Step 1 asks them to REPLACE a read with the store's
    // operand; this asks them to DROP the box's initial from that read's type.
    // Both rest on the same fact - no read of the binding, in this frame or in
    // a closure over it, can happen on a path that skipped the store - and
    // step 1's own comment beside `writtenOnce` is the argument for it.
    //
    // WHERE THE TWO DELIBERATELY PART: step 1 also needs the stored VALUE to
    // dominate each call (`byValueMissesACall`), because it copies that value
    // into a parameter. This copies nothing - the pointer is passed and the
    // callee loads through it - so only the STORE has to dominate. That is why
    // the OUTERSTORE shape (`var t = k*2; if (k>0) { v = t; }`), whose value
    // dominates every call and whose store dominates none, is carried by step
    // 2 and REFUSED narrowing by this one. Asking about the value here would
    // have admitted exactly the wrong answer step 1 shipped once already.
    //
    // AND NO CALLEE WRITE IS NEEDED, which is the interprocedural question this
    // rule could have got wrong. In `function counter() { var n = 0; function
    // tick() { n = n + 1; } tick(); return n; }` the write inside `tick`
    // dominates the read in `counter` in no single CFG - and it does not have
    // to, because `var n = 0` already dominates both the frame's own read and
    // every call of `tick`. A callee's write can only ADD a value to the join;
    // it can never put the initial back. So this rule asks only about writes in
    // the box's OWN frame, which is exactly the set `ctjs.cell_set` names, and
    // the `ctjs.store_upvalue` a callee makes is left to the type join.
    llvm::DenseSet<mlir::Operation *> assignedBeforeRead;

    // --- PHASE 59 SLICE 2 STEP 4: A LOCAL BINDING THAT HOLDS A FUNCTION ----
    //
    // `var f = function () { ... };` inside a function body is a hoisted local,
    // and it is BOXED exactly when some nested function mentions the name -
    // `compiler_impl::declare_local` asks `is_captured` (capture.cpp) and
    // `predeclare_locals` emits `op::new_cell` only for the answer yes. So the
    // shape this step is about is a `ctjs.create_cell` of `undefined`, a
    // `ctjs.create_closure`, a `ctjs.cell_set` putting the second in the first,
    // and then a `ctjs.cell_get` in this frame or a `ctjs.load_upvalue` in a
    // nested one wherever the name is used. Slice 1 refused the closure by the
    // MECHANISM - "it reaches `ctjs.cell_set`, which slice 1 does not lower" -
    // which is 87 of bootstrap's closure refusals and the terminal of 9 of the
    // 19 chains a `ctjs.call_direct` can reach.
    //
    // A BINDING WRITTEN ONCE WITH A CLOSURE, AND ONLY EVER CALLED THROUGH, IS
    // THAT FUNCTION. That is the DECLARATION case one scope in:
    // `isDeclarationClosure` already lowers a create_closure whose one use is a
    // `ctjs.store_global` to nothing, because the closed world names the callee
    // and every call of it is direct. The box adds nothing a call needs, so the
    // box, its store and its reads all lower to nothing and each call through
    // the name becomes a `ctjs.call_direct` of the target.
    //
    // THE CONDITIONS, each a named refusal when it fails (`bindingClosures` is
    // where the admitted ones end up):
    //
    //  1. ONE STORE, HOLDING A CLOSURE MADE HERE AND USED FOR NOTHING ELSE.
    //     Two stores make the binding a variable again and which function a
    //     call reaches depends on the path to it; a closure with a second use
    //     is a value as well as a name, and that use is what the other rules
    //     are for.
    //  2. EVERY OTHER USE OF THE BOX IS A READ OR A CAPTURE. A box stored into
    //     an object, put in another box, returned or passed is one something
    //     else holds, and a name that lowers to nothing cannot stand in for it.
    //  3. THE STORE REACHES EVERY READ - `writeReachesEveryRead`, the same
    //     function slice 2 steps 1 and 3 ask, and the same two clauses: it
    //     dominates every `ctjs.cell_get` of the box, and it dominates every
    //     CALL of every closure that captured it. A read before it yields the
    //     `undefined` the binding was hoisted with, which is not a function.
    //  4. EVERY READ IS A CALL THIS TIER LOWERS. In this frame that is a
    //     `ctjs.call` at operand 0 of a `ctjs.cell_get` result; in a nested
    //     function it is a `ctjs.call` at operand 0 of a `ctjs.load_upvalue`
    //     result. A read used for anything else - compared, stored, passed -
    //     is a function VALUE, and this step makes no value. ASKED AT EVERY
    //     DEPTH THE BINDING REACHES - Phase 59 slice 2 step 5: a closure made
    //     inside one of those nested functions can fill a slot of its own from
    //     the one holding the box (`enclosing_indices`), and then the same
    //     three questions are asked one frame further in.
    //  5. AND THE CAPTURES REACH THE CALLS THIS STEP WRITES ITSELF. A call in
    //     ANOTHER function is one lifting has nothing to prepend at - the
    //     METHODCAP argument, one binding along - so a binding read from a
    //     nested function may hold only a closure whose every capture is
    //     ITSELF a binding this step erases. That is not the empty condition
    //     it looks like: a bundle's helpers close over each other, and the
    //     recursive `var f = function () { ... f(); }` is exactly the case
    //     where the one capture is the binding being defined.
    //
    // RECURSION IS ADMITTED, AND IT IS THE SHAPE THE RULE IS BUILT AROUND.
    // `f` names itself, so the closure captures the very box it is written
    // into; condition 5 sees a capture that is a binding this step erases and
    // takes it, and the `ctjs.load_upvalue` inside the body becomes a
    // `ctjs.call_direct` of the target it sits in. Condition 3 needs one
    // sentence for it, and it is beside the clause in `writeReachesEveryRead`:
    // the closure's only use is the store, and a store is not a call, so
    // asking it to dominate itself asks the wrong question - a read of the
    // binding inside that function happens when the function RUNS, and every
    // call of it is one of the reads this walk has already checked.
    //
    // THE CAPTURE SLOT IS REMOVED, WHICH IS THE ONLY IR SURGERY HERE. A slot
    // holding a binding that lowers to nothing has no value to pass, so the
    // operand goes, `enclosing_indices` shortens with it, the target's
    // `upvalue_count` drops and every `ctjs.load_upvalue` index past the hole
    // is renumbered. A nested closure that names the removed slot through its
    // own `enclosing_indices` HAS ITS OWN SLOT REMOVED TOO, deepest first -
    // Phase 59 slice 2 step 5, and the reason step 4 refused that shape does
    // not survive step 4's own arrival: `makeBoundCallDirect` writes a
    // `ctjs.call_direct` in a frame the closure VALUE cannot reach, so a read
    // two frames in has the same call site as a read one frame in.
    //
    // IT RUNS BEFORE `census()`, and it has to: the censuses record the uses of
    // every cell, and this erases cells, stores, reads and calls. It is the
    // same ordering rule the three censuses state about `lift()`.
    //
    // AND IT IS `--ctjs-resolve-globals` FOR LOCALS, which is why so little of
    // the machinery below had to change. The resolver turns a global bound once
    // in the prologue to a closure into a symbol every call names; this turns a
    // LOCAL bound once in a frame into the same thing. What is left for the
    // ordinary lift is a `ctjs.create_closure` whose every use is a call, which
    // is slice 1's own shape.
    //
    // AND CONDITION 3 IS CONSERVATIVE ABOUT SOURCE ORDER, WHICH IS WHERE THIS
    // RULE STOPS. `writeReachesEveryRead` asks that the store dominate every
    // USE of every closure that captured the box, not every CALL of one - it
    // cannot ask about calls, because a closure whose own value goes into a box
    // has no call site at all until this very rewrite makes one. That is sound:
    // a call of a closure happens at or after some use of its value, so a store
    // dominating every use ran before any call could. It also refuses
    // `function g() { f(); } function f() {}` written in that order, because
    // g's own store comes first - a real shape, and this rule's next lever.
    // The closures the admitted bindings held. Condition 4 of `whyNotLiftable` asks
    // that SOMETHING call the closure, and for one of these the calls may all
    // be `ctjs.call_direct`s in other frames that this step wrote - which are
    // not uses of the closure value at all.
    llvm::DenseSet<mlir::Operation *> bindingClosures;
    // Why a box that holds a function is not one, for the diagnostic. Written
    // for every cell whose `ctjs.cell_set` stores a `ctjs.create_closure`, so
    // that "it reaches `ctjs.cell_set`" is replaced by the clause that failed.
    llvm::DenseMap<mlir::Operation *, std::string> whyNotAFunctionBinding;

    // ONE PER LIFTER, and it stays valid across the classify-then-lift
    // fixpoint: lift() inserts entry-block ARGUMENTS and erases and creates
    // operations, and neither changes the block structure a dominator tree is
    // over. MLIR invalidates a block's operation-order cache itself on every
    // insertion and removal, so the intra-block half is maintained too.
    mlir::DominanceInfo dominance{nullptr};

    // --- the receiver lift's census ---------------------------------------
    llvm::SmallVector<ctjs::CreateObjectOp> objects;
    llvm::SmallVector<ctjs::CallOp> allCalls;
    llvm::SmallVector<ctjs::CallDirectOp> allDirectCalls;
    // The single closure a literal's key holds, per literal, per key.
    struct methodField {
        ctjs::SetPropertyOp store;
        ctjs::CreateClosureOp closure;
    };
    llvm::DenseMap<mlir::Value, llvm::StringMap<methodField>> methodsOf;
    // Which closed literals a receiver value may name: a literal names itself,
    // and a lifted method's `%arg0` names everything its call sites pass. The
    // second row is a fixpoint, because `this.other()` inside a method is a
    // call whose receiver is that `%arg0`.
    llvm::DenseMap<mlir::Value, llvm::SmallVector<mlir::Value, 2>> behind;
    // One method call this rewrite makes direct.
    struct methodCall {
        ctjs::CallOp call;
        ctjs::GetPropertyOp load;
        mlir::Value receiver;
        ctjs::FuncOp target;
    };
    llvm::MapVector<mlir::Operation *, llvm::SmallVector<methodCall>> callsOfTarget;
    llvm::DenseSet<mlir::Operation *> methodClosures; // create_closures bound to a method field

    // --- the constructor lift ----------------------------------------------
    //
    // `new X(a, b)` WHERE X IS A ctjs.create_closure RESULT. The whole of this
    // rule is a rewrite into two operations that already lower: the instance
    // becomes an empty ctjs.create_object - a frame-scope struct, allocated
    // nowhere - and the constructor becomes the RECEIVER CARRIER's free
    // function over it, reached by the same ctjs.call_direct the method lift
    // emits. So `hasClosedShape`, `groupReceivers`, `fieldsOf`, `censusShapes`
    // and `replace` need no constructor case at all: after the rewrite there
    // is no constructor, only a literal and a call that takes its address.
    //
    // WHAT THIS DELIBERATELY DOES NOT BUILD IS THE PROTOTYPE CHAIN. The VM
    // gives every instance one (call.cpp, `make_instance` -> `ensure_prototype`)
    // and Phase 60 owns turning that into C++ inheritance, so any program that
    // touches a constructor's `prototype` is refused by name here rather than
    // compiled to something with no chain at all.
    llvm::SmallVector<ctjs::ConstructOp> allConstructs;
    llvm::MapVector<mlir::Operation *, llvm::SmallVector<ctjs::ConstructOp>> constructsOfTarget;
    llvm::DenseSet<mlir::Operation *> constructorClosures; // used ONLY as `new` callees

    // --- THE CENSUS, which is a MEASUREMENT and not a rule ------------------
    //
    // Widening `closedAfterLift` is only worth what the corpora actually
    // contain, and this project has twice widened a rule for a shape no bundle
    // has. So the pass can be asked what is blocking every method-bearing
    // literal it refuses: one bucket per blocking use, spelled as the
    // operation and the operand position so that no assumption about what
    // "escapes" means gets to shape the answer. `soleBlocker` is the number
    // that decides anything - an object with two kinds of blocking use is not
    // unblocked by widening one of them.
    bool censusOn = false;
    llvm::StringMap<unsigned> censusUses;
    llvm::StringMap<unsigned> censusSole;
    llvm::StringMap<unsigned> censusSoleFields;
    llvm::StringMap<unsigned> censusRoot;
    llvm::StringMap<unsigned> censusDepth;
    // ONE EXAMPLE PER BUCKET, WITH ITS SOURCE LOCATION. A distribution says
    // how much a rule would be worth and nothing at all about whether the
    // shape behind it is what the label suggests; this is what makes the
    // census checkable against the JavaScript it came from.
    llvm::StringMap<mlir::Location> censusExample;
    unsigned censusOpenObjects = 0;
    unsigned censusMethodFields = 0;

    explicit closureLifter(mlir::ModuleOp m, bool doCensus = false)
        : module(m), context(m.getContext()), censusOn(doCensus) {}
    ctjs::FuncOp targetOf(ctjs::CreateClosureOp c);

    // ONE CALL SITE OF A CLOSURE, IN EITHER OF THE TWO SHAPES IT NOW HAS.
    //
    // --ctjs-resolve-globals names a ctjs.call whose callee is a
    // ctjs.create_closure result, so by the time this pass runs the SAME site
    // is either a ctjs.call with the closure at operand 0 and its arguments
    // from operand 2, or a ctjs.call_direct with the closure at operand 2
    // (`$callee_value`) and its arguments from operand 3 - because
    // call_direct's operands ARE the callee's entry block in order.
    //
    // FOUR SEPARATE CENSUSES IN THIS FILE ASKED THAT QUESTION BY CASTING TO
    // ctjs::CallOp, and every one of them silently answered "not a call" for a
    // named site. Measured: with the naming in place and this abstraction
    // absent, native-object-argument-fixture.js stopped being native at all -
    // "an object literal passed to a direct call as an argument". The two
    // shapes are one question, so they are asked in one place.
    struct closureCall {
        mlir::Operation * op = nullptr;
        mlir::ValueRange args;
        mlir::Value receiver;
        explicit operator bool() const { return op != nullptr; }
    };
    closureCall callSiteOf(mlir::OpOperand & use, ctjs::FuncOp target);

    static ctjs::CreateClosureOp closureCalledBy(mlir::Operation * user);

    static mlir::ValueRange argsOfCallSite(mlir::Operation * user);

    void indexAndNewTargets();

    void specializeCallbacks(liftReport & out);

    void census();

    void singleWriteCensus();

    void sharedCellCensus();

    ctjs::CellSetOp dominatingWriteOf(ctjs::CreateCellOp cell);

    bool writeReachesEveryRead(ctjs::CreateCellOp cell, ctjs::CellSetOp store);

    bool isAssignedBeforeRead(ctjs::CreateCellOp cell) const;

    // --- PHASE 59 SLICE 2 STEP 4: THE RULE ---------------------------------

    // What the check found, so that it and the rewrite cannot disagree about
    // which operations they mean. `examineFunctionBinding` fills it and
    // `bindLocalFunctions` reads it; nothing else constructs one.
    struct functionBinding {
        ctjs::CreateCellOp cell;
        ctjs::CellSetOp store;
        ctjs::CreateClosureOp closure;
        ctjs::FuncOp target;
        ctjs::FuncOp owner;
        // The reads in the frame that owns the box.
        llvm::SmallVector<ctjs::CellGetOp> reads;
        // And the closures of THIS frame that carry it one frame in, with the
        // slot each one holds it at - the entries the cell's own use list
        // names, and the roots of the walk below.
        llvm::SmallVector<std::pair<ctjs::CreateClosureOp, unsigned>> captured;
        // EVERY SLOT THE REWRITE HAS TO REMOVE, THE DEEP ONES INCLUDED -
        // PHASE 59 SLICE 2 STEP 5. A closure made INSIDE one of those targets
        // can fill a slot of its own from the enclosing closure's slot
        // (`enclosing_indices`, slice 1b), which is the binding travelling one
        // frame further in; `examineCapturedSlot` follows it and appends in
        // POST-ORDER, so a slot always stands before the slot it is filled
        // from. `depth` is 0 for a slot the owning frame filled and one more
        // for each frame after that, and pass B sorts on it.
        struct capturedSlot {
            ctjs::CreateClosureOp made;
            unsigned slot;
            unsigned depth;
        };
        llvm::SmallVector<capturedSlot> slots;
        unsigned calls = 0;
    };

    // WHICH SLOTS OF ONE CLOSURE GO, AND HOW FAR IN THE SHALLOWEST OF THEM WAS.
    // One closure can hold slots from more than one binding, so the removals
    // are collected per closure and the whole set goes in one rewrite -
    // `removeCaptureSlots` renumbers, and a second rewrite of the same closure
    // would be renumbering indices the first one already moved.
    struct slotRemoval {
        llvm::SmallVector<unsigned> slots;
        unsigned depth = 0;
    };
    static bool holdsAFunction(ctjs::CreateCellOp cell);

    static bool readsRawArguments(ctjs::FuncOp target);

    std::optional<std::string> whyReadIsNotACall(mlir::Value read, ctjs::FuncOp target,
                                                 unsigned & calls);

    std::optional<std::string> examineCapturedSlot(
        functionBinding & plan, ctjs::CreateClosureOp made, unsigned slot, unsigned depth,
        llvm::DenseSet<std::pair<mlir::Operation *, unsigned>> & examined);

    std::optional<std::string> examineFunctionBinding(ctjs::CreateCellOp cell,
                                                      functionBinding & plan);

    void makeBoundCallDirect(ctjs::CallOp call, ctjs::FuncOp target);

    void removeCaptureSlots(ctjs::CreateClosureOp c, llvm::ArrayRef<unsigned> slots);

    void bindLocalFunctions(liftReport & out);

    std::string whyNotABoundFunction(ctjs::CellSetOp store);

    bool byValueMissesACall(ctjs::CreateCellOp cell);

    bool isCarried(ctjs::CreateCellOp cell) const;

    bool slotIsCarried(ctjs::CreateClosureOp c, unsigned i);

    mlir::Value constantValueOf(ctjs::CreateCellOp cell);

    bool isConstantCell(ctjs::CreateCellOp cell);

    std::optional<std::string> whyTargetIsNotLiftable(ctjs::CreateClosureOp c);

    // WHERE A LIFTED FUNCTION'S UPVALUE k IS, WRITTEN ONCE. lift() ESTABLISHES
    // this layout - it inserts the captures at 3, after the three implicit
    // arguments, and rewrites every ctjs.load_upvalue k to the argument here -
    // and capturedValue() READS it, to hand a nested closure the enclosing
    // function's capture k without an operand to follow (slice 1b's attribute
    // encoding). Two places, one claim, so it is spelled in one.
    //
    // AND THE PIN IS THE POINT, NOT THE ARITHMETIC. While the index lived in an
    // operand, capturedValue range-CHECKED an argument lift() had already
    // written there, so a change to the layout could not make the two disagree:
    // the wrong shape simply failed the check and the closure was refused. A
    // reader that re-derives the position instead SELECTS an argument, and a
    // layout change that this function did not hear about selects the wrong
    // one - a capture silently swapped for a parameter, which compiles clean.
    // A shared helper makes the divergence impossible rather than detectable,
    // which is why it is preferred here to a test that would notice it.
    static constexpr unsigned captureArgument(unsigned k) { return 3 + k; }
    static std::int32_t enclosingIndex(ctjs::CreateClosureOp c, unsigned i);

    mlir::Value capturedValue(ctjs::CreateClosureOp c, unsigned i);

    mlir::Value liftedCapture(ctjs::CreateClosureOp c, unsigned i);

    std::optional<std::string> whyCapturesDoNotReach(ctjs::CreateClosureOp c, mlir::Operation * at);

    // A CAPTURE THE ENCLOSING CLOSURE FILLS, WHILE THAT CLOSURE IS UNLIFTED:
    // `enclosing_indices` names an upvalue of a function that carries no
    // `ctnative.captures`, because it was not lifted. The closure is refused
    // for it, and run() appends the ENCLOSING closure's own reason to the
    // sentence once the fixpoint has settled it - which is why this map exists:
    // the reason cannot be known here.
    llvm::DenseMap<mlir::Operation *, ctjs::FuncOp> chainedThrough;
    std::optional<std::string> whyCapturesDoNotLift(ctjs::CreateClosureOp c);

    std::optional<std::string> whyOwnClosureEscapes(ctjs::FuncOp target);

    std::optional<std::string> whyUpvalueReadsDoNotLift(ctjs::CreateClosureOp c,
                                                        ctjs::FuncOp target);

    std::optional<std::string> whyNotLiftable(ctjs::CreateClosureOp c);

    bool closedAfterLift(mlir::Value object);

    bool makesAnInstance(mlir::Value object);

    bool usesCloseTheShape(mlir::Value object);

    ctjs::FuncOp resolveMethod(ctjs::CallOp call, mlir::Value & receiverOut,
                               ctjs::GetPropertyOp & loadOut);

    std::string whyNotAMethodField(ctjs::SetPropertyOp set);

    std::string blockingLabel(mlir::Value object, mlir::OpOperand & use);

    std::string argumentDetail(mlir::Operation * call, unsigned j);

    static bool onlyConstantKeyAccess(mlir::Value v);

    // --- PHASE 59 SLICE 1, ARGUMENT FORM: A PARAMETER CARRIES AN OBJECT -----
    //
    // `f(o)` IS `o.m()` WITH THE POINTER IN A DIFFERENT OPERAND. The receiver
    // lift's whole content is that a closed-shape literal reaches a function as
    // a `ctn_x *` and its fields as `self->x`; nothing in that carrier is about
    // operand 0. So an ARGUMENT gets it too, on the same proof and with the
    // same refusals - and the two share `receiverType`, `receiverArgs`,
    // `memberAccess` and the alias groups rather than growing a second copy.
    //
    // THE CONDITIONS, each a named refusal when it fails:
    //
    //  1. THE CALLEE IS ONE FUNCTION THIS REWRITE IS ABOUT TO MAKE DIRECT -
    //     a `ctjs.create_closure` made here whose every use is a call of it.
    //     Anything else has no call site to put an address at.
    //  2. EVERY CALL PASSES AN OBJECT LITERAL IN THAT POSITION, and every one
    //     of those literals is itself closed. One parameter is one C++ type;
    //     a position that is a literal at one site and a number at another has
    //     no single spelling, and a short call that omits it would pass the
    //     padding `undefined`.
    //  3. THE PARAMETER IS READ, AND ONLY THROUGH CONSTANT KEYS. Unread, the
    //     emitted parameter is `-Wunused-parameter` under -Werror; reached any
    //     other way it needs an owner, which this slice introduces none of.
    //
    // WHAT CONDITION 2 COSTS, AND WHY IT IS A FIXPOINT. Whether a literal is
    // closed depends on whether being passed here opens it, which depends on
    // whether this slot carries an object, which depends on whether the
    // literals passed to it are closed. `argumentCensus` starts from every
    // candidate slot admitted and DROPS the ones whose literals do not hold up,
    // to a fixpoint - the greatest one, which is the right reading of "no use
    // opens it": two literals passed to one read-only parameter support each
    // other, and neither opens anything.
    llvm::DenseMap<mlir::Operation *, llvm::SmallVector<unsigned, 2>> objectSlotsOf;
    bool slotIsACandidate(ctjs::CreateClosureOp c, unsigned j);

    bool slotCarriesAnObject(ctjs::CreateClosureOp c, unsigned j) const;

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
    void returnedMethodTableCensus();
    std::optional<std::string> whyNotReturnedClosure(ctjs::CreateClosureOp c);
    void liftReturnedClosure(ctjs::CreateClosureOp c, ctjs::FuncOp target, unsigned captures,
                             unsigned parameters, liftReport & out);

    static bool isNotObjectLike(mlir::Value v);

    std::optional<std::string> whyConstructorReturnsAnObject(ctjs::FuncOp target);

    std::optional<std::string> whyPrototypeIsTouched(ctjs::CreateClosureOp c);

    std::optional<std::string> whyNotLiftableConstructor(ctjs::CreateClosureOp c);

    liftReport run();

    static bool admissionIsDeclaration(ctjs::CreateClosureOp c);

    void lift(ctjs::FuncOp target, llvm::ArrayRef<ctjs::CreateClosureOp> made, liftReport & out);

    static bool namesACellArgument(mlir::Operation * call, mlir::OpOperand & use);

    std::optional<std::string> whyCarriedCellStaysABox(ctjs::CreateCellOp cell);

    void unboxCells(liftReport & out);
};

} // namespace ctcompile::ctnative::lowering_detail
