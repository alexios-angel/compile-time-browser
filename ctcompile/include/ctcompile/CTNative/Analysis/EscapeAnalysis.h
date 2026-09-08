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
// keeps the FIRST one it finds. A separate, complete local-container proof may
// discharge a Stored verdict when no return path can retain the site. It
// never relabels a stored child as uniquely returned or chooses a graph owner.
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
#include "mlir/IR/BuiltinAttributes.h"
#include "mlir/IR/Operation.h"
#include "mlir/IR/Value.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/MapVector.h"
#include "llvm/ADT/SmallVector.h"

#include <cstddef>
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
// - a closure base breaks them (lookup_property -> ensure_prototype, the
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

/// The direct target of the first Stored witness: create_array's result,
/// append's array, or set_property's base, read from the existing alias lattice.
/// Uninitialized means an unsupported witness or a missing lattice; an empty
/// initialized set means a non-object target. External may coexist with sites.
///
/// This is diagnostic evidence about the written operation, not a complete
/// contents or retention proof. Other stores and loads are not followed, a
/// setter may retain elsewhere, and distinct instances may share one site.
/// In particular, even a confined target can expose its elements through a
/// spread call. Nothing here permits weakening the stored value's verdict.
[[nodiscard]] AliasValue directStorageTarget(const mlir::DataFlowSolver & solver,
                                             const Verdict & verdict);

/// One live top-level operand whose ODS escape role is Stored. Unlike Verdict,
/// this record survives earlier sinks and includes external/primitive values:
/// those writes matter to the contents of a local target too. A variadic array
/// initializer contributes one record per element, in operand order.
struct DirectStorageWrite {
    mlir::Operation * by = nullptr;
    unsigned position = 0;
    AliasValue value;
    AliasValue target;
};

struct DirectStorageEvidence {
    llvm::SmallVector<DirectStorageWrite, 0> writes;
    /// All Stored operands in the live top-level CFG have initialized value
    /// and supported direct-target aliases, with no nested-region or raw-frame
    /// refusal. False preserves the partial records, never an empty success.
    ///
    /// This is completeness of the direct-write census ONLY. It says nothing
    /// about indirect contents transfers (copy_props, loads, spread calls),
    /// setters, other escape reasons or object instances sharing a site. Even
    /// when true it is not a contents/retention proof or permission to weaken
    /// Stored. External alternatives are complete evidence, not local owners.
    bool complete = true;
};

/// A live top-level get_property and the direct writes whose target may share
/// a local allocation site with its base. Indices address directStorage.writes
/// in ascending order, once each even when several joined sites overlap.
struct DirectPropertyRead {
    mlir::Operation * by = nullptr;
    AliasValue base;
    llvm::SmallVector<std::size_t, 2> candidateWrites;
};

struct DirectLoadEvidence {
    llvm::SmallVector<DirectPropertyRead, 0> reads;
    /// Every top-level get_property base has initialized aliases and the
    /// direct-write census is complete. External bases are resolved evidence;
    /// they contribute no local-site links, not proof that no write aliases.
    ///
    /// Links ignore keys, order, overwrites and distinct instances of one site.
    /// They do not follow loaded aliases, prototypes, accessors, copy_props or
    /// other indirect transfers. An empty candidate list says only that no
    /// recorded write shares a known local site. Even a complete census is
    /// NOT a points-to/contents proof, and never changes load result lattices
    /// or escape verdicts. Recompute after any IR mutation.
    bool complete = true;
};

struct EscapeVerdicts {
    /// Every tracked site in a LIVE top-level CFG block, in program order
    /// (a MapVector so the claims file is deterministic). Sites in dead CFG
    /// blocks are dropped and counted. Nested-region sites are out of scope.
    llvm::MapVector<mlir::Operation *, Verdict> sites;
    /// Every direct Stored write, independently of the first-reason verdicts.
    DirectStorageEvidence directStorage;
    /// Diagnostic local-site links from direct writes to property reads.
    DirectLoadEvidence directLoads;
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
    /// A separate complete local-container proof discharged these Stored sites.
    /// Its all-write graph must be acyclic, every operation must be supported,
    /// and none of these sites may be reachable through the returned value.
    /// This does not discharge native type, identity or lifetime obligations.
    unsigned confinedStoredSites = 0;
    /// Complete contents, acyclicity, return reachability and the full verdict
    /// refinement finished. False leaves every original verdict untouched.
    bool arrayRetentionComplete = false;
    /// Contents-query work plus container/write graph and verdict visits. A zero
    /// limit disables the refinement and preserves the original sink verdicts.
    std::size_t arrayRetentionWork = 0;
};

