//===- HostContract.h - Embedding promises for native code ------*- C++ -*-===//
//
// A host contract is a JSON manifest in which the embedding (the program that
// runs the compiled script) promises facts about the script's starting
// environment: which globals exist, which standard bindings are unmodified,
// and how the entry function is called. HostContractAnalysis and
// DOMEntryAnalysis then check, against the current module, that the source
// never breaks those promises, and record what they proved.
//
// Vocabulary used below:
//  - proof: a fact checked against the current module's IR. Proofs are never
//    read back from printed attributes; changing the module invalidates them.
//  - census: an enumeration of every use of a value in the current module,
//    so a fact holds for all uses, not only the ones a pattern matched.
//  - reprove: run the analysis again on a rewritten (normalized) clone.
//  - family: one captured Map plus every closure that shares its slot.
//
//===----------------------------------------------------------------------===//

#pragma once

#include "ctcompile/CTJS/IR/CTJSOps.h"
#include "ctcompile/CTNative/Analysis/PrimitiveAlternatives.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/IRMapping.h"
#include "mlir/Support/TypeID.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/ErrorHandling.h"

#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace ctcompile::ctnative {

/// A manifest `roots` entry: the global `binding` and the property path under
/// it that the host reads after the entry returns.
struct HostRootRequest {
    std::string binding;
    std::vector<std::string> properties;
};

/// The primitive category a native caller passes for one explicit parameter
/// of a ctbrowser-intrinsics-v1 entry.
enum class HostIntrinsicParameter {
    boolean,
    number,
    string,
    symbol
};

