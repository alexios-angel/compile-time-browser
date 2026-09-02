// PHASE 55A - CAN AN OBJECT BORN HERE OUTLIVE THE ACTIVATION THAT MADE IT?
//
// One question per allocation site, two answers. `confined`: no object
// allocated at this ctjs.create_object / ctjs.create_array is reachable from
// anything once the activation has returned or unwound, so Phase 56/57 may
// give it automatic storage (subject to O-2/O-3/O-4 in
// 25-escape-analysis.md §4). `escapes(reason)`: it can be, or this analysis
// could not prove otherwise - and on the native target, which has no
// collector and no VM, that means the site needs an OWNER. Phase 55B reads
// the reason to pick one: a site that escapes only by `returned` is the
// std::unique_ptr candidate, `stored`/`captured`/`passed` are shared, and
// `arguments`, `suspended`, `unknown_op` and the `unvisited*` gaps are outside
// the native subset and become a compile-time diagnostic naming the site and
// the reason. So the reason is load-bearing, not a roadmap, and the analysis
// keeps the FIRST one it finds.
//
// TWO LATTICES, ONE POST-PASS, and the split is the design's central fact:
//
//   (1) ALIAS, per SSA value - AliasValue below, a sparse forward analysis on
//       MLIR's DataFlowFramework in the shape of TypeInference.h. "This value
//       may denote an object allocated at any of `sites` in THIS function,
//       and/or an object not allocated at a tracked site."
//
//   (2) VERDICT, per site - NOT a lattice element. computeVerdicts runs AFTER
//       the fixpoint, walks every operation in every LIVE block, and for every
//       operand position whose role is SINK marks every site in that operand's
//       alias set. It is a post-pass for a reason worth stating twice: MLIR's
//       AbstractSparseForwardDataFlowAnalysis::visitOperation exits early on
//       an operation with no results ("Exit early on operations with no
//       results", SparseAnalysis.cpp), and ctjs.return, throw, store_global,
//       set_property, append, cell_set, store_upvalue, set_proto,
//       define_accessor, copy_props and every delete have no results. A sink
//       fired from visitOperation would silently never fire for `return {}`.
//       The unit test's return and throw rows are what turn that into a red
//       number if anyone moves it back.
//
// THE TABLE IS NOT HERE. Which operand positions sink, carry, or neither is
// TableGen's: Arg<..., [CTJS_Sink...]> decorators in CTJSOps.td, read through
// ctjs::EscapeEffectOpInterface (part 23 §1, ODS first). What is here is the
// default rule for an operation that has no annotation - every !ctjs.value
// operand sinks with reason `unknown_op` - and the one C++ switch the design
// allows: ctjs.unary, ctjs.compare and ctjs.convert, whose role depends on an
// attribute a decorator cannot see.
//
// SOUNDNESS IS ABSOLUTE AND PRECISION IS A BACKLOG, as in Phase 54A. "Result
// is external and every tracked operand sinks" is always sound; CARRY and
// NEITHER exist for precision and every NEITHER carries its VM proof in the
// .td. A NEITHER that is wrong is the only way this analysis is unsound.
#ifndef CTCOMPILE_CTNATIVE_ANALYSIS_ESCAPEANALYSIS_H
#define CTCOMPILE_CTNATIVE_ANALYSIS_ESCAPEANALYSIS_H

#include "ctcompile/CTJS/IR/CTJSOps.h"
#include "ctcompile/CTNative/IR/CTNativeEnums.h"

#include "mlir/Analysis/DataFlow/SparseAnalysis.h"
#include "mlir/Analysis/DataFlowFramework.h"
#include "mlir/IR/Operation.h"
#include "mlir/IR/Value.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/MapVector.h"
#include "llvm/ADT/SmallVector.h"

#include <optional>

namespace ctcompile::ctnative {

// What one SSA value may denote, as a lattice element.
//
// UNINITIALIZED IS THE IDENTITY, exactly as TypeValue's null Type is: the
// framework constructs every element before it visits anything that could
// define it, and the first real answer must replace that state rather than
// join with it. `join` is set union on the sites and OR on `external`; the
// height is sites-in-function + 1, so termination is by construction.
//
// A SITE IS A ctjs.create_object OR A ctjs.create_array AND NOTHING ELSE. The
// NEITHER rows on the property operations are proved for those two kinds only
// - a closure base breaks them (objects.cpp:590 -> call.cpp:502, the
// `constructor` back-edge) - so the constructor asserts it, onlyTrackedSites
// re-checks it, and the unit test has rows for every other allocating
// operation showing its result enters no alias set.
class AliasValue {
public:
    AliasValue() = default;

    /// An initialized value that denotes no object: an i1, an i32, a
    /// !ctjs.context, or a ctjs.constant (primitives; strings are untracked).
    static AliasValue none();
    /// An object not allocated at any tracked site of this function.
    static AliasValue external();
    /// The object allocated at `site`, which must be a tracked site.
    static AliasValue site(mlir::Operation * site);

    static AliasValue join(const AliasValue & lhs, const AliasValue & rhs);