/// Diagnostic candidates after closing direct writes and property reads over
/// local sites, successor operands and ODS carries. The external alternative
/// of every original load result is preserved; this is not a points-to proof.
struct PropertyReadProvenance {
    mlir::Operation * by = nullptr;
    AliasValue base;
    AliasValue value;
};

/// Every top-level SINK operand, including exposures after the site's first
/// verdict and exposures reached through candidate loaded aliases.
struct EscapeExposure {
    mlir::Operation * by = nullptr;
    unsigned position = 0;
    EscapeReason reason = EscapeReason::Confined;
    AliasValue value;
};

struct LoadProvenanceEvidence {
    llvm::SmallVector<DirectStorageWrite, 0> writes;
    llvm::SmallVector<PropertyReadProvenance, 0> reads;
    llvm::SmallVector<EscapeExposure, 0> exposures;
    /// The supported constraints reached a fixed point within the work limit.
    /// A false value preserves partial candidates and forbids reading missing
    /// candidates as evidence. This does not claim all JS transfers are modeled.
    bool converged = false;
    /// The original direct census and every queried lattice were initialized,
    /// with no unsupported target, region or raw-frame refusal. Even together
    /// with converged this does NOT establish complete contents: keys, order,
    /// accessors, prototypes, calls and indirect transfers remain unproved.
    bool inputsComplete = true;
    /// Constraint visits and site joins performed during propagation. Building
    /// the census and reporting its partial result do not consume this limit.
    std::size_t work = 0;
};

/// Recompute from the live solver and verdict census for the SAME IR snapshot.
/// This separate, bounded diagnostic query never changes an AliasLattice or a
/// Verdict, and no native admission may consume it as a confinement proof.
[[nodiscard]] LoadProvenanceEvidence computeLoadProvenance(mlir::DataFlowSolver & solver,
                                                           ctjs::FuncOp function,
                                                           const EscapeVerdicts & verdicts,
                                                           std::size_t workLimit = 100000);

/// Independent complete own-element/field evidence, not the candidate graph above.
/// Values name their original constant or fresh allocation, following exact
/// earlier reads. Each write keeps its actual operand position as a witness.
struct ArrayElementWrite {
    mlir::Operation * by = nullptr;
    unsigned position = 0;
    mlir::Operation * array = nullptr;
    std::size_t index = 0;
    mlir::Value value;
};

struct ArrayElementRead {
    mlir::Operation * by = nullptr;
    mlir::Operation * array = nullptr;
    std::size_t index = 0;
    mlir::Value value;
};

using ObjectOwnProperties = llvm::MapVector<mlir::StringAttr, mlir::Value>;

struct ObjectPropertyWrite {
    mlir::Operation * by = nullptr;
    unsigned position = 0;
    mlir::Operation * object = nullptr;
    mlir::StringAttr key;
    mlir::Value value;
};

struct ObjectPropertyRead {
    mlir::Operation * by = nullptr;
    mlir::Operation * object = nullptr;
    mlir::StringAttr key;
    mlir::Value value;
};

struct ArrayContentsExit {
    mlir::Operation * by = nullptr; // return
    mlir::Value value;
    /// Exact final dense own elements on this path, including empty arrays.
    /// A join is explored separately for each incoming path; the same return
    /// may therefore have several records with different contents and roots.
    llvm::MapVector<mlir::Operation *, llvm::SmallVector<mlir::Value, 4>> arrays;
    /// Exact own data properties of every fresh ordinary object on this path.
    llvm::MapVector<mlir::Operation *, ObjectOwnProperties> objects;
    /// Every local object/array reachable from value at this exit, through
    /// current own elements/properties, once each. Includes a local root.
    /// Overwritten elements are absent unless another live path retains them.
    llvm::SmallVector<mlir::Operation *, 4> reachableSites;
};

