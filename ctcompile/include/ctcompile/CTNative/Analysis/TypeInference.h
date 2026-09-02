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
#include "mlir/IR/Types.h"
#include "mlir/IR/Value.h"

namespace ctcompile::ctnative {

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

// The analysis itself. Load it into a solver alongside DeadCodeAnalysis - which
// is not optional: without it the framework has no predecessor information, so
// block arguments never receive the operands branched into them and every
// register in a loop stays uninitialized.
class TypeInference : public mlir::dataflow::SparseForwardDataFlowAnalysis<TypeLattice> {
public:
    using SparseForwardDataFlowAnalysis::SparseForwardDataFlowAnalysis;

    mlir::LogicalResult visitOperation(mlir::Operation * op,
                                       llvm::ArrayRef<const TypeLattice *> operands,
                                       llvm::ArrayRef<TypeLattice *> results) override;

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
