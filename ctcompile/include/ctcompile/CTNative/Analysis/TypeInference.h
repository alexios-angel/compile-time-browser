// PHASE 54A - WHAT TYPE IS THIS VALUE, ANSWERED STATICALLY AND CHECKED AGAINST
// A RUNNING INTERPRETER.
//
// Every phase from 55 onward consumes this: escape analysis needs to know what
// it is proving a bound on, structification needs to know a field's type, and
// the native backend refuses any function whose values it cannot name. So the
// analysis is not the deliverable - the analysis TOGETHER WITH the oracle that
// checks it is, and part 24 says so: "An analysis nobody can check is a
// liability."
//
// THE SHAPE IS UPSTREAM'S. A sparse forward dataflow analysis over SSA values,
// on MLIR's DataFlowFramework, so the fixpoint, the worklist, the block
// argument joins and the dead-code interaction are all upstream's problem. Our
// contribution is the transfer function - one switch over ctjs operations
// stating what JavaScript guarantees about each one's result - and the lattice
// it moves in, which is CTNativeLattice.h's.
//
// TWO WORDS FOR ONE OPERATION, AND THEY COLLIDE. MLIR's framework calls the
// binary lattice operation `join`; ctnative calls it `meet`. THEY ARE THE SAME
// FUNCTION HERE. CTNativeLattice.h explains the naming: ctnative orders the
// lattice bottom-to-top as bottom -> concrete -> boxed, so "the smallest type
// correct for both" is a JOIN in that ordering and is called `meet` for the
// lattice-theory reason that it is the greatest lower bound in the ordering by
// PRECISION. Nothing here may introduce a second rule; TypeValue::join simply
// calls ctnative::meet.
//
// SOUNDNESS IS ABSOLUTE AND PRECISION IS A BACKLOG. A transfer function that
// answers `num<i32>` for a value the interpreter observed as 1.5 is a DEFECT,
// and Phase 54B's oracle names it. A transfer function that answers `boxed`
// for a value that was always a string is merely disappointing. When the two
// are in tension the answer is `boxed`, every time.
#ifndef CTCOMPILE_CTNATIVE_ANALYSIS_TYPEINFERENCE_H
#define CTCOMPILE_CTNATIVE_ANALYSIS_TYPEINFERENCE_H

#include "ctcompile/CTNative/IR/CTNativeLattice.h"

#include "mlir/Analysis/DataFlow/SparseAnalysis.h"
#include "mlir/Analysis/DataFlowFramework.h"
#include "mlir/IR/Dominance.h"
#include "mlir/IR/Types.h"
#include "mlir/IR/Value.h"

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringRef.h"

namespace ctcompile::ctnative {

/// PHASE 59 SLICE 2 STEP 3: THE ATTRIBUTE THAT SAYS A CARRIED BINDING'S
/// HOISTED `undefined` CANNOT BE READ.
///
/// `--ctnative-lower-to-emitc`'s closure lift writes it onto a
/// `ctjs.create_cell` it also marked `ctnative.carried` when one
/// `ctjs.cell_set` of that box properly dominates every read of the binding -
/// every `ctjs.cell_get` in the owning frame, and every CALL of every closure
/// that captured it. `cellTypeOf` then joins the WRITES alone and leaves the
/// cell's initial out, which is what takes `var n = 0; function tick() { n = n
/// + 1; }` from `opt<num>` to `num`.
///
/// THE PROOF IS THE LIFT'S AND NOT THIS ANALYSIS'S, deliberately. It is a
/// dominance question over the `ctjs.call`s of the capturing closures, and
/// `lift()` erases every one of them before the solver runs - so the only
/// place it can be asked is where slice 2 step 1 already asks it. Spelled once,
/// here, because the writer and the reader are in different translation units
/// and a mistyped attribute name is a silent narrowing that nothing verifies.
inline constexpr llvm::StringLiteral kAssignedBeforeRead = "ctnative.assigned_before_read";

// One SSA value's inferred type, as a lattice element.
//
// A NULL TYPE IS "NOTHING KNOWN YET" AND IS NOT bottom. The framework
// constructs every lattice element before it visits anything that could define
// it, and that initial state has to be the identity so the first real answer
// replaces it rather than joining with it. ctnative::meet already treats a null
// Type as absent - `meet(Type(), t)` is `t` - so the identity is free and this
// class does not re-implement it.
class TypeValue {
public:
    TypeValue() = default;
    explicit TypeValue(mlir::Type type) : type_{type} {}