enum class ArrayContentsFailure {
    None,
    UnsupportedControlFlow,
    UnsupportedOperation,
    UnknownValue,
    UnknownArray,
    UnknownIndex,
    MissingElement,
    WorkLimit,
    InvalidFrame,
    UnknownPropertyKey,
    MissingProperty
};

struct ArrayContentsEvidence {
    /// Every array site visited on any path, once each. Exact final contents
    /// live on exits; writes/reads preserve all path-specific earlier states.
    llvm::SmallVector<mlir::Operation *, 4> arrays;
    llvm::SmallVector<ArrayElementWrite, 0> writes;
    llvm::SmallVector<ArrayElementRead, 0> reads;
    llvm::SmallVector<mlir::Operation *, 4> objects;
    llvm::SmallVector<ObjectPropertyWrite, 0> propertyWrites;
    llvm::SmallVector<ObjectPropertyRead, 0> propertyReads;
    llvm::SmallVector<ArrayContentsExit, 1> exits;
    /// Published only after the entire function and exit reachability pass.
    /// Refusal/exhaustion returns NO container, write, read or exit records.
    bool complete = false;
    ArrayContentsFailure failure = ArrayContentsFailure::None;
    mlir::Operation * refusedBy = nullptr;
    /// Operation, forwarded-argument, initializer-element, branch-state-copy
    /// and exit graph visits. Array key validation examines one Number;
    /// object keys must be Strings of at most 256 bytes.
    std::size_t work = 0;
};

/// Recompute from the CURRENT verified IR; needs neither trusted annotations
/// nor alias lattices. An acyclic cf.br/cf.cond_br/cf.switch graph may contain
/// constants, fresh objects/arrays, literal append, constant-Number-index array
/// reads/overwrites, own String-property object writes/reads, truthy and return.
/// Object reads require an earlier own write; keys longer than 256 bytes and
/// __proto__ refuse. String array indices refuse because the current VM's named
/// property path does not access dense elements, unlike JavaScript's String
/// index semantics. Loaded aliases share one contents state per path; cycles
/// are visited once at exits. Unknown values/keys,
/// holes, calls, throws, publication, regions, prototypes and accessors refuse.
/// Every conditional/switch edge is explored, including default and statically
/// untaken cases. Switch flags must have an independently known origin; no
/// additional selector-producing operations or JS coercions are admitted. Joins
/// keep exact separate states rather than unioning overwrite targets. Truthy
/// accepts an external value only as a noncapturing predicate, never an element.
/// Exact forwarded values are required; revisiting a block on one path refuses
/// loops. Unvisited blocks are unreachable independently of solver flags.
/// An optional imported frame must enter first and exit immediately before
/// every return; roots name that active frame and an independently known value.
/// Checked branch arguments may forward the same frame handle.
/// Entry failure precedes every tracked allocation, not a nonthrowing claim.
///
/// This query changes no escape verdict itself. computeVerdicts independently
/// recomputes it before its bounded Stored refinement; neither query proves
/// native element types or ownership/lifetime. Any IR mutation invalidates all
/// returned records, including successful ones.
[[nodiscard]] ArrayContentsEvidence computeArrayContents(ctjs::FuncOp function,
                                                         std::size_t workLimit = 100000);

/// The post-pass. Takes the solver by non-const reference only because
/// DataFlowSolver::getProgramPointBefore(Block *) interns its anchor and is
/// not const; nothing here changes a lattice.
/// Models the function's top-level CFG. A live operation's nested regions
/// sink their implicit captures as unknown_op; no region-control-flow proof
/// is inferred. Allocations inside nested regions have no verdict and must
/// never be treated as confined by a consumer. Frame-retaining operations
/// inside those regions still trigger the whole-function refusals.
///
/// With initialized solver evidence, a complete current computeArrayContents
/// proof may discharge Stored sites absent from every return path. A bounded
/// acyclicity check over ALL array/object writes excludes transient cycles. Retained
/// children keep their original Stored witness; no unique owner is inferred.
/// Unsupported operations, cycles, missing lattices or budget exhaustion leave
/// every original verdict intact. No native admission consumes this refinement.
[[nodiscard]] EscapeVerdicts computeVerdicts(mlir::DataFlowSolver & solver, ctjs::FuncOp function,
                                             std::size_t arrayRetentionWorkLimit = 100000);

} // namespace ctcompile::ctnative

#endif