/// The host contract a driver reads from a manifest: what the embedding
/// promises about the program's initial globals and entry point. It is input
/// to the analysis, never an IR proof annotation. closed-source-v1, for
/// example, promises ordinary own global bindings and unmodified intrinsic
/// prototypes; every later mutation or call in the source is still checked by
/// the analyses below.
struct HostContract {
    /// Who embeds the program; the manifest spells it as hostProviderName().
    enum class Provider {
        /// closed-source-v1: all code comes from the supplied source.
        closedSource,
        /// The closed-source proof plus a captured method table whose
        /// generated methods stay attached to their non-movable owner. It does
        /// not allow DOM keys or callbacks retained outside the program.
        closedSourceSession,
        /// ctbrowser-dom-v1: isolated initial Object/String/RegExp prototype
        /// chains and a reserved regexp-literal factory. The source proof rules
        /// out mutating them and reentry from external scripts.
        ctbrowserDOM,
        /// ctbrowser-dom-session-v1: the same synchronous proof, plus a
        /// non-movable native owner of the atom table, the document and (when
        /// needed) the selector engine. Retaining data and browser callbacks
        /// still need separate proofs.
        ctbrowserDOMSession,
        /// ctbrowser-dom-data-session-v1: explicit DOM inputs may be retained
        /// only as outer keys of the Data Map. The proof establishes where the
        /// elements came from, not how long the document lives, so native
        /// ownership refuses until storage is confined to that owner.
        ctbrowserDOMDataSession,
        /// ctbrowser-intrinsics-v1: a named native export in an isolated
        /// standard realm, whose native caller fixes each parameter's primitive
        /// category. Symbol, optional String and undefined are fixed. The
        /// source may use primitive operations and proved Symbol/String calls,
        /// but no browser inputs, arbitrary calls, global publication or
        /// external script reentry.
        ctbrowserIntrinsics
    };
    Provider provider = Provider::closedSource;
    /// hostContractFingerprint() of the module the manifest was written for; a
    /// mismatch means the manifest is stale.
    std::string moduleSha256;
    /// Name of the entry function the embedding calls.
    std::string entry;
    /// Indices of the explicit JS parameters (not counting the three implicit
    /// ones) that receive borrowed DOM elements. DOM providers call the entry
    /// once, synchronously; the document owns each node and must outlive the
    /// call (ctbrowser-dom-session-v1 generates that non-movable owner). An
    /// element's identity includes its document, not just its node_id. For
    /// ctbrowserDOMDataSession these elements may only be used as outer Map
    /// keys; this field does not authorize retaining them in native storage.
    std::vector<unsigned> elementParameters;
    /// DOM entry and session providers only: the parameter whose owning
    /// document the source's `document` binding names, with the original root
    /// and query access and live style.
    std::optional<unsigned> currentDocumentParameter;
    /// The element parameters whose namespace is HTML or SVG (checked at native
    /// entry), so `dataset` is available. Other namespaces would need namespace
    /// URI ownership first.
    std::vector<unsigned> datasetParameters;
    /// ctbrowser-intrinsics-v1 only: one exact category per explicit parameter.
    /// The values stay run-time inputs; nothing is promised about identity,
    /// description or truthiness.
    std::vector<HostIntrinsicParameter> intrinsicParameters;
    /// Global objects and properties the host reads after the entry returns.
    std::vector<HostRootRequest> roots;
    /// Global names the host observes after the entry returns.
    std::vector<std::string> observations;
    /// Globals that do not exist. Absence can answer `typeof`, but does not
    /// justify a bare identifier read.
    std::vector<std::string> absentBindings;
    /// Globals that exist and hold `undefined`.
    std::vector<std::string> undefinedBindings;
    /// Global names the embedding promises are the original, unmodified
    /// standard bindings. This is an identity promise, not an effect summary;
    /// source replacement, escape and external script reentry are still
    /// checked and refuse the proof.
    ///
    /// Closed-source providers accept Map, Array and the class helper. DOM
    /// entry providers accept:
    ///  - Object, Number, decodeURIComponent, JSON, Array and String as
    ///    standard own global data bindings, including Number.prototype.toString,
    ///    Object.keys, JSON.parse, Array.prototype.filter,
    ///    String.prototype.startsWith and the default Array constructor and
    ///    species chain;
    ///  - Array: also the original Array.prototype.concat with no spreadability
    ///    hooks on the fresh empty receiver, and the original Array iteration
    ///    (Symbol.iterator/values and the iterator prototype chain, with no
    ///    custom next or return hooks);
    ///  - Element: inert query-result wrappers and their whole prototype chains
    ///    with no Symbol.isConcatSpreadable hooks, the original own prototype
    ///    and querySelector/querySelectorAll. Together with the Array promises
    ///    this allows confined spread/concat lengths and bounded element reads
    ///    without exceeding the proxy materialization limit;
    ///  - Function: those methods' original prototype chains and
    ///    Function.prototype.call with no own call shadows or accessors, and,
    ///    for ordinary class `instanceof`, its original prototype and inherited
    ///    Symbol.hasInstance;
    ///  - Symbol: the standard constructor, well-known keys and
    ///    Symbol.prototype toString/valueOf, with no replacement or accessor on
    ///    those lookup paths.
    ///
    /// DOM iteration also requires the original __ctbrowser_for_of_open,
    /// __ctbrowser_iter_next and __ctbrowser_iter_close source helpers.
    /// Computed custom methods also declare __ctbrowser_to_property_key and
    /// __ctbrowser_set_function_name; they are recognized only with the exact
    /// well-known Symbol key and an unobserved private closure, so key
    /// conversion keeps that Symbol and naming changes only the closure's own
    /// name. Neither allows arbitrary conversions or observable renaming.
    ///
    /// Compiler source helpers a bare context installs may also be declared:
    /// __ctbrowser_define_own (4 arguments), __ctbrowser_define_accessor (4),
    /// __ctbrowser_in_object (1) and __ctbrowser_require_object (1). Each stays
    /// trusted only as a direct call with an undefined receiver and exactly that
    /// arity; reflecting or storing the binding refuses.
    std::vector<std::string> initialIntrinsics;
    /// A load of `globalThis` names the realm's global object.
    bool realmGlobalThis = false;
    /// The embedding invokes this entry as a classic script with a stable realm
    /// receiver. This says nothing about an ordinary call's `this`.
    bool classicScriptRealm = false;
    /// Realm properties that start as writable own data slots: reading or
    /// writing them cannot call JS, but their values stay unknown.
    std::vector<std::string> realmOwnDataProperties;
};

/// Parses a host-contract manifest (JSON) and validates its fields.
llvm::Expected<HostContract> parseHostContract(llvm::StringRef json);
/// Reads and parses the manifest at `path`. An unreadable file fails with the
/// message `unreadable` followed by the system's reason.
llvm::Expected<HostContract> readHostContract(llvm::StringRef path, llvm::StringRef unreadable);
/// The manifest spelling of `provider`, such as "closed-source-v1".
llvm::StringRef hostProviderName(HostContract::Provider provider);
/// A SHA-256 fingerprint of the whole driver and program IR. Locations and
/// earlier host-contract reports are excluded; every semantic operation is
/// included.
std::string hostContractFingerprint(mlir::ModuleOp module);
/// Removes the report attributes earlier host-contract analyses printed.
void clearHostContractReports(mlir::ModuleOp module);
/// Removes every attribute of `op` whose name starts with `prefix`: the
/// analyses' reports and proof markers, which a clone must never inherit.
void removeAttrsWithPrefix(mlir::Operation * op, llvm::StringRef prefix);
/// Inlines local helper calls in `entry`, within `maxSteps` of work. Use only
/// on a private clone whose fingerprint was checked; the caller must reprove
/// the typed DOM or intrinsic entry afterwards.
llvm::Error expandDOMHelpers(mlir::ModuleOp candidate, llvm::StringRef entry, unsigned maxSteps,
                             unsigned * workSteps = nullptr,
                             llvm::ArrayRef<mlir::Value> inactiveFillers = {});