    [[nodiscard]] mlir::Type getType() const { return type_; }
    [[nodiscard]] bool isUninitialized() const { return type_ == nullptr; }

    /// THE JOIN, WHICH IS ctnative::meet. See the file comment: the two names
    /// describe the same operation and this is the only place they touch.
    static TypeValue join(const TypeValue & lhs, const TypeValue & rhs) {
        return TypeValue{meet(lhs.type_, rhs.type_)};
    }

    bool operator==(const TypeValue & other) const { return type_ == other.type_; }

    void print(llvm::raw_ostream & os) const;

private:
    mlir::Type type_{};
};

using TypeLattice = mlir::dataflow::Lattice<TypeValue>;

// The analysis itself. Load it into a solver alongside BOTH DeadCodeAnalysis
// AND SparseConstantPropagation, and neither is optional:
//
//   * without DeadCodeAnalysis the framework has no predecessor information,
//     so block arguments never receive the operands branched into them;
//   * without SparseConstantPropagation, DeadCodeAnalysis cannot decide which
//     successor of a branch is live - it asks for each branch operand's
//     ConstantValue lattice, finds it uninitialized, and marks NO successor
//     live. Every op past the first branch is then never visited. The unit
//     test has a multi-block row so this cannot regress quietly.
class TypeInference : public mlir::dataflow::SparseForwardDataFlowAnalysis<TypeLattice> {
public:
    using SparseForwardDataFlowAnalysis::SparseForwardDataFlowAnalysis;

    mlir::LogicalResult visitOperation(mlir::Operation * op,
                                       llvm::ArrayRef<const TypeLattice *> operands,
                                       llvm::ArrayRef<TypeLattice *> results) override;

    /// THE CLOSED WORLD FOR GLOBALS (part 24 Phase 62½-A). A whole-program
    /// compile sees every `ctjs.store_global` of a name, so a
    /// `ctjs.load_global` of that name is the join of everything ever stored
    /// under it - and the framework re-visits the load when any of those
    /// stores' operands change, because the load subscribes to them. Three
    /// cases keep it sound:
    ///   * no store anywhere: the name is a builtin (`Math`) or undeclared,
    ///     and the load is `boxed`;
    ///   * any dynamic write to the globals table in the program (a property
    ///     store through `globalThis`/`window`): every load is `boxed`;
    ///   * otherwise: the join over the stores' operand types.
    /// The index is built once, in initialize(), from the module being solved.
    mlir::LogicalResult initialize(mlir::Operation * top) override;

    /// THE CLOSED SHAPE (part 24 Phase 56A, the typing half). An object
    /// literal whose every use is a `get_property` or `set_property` with a
    /// constant string key has a shape nothing else can change: no dynamic
    /// key, no delete, no enumeration, no escape - it never reaches anything
    /// that could add or remove a field. For such an object a read of key
    /// `k` is the join of every store of `k` to it, starting from undefined
    /// because nothing orders the read after a store. Any other use of the
    /// object - a call, a return, a dynamic key, a `delete`, `in`, spread -
    /// makes its shape open and every read of it `boxed`.
    static bool hasClosedShape(mlir::Value object);

    /// THE RECEIVER IS A PARAMETER (part 24, the receiver carrier). A method
    /// whose `this` uses are all constant-key property accesses is lifted:
    /// `--ctnative-lower-to-emitc` marks the target `ctnative.receiver` and
    /// each of its calls with the same attribute, and `%arg0` - the entry
    /// block's receiver - then names the object literal the call passes. So a
    /// receiver argument has a closed shape by the lift's own proof, and
    /// `this.x` is the same field read as `o.x` in the caller.
    ///
    /// TRUE FOR `%arg0` OF A `ctnative.receiver` FUNCTION, with the same use
    /// loop as a literal: the argument is closed only if every use of it is a
    /// constant-key get or set, or the receiver of another lifted method call.
    static bool isReceiverArgument(mlir::Value v);

    /// AND AN ARGUMENT IS A PARAMETER TOO. `f(o)` where every call passes a
    /// closed literal there and the parameter is only ever read through
    /// constant keys is lifted the same way: the lowering marks the target and
    /// each of its calls `ctnative.object_args` with the ENTRY-BLOCK indices
    /// that carry a `ctn_x *`. Nothing in the receiver carrier was about
    /// operand 0, so this is the same row of the table one operand along.
    ///
    /// TRUE FOR ANY ENTRY ARGUMENT THE LIFT LISTED, `%arg0` included via
    /// isReceiverArgument - so `hasClosedShape` and `groupReceivers` ask this
    /// one question rather than two.
    static bool namesAnObjectParameter(mlir::Value v);

