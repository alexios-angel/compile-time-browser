#pragma once

#include "ctcompile/CTJS/IR/CTJSOps.h"
#include "ctcompile/CTNative/Analysis/PrimitiveAlternatives.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/Support/TypeID.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/Support/Error.h"

#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace ctcompile::ctnative {

struct HostRootRequest {
    std::string binding;
    std::vector<std::string> properties;
};

// Driver input, never an IR proof annotation. closed-source-v1 starts with
// ordinary own global bindings and unmodified intrinsic prototypes; all
// subsequent source mutations/calls still require the analysis below.
struct HostContract {
    enum class Provider {
        closedSource,
        // The same closed source proof, with a captured method table whose
        // generated methods remain attached to their nonmovable owner.
        // This does not authorize DOM keys or externally retained callbacks.
        closedSourceSession,
        ctbrowserDOM
    };
    Provider provider = Provider::closedSource;
    std::string moduleSha256;
    std::string entry;
    // ctbrowser-dom-v1 invokes one ordinary function with borrowed elements.
    // Indices name explicit JS parameters, excluding the three implicit ones.
    // The document owns each node and must outlive this synchronous invocation;
    // identity includes the document, not just the node_id's bits.
    // The provider starts with the standard undefined binding. The complete
    // source proof excludes replacement and external script reentry.
    std::vector<unsigned> elementParameters;
    std::vector<HostRootRequest> roots;
    std::vector<std::string> observations;
    std::vector<std::string> absentBindings;
    std::vector<std::string> undefinedBindings;
    // Explicit identities supplied by the embedding, not effect summaries.
    // Only standard Map/Array are supported. Source replacement/escape refuses.
    std::vector<std::string> initialIntrinsics;
    bool realmGlobalThis = false;
    // The embedding invokes this entry as a classic script, with its stable
    // realm receiver. This does not characterize an ordinary call's `this`.
    // Listed realm properties initially exist as writable own data slots;
    // reading/writing them cannot call JS. Their values remain unknown.
    bool classicScriptRealm = false;
    std::vector<std::string> realmOwnDataProperties;
};

llvm::Expected<HostContract> parseHostContract(llvm::StringRef json);
// Includes the whole driver/program IR. Presentation locations and previous
// host-contract reports are excluded; all semantic operations remain bound.
std::string hostContractFingerprint(mlir::ModuleOp module);
void clearHostContractReports(mlir::ModuleOp module);
// Remove every attribute of `op` whose name starts with `prefix` - the
// analyses' reports and proof markers, which a clone must never inherit.
void removeAttrsWithPrefix(mlir::Operation * op, llvm::StringRef prefix);

enum class HostDOMMethod {
    toggleClass,
    setAttribute,
    toggleAttribute,
    hasAttribute,
    removeAttribute,
    contains,
    matches,
    closest
};

struct HostDOMCall {
    ctjs::CallOp operation;
    HostDOMMethod kind;
    mlir::Value element;
    [[nodiscard]] bool returnsElement() const { return kind == HostDOMMethod::closest; }
    [[nodiscard]] bool returnsBoolean() const {
        return !returnsElement() && kind != HostDOMMethod::setAttribute &&
               kind != HostDOMMethod::removeAttribute;
    }
    [[nodiscard]] bool usesStyle() const {
        return kind == HostDOMMethod::matches || kind == HostDOMMethod::closest;
    }
};