/// Folds truthiness and `=== undefined` guards on the entry's element
/// parameters, which are never null, to constants. Only the parameters' own
/// SSA values count; nullable `closest` results, cells and joins do not.
/// Private candidate only; reprove afterwards.
llvm::Error normalizeDOMElementGuards(mlir::ModuleOp candidate, const HostContract & contract,
                                      unsigned maxSteps);
/// Rewrites for-of loops over DOM query snapshots (__ctbrowser_for_of_open
/// and its helpers) into indexed loops. Private candidate only; reprove
/// afterwards, since the DOM proof must still reject writes, reentry, escaped
/// snapshots and unknown calls.
llvm::Error normalizeDOMIteration(mlir::ModuleOp candidate, const HostContract & contract,
                                  unsigned maxSteps);
/// Rewrites confined custom iteration protocols into ordinary calls and
/// scalar completion state. Only a private candidate may change; the helper
/// and DOM proofs are run again afterwards. `inactiveFillers`, when given,
/// receives placeholder values that belong to this exact candidate (never to
/// source attributes); pass it straight to expandDOMHelpers() without cloning
/// the module in between.
llvm::Error normalizeDOMCustomIteration(mlir::ModuleOp candidate, const HostContract & contract,
                                        unsigned maxSteps,
                                        std::vector<mlir::Value> * inactiveFillers = nullptr);
/// Replaces one original iterator-close suppression handler with an invoke
/// whose two close outcomes both resume the same saved throw. Returns false
/// when there is no such handler (including when a catch observes the error);
/// false and failure both leave the candidate untouched. This is a structural
/// rewrite, not permission to inline the close or emit the remaining abrupt
/// completion. Use only on a private candidate: the initial lookup moves out of
/// the suppressed region, so the prefix, global, reentry and DOM proofs must
/// all run again before the result is used.
llvm::Expected<bool> normalizeDOMIteratorClose(mlir::ModuleOp candidate,
                                               const HostContract & contract,
                                               unsigned maxSteps = 100000);

/// Replaces confined spreads of query results, and concatenations onto an
/// empty array, with their lengths. Run on a private clone and reprove; a
/// refusal leaves the original module unchanged.
llvm::Error normalizeDOMSnapshotLengths(mlir::ModuleOp candidate, const HostContract & contract,
                                        unsigned maxSteps, mlir::IRMapping * mapping = nullptr,
                                        unsigned * workSteps = nullptr);

/// A browser or standard-library method that DOMEntryAnalysis recognizes and
/// lowers to a typed native call.
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
    querySelectorAll,
    documentQuerySelector,
    documentQuerySelectorAll,
    number,
    numberToString,
    symbol,
    symbolToString,
    symbolValueOf,
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

/// What a HostDOMMethod call produces.
enum class HostDOMResult {
    none,
    boolean,
    optionalString,
    element,
    elementVector,
    stringVector,
    number,
    symbol,
    json,
    string
};

/// What a call of `method` produces. Every method is listed, so a new one is a
/// -Wswitch error rather than a silent Boolean.
[[nodiscard]] inline HostDOMResult resultOf(HostDOMMethod method) {
    switch (method) {
    case HostDOMMethod::addClass:
    case HostDOMMethod::removeClass:
    case HostDOMMethod::setAttribute:
    case HostDOMMethod::removeAttribute: return HostDOMResult::none;
    case HostDOMMethod::toggleClass:
    case HostDOMMethod::containsClass:
    case HostDOMMethod::toggleAttribute:
    case HostDOMMethod::hasAttribute:
    case HostDOMMethod::contains:
    case HostDOMMethod::matches:
    case HostDOMMethod::startsWith: return HostDOMResult::boolean;
    case HostDOMMethod::getAttribute: return HostDOMResult::optionalString;
    case HostDOMMethod::closest:
    case HostDOMMethod::querySelector:
    case HostDOMMethod::documentQuerySelector: return HostDOMResult::element;
    case HostDOMMethod::querySelectorAll:
    case HostDOMMethod::documentQuerySelectorAll: return HostDOMResult::elementVector;
    case HostDOMMethod::datasetKeys:
    case HostDOMMethod::filterStrings: return HostDOMResult::stringVector;
    case HostDOMMethod::number: return HostDOMResult::number;
    case HostDOMMethod::symbol:
    case HostDOMMethod::symbolValueOf: return HostDOMResult::symbol;
    case HostDOMMethod::jsonParse: return HostDOMResult::json;
    case HostDOMMethod::numberToString:
    case HostDOMMethod::symbolToString:
    case HostDOMMethod::decodeURIComponent:
    case HostDOMMethod::removeStringPrefix:
    case HostDOMMethod::replaceUppercase:
    case HostDOMMethod::stringCharAt:
    case HostDOMMethod::stringSlice:
    case HostDOMMethod::stringLowercaseUnit: return HostDOMResult::string;
    }
    llvm_unreachable("unknown HostDOMMethod");
}