    [[nodiscard]] bool isUninitialized() const { return !initialized_; }
    [[nodiscard]] bool isExternal() const { return external_; }
    [[nodiscard]] llvm::ArrayRef<mlir::Operation *> getSites() const { return sites_; }
    /// True when every site is a ctjs.create_object or ctjs.create_array.
    [[nodiscard]] bool onlyTrackedSites() const;

    bool operator==(const AliasValue & other) const;

    void print(llvm::raw_ostream & os) const;

private:
    bool initialized_ = false;
    bool external_ = false;
    llvm::SmallVector<mlir::Operation *, 4> sites_; // sorted by address, unique
};

using AliasLattice = mlir::dataflow::Lattice<AliasValue>;

// The alias half. Load it into a solver alongside BOTH DeadCodeAnalysis AND
// SparseConstantPropagation, for TypeInference.h's two reasons: without the
// first there is no predecessor information and no block argument ever
// receives anything; without the second DeadCodeAnalysis cannot decide which
// successor of a branch is live and marks none.
class EscapeAnalysis : public mlir::dataflow::SparseForwardDataFlowAnalysis<AliasLattice> {
public:
    using SparseForwardDataFlowAnalysis::SparseForwardDataFlowAnalysis;

    /// SITE for the two tracked operations; CARRY (union of the carried
    /// operands, plus external) for an operation whose interface says so;
    /// none for a non-value result or a constant; external for everything
    /// else. NO SINK IS APPLIED HERE - see the file comment.
    mlir::LogicalResult visitOperation(mlir::Operation * op,
                                       llvm::ArrayRef<const AliasLattice *> operands,
                                       llvm::ArrayRef<AliasLattice *> results) override;

    /// Entry-block arguments - receiver, new.target, callee, then the
    /// parameters - denote objects made elsewhere: external.
    void setToEntryState(AliasLattice * lattice) override;
};

// --- the roles, as the post-pass reads them ----------------------------------

enum class OperandRole {
    Sink,
    Carry,
    Neither
};

struct RoleOf {
    OperandRole role = OperandRole::Neither;
    EscapeReason reason = EscapeReason::Confined; // meaningful for Sink only
};

/// The role of operand `index` of `op`: the kind switch for unary/compare/
/// convert; the ODS interface for an operation that has it; CARRY for a
/// BranchOpInterface's value operands (the framework moves them); and the
/// DEFAULT RULE - SINK(unknown_op) for every !ctjs.value operand - for an
/// operation with none of those. Exposed so the unit test can assert every
/// row's operand through the same path the verdict takes.
[[nodiscard]] RoleOf operandRole(mlir::Operation * op, unsigned index);

/// A ctjs.create_object or ctjs.create_array.
[[nodiscard]] bool isTrackedSite(mlir::Operation * op);

/// An allocating operation the MVP claims `escapes(reason)` rather than
/// tracks - the Res<..., [CTJS_Boxed...]> decorators in CTJSOps.td.
[[nodiscard]] bool isBoxedSite(mlir::Operation * op, EscapeReason & reason);

/// The bytecode pc the importer stamped on `op` - the `<at>` of the NameLoc
/// "program:<id>:<fn>:<at>" inside its FusedLoc - or nothing when the
/// location does not parse (a claim for such an op is `unvisited`, counted).
[[nodiscard]] std::optional<unsigned> allocationPc(mlir::Operation * op);

// --- the verdicts --------------------------------------------------------------

struct Verdict {
    EscapeReason reason = EscapeReason::Confined;
    mlir::Operation * by = nullptr; // the sinking operation, when escaping
    unsigned position = 0;          // its operand index
};

struct EscapeVerdicts {
    /// Every tracked site in a LIVE block, in program order (a MapVector so
    /// the claims file is deterministic). Sites in dead blocks are dropped and
    /// counted - never executed, never observed, so dropping is exact.
    llvm::MapVector<mlir::Operation *, Verdict> sites;
    unsigned deadSites = 0;
    /// A site in a live block whose result lattice the solver never
    /// initialized: `escapes(unvisited)`, counted, gated at zero.
    unsigned unvisitedSites = 0;
    /// A SINK operand in a live block with no lattice: EVERY site of the
    /// function becomes `escapes(unvisited_operand)`, counted, gated at zero.
    unsigned unvisitedOperands = 0;
    unsigned blocks = 0;
    unsigned liveBlocks = 0;
    /// R1: the function built an `arguments` object or a rest parameter, so
    /// every parameter has a heap alias and a future callee summary must treat
    /// it as capturing every argument.
    bool capturesAllArguments = false;
    /// R4 `suspended` or R1's guard `arguments_late`: every site escapes with
    /// this reason.
    std::optional<EscapeReason> wholeFunction;
};

/// The post-pass. Takes the solver by non-const reference only because
/// DataFlowSolver::getProgramPointBefore(Block *) interns its anchor and is
/// not const; nothing here changes a lattice.
[[nodiscard]] EscapeVerdicts computeVerdicts(mlir::DataFlowSolver & solver, ctjs::FuncOp function);

} // namespace ctcompile::ctnative

#endif
