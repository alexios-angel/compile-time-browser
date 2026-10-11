//===- TypeInference.h - Static types for ctjs values -----------*- C++ -*-===//
//
// Static type inference for ctjs values, checked against a running
// interpreter.
//
// Escape analysis, structification (objects lowered to C++ structs) and the
// native backend all consume these types; the native backend refuses any
// function whose values it cannot type. Every claim made here is checked
// against the types the interpreter actually recorded, by
// tools/check/type-oracle.py and ctcompile/test/Analysis/Types/Oracle/.
//
// The analysis is a sparse forward dataflow analysis over SSA values on MLIR's
// DataFlowFramework, which supplies the fixpoint, the worklist, block-argument
// joins and dead-code handling. This file adds the transfer function (one
// switch over ctjs operations stating what JavaScript guarantees about each
// result) and uses the lattice from CTNativeLattice.h.
//
// Two names for one operation: MLIR's framework calls the binary lattice
// operation `join`; ctnative calls it `meet`. They are the same function here.
// CTNativeLattice.h explains the naming: ctnative orders the lattice bottom to
// top as bottom -> concrete -> boxed, so "the smallest type correct for both"
// is a join in that order, and is called `meet` because it is the greatest
// lower bound when ordered by precision. Nothing here may add a second rule;
// TypeValue::join just calls ctnative::meet.
//
// Soundness is required; precision is best-effort. Answering `num<i32>` for a
// value the interpreter observed as 1.5 is a bug, and the type oracle reports
// it. Answering `boxed` for a value that was always a string is only
// imprecise. When the two conflict, answer `boxed`.
//
//===----------------------------------------------------------------------===//
#ifndef CTCOMPILE_CTNATIVE_ANALYSIS_TYPEINFERENCE_H
#define CTCOMPILE_CTNATIVE_ANALYSIS_TYPEINFERENCE_H

#include "ctcompile/CTNative/IR/CTNativeLattice.h"

#include "mlir/Analysis/DataFlow/SparseAnalysis.h"
#include "mlir/Analysis/DataFlowFramework.h"
#include "mlir/IR/Dominance.h"
#include "mlir/IR/Types.h"
#include "mlir/IR/Value.h"

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringRef.h"

#include <array>
#include <string>

namespace ctcompile::ctnative {

class OwnedGlobalRoots;
class DOMEntryAnalysis;

/// Attribute meaning a carried binding's hoisted `undefined` can never be read.
///
/// The closure lift in `--ctnative-lower-to-emitc` sets it on a
/// `ctjs.create_cell` (already marked `ctnative.carried`) when one
/// `ctjs.cell_set` of that cell properly dominates every read of the binding:
/// every `ctjs.cell_get` in the owning frame and every call of every closure
/// that captured it. `cellTypeOf` then joins only the writes, dropping the
/// cell's initial value, so `var n = 0; function tick() { n = n + 1; }` types
/// as `num` rather than `opt<num>`.
///
/// The lift proves this, not this analysis, on purpose: it is a dominance
/// question over the `ctjs.call`s of the capturing closures, and `lift()`
/// erases those calls before the solver runs, so it can only be asked where
/// the lift already decides which calls to rewrite. The name is spelled once,
/// here, because the writer and the reader are in different translation units
/// and a mistyped attribute name is a silent narrowing nothing would catch.
inline constexpr llvm::StringLiteral kAssignedBeforeRead = "ctnative.assigned_before_read";

/// Attribute on a function whose every return is an object literal it built,
/// and on each direct call of it, when markRecordReturns proved that the call's
/// result can receive a copy (Analysis/Inference/Records.cpp).
inline constexpr llvm::StringLiteral kRecordReturn = "ctnative.record_return";

/// One SSA value's inferred type, as a lattice element.
///
/// A null type means "nothing known yet" and is not bottom. The framework
/// constructs every lattice element before it visits anything that could
/// define it, and that initial state has to be the identity so the first real
/// answer replaces it rather than joining with it. ctnative::meet already
/// treats a null Type as absent (`meet(Type(), t)` is `t`), so this class gets
/// the identity for free.
class TypeValue {
public:
    TypeValue() = default;
    explicit TypeValue(mlir::Type type) : type_{type} {}

    [[nodiscard]] mlir::Type getType() const { return type_; }
    [[nodiscard]] bool isUninitialized() const { return type_ == nullptr; }