/// One proved call of a HostDOMMethod in the entry.
struct HostDOMCall {
    ctjs::CallOp operation;
    HostDOMMethod kind;
    /// The browser receiver, or the original scalar input for number and
    /// numberToString.
    mlir::Value element;
    ctjs::FuncOp callback{};
    /// True when Function.prototype.call supplies the receiver as argument zero.
    bool explicitReceiver = false;
    /// The single original document/style input, once the analysis has proved
    /// how it reaches this call.
    mlir::BlockArgument styleParameter{};
    /// When several inputs can reach the call, each keeps its association
    /// through the same SSA edges.
    llvm::SmallVector<mlir::BlockArgument> styleParameters{};
    [[nodiscard]] HostDOMResult result() const { return resultOf(kind); }
    [[nodiscard]] bool returnsElement() const { return result() == HostDOMResult::element; }
    [[nodiscard]] bool returnsOptionalString() const {
        return result() == HostDOMResult::optionalString;
    }
    [[nodiscard]] bool returnsStringVector() const {
        return result() == HostDOMResult::stringVector;
    }
    [[nodiscard]] bool returnsElementVector() const {
        return result() == HostDOMResult::elementVector;
    }
    [[nodiscard]] bool returnsNumber() const { return result() == HostDOMResult::number; }
    [[nodiscard]] bool returnsSymbol() const { return result() == HostDOMResult::symbol; }
    /// An owning ctbrowser::json_value tree, from the shared public Core parser.
    [[nodiscard]] bool returnsJSON() const { return result() == HostDOMResult::json; }
    [[nodiscard]] bool returnsString() const { return result() == HostDOMResult::string; }
    [[nodiscard]] bool returnsBoolean() const { return result() == HostDOMResult::boolean; }
    [[nodiscard]] bool usesStyle() const {
        return kind == HostDOMMethod::matches || returnsElement() || returnsElementVector();
    }
};

/// One operand position that reads a refined string.
struct HostDOMStringUse {
    mlir::Operation * operation;
    unsigned operandIndex;
};

/// An optional string that is known to be a String inside one branch (`block`).
/// The uses listed keep their source identities; the producer and the other
/// branch still see it as optional.
struct HostDOMStringRefinement {
    mlir::Block * block;
    mlir::Value optional;
    std::vector<HostDOMStringUse> uses;
};

/// Placeholder values for the structural yield at the end of a branch proved
/// never to return.
struct HostDOMUnreachableYield {
    mlir::Operation * operation;
    std::vector<mlir::Attribute> values;
};

/// Proves that one synchronous DOM entry can be compiled with typed browser
/// calls, and records the evidence. Only the checked source-declaration wrapper
/// may be dropped. The proof allows no source invocation, retained handle,
/// observation of the receiver or captures, or arbitrary property dispatch.
/// Mutating the module invalidates the result; printed attributes are ignored.
class DOMEntryAnalysis {
public:
    /// Everything the proof establishes, published whole once every check passes.
    struct Evidence {
        ctjs::FuncOp entry, wrapper;
        bool undefinedReturn = false;
        std::vector<ctjs::FuncOp> callbacks;
        std::vector<std::pair<ctjs::CreateClosureOp, ctjs::FuncOp>> closures;
        std::vector<mlir::BlockArgument> elements, datasetElements;
        llvm::DenseSet<mlir::Value> elementIdentities;
        llvm::DenseMap<mlir::Value, HostIntrinsicParameter> intrinsicParameters;
        std::vector<ctjs::GetPropertyOp> tokenLists, datasets;
        std::vector<ctjs::GetPropertyOp> stringVectorLengths, stringVectorIndices;
        llvm::DenseSet<ctjs::GetPropertyOp> elementVectorLengths, elementVectorIndices;
        llvm::DenseSet<ctjs::GetPropertyOp> elementPrototypes, documentRoots, symbolDescriptions;
        mlir::BlockArgument document;
        std::vector<mlir::Value> styles;
        /// Loads of the initial intrinsics, for every supported name.
        llvm::DenseSet<ctjs::LoadGlobalOp> documentLoads, intrinsics;
        llvm::DenseMap<ctjs::GetPropertyOp, llvm::StringRef> symbols;
        llvm::DenseMap<ctjs::GetPropertyOp, mlir::Value> datasetValues;
        std::vector<ctjs::CallOp> stringPrefixRegExps;
        std::vector<ctjs::InvokeOp> invocations;
        std::vector<ctjs::ThrowOp> throws;
        std::vector<HostDOMUnreachableYield> throwYields;
        std::vector<mlir::Value> optionalStrings, strings;
        std::vector<HostDOMStringRefinement> refinements;
        std::vector<std::pair<mlir::Value, bool>> booleans;
        llvm::DenseMap<ctjs::GetPropertyOp, HostDOMMethod> methods;
        std::vector<HostDOMCall> calls;
        std::vector<ctjs::CreateObjectOp> jsonObjects;
        /// Scalar-field records a local Map stores; they keep their identity.
        std::vector<ctjs::CreateObjectOp> records;
        std::vector<ctjs::CopyPropsOp> jsonCopies;
        std::vector<ctjs::SetPropertyOp> jsonAssignments, snapshotAssignments;
    };

