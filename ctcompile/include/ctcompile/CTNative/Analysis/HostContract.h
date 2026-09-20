#pragma once

#include "ctcompile/CTJS/IR/CTJSOps.h"
#include "ctcompile/CTNative/Analysis/PrimitiveAlternatives.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/Support/TypeID.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseMap.h"
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
        // Isolated initial Object/String/RegExp prototype chains and reserved
        // regexp literal factory. Complete source reproof excludes mutation
        // and external script reentry.
        ctbrowserDOM,
        // The same synchronous source proof with a nonmovable native owner
        // of the atom table, document and (when needed) selector engine.
        // Data retention and browser callbacks still require separate proofs.
        ctbrowserDOMSession,
        // Explicit DOM inputs may be retained only as outer Data Map keys.
        // The host proof establishes provenance, not document lifetime;
        // native ownership refuses until storage is confined to that owner.
        ctbrowserDOMDataSession
    };
    Provider provider = Provider::closedSource;
    std::string moduleSha256;
    std::string entry;
    // DOM providers invoke one ordinary function with borrowed elements.
    // Indices name explicit JS parameters, excluding the three implicit ones.
    // The document owns each node and must outlive this synchronous invocation;
    // ctbrowser-dom-session-v1 generates that nonmovable document owner.
    // The Data provider proves only complete-family outer-key uses for now;
    // these declarations cannot authorize its retained native storage.
    // identity includes the document, not just the node_id's bits.
    // The provider starts with the standard undefined binding. The complete
    // source proof excludes replacement and external script reentry.
    std::vector<unsigned> elementParameters;
    // Explicit subset whose namespace is HTML or SVG, checked at native entry.
    // Other namespaces need public namespace URI ownership before admission.
    std::vector<unsigned> datasetParameters;
    std::vector<HostRootRequest> roots;
    std::vector<std::string> observations;
    std::vector<std::string> absentBindings;
    std::vector<std::string> undefinedBindings;
    // Explicit identities supplied by the embedding, not effect summaries.
    // Closed-source providers accept Map/Array and the class helper. DOM entry
    // providers accept Object, Number, decodeURIComponent, JSON, Array and String
    // as standard own global data bindings, including Number.prototype.toString,
    // Object.keys, JSON.parse, Array.prototype.filter, String.prototype.startsWith
    // and the default Array constructor/species chain. Array also promises
    // original Array iteration, including its Symbol.iterator/values method
    // and iterator-prototype chain, with no custom next or return hooks. DOM
    // iteration additionally requires the original __ctbrowser_for_of_open,
    // __ctbrowser_iter_next and __ctbrowser_iter_close source-helper bindings. Source
    // replacement/escape and external script reentry still refuse the complete live proof.
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
// Only on a private, fingerprint-checked clone. The caller must reprove its
// complete DOM entry after this bounded local-call normalization.
llvm::Error expandDOMHelpers(mlir::ModuleOp candidate, llvm::StringRef entry, unsigned maxSteps);
llvm::Error normalizeDOMElementGuards(mlir::ModuleOp candidate, const HostContract & contract,
                                      unsigned maxSteps);
llvm::Error normalizeDOMIteration(mlir::ModuleOp candidate, const HostContract & contract,
                                  unsigned maxSteps);

enum class HostDOMMethod {
    toggleClass,
    containsClass,
    addClass,
    removeClass,
    setAttribute,
    getAttribute,
    toggleAttribute,
    hasAttribute,
    removeAttribute,
    contains,
    matches,
    closest,
    querySelector,
    number,
    numberToString,
    decodeURIComponent,
    jsonParse,
    datasetKeys,
    filterStrings,
    startsWith,
    removeStringPrefix,
    replaceUppercase,
    stringCharAt,
    stringSlice,
    stringLowercaseUnit
};