    /// The framework's join, which is ctnative::meet. See the file comment: the
    /// two names describe the same operation and this is the only place they
    /// meet.
    static TypeValue join(const TypeValue & lhs, const TypeValue & rhs) {
        return TypeValue{meet(lhs.type_, rhs.type_)};
    }

    bool operator==(const TypeValue & other) const { return type_ == other.type_; }

    void print(llvm::raw_ostream & os) const;

private:
    mlir::Type type_{};
};

using TypeLattice = mlir::dataflow::Lattice<TypeValue>;

/// The analysis itself. Load it into a solver together with both
/// DeadCodeAnalysis and SparseConstantPropagation; neither is optional:
///
///   * without DeadCodeAnalysis the framework has no predecessor information,
///     so block arguments never receive the operands branched into them;
///   * without SparseConstantPropagation, DeadCodeAnalysis cannot decide which
///     successor of a branch is live: it asks for each branch operand's
///     ConstantValue lattice, finds it uninitialized, and marks no successor
///     live, so no op past the first branch is ever visited. The unit test has
///     a multi-block case so this cannot regress quietly.
class TypeInference : public mlir::dataflow::SparseForwardDataFlowAnalysis<TypeLattice> {
public:
    explicit TypeInference(mlir::DataFlowSolver & solver,
                           const OwnedGlobalRoots * ownedRoots = nullptr,
                           const DOMEntryAnalysis * domEntry = nullptr)
        : SparseForwardDataFlowAnalysis(solver), ownedRoots_(ownedRoots), domEntry_(domEntry) {}

    mlir::LogicalResult visitOperation(mlir::Operation * op,
                                       llvm::ArrayRef<const TypeLattice *> operands,
                                       llvm::ArrayRef<TypeLattice *> results) override;

    /// The payload on an invoke's unwind edge is produced by the call, not
    /// forwarded from its (unavailable) normal result, so it is inferred from
    /// the throw operands of the direct callee in a bounded query. Other
    /// non-forwarded region arguments keep the framework's conservative entry
    /// state. A type here does not mean the native backend accepts the code.
    void visitNonControlFlowArguments(
        mlir::Operation * op, const mlir::RegionSuccessor & successor,
        mlir::ValueRange nonSuccessorInputs,
        llvm::ArrayRef<TypeLattice *> nonSuccessorInputLattices) override;

    /// Globals are typed under a closed-world assumption. A whole-program
    /// compile sees every `ctjs.store_global` of a name, so a
    /// `ctjs.load_global` of that name is the join of everything ever stored
    /// under it, and the framework revisits the load when any of those stores'
    /// operands change, because the load subscribes to them. Three cases keep
    /// it sound:
    ///   * no store anywhere: the name is a builtin (`Math`) or undeclared,
    ///     and the load is `boxed`;
    ///   * any dynamic write to the globals table in the program (a property
    ///     store through `globalThis`/`window`): every load is `boxed`;
    ///   * otherwise: the join over the stores' operand types and an implicit
    ///     Undefined. When the owned-globals proof shows this scalar load has
    ///     exactly one earlier store, the implicit Undefined is dropped; the
    ///     store's operand still supplies its type, even if pending or boxed.
    /// The index is built once, in initialize(), from the module being solved.
    mlir::LogicalResult initialize(mlir::Operation * top) override;

    /// Whether `object` is a literal with a closed shape. An object literal
    /// whose every use is a `get_property` or `set_property` with a constant
    /// string key has a shape nothing else can change: no dynamic key, no
    /// delete, no enumeration, no escape, so it never reaches anything that
    /// could add or remove a field. For such an object a read of key `k` is
    /// the join of every store of `k` to it, starting from undefined because
    /// nothing orders the read after a store. Any other use of the object (a
    /// call, a return, a dynamic key, `delete`, `in`, spread) makes its shape
    /// open and every read of it `boxed`. A data record (markRecordReturns) is
    /// the exception: its literal may be returned, and the call's result is a
    /// closed object of the same shape.
    static bool hasClosedShape(mlir::Value object);