    /// `inputFingerprint`, when given, is the hostContractFingerprint() an
    /// earlier analysis took of the module as it arrived (fingerprint()), and
    /// is compared with the contract instead of hashing `module` again: the
    /// lowering's own passes annotate the module between its first analysis
    /// and the admission-time one, and the fingerprint states which input the
    /// contract was written for. Every other check runs on `module` as it is.
    DOMEntryAnalysis(mlir::ModuleOp module, const HostContract & contract,
                     unsigned maxSteps = 100000, llvm::StringRef inputFingerprint = {});
    [[nodiscard]] bool proved() const { return refusal.empty(); }
    [[nodiscard]] llvm::StringRef reason() const { return refusal; }
    [[nodiscard]] unsigned steps() const { return workSteps; }
    [[nodiscard]] bool exhausted() const { return budgetExhausted; }
    /// The fingerprint this analysis compared with the contract; empty when it
    /// refused before reaching that check.
    [[nodiscard]] llvm::StringRef fingerprint() const { return moduleFingerprint; }
    [[nodiscard]] ctjs::FuncOp entry() const { return proof.entry; }
    [[nodiscard]] bool returnsUndefined() const { return proof.undefinedReturn; }
    [[nodiscard]] ctjs::FuncOp wrapper() const { return proof.wrapper; }
    [[nodiscard]] llvm::ArrayRef<ctjs::FuncOp> callbacks() const { return proof.callbacks; }
    [[nodiscard]] ctjs::FuncOp callback(ctjs::CreateClosureOp closure) const;
    [[nodiscard]] bool isCallbackParameter(mlir::Value value) const;
    [[nodiscard]] std::optional<HostIntrinsicParameter> intrinsicParameter(mlir::Value value) const;
    [[nodiscard]] llvm::ArrayRef<mlir::BlockArgument> parameters() const { return proof.elements; }
    [[nodiscard]] bool isElement(mlir::Value value) const;
    /// Validated element parameters or nullable `closest` results, usable only
    /// in equality tests.
    [[nodiscard]] bool isElementIdentity(mlir::Value value) const;
    [[nodiscard]] bool isTokenList(mlir::Value value) const;
    [[nodiscard]] bool isDataset(mlir::Value value) const;
    [[nodiscard]] bool isDatasetElement(mlir::Value value) const;
    [[nodiscard]] bool isElementPrototype(ctjs::GetPropertyOp read) const;
    [[nodiscard]] bool isCurrentDocument(ctjs::LoadGlobalOp load) const;
    [[nodiscard]] bool isDocumentElement(ctjs::GetPropertyOp read) const;
    [[nodiscard]] mlir::BlockArgument documentParameter() const { return proof.document; }
    [[nodiscard]] llvm::ArrayRef<mlir::Value> styleValues() const { return proof.styles; }
    [[nodiscard]] bool isStringVectorLength(ctjs::GetPropertyOp read) const;
    [[nodiscard]] bool isStringVectorIndex(ctjs::GetPropertyOp read) const;
    [[nodiscard]] bool isElementVectorLength(ctjs::GetPropertyOp read) const;
    [[nodiscard]] bool isElementVectorIndex(ctjs::GetPropertyOp read) const;
    /// The element whose `dataset` `read` reads, when the member is present in
    /// that element's immutable key snapshot; null otherwise.
    [[nodiscard]] mlir::Value datasetValueElement(ctjs::GetPropertyOp read) const;
    [[nodiscard]] bool isStringPrefixRegExp(ctjs::CallOp call) const;
    [[nodiscard]] bool isNumberIntrinsic(ctjs::LoadGlobalOp load) const;
    [[nodiscard]] bool isInitialIntrinsic(ctjs::LoadGlobalOp load) const;
    [[nodiscard]] llvm::StringRef wellKnownSymbol(ctjs::GetPropertyOp read) const;
    [[nodiscard]] bool isSymbolDescription(ctjs::GetPropertyOp read) const;
    /// True for a decodeURIComponent or JSON.parse invoke whose continuations
    /// own a String or json_value and never use the implicit error payload. A
    /// parse invoke may nest in the decode's success continuation.
    [[nodiscard]] bool invocation(ctjs::InvokeOp operation) const;
    /// Throws whose payload is an owned Number, Boolean or String, in regions
    /// proved never to return.
    [[nodiscard]] llvm::ArrayRef<ctjs::ThrowOp> savedThrows() const { return proof.throws; }
    [[nodiscard]] llvm::ArrayRef<HostDOMUnreachableYield> unreachableYields() const {
        return proof.throwYields;
    }
    [[nodiscard]] llvm::ArrayRef<mlir::Value> optionalStringJoins() const {
        return proof.optionalStrings;
    }
    [[nodiscard]] llvm::ArrayRef<HostDOMStringRefinement> stringRefinements() const {
        return proof.refinements;
    }
    /// Branch and invoke results that are a String on every path, regardless of
    /// the producer's own type. This never narrows a getAttribute result.
    [[nodiscard]] llvm::ArrayRef<mlir::Value> stringResults() const { return proof.strings; }
    /// Tests against `undefined` (and their truth conversions) whose outcome is
    /// constant once every branch is proved, with that outcome.
    [[nodiscard]] llvm::ArrayRef<std::pair<mlir::Value, bool>> constantBooleans() const {
        return proof.booleans;
    }
    [[nodiscard]] std::optional<HostDOMMethod> method(ctjs::GetPropertyOp read) const;
    [[nodiscard]] const HostDOMCall * call(ctjs::CallOp operation) const;
    /// Fresh owned data objects and guarded spreads, once every use has been
    /// checked for observable aliases and mutation after a value copy.
    [[nodiscard]] bool jsonObject(ctjs::CreateObjectOp operation) const;
    [[nodiscard]] bool jsonCopy(ctjs::CopyPropsOp operation) const;
    [[nodiscard]] bool jsonAssignment(ctjs::SetPropertyOp operation) const;
    /// The only writer of unique direct snapshot keys; readers see only the
    /// final own data.
    [[nodiscard]] bool jsonSnapshotAssignment(ctjs::SetPropertyOp operation) const;
    /// A fresh scalar-field record stored as a local Map payload.
    [[nodiscard]] bool recordObject(ctjs::CreateObjectOp operation) const;
    /// True for typed browser calls and invokes and for reads of initial
    /// intrinsics and the document: none of them rebinds a standard
    /// constructor, writes a prototype or reenters script.
    [[nodiscard]] bool typedOperation(mlir::Operation * operation) const;

private:
    std::string refusal;
    Evidence proof;
    unsigned workSteps = 0;
    bool budgetExhausted = false;
    std::string moduleFingerprint;
};

