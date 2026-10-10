//===- EscapeAnalysis.h - Escape analysis for native code -------*- C++ -*-===//
//
// Can an object allocated in this function outlive the call that made it?
//
// One question per allocation site, two answers. `confined`: no object
// allocated at this ctjs.create_object / ctjs.create_array is reachable from
// anything once the activation has returned or unwound, so the native backend
// may give it automatic (stack) storage. `escapes(reason)`: it may be
// reachable, or this analysis could not prove otherwise. The native target has
// no collector and no VM, so such a site needs an owner, and the ownership
// lowering picks one from the reason: a site that escapes only by `returned`
// is the std::unique_ptr candidate, `stored`/`captured`/`passed` are shared,
// and `arguments`, `suspended`, `unknown_op` and the `unvisited*` reasons are
// outside the native subset and become a compile-time diagnostic naming the
// site and the reason. So the reason matters, and the analysis keeps the first
// one it finds. A separate, complete local-container proof may discharge a
// Stored verdict when no return path can retain the site. It never relabels a
// stored child as uniquely returned or chooses a graph owner.
//
// Two lattices' worth of work, split into a dataflow analysis and a post-pass:
//
//   (1) Alias, per SSA value: AliasValue below, a sparse forward analysis on
//       MLIR's DataFlowFramework in the shape of TypeInference.h. "This value
//       may denote an object allocated at any of `sites` in this function,
//       and/or an object not allocated at a tracked site."
//
//   (2) Verdict, per site: not a lattice element. computeVerdicts runs after
//       the fixpoint, walks every operation in every live block, and for every
//       operand position whose role is SINK marks every site in that operand's
//       alias set. It must be a post-pass: MLIR's
//       AbstractSparseForwardDataFlowAnalysis::visitOperation exits early on an
//       operation with no results (SparseAnalysis.cpp), and ctjs.return, throw,
//       store_global, set_property, append, cell_set, store_upvalue, set_proto,
//       define_accessor, copy_props and every delete have no results. A sink
//       fired from visitOperation would silently never fire for `return {}`.
//       The unit test's return and throw rows fail if anyone moves it back.
//
// The per-operand table is not here. Which operand positions sink, carry, or
// neither is declared in TableGen with Arg<..., [CTJS_Sink...]> decorators on
// the ops in ctcompile/include/ctcompile/CTJS/IR/Ops/*.td (the classes are in
// CTJSInterfaces.td) and read through ctjs::EscapeEffectOpInterface. What is
// here is the default rule for an operation that has no annotation (every
// !ctjs.value operand sinks with reason `unknown_op`) and the one C++ switch:
// ctjs.unary, ctjs.compare and ctjs.convert, whose role depends on an
// attribute a decorator cannot see.
//
// Soundness is required; precision can be improved later. "The result is
// external and every tracked operand sinks" is always sound. CARRY and NEITHER
// exist only for precision, and every NEITHER role has its VM justification
// next to it in the .td. A wrong NEITHER is the only way this analysis can be
// unsound.
//
//===----------------------------------------------------------------------===//
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

/// What one SSA value may denote, as a lattice element.
///
/// Uninitialized is the identity, exactly as TypeValue's null Type is: the
/// framework constructs every element before it visits anything that could
/// define it, and the first real answer must replace that state rather than
/// join with it. `join` is set union on the sites and OR on `external`; the
/// height is sites-in-function + 1, so termination is by construction.
///
/// A site is a ctjs.create_object or a ctjs.create_array and nothing else. The
/// NEITHER rows apply only to these ordinary allocation kinds; property access
/// still exposes them through possible inherited accessors. The constructor
/// asserts the restriction, onlyTrackedSites re-checks it, and tests ensure
/// other allocating operations enter no alias set.
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

    /// The lattice join: union of sites, OR of `external`.
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
    llvm::SmallVector<mlir::Operation *, 4> sites_; // Sorted by address, unique.
};