    /// Whether `v` is the lifted `this` of a method. A method whose `this` uses
    /// are all constant-key property accesses is lifted:
    /// `--ctnative-lower-to-emitc` marks the target and each of its calls
    /// `ctnative.receiver`, and `%arg0` (the entry block's receiver) then
    /// names the object literal the call passes. So a receiver argument has a
    /// closed shape by the lift's own proof, and `this.x` is the same field
    /// read as `o.x` in the caller.
    ///
    /// True for `%arg0` of a `ctnative.receiver` function, checked with the
    /// same use loop as a literal: the argument is closed only if every use of
    /// it is a constant-key get or set, or the receiver of another lifted
    /// method call.
    static bool isReceiverArgument(mlir::Value v);

    /// Whether `v` is a lifted object parameter. An ordinary argument is
    /// lifted the same way as a receiver: for `f(o)` where every call passes a
    /// closed literal and the parameter is only read through constant keys,
    /// the lowering marks the target and each of its calls
    /// `ctnative.object_args` with the entry-block indices that carry a
    /// `ctn_x *`.
    ///
    /// True for any entry argument the lift listed, and for `%arg0` via
    /// isReceiverArgument, so `hasClosedShape` and `groupReceivers` ask one
    /// question rather than two.
    static bool namesAnObjectParameter(mlir::Value v);

    /// Groups the values that name one object once `this` is a parameter. A
    /// method lifted onto two literals of one shape has one `%arg0` standing
    /// for both, and `this.x = 5` inside it is a store the caller's `o.x` has
    /// to see, so the field index, the shape census and the emitted class all
    /// work over the group rather than one value. Built by one walk of `top`:
    /// every closed literal and every receiver argument is a node, and each
    /// `ctnative.receiver` call joins the callee's `%arg0` to the receiver it
    /// passes. Every member maps to the whole group, itself included, so a
    /// singleton literal is a group of one and needs no special case.
    ///
    /// Both TypeInference::initialize() (to index field stores) and the EmitC
    /// lowering's shape census (to collect a class's fields) call this one
    /// function, so they cannot disagree about what a field is.
    static llvm::DenseMap<mlir::Value, llvm::SmallVector<mlir::Value, 2>> groupReceivers(
        mlir::Operation * top);

    /// Marks data records returned by value with kRecordReturn: a private
    /// function, called only directly, whose every `ctjs.return` returns an
    /// object literal it made, used otherwise only through constant keys, and
    /// whose every call result is used only through constant keys or lent to
    /// a record parameter. A record parameter belongs to a private function
    /// called only directly, every call passes it a record, and it is used the
    /// same way; its index joins `ctnative.object_args`. Run once, after
    /// closure lifting and before groupReceivers.
    static void markRecordReturns(mlir::Operation * top);
    /// Whether `value` is the result of a call kRecordReturn marks.
    static bool isRecordCall(mlir::Value value);
    /// Whether `ret` is a `ctjs.return` of a function kRecordReturn marks.
    static bool returnsRecord(mlir::Operation * ret);

    /// Whether `array` is a dense array literal that can be a `std::vector`:
    /// its uses keep it dense (no holes) and local. Allowed uses are appends,
    /// index and length reads, overwrites of elements proved to exist, and
    /// literal length stores that do not grow it. `scf` selections and checked
    /// counted loops may borrow an entry-block literal once all its uses are
    /// checked. Any other transport, a sparse write, deletion or escape
    /// disqualifies the site.
    ///
    /// For such an array a read of an index is the join of every appended or
    /// stored value, starting from undefined (an index nothing appended, and
    /// every index past the end, reads `undefined`), and a read of `length` is
    /// a Number.
    static bool isDenseVectorSite(mlir::Value array);

    /// The local ownership group of `array`: the owning literals and the
    /// values that borrow them through `scf` control flow. Empty when any
    /// origin or use is unknown or the contents proof is incomplete. Always
    /// recomputed from the current IR; no annotation grants ownership.
    static llvm::SmallVector<mlir::Value, 4> denseVectorAliases(mlir::Value array);

    /// A call the native runtime implements on a String or on a vector of one
    /// proved element type (Analysis/Inference/Vectors.cpp):
    /// `a.push(x)`, `a.join()` or `a.join(s)`, `a.sort()`, `s.split(t)` with
    /// any one argument, `s.split(/\s+/)`, and the RegExp literal factory
    /// call that builds a split's separator.
    enum class VectorCall {
        none,
        push,
        join,
        sort,
        split,
        splitSpace,
        regexp,
        /// `a.includes(x)` on an array of Strings.
        includes,
        /// A static method of a built-in namespace object, such as
        /// `Number.parseInt(s, radix)` (builtinStaticOf).
        builtinStatic,
        /// A callback method closure lifting specialised
        /// (Inference/ArrayCallbacks.cpp).
        callback
    };
    static VectorCall vectorCallOf(mlir::Operation * op);