/// A write to a host slot and a read that is proved to see that write.
struct HostSlotEdge {
    ctjs::SetPropertyOp write;
    ctjs::GetPropertyOp read;
};

/// What each parameter of `function` can receive, over every call in the
/// current module: a finite set of primitive kinds, or caller objects. A
/// position that may receive objects has no primitive alternatives; the
/// actual allocations and their uses are checked separately. The captured Map
/// may keep those source objects ("leaves", whose own fields hold only
/// scalars) as keys or payloads. Explicit DOM inputs need the narrower
/// outer-key proof (see HostCapturedMapFamily::outerKeyInputs).
struct HostMethodParameters {
    ctjs::FuncOp function;
    std::vector<PrimitiveAlternatives> alternatives;
    std::vector<mlir::BlockArgument> objectKeys{};
    bool operator==(const HostMethodParameters &) const = default;
};

/// One actual argument of a call into a captured method.
struct HostMethodArgument {
    mlir::BlockArgument parameter;
    mlir::Value actual;
    PrimitiveAlternatives alternatives;
    ctjs::CreateObjectOp object{};
    /// An external entry input, never an owned source allocation. Two distinct
    /// inputs may be the same element; only identity with itself is known.
    mlir::BlockArgument element{};
};

/// A key every child Map holds, and the primitive kinds of its value.
struct HostChildMapEntry {
    mlir::Value key;
    PrimitiveAlternatives alternatives;
    bool operator==(const HostChildMapEntry &) const = default;
};

/// A source global that holds a Number from its initialization through every
/// later write. The values and effects still happen at run time, across
/// future invocations.
struct HostMutableScalarGlobal {
    ctjs::StoreGlobalOp initialization;
    std::vector<ctjs::StoreGlobalOp> writes;
    std::vector<ctjs::LoadGlobalOp> reads;
    bool operator==(const HostMutableScalarGlobal &) const = default;
};