// Live evidence for one synchronous typed DOM entry. Only the checked source
// declaration wrapper may be omitted. No source invocation, retained handle,
// receiver/capture observation, or arbitrary property dispatch is authorized.
// Mutating the module invalidates this query; printed attributes are ignored.
class DOMEntryAnalysis {
public:
    DOMEntryAnalysis(mlir::ModuleOp module, const HostContract & contract,
                     unsigned maxSteps = 100000);
    [[nodiscard]] bool proved() const { return refusal.empty(); }
    [[nodiscard]] llvm::StringRef reason() const { return refusal; }
    [[nodiscard]] unsigned steps() const { return workSteps; }
    [[nodiscard]] bool exhausted() const { return budgetExhausted; }
    [[nodiscard]] ctjs::FuncOp entry() const { return checkedEntry; }
    [[nodiscard]] ctjs::FuncOp wrapper() const { return checkedWrapper; }
    [[nodiscard]] llvm::ArrayRef<mlir::BlockArgument> parameters() const { return elements; }
    [[nodiscard]] bool isElement(mlir::Value value) const;
    // Validated parameters or nullable closest results, for equality only.
    [[nodiscard]] bool isElementIdentity(mlir::Value value) const;
    [[nodiscard]] bool isTokenList(mlir::Value value) const;
    [[nodiscard]] std::optional<HostDOMMethod> method(ctjs::GetPropertyOp read) const;
    [[nodiscard]] const HostDOMCall * call(ctjs::CallOp operation) const;

private:
    std::string refusal;
    ctjs::FuncOp checkedEntry;
    ctjs::FuncOp checkedWrapper;
    std::vector<mlir::BlockArgument> elements;
    std::vector<ctjs::GetPropertyOp> tokenLists;
    std::vector<std::pair<ctjs::GetPropertyOp, HostDOMMethod>> methods;
    std::vector<HostDOMCall> calls;
    unsigned workSteps = 0;
    bool budgetExhausted = false;
};

struct HostSlotEdge {
    ctjs::SetPropertyOp write;
    ctjs::GetPropertyOp read;
};

// Finite primitive categories or caller leaf arguments, proved over the
// complete current call census. Positions that may receive objects have no primitive alternatives;
// their actual allocation and allowed uses are checked independently. The
// captured Map may retain them as keys or payloads; their own fields contain only scalars.
struct HostMethodParameters {
    ctjs::FuncOp function;
    std::vector<PrimitiveAlternatives> alternatives;
    std::vector<mlir::BlockArgument> objectKeys{};
    bool operator==(const HostMethodParameters & other) const {
        return function == other.function && alternatives == other.alternatives &&
               objectKeys == other.objectKeys;
    }
};

struct HostMethodArgument {
    mlir::BlockArgument parameter;
    mlir::Value actual;
    PrimitiveAlternatives alternatives;
    ctjs::CreateObjectOp object{};
};

struct HostChildMapEntry {
    mlir::Value key;
    PrimitiveAlternatives alternatives;
    bool operator==(const HostChildMapEntry & other) const {
        return key == other.key && alternatives == other.alternatives;
    }
};

// A Number category invariant over one initialized source global and every
// subsequent write. Values and effects remain live across future invocations.
struct HostMutableScalarGlobal {
    ctjs::StoreGlobalOp initialization;
    std::vector<ctjs::StoreGlobalOp> writes;
    std::vector<ctjs::LoadGlobalOp> reads;
    bool operator==(const HostMutableScalarGlobal & other) const {
        return initialization == other.initialization && writes == other.writes &&
               reads == other.reads;
    }
};

// A fixed own callable slot, with a complete scalar-only body/effect census.
// This source callback cannot reach the captured Map or its caller's objects.
// Its calls are distinct from exported methods and pure snapshot operations.
struct HostScalarCallback {
    ctjs::CreateObjectOp owner;
    ctjs::StoreGlobalOp initialization;
    ctjs::SetPropertyOp write;
    ctjs::CreateClosureOp closure;
    ctjs::FuncOp function;
    std::vector<ctjs::LoadGlobalOp> loads;
    std::vector<ctjs::GetPropertyOp> reads;
    std::vector<mlir::Operation *> calls;
    std::vector<mlir::Operation *> operations;
    std::vector<HostMutableScalarGlobal> globals;
    bool operator==(const HostScalarCallback & other) const {
        return owner == other.owner && initialization == other.initialization &&
               write == other.write && closure == other.closure && function == other.function &&
               loads == other.loads && reads == other.reads && calls == other.calls &&
               operations == other.operations && globals == other.globals;
    }
};