    /// One static method of a built-in namespace object that native code
    /// calls as `<callee>(args...)`. Every row returns a Number.
    struct BuiltinStatic {
        /// What an argument must be for the call to lower.
        enum class Param {
            string,
            number
        };
        llvm::StringLiteral global; ///< `Number`, `Math`.
        llvm::StringLiteral member; ///< `parseInt`, `min`.
        llvm::StringLiteral callee; ///< `ctnative::Number.parseInt`.
        std::array<Param, 2> params;
        unsigned arity;    ///< How many of `params` the method takes.
        unsigned required; ///< How many of them a call must pass.
    };
    /// The row of the built-in statics table (Inference/BuiltinStatics.cpp)
    /// that `op` calls, or null. The table is the one place built-in statics
    /// are recognised; a new one is a row there plus its runtime member.
    static const BuiltinStatic * builtinStaticOf(mlir::Operation * op);

    // Array-method callbacks (Inference/ArrayCallbacks.cpp).
    /// Whether `key` names an Array method the runtime calls back from.
    static bool isArrayCallbackMethod(llvm::StringRef key);
    /// Whether `op` is a call closure lifting specialised for its callback.
    static bool isArrayCallbackCall(mlir::Operation * op);
    /// Whether that call answers a new array (filter and map).
    static bool arrayCallbackMakesArray(mlir::Operation * op);
    /// Whether operand `index` of that call only lends an array to it: the
    /// receiver, or a captured value the callback reads.
    static bool arrayCallbackBorrows(mlir::Operation * op, unsigned index);
    /// Whether `array` is a vector of Numbers the element-vector proof accepts
    /// and no array literal owns (a map result, or an array a call returns
    /// or lends): `std::vector<ctnative::js_num>`. Number literals keep the
    /// dense proof (Shapes.cpp) and its `std::vector<double>`.
    static bool isNumberElementVector(mlir::Value array);
    /// Whether `value` is a parameter of a specialised callback, which its
    /// call types rather than the entry state.
    static bool isArrayCallbackParameter(mlir::Value value);

    /// The values that name one array lowered to `std::vector<T>` for a
    /// proved element type T: its owners (array literals and split results),
    /// `a.sort()` results, and the values that borrow it through `scf`
    /// control flow. Allowed uses are appends while it is built, index and
    /// `length` reads, and push, join and sort. An owner carried through
    /// `scf` must be in the entry block; one used only in its own region may
    /// be anywhere. Empty when any origin or use is outside that, with the
    /// reason in `why`. Always recomputed from the current IR.
    static llvm::SmallVector<mlir::Value, 4> elementVectorAliases(mlir::Value array,
                                                                  std::string * why = nullptr);
    /// Everything stored into the array `members` names: literal elements,
    /// appends and pushed values. A split result's Strings are not listed;
    /// splitsAString says whether there is one.
    static llvm::SmallVector<mlir::Value, 4> elementVectorStores(
        llvm::ArrayRef<mlir::Value> members);
    /// Whether `op` reads the global `undefined` in a program that never
    /// stores to it. `undefined` is a non-writable, non-configurable property
    /// of the global object (ECMA-262 19.1.4), so such a read is the undefined
    /// value, whatever else writes the globals table.
    static bool isUndefinedGlobal(mlir::Operation * op);
    /// Whether `op` reads `x.getBoundingClientRect` only to test it.
    static bool isRectTest(mlir::Operation * op);
    /// Whether `op` is `x.getBoundingClientRect()`, its callee read for that
    /// call alone.
    static bool isRectCall(mlir::Operation * op);
    /// When `op` is the null of `x.getBoundingClientRect ?
    /// x.getBoundingClientRect() : null`, `x`; null otherwise. For an element
    /// `x` the test is always true and the null never produced.
    static mlir::Value deadRectReceiver(mlir::Operation * op);
    /// Whether `op` calls the realm's own `getComputedStyle` with one argument.
    static bool isStyleCall(mlir::Operation * op);
    /// Whether `op` is `x.getPropertyValue(name)`, its callee read for that
    /// call alone.
    static bool isStylePropertyCall(mlir::Operation * op);
    /// Whether `op` loads the global `name` in a program that never stores
    /// to it, so the load is the realm's own binding of that name.
    static bool isRealmGlobal(mlir::Operation * op, llvm::StringRef name);