/// A callback stored in a fixed own slot whose body only touches scalars, on
/// every path. It cannot reach the captured Map or its caller's objects, and
/// its calls are distinct from exported methods and pure snapshot operations.
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
    bool operator==(const HostScalarCallback &) const = default;
};

/// The caller allocation one checked entry call returns. This narrows neither
/// the reusable method's result type nor any other call.
struct HostReturnedLeaf {
    mlir::Operation * call = nullptr;
    ctjs::CreateObjectOp object;
    bool operator==(const HostReturnedLeaf &) const = default;
};

/// The primitive kinds one checked entry call returns, independent of the
/// reusable method's result and parameter kinds.
struct HostReturnedScalar {
    mlir::Operation * call = nullptr;
    PrimitiveAlternatives alternatives;
    bool operator==(const HostReturnedScalar &) const = default;
};

/// Every fact about a captured Map family except the actual call's own
/// argument, so two call edges into one family compare equal.
///
/// The family is one standard Map, constructed empty and held in one
/// immutable closure slot, plus every closure sharing that slot. Checking all
/// their uses allows only: primitive contents; fresh method-local leaves with
/// fixed scalar fields, or checked scalar-field leaves from the caller; fresh
/// child Maps (which cannot hold Maps); the standard
/// size/set/get/has/delete/clear calls; and confined immediate key snapshots.
/// Caller parameters may only be used as keys or payloads, and method-local
/// leaves cannot be keys. Only checked owned leaves may leave a method through
/// its result; fields and unchecked uses cannot publish objects. The effects
/// still happen at run time: the family promises no startup value or call
/// result. The optional cell operations describe the original binding; after
/// lambda lifting the call reads its environment value instead. Every handle
/// is recomputed from the current module, never read from native attributes.
struct HostCapturedMapFamily {
    ctjs::LoadGlobalOp intrinsic;
    ctjs::ConstructOp allocation;
    ctjs::CreateCellOp cell;
    ctjs::CellSetOp initialization;
    std::vector<ctjs::CreateClosureOp> closures;
    std::vector<HostMethodParameters> parameters;
    // Effects of the whole family; HostCapturedMap::argument belongs to the
    // actual call.
    std::vector<ctjs::LoadUpvalueOp> upvalues;
    std::vector<ctjs::GetPropertyOp> reads;
    std::vector<ctjs::CallOp> calls;
    std::vector<ctjs::CreateObjectOp> leafObjects;
    std::vector<ctjs::SetPropertyOp> leafWrites;
    std::vector<ctjs::GetPropertyOp> leafReads;
    std::vector<ctjs::ConstructOp> childMaps{};
    /// Every `set` on the outer Map, across the family, stores a checked fresh
    /// child Map. Says nothing about a returned child's identity or contents.
    bool childMapContents = false;
    std::vector<mlir::Value> returnedChildMaps{};
    /// Entries set before every publication of a child and kept by every child
    /// write. Says nothing about identity, other keys, size or startup values.
    std::vector<HostChildMapEntry> childEntries{};
    /// The scalar kinds of every child write, whatever the key. Publishing an
    /// empty child, delete and clear keep it; reads may still be Undefined.
    PrimitiveAlternatives childScalarContents{};
    /// Every child write is a supported scalar or a checked scalar-field leaf.
    /// Reads may be Undefined; this gives no primitive, field or identity facts.
    bool childLeafContents = false;
    /// Every insertion, in every sibling method, uses a String key (into the
    /// outer Map, or into a child Map). Neither implies a non-empty snapshot or
    /// allows converting object keys.
    bool outerStringKeys = false;
    bool childStringKeys = false;
    /// Confined immediate key copies and read-only length/index reads. Element
    /// reads allow equality tests; with String keys, checked Concat too.
    std::vector<mlir::Operation *> snapshotOperations{};
    std::vector<HostScalarCallback> scalarCallbacks{};
    std::vector<HostReturnedLeaf> returnedLeaves{};
    std::vector<HostReturnedScalar> returnedScalars{};
    /// Parameters that may receive objects and are used only as direct keys of
    /// the outer Map, across the whole family; a snapshot of the outer keys
    /// disqualifies them. This describes uses, not where the objects come from,
    /// whether they are distinct, or permission to retain a borrowed DOM input.
    /// A caller's field or payload use can exclude an allocation from
    /// outerKeyObjects without changing the callee parameter's role here.
    std::vector<mlir::BlockArgument> outerKeyParameters{};
    /// Caller allocations used only as outer Map keys across every family call.
    /// Use as a payload, a child key, an argument passed on, or in an outer key
    /// snapshot excludes an allocation. This says how the source uses it, not
    /// where a DOM object came from or that an external object may be
    /// borrowed; HostMethodParameters::objectKeys keeps its owning meaning.
    std::vector<ctjs::CreateObjectOp> outerKeyObjects{};
    /// Explicit DOM entry inputs used only through outerKeyParameters. This is
    /// evidence about uses only; it does not allow retaining the inputs.
    std::vector<mlir::BlockArgument> outerKeyInputs{};
    bool operator==(const HostCapturedMapFamily &) const = default;
};