    /// THE VALUES THAT NAME ONE OBJECT, once `this` is a parameter. A method
    /// lifted onto two literals of one shape has ONE `%arg0` standing for
    /// both, and `this.x = 5` inside it is a store the caller's `o.x` has to
    /// see - so the field index, the shape census and the emitted class all
    /// have to work over the GROUP and not over one value. Built by one walk
    /// of `top`: every closed literal and every receiver argument is a node,
    /// and each `ctnative.receiver` call joins the callee's `%arg0` to the
    /// receiver it passes. Every member maps to the whole group, itself
    /// included, so a singleton literal is a group of one and nothing else in
    /// the pipeline needs a special case for it.
    ///
    /// ONE FUNCTION, TWO CALLERS: TypeInference::initialize() indexes the
    /// field stores with it, and the EmitC lowering's shape census collects a
    /// class's fields with it. A second copy of this walk is a second chance
    /// for the two to disagree about what a field is.
    static llvm::DenseMap<mlir::Value, llvm::SmallVector<mlir::Value, 2>> groupReceivers(
        mlir::Operation * top);

    /// THE DENSE ARRAY (part 24 Phase 57A, the typing half). An array literal
    /// whose every use is an `append` onto it, an index read of it, or a read
    /// of its `length` is a `std::vector` and not a JavaScript array: nothing
    /// can make it sparse, nothing can rename an element, and it never leaves
    /// the frame. THE DEFAULT ARM IS THE PROOF - every other use, including
    /// the two the plan names by hand (`a[i] = v`, which `a[100] = 1` turns
    /// into a sparse array with `length` 101, and `delete a[0]`, which punches
    /// a hole in one) opens the site and every read of it is `boxed`.
    ///
    /// For such an array a read of an index is the join of every appended
    /// value, starting from undefined - because an index nothing appended,
    /// and every index past the end, reads `undefined` - and a read of
    /// `length` is a Number.
    static bool isDenseVectorSite(mlir::Value array);

    /// A value that names a shared binding the closure lift made a
    /// frame-local variable: the `ctjs.create_cell` it marked
    /// `ctnative.carried`, or an entry-block argument its
    /// `ctnative.cell_args` lists - the pointer a lifted call passed. Both
    /// read and write the same storage, so both answer the same type.
    static bool namesACarriedCell(mlir::Value v);

    /// The values that name ONE carried binding: the cell, and every capture
    /// parameter reached from it through a `ctnative.cell_args` operand of a
    /// `ctjs.call_direct`. Built the way groupReceivers is and for the same
    /// reason - a write through the pointer is a write the box has to report.
    static llvm::DenseMap<mlir::Value, llvm::SmallVector<mlir::Value, 4>> groupCells(
        mlir::Operation * top);

private:
    llvm::StringMap<llvm::SmallVector<mlir::Value, 4>> globalStores_;
    bool globalsAreDynamic_ = false;
    // (object value, key) -> the values ever stored under that key, for
    // closed-shape objects only; built in initialize().
    llvm::DenseMap<std::pair<mlir::Value, llvm::StringRef>, llvm::SmallVector<mlir::Value, 2>>
        fieldStores_;
    /// PHASE 59 SLICE 2 STEP 3, THE FIELD HALF: the same key, indexed by the
    /// STORE'S OWN object value and holding the `ctjs.set_property` OPERATIONS
    /// rather than the values they wrote.
    ///
    /// NOT OVER THE GROUP, AND THAT IS THE WHOLE SOUNDNESS OF IT. `fieldStores_`
    /// above is over the group because a store made through a lifted method's
    /// `%arg0` is a store the caller's read has to SEE - joining more values in
    /// is always safe. Ordering is the opposite: a store to one member of a
    /// group says nothing about when another member's field was written, and
    /// `{}` and `{n: 1}` reached through one parameter are one group. So this
    /// index is keyed on the exact SSA value, which is one object.
    llvm::DenseMap<std::pair<mlir::Value, llvm::StringRef>, llvm::SmallVector<mlir::Operation *, 2>>
        fieldStoreSites_;
    /// For the dominance question above. Built lazily per region by MLIR and
    /// valid for the whole solve, because nothing mutates the IR while the
    /// solver runs - the closure lift and every rewrite in
    /// `--ctnative-lower-to-emitc` happen before it is loaded.
    mlir::DominanceInfo dominance_{nullptr};
    // array value -> everything ever appended to it, in source order, for
    // dense vector sites only; built in initialize() beside fieldStores_.
    llvm::DenseMap<mlir::Value, llvm::SmallVector<mlir::Value, 4>> appends_;