    /// Whether the array `members` names is a function's parameter: a
    /// read-only borrow of the caller's array, lowered as std::span.
    static bool elementVectorParameter(llvm::ArrayRef<mlir::Value> members);
    /// Whether one owner in `members` is a split result.
    static bool splitsAString(llvm::ArrayRef<mlir::Value> members);

    /// Whether `array` is a tuple literal: a two-element array literal whose
    /// elements are its inline elements and appends, all in its own block,
    /// and whose only other use is as the one argument of a push onto an
    /// element vector, after those appends. Its positions are tupleParts.
    static bool isTupleLiteral(mlir::Value array);
    /// The values a tuple literal holds, in position order.
    static llvm::SmallVector<mlir::Value, 2> tupleParts(mlir::Value array);
    /// The position a constant index reads (`t[1]` is 1), or -1 when the key
    /// is not a constant integer.
    static int64_t constantIndex(mlir::Value key);
    /// Whether the index read `read` (a get_property of an element vector)
    /// is proved in bounds: its key is the counter of an scf.while whose
    /// condition is `counter < sameArray.length`, which starts at a
    /// non-negative integer constant and steps by 1. Element vectors never
    /// shrink, so the element exists when the body reads it.
    static bool isBoundedIndexRead(mlir::Operation * read);

    /// A value that names a shared binding the closure lift made a
    /// frame-local variable: the `ctjs.create_cell` it marked
    /// `ctnative.carried`, or an entry-block argument its
    /// `ctnative.cell_args` lists - the pointer a lifted call passed. Both
    /// read and write the same storage, so both answer the same type.
    static bool namesACarriedCell(mlir::Value v);

    /// The values that name one carried binding: the cell, and every capture
    /// parameter reached from it through a `ctnative.cell_args` operand of a
    /// `ctjs.call_direct`. Built like groupReceivers and for the same reason:
    /// a write through the pointer is a write the cell has to report.
    static llvm::DenseMap<mlir::Value, llvm::SmallVector<mlir::Value, 4>> groupCells(
        mlir::Operation * top);
    /// The native type a Map key/read/write tag (mapScalarTag or
    /// "nullable_string") names, or null.
    static mlir::Type mapTagType(mlir::MLIRContext * c, llvm::StringRef tag);

private:
    /// For the one direct call wrapped in an invoke, the result on the normal
    /// path is the join of the callee's live return operands, found by a
    /// bounded query that does not treat throw exits as returns. Every other
    /// call, including calls in either continuation, keeps upstream's
    /// conservative call inference. A type here does not mean the native
    /// backend accepts exceptions.
    mlir::LogicalResult visitCallOperation(
        mlir::CallOpInterface call,
        llvm::ArrayRef<const mlir::dataflow::AbstractSparseLattice *> operands,
        llvm::ArrayRef<mlir::dataflow::AbstractSparseLattice *> results) override;