struct HostDOMCall {
    ctjs::CallOp operation;
    HostDOMMethod kind;
    // Browser receiver, or the original scalar input for number/numberToString.
    mlir::Value element;
    ctjs::FuncOp callback{};
    [[nodiscard]] bool returnsElement() const {
        return kind == HostDOMMethod::closest || kind == HostDOMMethod::querySelector;
    }
    [[nodiscard]] bool returnsOptionalString() const { return kind == HostDOMMethod::getAttribute; }
    [[nodiscard]] bool returnsStringVector() const {
        return kind == HostDOMMethod::datasetKeys || kind == HostDOMMethod::filterStrings;
    }
    [[nodiscard]] bool returnsNumber() const { return kind == HostDOMMethod::number; }
    // An owning ctbrowser::json_value tree, from the shared public Core parser.
    [[nodiscard]] bool returnsJSON() const { return kind == HostDOMMethod::jsonParse; }
    [[nodiscard]] bool returnsString() const {
        return kind == HostDOMMethod::numberToString || kind == HostDOMMethod::decodeURIComponent ||
               kind == HostDOMMethod::removeStringPrefix ||
               kind == HostDOMMethod::replaceUppercase || kind == HostDOMMethod::stringCharAt ||
               kind == HostDOMMethod::stringSlice || kind == HostDOMMethod::stringLowercaseUnit;
    }
    [[nodiscard]] bool returnsBoolean() const {
        return !returnsOptionalString() && !returnsElement() && !returnsNumber() &&
               !returnsString() && !returnsJSON() && !returnsStringVector() &&
               kind != HostDOMMethod::setAttribute && kind != HostDOMMethod::removeAttribute &&
               kind != HostDOMMethod::addClass && kind != HostDOMMethod::removeClass;
    }
    [[nodiscard]] bool usesStyle() const {
        return kind == HostDOMMethod::matches || returnsElement();
    }
};

struct HostDOMStringUse {
    mlir::Operation * operation;
    unsigned operandIndex;
};

// One exact optional snapshot, observed as String only in this selected arm.
// Uses retain their source identities; the producer and sibling stay optional.
struct HostDOMStringRefinement {
    mlir::Block * block;
    mlir::Value optional;
    std::vector<HostDOMStringUse> uses;
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
    [[nodiscard]] bool returnsUndefined() const { return undefinedReturn; }
    [[nodiscard]] ctjs::FuncOp wrapper() const { return checkedWrapper; }
    [[nodiscard]] llvm::ArrayRef<ctjs::FuncOp> callbacks() const { return checkedCallbacks; }
    [[nodiscard]] ctjs::FuncOp callback(ctjs::CreateClosureOp closure) const;
    [[nodiscard]] bool isCallbackParameter(mlir::Value value) const;
    [[nodiscard]] llvm::ArrayRef<mlir::BlockArgument> parameters() const { return elements; }
    [[nodiscard]] bool isElement(mlir::Value value) const;
    // Validated parameters or nullable closest results, for equality only.
    [[nodiscard]] bool isElementIdentity(mlir::Value value) const;
    [[nodiscard]] bool isTokenList(mlir::Value value) const;
    [[nodiscard]] bool isDataset(mlir::Value value) const;
    [[nodiscard]] bool isDatasetElement(mlir::Value value) const;
    [[nodiscard]] bool isStringVectorLength(ctjs::GetPropertyOp read) const;
    [[nodiscard]] bool isStringVectorIndex(ctjs::GetPropertyOp read) const;
    // Present own member from this element's immutable, uninvalidated key snapshot.
    [[nodiscard]] mlir::Value datasetValueElement(ctjs::GetPropertyOp read) const;
    [[nodiscard]] bool isStringPrefixRegExp(ctjs::CallOp call) const;
    [[nodiscard]] bool isNumberIntrinsic(ctjs::LoadGlobalOp load) const;
    [[nodiscard]] bool isInitialIntrinsic(ctjs::LoadGlobalOp load) const;
    // One URI or JSON.parse call with owning String/json_value continuations;
    // the implicit error payload is proved unused. A parse invoke may nest in
    // the decode success continuation. This is fresh provider evidence.
    [[nodiscard]] bool invocation(ctjs::InvokeOp operation) const;
    [[nodiscard]] llvm::ArrayRef<mlir::Value> optionalStringJoins() const {
        return optionalStrings;
    }
    [[nodiscard]] llvm::ArrayRef<HostDOMStringRefinement> stringRefinements() const {
        return refinements;
    }
    // Complete String-only branch/invocation yields, independently of the
    // producer-wide lattice. This never narrows a getAttribute result.
    [[nodiscard]] llvm::ArrayRef<mlir::Value> stringResults() const { return strings; }
    // Exact undefined tests and their truth conversions, after every arm is proved.
    [[nodiscard]] llvm::ArrayRef<std::pair<mlir::Value, bool>> constantBooleans() const {
        return booleans;
    }
    [[nodiscard]] std::optional<HostDOMMethod> method(ctjs::GetPropertyOp read) const;
    [[nodiscard]] const HostDOMCall * call(ctjs::CallOp operation) const;
    // Fresh owning data objects and guarded spreads, after the complete use
    // census excludes observable aliases and mutation after a value copy.
    [[nodiscard]] bool jsonObject(ctjs::CreateObjectOp operation) const;
    [[nodiscard]] bool jsonCopy(ctjs::CopyPropsOp operation) const;
    [[nodiscard]] bool jsonAssignment(ctjs::SetPropertyOp operation) const;
    // Sole writer over unique direct snapshot keys; only final own data is observed.
    [[nodiscard]] bool jsonSnapshotAssignment(ctjs::SetPropertyOp operation) const;

private:
    std::string refusal;
    ctjs::FuncOp checkedEntry;
    bool undefinedReturn = false;
    ctjs::FuncOp checkedWrapper;
    std::vector<ctjs::FuncOp> checkedCallbacks;
    std::vector<std::pair<ctjs::CreateClosureOp, ctjs::FuncOp>> callbackClosures;
    std::vector<mlir::BlockArgument> elements;
    std::vector<ctjs::GetPropertyOp> tokenLists, datasets;
    std::vector<ctjs::GetPropertyOp> stringVectorLengths;
    std::vector<ctjs::GetPropertyOp> stringVectorIndices;
    llvm::DenseMap<ctjs::GetPropertyOp, mlir::Value> datasetValues;
    std::vector<ctjs::CallOp> stringPrefixRegExps;
    std::vector<mlir::BlockArgument> datasetElements;
    std::vector<ctjs::LoadGlobalOp> numberIntrinsics;
    std::vector<ctjs::LoadGlobalOp> uriIntrinsics, jsonIntrinsics, objectIntrinsics,
        regexpIntrinsics;
    std::vector<ctjs::InvokeOp> invocations;
    std::vector<mlir::Value> optionalStrings;
    std::vector<HostDOMStringRefinement> refinements;
    std::vector<mlir::Value> strings;
    std::vector<std::pair<mlir::Value, bool>> booleans;
    llvm::DenseMap<ctjs::GetPropertyOp, HostDOMMethod> methods;
    std::vector<HostDOMCall> calls;
    std::vector<ctjs::CreateObjectOp> jsonObjects;
    std::vector<ctjs::CopyPropsOp> jsonCopies;
    std::vector<ctjs::SetPropertyOp> jsonAssignments;
    std::vector<ctjs::SetPropertyOp> snapshotAssignments;
    unsigned workSteps = 0;
    bool budgetExhausted = false;
};