    /// The element type of a dense array: the join over everything appended to
    /// it, from `undefined`. `op` is the operation asking, so every appended
    /// value's lattice subscribes it and a store that widens later re-visits
    /// the read.
    mlir::Type elementTypeOf(mlir::Operation * op, mlir::Value array);

    /// PHASE 59 SLICE 2 STEP 2: what a shared binding holds. The closure lift
    /// turns a `ctjs.create_cell` it can make a frame-local variable into
    /// `ctnative.carried` and hands its ADDRESS to each lifted call, so a
    /// write of that binding can be in a different `ctjs.func` from the box.
    /// This index is over the whole GROUP of values that name one box - the
    /// cell and every capture parameter a `ctnative.cell_args` operand reaches
    /// - which is the receiver grouping one operand along, and for the same
    /// reason: a store made through the pointer is a store the owning frame's
    /// read has to see. Keyed by every member, so either end asks once.
    llvm::DenseMap<mlir::Value, llvm::SmallVector<mlir::Value, 4>> cellStores_;

    /// PHASE 59 SLICE 2 STEP 3, THE FIELD HALF: does a store of `key` to THIS
    /// object come before this read on every path that reaches it?
    ///
    /// A closed-shape field read is the join over the stores of that key
    /// STARTING FROM `undefined`, because nothing orders a read after a store -
    /// so `var p = { n: 8 }; return p.n;` is `opt<num>` and a
    /// `ctjs.store_global` of it is refused for a `nan` it can never print.
    /// When a `ctjs.set_property` of the same key, on the SAME SSA value, in
    /// the SAME `ctjs.func`, properly dominates the read, every execution that
    /// reaches the read has already written the field on this object and the
    /// `undefined` seed is dropped.
    ///
    /// THREE CONDITIONS AND EACH ONE IS LOAD-BEARING. The same VALUE, because
    /// a store to another member of the alias group is a store to another
    /// object. The same FUNCTION, because `builtin.module`'s body is a graph
    /// region in which `properlyDominates` answers yes for every pair, so a
    /// store inside a lifted method would "dominate" a read in its caller.
    /// And DOMINANCE and not program order, so a store on one arm of an `if`
    /// or inside a loop body does not count - `read_before_write` in
    /// native-struct-fixture.js is the read that must keep its `undefined`.
    ///
    /// THE SEED IS ALL THIS DROPS. The join itself stays over the whole group:
    /// what the field may HOLD is still everything anyone ever stored in it.
    bool fieldIsAssignedBefore(mlir::Value object, llvm::StringRef key, mlir::Operation * read);

    /// The type a carried binding holds: the join over its initial and every
    /// value ever assigned to it, anywhere in the program. FROM THE INITIAL
    /// AND NOT FROM NOTHING - a read on a path that reaches no assignment
    /// yields what the box was built with, which for a hoisted `var` is
    /// `undefined`, and claiming otherwise is the one direction this lattice
    /// cannot undo.
    mlir::Type cellTypeOf(mlir::Operation * op, mlir::Value cell);

    /// ANYTHING THIS ANALYSIS DID NOT DERIVE IS `boxed`. Function parameters,
    /// values crossing a call boundary, and every operation the switch does not
    /// name land here. That is the top of the lattice and it is the safe
    /// answer - part 24's SUBSET RULE is prove-or-fall-back, and this is the
    /// fall-back.
    void setToEntryState(TypeLattice * lattice) override;
};

/// The type this analysis proves for one operation's result, ignoring its
/// operands - the whole transfer function for the ops whose result type is a
/// property of the OPERATION rather than of what flowed into it, which in this
/// dialect is nearly all of them.
///
/// Returns a null Type when the operation says nothing, which the caller reads
/// as `boxed`. Exposed for the unit test, which asserts the JavaScript
/// semantics in this table one operation at a time.
[[nodiscard]] mlir::Type staticResultType(mlir::Operation * op);

} // namespace ctcompile::ctnative

#endif