    // Proofs supplied by the caller, never rebuilt from diagnostic annotations.
    // The module must not change while the solver runs.
    const OwnedGlobalRoots * ownedRoots_;
    const DOMEntryAnalysis * domEntry_;
    llvm::StringMap<llvm::SmallVector<mlir::Value, 4>> globalStores_;
    bool globalsAreDynamic_ = false;
    llvm::StringMap<llvm::SmallVector<mlir::Value, 4>> environmentCaptures_;
    // (object value, key) -> the values ever stored under that key, for
    // closed-shape objects only; built in initialize().
    llvm::DenseMap<std::pair<mlir::Value, llvm::StringRef>, llvm::SmallVector<mlir::Value, 2>>
        fieldStores_;
    // (field group id, key) -> values stored. Owned object families share a
    // scalar field schema, not run-time identity.
    llvm::DenseMap<std::pair<int64_t, llvm::StringRef>, llvm::SmallVector<mlir::Value, 2>>
        identityFieldStores_;
    // Reads whose identity field is proved assigned before the read
    // (queryNativeObjectFieldPresence), computed in initialize() independently
    // of schema groups and host reports.
    llvm::DenseSet<mlir::Operation *> assignedIdentityFields_;
    /// Field stores indexed by the exact object value written to (not its alias
    /// group), holding the `ctjs.set_property` operations rather than the
    /// values they wrote. Used to decide whether a store dominates a read.
    ///
    /// This must not be over the group; soundness depends on it. `fieldStores_`
    /// is over the group because a store made through a lifted method's
    /// `%arg0` is one the caller's read has to see, and joining more values is
    /// always safe. Ordering is the opposite: a store to one member of a group
    /// says nothing about when another member's field was written, and `{}`
    /// and `{n: 1}` reached through one parameter are one group. So this index
    /// is keyed on the exact SSA value, which is one object.
    llvm::DenseMap<std::pair<mlir::Value, llvm::StringRef>, llvm::SmallVector<mlir::Operation *, 2>>
        fieldStoreSites_;
    // Every direct call site of each closed private object parameter. Every
    // member's uses keep its shape, and callable values cannot escape. A
    // method store left after lifting counts only when no member of its closed
    // alias group still reads the callable's key. Field presence is still
    // checked against each actual operand's stores in its own caller.
    llvm::DenseMap<mlir::Value, llvm::SmallVector<mlir::Operation *, 2>> objectParameterCallSites_;
    /// For the dominance question above. Built lazily per region by MLIR and
    /// valid for the whole solve, because nothing mutates the IR while the
    /// solver runs: the closure lift and every rewrite in
    /// `--ctnative-lower-to-emitc` happen before it is loaded.
    mlir::DominanceInfo dominance_{nullptr};
    // array value -> everything appended or stored into it, for
    // dense vector sites only; built in initialize() beside fieldStores_.
    llvm::DenseMap<mlir::Value, llvm::SmallVector<mlir::Value, 4>> appends_;

    // Direct array reads that hit an existing own element on every path, and
    // the values they can return, from a bounded contents proof in this solve.
    llvm::DenseMap<mlir::Operation *, llvm::SmallVector<mlir::Value, 2>> arrayReadValues_;

    // Per proved Map schema family (nativeMapGroup): every key queried or
    // stored and every value stored, including through `set` aliases and
    // closed parameters, captures and returns. An exact key type proved at one
    // Map operation applies only there; everywhere else, including other
    // calls, the value keeps its broader type.
    llvm::DenseMap<int64_t, llvm::SmallVector<std::pair<mlir::Value, mlir::Type>, 4>> mapKeys_;
    llvm::DenseMap<int64_t, llvm::SmallVector<mlir::Value, 4>> mapValues_;
    mlir::Type mapTypeOf(mlir::Operation * op, mlir::Value map);

    /// The type of `op` when it builds, reads or calls a method of an element
    /// vector (elementVectorAliases), or calls split on a String; null when it
    /// does none of these. Sets `pending` when the answer has to wait for a
    /// stored value's lattice.
    /// The type of a page read of an ordinary script: `document.documentElement`
    /// (an element), an element's tagName, id and className (Strings),
    /// children (a vector of elements), clientWidth and clientHeight, and
    /// `window.devicePixelRatio` (Numbers); getBoundingClientRect (a rect, its
    /// existence test a Boolean, the null of its guard the rect too), the
    /// rect's left, top, width and height (Numbers), getComputedStyle(el) and
    /// its getPropertyValue (a String). Null for anything else.
    mlir::Type domReadType(mlir::Operation * op, llvm::ArrayRef<const TypeLattice *> operands);
    mlir::Type elementVectorType(mlir::Operation * op, llvm::ArrayRef<const TypeLattice *> operands,
                                 bool & pending);
    /// The type of a specialised Array-method call, after joining its
    /// receiver's element type, the index and its captured values into the
    /// callback's parameters.
    mlir::Type arrayCallbackType(mlir::Operation * op, llvm::ArrayRef<const TypeLattice *> operands,
                                 bool & pending);

    /// The element type of a dense array: the join over everything appended or
    /// stored into it, from `undefined`. `op` is the operation asking, so every
    /// value's lattice subscribes it and a store that widens later re-visits
    /// the read.
    mlir::Type elementTypeOf(mlir::Operation * op, mlir::Value array);
    /// `seed` met with the type of every value after `op`, subscribing `op` to
    /// each. A value not visited yet contributes nothing now and revisits `op`.
    mlir::Type joinLattices(mlir::Operation * op, mlir::ValueRange values, mlir::Type seed);