struct HostSlotEdge {
    ctjs::SetPropertyOp write;
    ctjs::GetPropertyOp read;
};

// Finite primitive categories or caller object arguments, proved over the
// complete current call census. Positions that may receive objects have no primitive alternatives;
// their actual allocation and allowed uses are checked independently. The
// captured Map may retain source leaves as keys or payloads; their own fields
// contain only scalars. Explicit DOM inputs require the narrower outer-key proof.
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
    // Exact external entry input, never an owning source allocation. Distinct
    // inputs may denote the same element; only identity with itself is known.
    mlir::BlockArgument element{};
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
    // Object-capable formals used only as direct keys of this captured outer
    // Map. Recomputed over the complete family; outer snapshots withhold all
    // roles. This describes uses, not actual origins, distinct identities or
    // permission to retain a borrowed DOM input. Caller field/payload uses may
    // exclude an allocation below without changing its callee's formal role.
    std::vector<mlir::BlockArgument> outerKeyParameters{};
    // Caller allocations used only as outer Map keys across every family call.
    // Payloads, child keys, parameter transport and outer key snapshots exclude
    // an allocation. This is source-use evidence, not DOM provenance or permission
    // to borrow an external object; objectKeys retains its ordinary owning contract.
    std::vector<ctjs::CreateObjectOp> outerKeyObjects{};
    // Explicit DOM entry inputs used exclusively through the outer-key formals
    // above. Complete source/use evidence only; no retained lifetime authority.
    std::vector<mlir::BlockArgument> outerKeyInputs{};
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

    // Exact inert source declaration, available only after complete live proof.
    [[nodiscard]] ctjs::FuncOp wrapper() const { return checkedWrapper; }
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
    ctjs::FuncOp checkedWrapper;
    std::vector<HostSlotReport> reports;
    std::vector<ctjs::StoreGlobalOp> observed;
    std::vector<HostCallableEdge> checkedCalls;
    std::vector<HostScalarGlobalRead> checkedScalarReads;
    std::vector<HostObjectGlobalRead> checkedObjectReads;
    unsigned workSteps = 0;
    bool budgetExhausted = false;
};

} // namespace ctcompile::ctnative