// Exact caller allocation returned by one checked entry invocation. Neither
// the reusable method's result ABI nor another invocation is narrowed.
struct HostReturnedLeaf {
    mlir::Operation * call = nullptr;
    ctjs::CreateObjectOp object;
    bool operator==(const HostReturnedLeaf & other) const {
        return call == other.call && object == other.object;
    }
};

// Primitive alternatives of one checked entry invocation, independent of the
// reusable method's result and parameter categories.
struct HostReturnedScalar {
    mlir::Operation * call = nullptr;
    PrimitiveAlternatives alternatives;
    bool operator==(const HostReturnedScalar & other) const {
        return call == other.call && alternatives == other.alternatives;
    }
};

// One immutable environment slot owns this exact standard Map, constructed
// empty. The complete live census of every closure sharing that slot permits
// primitive contents, fresh method-local leaves with fixed scalar fields, or
// checked scalar-field caller leaves, fresh child Maps, and standard
// size/set/get/has/delete/clear effects and confined immediate key snapshots.
// Child Maps cannot retain Maps.
// Caller formals permit only key/payload uses; method-local leaves cannot be keys.
// Only checked owning leaves may leave a method through its result. Fields
// and unchecked uses cannot publish objects. Effects remain runtime; the
// family promises no startup value or invocation result. Optional cell operations describe
// the original binding; after lifting, the call reads its environment value.
// Every handle is rederived from the current module, never from native markers.
struct HostCapturedMap {
    ctjs::LoadGlobalOp intrinsic;
    ctjs::ConstructOp allocation;
    ctjs::CreateCellOp cell;
    ctjs::CellSetOp initialization;
    std::vector<ctjs::CreateClosureOp> closures;
    std::vector<HostMethodParameters> parameters;
    // Complete family effects; argument below belongs to this actual call.
    std::vector<ctjs::LoadUpvalueOp> upvalues;
    std::vector<ctjs::GetPropertyOp> reads;
    std::vector<ctjs::CallOp> calls;
    std::vector<ctjs::CreateObjectOp> leafObjects;
    std::vector<ctjs::SetPropertyOp> leafWrites;
    std::vector<ctjs::GetPropertyOp> leafReads;
    ctjs::LoadUpvalueOp argument;
    std::vector<ctjs::ConstructOp> childMaps{};
    // Every outer set stores a checked fresh child, across the complete family.
    // This promises neither a returned child's identity nor its contents.
    bool childMapContents = false;
    std::vector<mlir::Value> returnedChildMaps{};
    // Initialized before every publication and preserved by every child write.
    // This says nothing about identity, other keys, cardinality or startup values.
    std::vector<HostChildMapEntry> childEntries{};
    // Every child write has this scalar category, independently of membership.
    // Empty publication, delete and clear preserve it; reads may be Undefined.
    PrimitiveAlternatives childScalarContents{};
    // Every child write is a supported scalar or a checked scalar-field leaf.
    // Reads may be Undefined; this gives no primitive, field or identity facts.
    bool childLeafContents = false;
    // Independent complete insertion censuses, including all sibling methods.
    // Neither fact implies nonempty snapshots or permits object-key coercion.
    bool outerStringKeys = false;
    bool childStringKeys = false;
    // Confined immediate key copies and read-only length/index observations.
    // Element reads permit equality; String-key facts also permit checked Concat.
    std::vector<mlir::Operation *> snapshotOperations{};
    std::vector<HostScalarCallback> scalarCallbacks{};
    std::vector<HostReturnedLeaf> returnedLeaves{};
    std::vector<HostReturnedScalar> returnedScalars{};
    // Caller allocations used only as outer Map keys across every family call.
    // Payloads, child keys, parameter transport and outer key snapshots exclude
    // an allocation. This is source-use evidence, not DOM provenance or permission
    // to borrow an external object; objectKeys retains its ordinary owning contract.
    std::vector<ctjs::CreateObjectOp> outerKeyObjects{};
};