    /// What a shared (carried) binding holds. The closure lift marks a
    /// `ctjs.create_cell` it can make a frame-local variable
    /// `ctnative.carried` and passes its address to each lifted call, so a
    /// write of that binding can be in a different `ctjs.func` from the cell.
    /// This index is over the whole group of values that name one cell (see
    /// groupCells), for the same reason as groupReceivers: a store made
    /// through the pointer is one the owning frame's read has to see. Keyed by
    /// every member, so either end asks once.
    llvm::DenseMap<mlir::Value, llvm::SmallVector<mlir::Value, 4>> cellStores_;

    /// Whether a store of `key` to this exact object precedes `read` on every
    /// path that reaches it.
    ///
    /// A closed-shape field read is the join over the stores of that key
    /// starting from `undefined`, because nothing orders a read after a store,
    /// so `var p = { n: 8 }; return p.n;` is `opt<num>` and a
    /// `ctjs.store_global` of it is refused for a `nan` it can never print.
    /// When a `ctjs.set_property` of the same key, on the same SSA value, in
    /// the same `ctjs.func`, properly dominates the read, every execution that
    /// reaches the read has already written the field on this object and the
    /// `undefined` seed is dropped.
    ///
    /// Each of the three conditions is needed. The same value, because a store
    /// to another member of the alias group is a store to another object. The
    /// same function, because `builtin.module`'s body is a graph region in
    /// which `properlyDominates` answers yes for every pair, so a store inside
    /// a lifted method would "dominate" a read in its caller. And dominance,
    /// not program order, so a store on one arm of an `if` or inside a loop
    /// body does not count: `read_before_write` in
    /// ctcompile/test/CTNative/Fixtures/Objects/struct.js is the read that must
    /// keep its `undefined`.
    ///
    /// Only the `undefined` seed is dropped. The join itself stays over the
    /// whole group: what the field may hold is still everything anyone ever
    /// stored in it. Owned identity fields use the separate allocation and
    /// Map-alias query cached in initialize(); the schema group gives no
    /// ordering evidence. A closed private object parameter also drops the seed
    /// when every actual call traces back to a dominating store in the same
    /// function. Forwarded slots need their own check of every callable and
    /// use; cycles and the bounded walk's cutoff keep the seed. Field removal
    /// is ruled out by the closed-alias check, and every callee write stays in
    /// the type join. A data record's call result drops the seed when, in the
    /// callee, a store of the key dominates every return of its literal.
    bool fieldIsAssignedBefore(mlir::Value object, llvm::StringRef key, mlir::Operation * read);

    /// The type a carried binding holds: the join over its initial and every
    /// value ever assigned to it, anywhere in the program. It starts from the
    /// initial value, not from nothing: a read on a path that reaches no
    /// assignment yields what the cell was built with, which for a hoisted
    /// `var` is `undefined`, and claiming otherwise is the one direction this
    /// lattice cannot undo (unless kAssignedBeforeRead is set).
    mlir::Type cellTypeOf(mlir::Operation * op, mlir::Value cell);

    /// Anything this analysis did not derive is `boxed`: function parameters,
    /// values crossing a call boundary, and every operation the transfer
    /// function does not handle. `boxed` is the top of the lattice and always
    /// safe; the native compiler either proves a type or falls back to it.
    // Borrowed callbacks (Inference/BorrowedCallbacks.cpp).
    /// Whether `value` is a parameter of a borrowed callback, which the
    /// closure and the calls through its callee's parameter type rather than
    /// the entry state.
    static bool isBorrowedCallbackParameter(mlir::Value value);
    /// The transfer function of a borrowed callback's closure and of a call
    /// through a borrowed parameter; nullopt for any other operation.
    std::optional<mlir::LogicalResult> visitBorrowedCallback(
        mlir::Operation * op, llvm::ArrayRef<const TypeLattice *> operands,
        llvm::ArrayRef<TypeLattice *> results);

    void setToEntryState(TypeLattice * lattice) override;
};

/// The type this analysis proves for one operation's result, ignoring its
/// operands: the whole transfer function for the ops whose result type is a
/// property of the operation rather than of what flowed into it, which in this
/// dialect is nearly all of them.
///
/// Returns a null Type when the operation says nothing, which the caller reads
/// as `boxed`. Exposed for the unit test, which asserts the JavaScript
/// semantics in this table one operation at a time.
[[nodiscard]] mlir::Type staticResultType(mlir::Operation * op);

} // namespace ctcompile::ctnative

#endif