using AliasLattice = mlir::dataflow::Lattice<AliasValue>;

/// The alias half of the analysis. Load it into a solver alongside both
/// DeadCodeAnalysis and SparseConstantPropagation, for TypeInference.h's two
/// reasons: without the first there is no predecessor information and no
/// block argument ever receives anything; without the second DeadCodeAnalysis
/// cannot decide which successor of a branch is live and marks none.
class EscapeAnalysis : public mlir::dataflow::SparseForwardDataFlowAnalysis<AliasLattice> {
public:
    using SparseForwardDataFlowAnalysis::SparseForwardDataFlowAnalysis;

    /// SITE for the two tracked operations; CARRY (union of the carried
    /// operands, plus external) for an operation whose interface says so;
    /// none for a non-value result or a constant; external for everything
    /// else. No sink is applied here; see the file comment.
    mlir::LogicalResult visitOperation(mlir::Operation * op,
                                       llvm::ArrayRef<const AliasLattice *> operands,
                                       llvm::ArrayRef<AliasLattice *> results) override;

    /// Entry-block arguments - receiver, new.target, callee, then the
    /// parameters - denote objects made elsewhere: external.
    void setToEntryState(AliasLattice * lattice) override;
};

// --- The roles, as the post-pass reads them -----------------------------------

/// What an operand position does to the objects it may denote.
enum class OperandRole {
    Sink,
    Carry,
    Neither
};

/// An operand's role, with the escape reason when the role is Sink.
struct RoleOf {
    OperandRole role = OperandRole::Neither;
    EscapeReason reason = EscapeReason::Confined; ///< Meaningful for Sink only.
};

/// The role of operand `index` of `op`: the kind switch for unary/compare/
/// convert; the ODS interface for an operation that has it; CARRY for a
/// BranchOpInterface's value operands (the framework moves them); and the
/// default rule, SINK(unknown_op) for every !ctjs.value operand, for an
/// operation with none of those. Exposed so the unit test can assert every
/// row's operand through the same path the verdict takes.
[[nodiscard]] RoleOf operandRole(mlir::Operation * op, unsigned index);

/// A ctjs.create_object or ctjs.create_array.
[[nodiscard]] bool isTrackedSite(mlir::Operation * op);

/// True for an allocating operation that this analysis does not track and
/// always reports as `escapes(reason)`, and sets `reason`. These are the ops
/// with Res<..., [CTJS_Boxed...]> decorators in CTJS/IR/Ops/*.td.
[[nodiscard]] bool isBoxedSite(mlir::Operation * op, EscapeReason & reason);

/// The bytecode pc the importer stamped on `op` - the `<at>` of the NameLoc
/// "program:<id>:<fn>:<at>" inside its FusedLoc - or nothing when the
/// location does not parse (a claim for such an op is `unvisited`, counted).
[[nodiscard]] std::optional<unsigned> allocationPc(mlir::Operation * op);

// --- The verdicts --------------------------------------------------------------