/// A captured Map family as seen from one actual call.
struct HostCapturedMap : HostCapturedMapFamily {
    /// This call's argument, which differs between edges of one family.
    ctjs::LoadUpvalueOp argument;
};

/// A call of a source closure read from a host slot. This is evidence about
/// this call only, not a promise about future exported callers or private
/// visibility: the slot's current own-data initializer stores exactly this
/// closure. The original receiver and arguments are still evaluated at run
/// time.
struct HostCallableEdge {
    mlir::Operation * call = nullptr;
    ctjs::GetPropertyOp read;
    ctjs::SetPropertyOp write;
    ctjs::CreateClosureOp closure;
    ctjs::FuncOp function;
    std::optional<HostCapturedMap> capturedMap;
    std::vector<HostMethodArgument> arguments;
};

/// A read of an ordinary source global proved to hold a Number, Boolean or
/// String. Each read has exactly one earlier store in the entry.
/// `dependencies` lists the published results and owned field reads met while
/// walking the stored value's expression, in traversal order (possibly
/// repeated); it is empty for an expression built only from constants, which
/// still needs its scope, initialization and environment proofs. These are
/// run-time source edges, not constant values or a native type promise.
struct HostScalarGlobalRead {
    ctjs::StoreGlobalOp initialization;
    ctjs::LoadGlobalOp read;
    mlir::Value value;
    PrimitiveAlternatives alternatives;
    std::vector<mlir::Value> dependencies;
};

/// A read of an ordinary entry global that holds a fresh scalar-field object,
/// stored once, or an alias of one reached through an acyclic chain of such
/// globals. `initialization` is the store to this read's own binding;
/// `object` is the original allocation. Every read follows its store, and
/// every use is a checked initialization, a scalar own-field operation, a
/// strict identity test, or a key or payload argument into the captured-Map
/// family. The loads and stores still happen at run time.
struct HostObjectGlobalRead {
    ctjs::StoreGlobalOp initialization;
    ctjs::LoadGlobalOp read;
    ctjs::CreateObjectOp object;
};

/// The entry-local constructors and Maps, and the operations on them. Every
/// use of every record and Map alias was checked; field kinds alone do not
/// give a native lifetime. A store of an unknown field is still valid source
/// but makes primitiveFields false.
struct HostLocalRecords {
    llvm::SmallVector<ctjs::FuncOp> constructors;
    llvm::SmallVector<mlir::Operation *> operations;
    bool primitiveFields = false;
};

/// What the analysis found for one host slot (`binding`.`property`): its
/// owner, writes, reads and proved edges, or why it was refused in `reason`.
struct HostSlotReport {
    std::string binding;
    std::string property;
    ctjs::CreateObjectOp owner;
    std::vector<ctjs::SetPropertyOp> writes;
    std::vector<ctjs::GetPropertyOp> reads;
    std::vector<HostSlotEdge> edges;
    std::string reason;
};

/// Checks a closed-source host contract against `module` and records what it
/// proved. Slot reports are diagnostics and may hold partial evidence;
/// property() returns a proof only when every obligation of the contract
/// passed. Mutating the module invalidates this object, and no native pass
/// trusts the printed report attributes.
class HostContractAnalysis {
public:
    HostContractAnalysis(mlir::ModuleOp module, const HostContract & contract,
                         unsigned maxSteps = 100000);

    /// The inert source-declaration wrapper; null unless the whole proof passed.
    [[nodiscard]] ctjs::FuncOp wrapper() const { return checkedWrapper; }
    [[nodiscard]] bool proved() const { return refusal.empty(); }
    [[nodiscard]] llvm::StringRef reason() const { return refusal; }
    /// Work spent, for callers that share one budget across analyses.
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
    [[nodiscard]] const HostLocalRecords & localRecords() const { return checkedLocalRecords; }

private:
    std::string refusal;
    ctjs::FuncOp checkedWrapper;
    std::vector<HostSlotReport> reports;
    std::vector<ctjs::StoreGlobalOp> observed;
    std::vector<HostCallableEdge> checkedCalls;
    std::vector<HostScalarGlobalRead> checkedScalarReads;
    std::vector<HostObjectGlobalRead> checkedObjectReads;
    HostLocalRecords checkedLocalRecords;
    unsigned workSteps = 0;
    bool budgetExhausted = false;
};

} // namespace ctcompile::ctnative