// Evidence for this actual call, not a promise about future exported callers
// or private visibility. The current own-data initializer stores this exact
// source closure. The original receiver and arguments remain live.
struct HostCallableEdge {
    mlir::Operation * call = nullptr;
    ctjs::GetPropertyOp read;
    ctjs::SetPropertyOp write;
    ctjs::CreateClosureOp closure;
    ctjs::FuncOp function;
    std::optional<HostCapturedMap> capturedMap;
    std::vector<HostMethodArgument> arguments;
};

// A proved Number, Boolean or String origin in an ordinary source global. Each read has
// one earlier entry store; dependencies are the completed published results
// encountered in its value expression, in traversal order (possibly repeated).
// The list is empty for constant-only scalar expressions; their source scope,
// initialization and complete environment proof remain mandatory.
// These are live source edges, not constant values or a native type promise.
struct HostScalarGlobalRead {
    ctjs::StoreGlobalOp initialization;
    ctjs::LoadGlobalOp read;
    mlir::Value value;
    PrimitiveAlternatives alternatives;
    std::vector<mlir::Value> dependencies;
};

// A fresh scalar-field object stored once in an ordinary entry global, or an alias
// reached through a complete acyclic chain of those bindings. Initialization
// belongs to this read's binding; object remains the original allocation.
// Every read follows its store, and all uses are checked initializations or
// scalar own-field operations, strict identity tests or key/payload arguments in the
// completed captured-Map family. Loads/stores remain live.
struct HostObjectGlobalRead {
    ctjs::StoreGlobalOp initialization;
    ctjs::LoadGlobalOp read;
    ctjs::CreateObjectOp object;
};

struct HostSlotReport {
    std::string binding;
    std::string property;
    ctjs::CreateObjectOp owner;
    std::vector<ctjs::SetPropertyOp> writes;
    std::vector<ctjs::GetPropertyOp> reads;
    std::vector<HostSlotEdge> edges;
    std::string reason;
};

// A live, module-specific result. Diagnostic slot reports may expose partial
// evidence; property() returns a proof only when every contract obligation
// passed. Mutating the module invalidates this object. No native pass consumes
// the printed report attributes as authority.
class HostContractAnalysis {
public:
    HostContractAnalysis(mlir::ModuleOp module, const HostContract & contract,
                         unsigned maxSteps = 100000);

    [[nodiscard]] bool proved() const { return refusal.empty(); }
    [[nodiscard]] llvm::StringRef reason() const { return refusal; }
    // Bounded proof work, available to consumers sharing one analysis budget.
    [[nodiscard]] unsigned steps() const { return workSteps; }
    [[nodiscard]] bool exhausted() const { return budgetExhausted; }
    [[nodiscard]] llvm::ArrayRef<HostSlotReport> slots() const { return reports; }
    [[nodiscard]] llvm::ArrayRef<ctjs::StoreGlobalOp> observations() const { return observed; }
    [[nodiscard]] const HostSlotEdge * property(ctjs::GetPropertyOp read) const;
    [[nodiscard]] llvm::ArrayRef<HostCallableEdge> callables() const { return checkedCalls; }
    [[nodiscard]] const HostCallableEdge * callable(mlir::Operation * call) const;
    [[nodiscard]] llvm::ArrayRef<HostScalarGlobalRead> scalarReads() const {
        return checkedScalarReads;
    }
    [[nodiscard]] const HostScalarGlobalRead * scalarRead(ctjs::LoadGlobalOp read) const;
    [[nodiscard]] llvm::ArrayRef<HostObjectGlobalRead> objectReads() const {
        return checkedObjectReads;
    }
    [[nodiscard]] const HostObjectGlobalRead * objectRead(ctjs::LoadGlobalOp read) const;

private:
    std::string refusal;
    std::vector<HostSlotReport> reports;
    std::vector<ctjs::StoreGlobalOp> observed;
    std::vector<HostCallableEdge> checkedCalls;
    std::vector<HostScalarGlobalRead> checkedScalarReads;
    std::vector<HostObjectGlobalRead> checkedObjectReads;
    unsigned workSteps = 0;
    bool budgetExhausted = false;
};

} // namespace ctcompile::ctnative