/// One site's verdict: why it escapes (or Confined), and the first sinking
/// operand, when known.
struct Verdict {
    EscapeReason reason = EscapeReason::Confined;
    mlir::Operation * by = nullptr; ///< The sinking operation, when known.
    unsigned position = 0;          ///< Its operand index.
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

/// Every direct Stored write in a function, and whether the census is complete.
struct DirectStorageEvidence {
    llvm::SmallVector<DirectStorageWrite, 0> writes;
    /// All Stored operands in the live top-level CFG have initialized value
    /// and supported direct-target aliases, with no nested-region or raw-frame
    /// refusal. False preserves the partial records, never an empty success.
    ///
    /// This is completeness of the direct-write census only. It says nothing
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

/// Every live top-level property read, linked to the direct writes it may see.
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
    /// not a points-to or contents proof, and never changes load result lattices
    /// or escape verdicts. Recompute after any IR mutation.
    bool complete = true;
};

/// The result of computeVerdicts().
struct EscapeVerdicts {
    /// Every tracked site under a live top-level CFG block, in preorder
    /// (a MapVector so the inventory is deterministic). All descendants of dead
    /// top-level blocks are dropped and counted. Nested sites remain UnknownOp
    /// with no operand witness, unless an earlier whole-function refusal applies;
    /// neither nested liveness nor distinct dynamic instances are inferred.
    llvm::MapVector<mlir::Operation *, Verdict> sites;
    /// Every direct Stored write, independently of the first-reason verdicts.
    DirectStorageEvidence directStorage;
    /// Diagnostic local-site links from direct writes to property reads.
    DirectLoadEvidence directLoads;
    /// Tracked sites in dead top-level blocks, dropped from `sites`.
    unsigned deadSites = 0;
    /// Sites in live blocks whose result lattice the solver never initialized;
    /// each is reported `escapes(unvisited)`. Tests require this to be zero.
    unsigned unvisitedSites = 0;
    /// SINK operands in live blocks with no lattice. Each makes every site of
    /// the function `escapes(unvisited_operand)`. Tests require this to be
    /// zero.
    unsigned unvisitedOperands = 0;
    unsigned blocks = 0;
    unsigned liveBlocks = 0;
    /// The function builds an `arguments` object or a rest parameter (refusal
    /// R1), so every parameter has a heap alias and a future callee summary
    /// must treat the function as capturing every argument.
    bool capturesAllArguments = false;
    /// Set when a rule applies to the whole function: `suspended` for a
    /// function with a suspension point (refusal R4), or `arguments_late` when
    /// R1's placement guard fails. Every site escapes with this reason.
    std::optional<EscapeReason> wholeFunction;
    /// A separate complete local-container proof discharged these stored sites,
    /// including implicit region captures whose exact operand role is Stored.
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

/// The result of computeLoadProvenance().
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
    /// with converged this does not establish complete contents: keys, order,
    /// accessors, prototypes, calls and indirect transfers remain unproved.
    bool inputsComplete = true;
    /// Constraint visits and site joins performed during propagation. Building
    /// the census and reporting its partial result do not consume this limit.
    std::size_t work = 0;
};

/// Recompute from the live solver and verdict census for the same IR snapshot.
/// This separate, bounded diagnostic query never changes an AliasLattice or a
/// Verdict, and no native admission may consume it as a confinement proof.
[[nodiscard]] LoadProvenanceEvidence computeLoadProvenance(mlir::DataFlowSolver & solver,
                                                           ctjs::FuncOp function,
                                                           const EscapeVerdicts & verdicts,
                                                           std::size_t workLimit = 100000);

/// One write to a fresh array's element, from the complete contents proof
/// (computeArrayContents), not the candidate graph above. Values name their
/// original constant, independent primitive producer or fresh allocation,
/// following exact earlier reads. Each write keeps its actual operand position
/// as a witness.
struct ArrayElementWrite {
    mlir::Operation * by = nullptr;
    unsigned position = 0;
    mlir::Operation * array = nullptr;
    std::size_t index = 0;
    mlir::Value value;
};

/// One read of a fresh array's element, from computeArrayContents.
struct ArrayElementRead {
    mlir::Operation * by = nullptr;
    mlir::Operation * array = nullptr;
    std::size_t index = 0;
    mlir::Value value;
};

/// A fresh object's own data properties, by key.
using ObjectOwnProperties = llvm::MapVector<mlir::StringAttr, mlir::Value>;

/// One write to a fresh object's own data property.
struct ObjectPropertyWrite {
    mlir::Operation * by = nullptr;
    unsigned position = 0;
    mlir::Operation * object = nullptr;
    mlir::StringAttr key;
    mlir::Value value;
};

/// One read of a fresh object's own data property.
struct ObjectPropertyRead {
    mlir::Operation * by = nullptr;
    mlir::Operation * object = nullptr;
    mlir::StringAttr key;
    mlir::Value value;
};

/// One deletion of a fresh object's own property.
struct ObjectPropertyDeletion {
    mlir::Operation * by = nullptr;
    mlir::Operation * object = nullptr;
    mlir::StringAttr key;
    /// The removed own value, or empty when the key was already absent.
    /// Earlier writes and saved-read origins are never removed by deletion.
    mlir::Value value;
};

/// An indirect own-data transfer: neither copy_props operand is the copied
/// value. Keep this separate from direct Stored witnesses and preserve every
/// copied edge in the historical cycle graph, even after overwrite/deletion.
struct ObjectPropertyCopy {
    mlir::Operation * by = nullptr;
    mlir::Operation * source = nullptr;
    mlir::Operation * target = nullptr;
    mlir::StringAttr key;
    mlir::Value value;
};

/// The exact contents of every fresh container on one path to a return.
struct ArrayContentsExit {
    mlir::Operation * by = nullptr; ///< The return.
    mlir::Value value;
    /// Exact final dense own elements on this path, including empty arrays.
    /// A join is explored separately for each incoming path; the same return
    /// may therefore have several records with different contents and roots.
    llvm::MapVector<mlir::Operation *, llvm::SmallVector<mlir::Value, 4>> arrays;
    /// Exact own data properties of every fresh ordinary object on this path.
    /// Entry order is diagnostic only, not an OwnPropertyKeys ordering proof.
    llvm::MapVector<mlir::Operation *, ObjectOwnProperties> objects;
    /// Every local object/array reachable from value at this exit, through
    /// current own elements/properties, once each. Includes a local root.
    /// Replaced/deleted contents are absent unless another live path retains them.
    llvm::SmallVector<mlir::Operation *, 4> reachableSites;
};

/// Why computeArrayContents refused, or None.
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

/// The result of computeArrayContents().
struct ArrayContentsEvidence {
    /// Every array site visited on any path, once each. Exact final contents
    /// live on exits; writes/reads preserve all path-specific earlier states.
    llvm::SmallVector<mlir::Operation *, 4> arrays;
    llvm::SmallVector<ArrayElementWrite, 0> writes;
    llvm::SmallVector<ArrayElementRead, 0> reads;
    llvm::SmallVector<mlir::Operation *, 4> objects;
    llvm::SmallVector<ObjectPropertyWrite, 0> propertyWrites;
    llvm::SmallVector<ObjectPropertyRead, 0> propertyReads;
    llvm::SmallVector<ObjectPropertyDeletion, 0> propertyDeletions;
    llvm::SmallVector<ObjectPropertyCopy, 0> propertyCopies;
    llvm::SmallVector<ArrayContentsExit, 1> exits;
    /// Published only after the entire function and exit reachability pass.
    /// A refusal or exhaustion returns no container, write, read, deletion, copy
    /// or exit records.
    bool complete = false;
    ArrayContentsFailure failure = ArrayContentsFailure::None;
    mlir::Operation * refusedBy = nullptr;
    /// Entry-argument, operation, forwarded-argument, initializer-element, branch-state-copy,
    /// deletion-state-update, copy snapshot/field and exit graph visits. Array key validation
    /// examines one Number; object keys must be Strings of at most 256 bytes.
    std::size_t work = 0;
};

/// Proves the exact own contents of every fresh array and plain object on each
/// path to a return, and which local allocations each returned value can
/// reach. Works on the current verified IR alone; needs neither trusted
/// annotations nor alias lattices.
///
/// Accepted: acyclic cf.br/cf.cond_br/cf.switch control flow (every edge is
/// explored, including untaken switch cases), single-block scf.if (arms keep
/// separate exact states; an implicit empty else keeps its incoming state),
/// and read-only counted loops (CFG or single-block scf.while) whose index
/// starts at a proved nonnegative integral Number, steps by a proved positive
/// amount, and is guarded by a strict own-length test such as
/// `index < length`, all within 0..2^32-1. Inside that: constants, fresh
/// objects and arrays, literal append, constant-Number-index array reads and
/// overwrites, own-data object reads (keys at most 256 bytes, never
/// `__proto__`), deletions on fresh objects, copy_props between known fresh
/// own-data objects (every source field is snapshotted first, including
/// self-copy), truthy, return, and primitive operators whose operands are
/// independently proved primitive, so no user conversion can run. BigInt
/// operands are accepted only where the VM's BigInt path is exact and its
/// errors (TypeError, RangeError, the recursion-depth guard) carry no edge to
/// a local object; the whole-frame exclusion of calls, handlers and
/// publication then keeps such an early exit from retaining unpublished
/// locals.
///
/// Refused: calls, throws, publication, accessors, prototypes, unknown values
/// or keys, holes, every array deletion, string array indices (the VM's named
/// property path does not reach dense elements), ordinary-object writes (an
/// inherited setter could retain the value), allocation, mutation or nested
/// control inside a loop, and other repeated blocks. A refusal sets `failure`
/// and `refusedBy` and publishes no records.
///
/// Two VM deviations from source JavaScript are modelled as the VM behaves:
/// non-BigInt static conversion never invokes user code, even on a fresh
/// object, and Pow keeps the VM's unconditional exponent cap for small bases.
///
/// Joins keep exact separate states per incoming path rather than unioning
/// overwrite targets. An optional imported frame must enter first and exit
/// immediately before every return.
///
/// Success proves only that local objects are not retained past the frame. It
/// proves no native element type, ownership, normal completion, absence of
/// exceptions or allocation success. computeVerdicts recomputes this query
/// before its bounded Stored refinement. The per-operator rules are commented
/// beside each case in lib/CTNative/Analysis/EscapeAnalysis/*.cpp. Any IR
/// mutation invalidates every returned record, including successful ones.
[[nodiscard]] ArrayContentsEvidence computeArrayContents(ctjs::FuncOp function,
                                                         std::size_t workLimit = 100000);

/// The post-pass that computes every site's verdict. It takes the solver by
/// non-const reference only because
/// DataFlowSolver::getProgramPointBefore(Block *) interns its anchor and is
/// not const; nothing here changes a lattice.
///
/// Models the function's top-level CFG. A live operation's nested regions
/// sink their implicit captures as unknown_op; no region-control-flow proof
/// is inferred. Allocations inside nested regions are inventoried with an
/// UnknownOp verdict and no operand witness, after whole-function refusals.
/// Only the independent refinement below may discharge that inventory verdict.
/// Frame-retaining operations inside those regions still trigger whole-function refusals.
///
/// With initialized solver evidence, a complete current computeArrayContents
/// proof may discharge Stored sites absent from every return path, including
/// UnknownOp region captures whose exact operand role is Stored. A bounded
/// acyclicity check over all array/object writes excludes transient cycles.
/// Retained children keep their original witness; no unique owner is inferred.
/// For scf.if/scf.while, a census (charged to the same work limit) checks the
/// current operand/result lattices and direct Stored targets before the
/// contents proof runs; the top-level direct-write census
/// (DirectStorageEvidence) still reports such functions as incomplete. Nested
/// allocations require exclusively scf.if ancestors; the contents query
/// rejects repeated root blocks except allocation-free and region-free counted
/// loops, proving at most one instance per call. A site may be refined only
/// when absent from every return's reachable sites. This proves frame
/// confinement, never region lifetime or native ownership.
/// Unsupported operations, cycles, missing lattices or budget exhaustion leave
/// every original verdict intact. No native admission consumes this refinement.
[[nodiscard]] EscapeVerdicts computeVerdicts(mlir::DataFlowSolver & solver, ctjs::FuncOp function,
                                             std::size_t arrayRetentionWorkLimit = 100000);

} // namespace ctcompile::ctnative

#endif
