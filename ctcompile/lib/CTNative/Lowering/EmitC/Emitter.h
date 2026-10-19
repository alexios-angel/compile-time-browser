//===- Emitter.h - CTNative to EmitC lowering state ----------- -*- C++ -*-===//
//
// Declares `lowering`, the state shared by every file in EmitC/: it holds the
// analysis results, the census tables each emitter fills before rewriting, and
// the methods that turn admitted ctjs functions into emitc functions. Also
// declares the free helpers for building opaque calls.
//
//===----------------------------------------------------------------------===//
#pragma once

#include "../LoweringSupport.h"
#include "ctcompile/CTNative/Analysis/HostContract.h"

#include <map>
#include <set>

namespace ctcompile::ctnative {
class OwnedGlobalRoots;
}

namespace ctcompile::ctnative::lowering_detail {

inline constexpr llvm::StringLiteral kDOMOptionalStringType = "std::optional<std::string>";
inline constexpr llvm::StringLiteral kDOMElementViewVectorType =
    "std::vector<ctnative::js_element_t>";

/// Builds an emitc.call_opaque and records, in `ctnative.const_operands`, the
/// index of every operand that is not an lvalue.
///
/// Only for the compiler-owned runtime helpers: they accept non-lvalue
/// operands by value or const reference. Mutable binding arguments (vec_push's
/// vector, for example) must carry LValueType. This records a C++
/// const-acceptance contract, not a purity claim; Map handles may still mutate
/// their pointees. Arbitrary external C++ calls must use CallOpaqueOp without
/// this contract.
ec::CallOpaqueOp callWithConstValueOperands(mlir::OpBuilder & builder, mlir::Location where,
                                            mlir::TypeRange results, mlir::StringAttr callee,
                                            mlir::ValueRange operands);
/// A default-constructed C++ class that is neither copied nor moved, `indent` deep.
std::string nonmovable(llvm::StringRef name, llvm::StringRef indent);
/// A DOM session's invoke(): check each element's document, then call `entry`.
void sessionInvoke(llvm::raw_ostream & out, llvm::StringRef entry, size_t count,
                   llvm::StringRef extra);
/// The type SCF operand `index` flows into: a while's before argument, a for's
/// or condition's result, a yield's parent result or before argument. Null
/// otherwise.
mlir::Type edgeTarget(mlir::Operation * op, unsigned index);
/// The C++ type of a shared cell's handle holding a value spelled `value`:
/// `std::shared_ptr<ctnative::cell<value>>` (Runtime/Cell.hpp).
std::string sharedCellSpelling(llvm::StringRef value);
/// The spelling of the value a shared cell's handle type (or a variable of
/// one) holds, or empty for any other type.
std::string sharedCellValue(mlir::Type type);
/// `handle` as a value: loaded when it is the frame's variable.
mlir::Value loadedHandle(mlir::OpBuilder & builder, mlir::Location where, mlir::Value handle);
/// The single result of callWithConstValueOperands.
mlir::Value callValue(mlir::OpBuilder & builder, mlir::Location where, mlir::Type result,
                      const llvm::Twine & callee, mlir::ValueRange operands);
/// The single result of `receiver.name(operands...)`.
mlir::Value memberCall(mlir::OpBuilder & builder, mlir::Location where, mlir::Type result,
                       mlir::Value receiver, const llvm::Twine & name,
                       mlir::ValueRange operands = {});
/// The same where `receiver` may be a variable or other lvalue: the call is
/// emitted inside an expression, so it names the variable rather than a copy.
mlir::Value methodCall(mlir::OpBuilder & builder, mlir::Location where, mlir::Type result,
                       mlir::Value receiver, const llvm::Twine & name,
                       mlir::ValueRange operands = {});

/// The CTNative-to-EmitC lowering of one module. Censuses run first, while the
/// IR is still ctjs, and fill the tables below; lower() then rewrites each
/// accepted function and finish() adds the module-level declarations.
struct lowering {
    mlir::DataFlowSolver & solver;
    mlir::MLIRContext * context;
    mlir::ModuleOp module;
    /// UnaryPlusIsIdentity (LoweringSupport.cpp), frozen. Declared here so the
    /// member initialisation order matches the list below and -Wreorder stays
    /// quiet.
    mlir::FrozenRewritePatternSet declarative;
    lowering(mlir::DataFlowSolver & s, mlir::MLIRContext * c, mlir::ModuleOp m)
        : solver(s), context(c), module(m), declarative(declarativePatterns(c)) {}
    llvm::StringSet<> globals; ///< Scalar globals the emitted unit declares.
    /// An ordinary script (no host contract): its DOM elements are the
    /// runtime's views and its page reads call Runtime/Page.hpp.
    bool pageViews = false;
    /// Static cells (ClosureLifting/RunOnce.cpp): each namespace-scope variable
    /// a lowered function names, with its C++ type, declared by
    /// declareGlobals() in name order.
    std::map<std::string, mlir::Type> statics;
    /// Each run-once static's constant table, by the static's name: its
    /// namespace-scope struct and value (EmitC/StaticTables.cpp), declared by
    /// declareGlobals() when a lowered function names the static.
    std::map<std::string, std::string> staticTables;
    /// Each proved read of a table -> the field it reads, taken while its key
    /// is still a ctjs.constant.
    llvm::DenseMap<mlir::Operation *, std::string> staticTableReads;
    void censusStaticTables(llvm::ArrayRef<ctjs::FuncOp> accepted);
    bool replaceStaticTable(mlir::Operation * o);
    /// Instances of a class a registry Map owns, as `std::shared_ptr<C>`
    /// (EmitC/Instances.cpp): whether `v` is one, the `std::make_shared`
    /// results that stand for a `new`, and the lowering of the `new` and of
    /// an exact test against null or undefined.
    [[nodiscard]] bool isInstanceValue(mlir::Value v) const;
    llvm::DenseSet<mlir::Value> madeInstances;
    /// `std::shared_ptr<C>` for `instance<C>` or `opt<instance<C>>`; each
    /// spelling handed out is remembered, so a value an operation has already
    /// replaced is still known to be an instance by its C++ type.
    mlir::Type instanceType(mlir::Type type);
    llvm::StringSet<> instanceSpellings;
    /// The owner of `borrowed`, a method's `C *` to an instance a registry
    /// owns, through its qualified std::enable_shared_from_this base so JS
    /// fields cannot shadow the lookup. sharedFromThis records its class.
    mlir::Value ownerOf(mlir::OpBuilder & b, mlir::Location where, mlir::Value borrowed,
                        mlir::Type type);
    /// The function `call` runs: its ctjs.func, or the emitc.func lowered from
    /// it once that one is an emptied shell.
    [[nodiscard]] mlir::Operation * calleeOf(ctjs::CallDirectOp call) const;
    llvm::StringSet<> sharedFromThis;
    /// Adds every value of an identity class to the alias group of its
    /// struct, so the shape census records the member of each field access
    /// on a value read back from the Map (`groups` then points at
    /// instanceGroups, a copy of the receiver groups with these added).
    void joinInstanceValues(llvm::ArrayRef<ctjs::FuncOp> accepted);
    receiverGroups instanceGroups;
    bool replaceInstance(mlir::Operation * o);
    /// The receiver operand of `call` when the receiver is an instance: the
    /// owner itself for an identity class's constructor, else a `C *`
    /// borrowed with `owner.get()`, which is safe because the caller keeps its
    /// `std::shared_ptr` until the call returns.
    mlir::Value receiverOperand(mlir::OpBuilder & b, mlir::Location where, ctjs::CallDirectOp call);
    /// Whether `parameter`, an instance parameter of a ctjs.func, is only
    /// read through: field accesses, tests and method calls, never kept or
    /// passed on. It is then a `C *` borrowed from the caller's owner, which
    /// outlives the call.
    [[nodiscard]] bool borrowsInstance(mlir::Value parameter) const;
    /// Operand `index` of `call`, an instance: `owner.get()` for a borrowing
    /// parameter, the owner otherwise.
    mlir::Value instanceArgument(mlir::OpBuilder & b, mlir::Location where, ctjs::CallDirectOp call,
                                 unsigned index);
    /// The joined type of every source store to each global, computed before
    /// any value is retyped. Observation requests and host categories never
    /// choose a global's output type.
    llvm::StringMap<mlir::Type> globalTypes;
    llvm::DenseSet<mlir::Operation *> scalarStores;
    llvm::StringSet<> observations;
    bool explicitObservations = false;
    /// The test-only observe-globals option: main prints the observed globals
    /// before it returns. Without it a program prints only what the
    /// JavaScript prints.
    bool observeGlobals = false;
    /// Storage plan for one owning global, committed after whole-function
    /// admission while the source operations still exist. A plan only says how
    /// to emit the storage; it is not a reusable ownership proof.
    struct ownedGlobalStorage {
        std::string binding;
        std::string className;
        std::string field;
        mlir::Type type;
        mlir::Type fieldType;
    };
    llvm::SmallVector<ownedGlobalStorage> ownedGlobalStoragePlans;
    /// Each owning global's binding -> the index of its (last) plan.
    llvm::StringMap<unsigned> ownedGlobals;
    llvm::DenseMap<mlir::Value, mlir::Type> ownedObjectTypes;
    llvm::DenseMap<mlir::Operation *, unsigned> ownedGlobalOperations;
    void censusOwnedGlobals(const OwnedGlobalRoots & roots, llvm::ArrayRef<ctjs::FuncOp> accepted);
    bool replaceOwnedGlobal(mlir::Operation * operation);
    /// DOM emission plans, copied from DOMEntryAnalysis only after the whole
    /// entry was admitted.
    llvm::DenseMap<mlir::Operation *, HostDOMCall> domCalls;
    llvm::DenseSet<mlir::Operation *> domReads;
    mlir::BlockArgument domDocumentParameter;
    llvm::DenseSet<mlir::Operation *> domDocumentRoots;
    llvm::DenseMap<mlir::Operation *, llvm::StringRef> domSymbols;
    llvm::DenseSet<mlir::Operation *> domSymbolDescriptions;
    llvm::DenseSet<mlir::Value> domDatasetParameters;
    llvm::DenseSet<mlir::Operation *> domInvocations;
    llvm::DenseSet<mlir::Operation *> domThrows;
    llvm::DenseSet<mlir::Value> domUnusedPayloads;
    llvm::DenseSet<mlir::Operation *> domNulls;
    llvm::DenseSet<mlir::Value> domOptionalStrings;
    llvm::DenseSet<mlir::Value> domStringResults;
    llvm::DenseSet<mlir::Operation *> domStringVectorIndices;
    llvm::DenseSet<mlir::Operation *> domElementVectorIndices;
    llvm::DenseSet<mlir::Operation *> domSnapshotAssignments;
    llvm::DenseMap<mlir::Operation *, mlir::Value> domDatasetValues;
    std::vector<HostDOMStringRefinement> domStringRefinements;
    void prepareDOMStrings();
    llvm::DenseSet<mlir::Value> domParameters;
    llvm::SmallVector<mlir::BlockArgument> domStyleParameters;
    llvm::DenseMap<mlir::Value, mlir::Value> domStyles;
    llvm::SmallVector<mlir::Value> domStyleValues;
    llvm::DenseMap<mlir::Value, mlir::Value> domStylePointers;
    void prepareDOMStyles(ec::FuncOp function);
    bool needsDOM = false;
    /// A proved library entry keeps its typed return even without browser inputs.
    bool hasHostEntry = false;
    std::string domSessionDefinition;
    std::string domDataSession;
    std::string domDataEntry;
    mlir::Type tableType(MethodTableType type) const;
    std::string domDataDefinition() const;
    void censusDOM(const DOMEntryAnalysis & entry, bool ownedSession);
    bool replaceDOM(mlir::Operation * operation);
    /// ctjs symbol -> emitc symbol, decided for every accepted function before
    /// any is lowered, so a call lowered before its callee already names the
    /// callee's new symbol and no symbol use is ever rewritten in place. A
    /// rewrite would also reach a call_direct in a refused caller, which must
    /// keep naming a ctjs.func to verify.
    llvm::StringMap<std::string> names;
    /// The emptied ctjs.funcs, erased together in finish().
    llvm::SmallVector<ctjs::FuncOp> shells;
    /// Object shapes become C++ classes, one definition per shape program-wide.
    ///
    /// The key is the sorted list of field names, so two creation sites with
    /// the same fields get the same class name, which is what lets a generated
    /// struct be passed between functions. Where sites agree on names but
    /// disagree on a field's type, the class is a template over only the
    /// disagreeing positions: `{hit: false, at: 0}` and `{hit: 0, at: 0}`
    /// become `template <class T0> class ctn_at_hit { double at; T0 hit; };`.
    /// A family that agrees everywhere is not a template. A field of a union
    /// type is not used because each site is monomorphic, and a union would
    /// test its tag in front of every read.
    ///
    /// Admission allows scalar fields and checked owning method-table slots, so
    /// a family may vary between scalar carriers and table schemas across
    /// sites; string and object fields are still refused.
    struct family {
        llvm::SmallVector<std::string> fields;     ///< Field names, sorted: the family key.
        llvm::SmallVector<mlir::Type> types;       ///< The first site's carrier, per position.
        llvm::BitVector varies;                    ///< A later site disagreed at this position.
        llvm::SmallVector<std::string> parameters; ///< Per position: "" or the template parameter.
        llvm::SmallVector<std::string> where;      ///< The JavaScript sites, in first-seen order.
        unsigned instantiations = 0;               ///< Distinct (name, type) keys in this family.
        std::string name;                          ///< E.g. ctn_at_hit; unique in the module.
        std::string className; ///< The JavaScript class an instance site belongs to, or "".
    };
    /// One creation site: its family, and the C++ types its own fields took.
    /// Field types are per site; field names and the class name are per family.
    struct siteShape {
        unsigned family = 0;
        llvm::SmallVector<mlir::Type> types;
    };
    /// N = 0: `family` is 320 bytes and SmallVector's default inline count
    /// static_asserts above 256. There is one per distinct shape in the whole
    /// program, so inline storage would buy nothing anyway.
    llvm::SmallVector<family, 0> families;
    llvm::StringMap<unsigned> familyIndex;          ///< Joined field names -> index in families.
    llvm::DenseMap<mlir::Value, siteShape> shapeOf; ///< create_object result -> its site.
    /// Property get/set -> C++ member name. Decided while the IR is still
    /// ctjs: by the time a key constant or an access is replaced, the object it
    /// keys is already an emitc.variable and no longer looks like a closed
    /// create_object.
    llvm::DenseMap<mlir::Operation *, std::string> accessKey;
    llvm::DenseMap<mlir::Operation *, mlir::Type> accessType;
    /// The class of each field that owns an instance by value
    /// (kNativeOwnedInstance), by key: the member's C++ type.
    llvm::StringMap<mlir::Type> ownedFieldTypes;
    std::set<std::string> identityFields;
    llvm::StringMap<mlir::Type> identityFieldTypes;
    llvm::DenseMap<mlir::Operation *, std::string> identityAccess;
    void censusIdentityFields(llvm::ArrayRef<ctjs::FuncOp> accepted);
    std::string identityDefinition() const;
    std::string identityFieldHelpers() const;
    bool replaceIdentityField(mlir::Operation * op);
    llvm::StringMap<mlir::Type> resultTypes;
    llvm::StringMap<llvm::SmallVector<mlir::Type>> parameterTypes;
    /// Array reads and writes that lower to std::vector operations. Decided
    /// while the IR is still ctjs: once an access is replaced, the array it
    /// reads is already an emitc.variable and no longer looks like a dense
    /// create_array.
    llvm::DenseSet<mlir::Operation *> vectorLengthReads;
    llvm::DenseSet<mlir::Operation *> vectorIndexReads;
    llvm::DenseSet<mlir::Operation *> vectorIndexWrites;
    llvm::DenseSet<mlir::Operation *> vectorLengthWrites;
    /// Receiver lift state: the alias groups shared with admission and, per
    /// lifted method, the lvalue local its `%arg0` is copied into (the
    /// `ctn_x * self; self = v0;` pair that `emitc.member_of_ptr` needs).
    const receiverGroups * groups = nullptr;
    llvm::DenseSet<mlir::Value> receiverArgs;
    llvm::DenseMap<mlir::Value, mlir::Value> receiverLocal;
    llvm::DenseMap<mlir::Operation *, MapType> mapSchemas;
    llvm::DenseMap<mlir::Operation *, std::string> recordMapPointers;
    /// The runtime is one header (include/ctcompile/CTNative/Runtime/ctnative.hpp),
    /// so a program only chooses its two defines (ordered Map storage and the
    /// DOM section) and whether it needs identity_object.
    bool needsMapOrder = false;
    bool needsObjectIdentity = false;
    bool needsObjectValue = false;
    struct exceptionStorage {
        mlir::Value result;
        llvm::SmallVector<mlir::Value> state;
    };
    llvm::DenseMap<mlir::Operation *, exceptionStorage> exceptionSlots;
    void prepareExceptions(ctjs::FuncOp fn);
    bool replaceException(mlir::Operation * op);
    llvm::SmallVector<std::string> environments;
    /// The class tag types a value has (`<Class>_class`), declared once each
    /// as an empty struct: a class held as an object field carries no data.
    std::set<std::string> classTags;
    llvm::SmallVector<std::string> methodTables;
    llvm::SmallVector<std::string> callableBuilders;
    llvm::StringMap<mlir::DictionaryAttr> callableBodies;
    /// Session method tables, selected only from the final owner proof after
    /// the whole shape family was admitted.
    llvm::StringSet<> sessionTables;
    llvm::StringMap<unsigned> sessionTargets;
    llvm::StringSet<> sessionMapTargets;
    llvm::DenseMap<mlir::Operation *, std::string> sessionCalls;
    struct sessionMapStorage {
        std::string tableName;
        std::string mapName;
        mlir::Type borrowedType;
        mlir::Value owner;
    };
    llvm::SmallVector<sessionMapStorage> sessionMaps;
    llvm::DenseMap<mlir::Operation *, unsigned> sessionAllocations;
    bool replaceSessionAllocation(mlir::Operation * operation);
    bool censusSession(const OwnedGlobalRoots & roots, llvm::ArrayRef<ctjs::FuncOp> accepted);
    std::string callableTypeSpelling(mlir::Type type);
    bool hasConcreteCallableSignature(ctjs::CreateClosureOp made) const;
    void censusStoredCallable(ctjs::CreateClosureOp made, bool namedLambda = false);
    void censusMethodTables(llvm::ArrayRef<ctjs::FuncOp> accepted);
    bool replaceMethodTable(mlir::Operation * op);
    void censusEnvironments(llvm::ArrayRef<ctjs::FuncOp> accepted);
    bool replaceEnvironment(mlir::Operation * op);
    /// Gives a borrowed parameter, and the closure of each callback it holds,
    /// its ctnative::function_ref; false for any other value.
    bool retypeBorrowed(mlir::Value v);
    void censusBorrowedCallbacks(llvm::ArrayRef<ctjs::FuncOp> accepted);
    /// The function_ref spelling of each borrowed parameter, and of each
    /// callback such a parameter holds, by the callback's symbol.
    llvm::DenseMap<mlir::Value, std::string> borrowedSignatures;
    llvm::StringMap<std::string> borrowedCallbackSignatures;
    /// Closure sets (ClosureSets.cpp): a set's value and each closure making
    /// one of its members is a struct emitted per set, whose enum class tag
    /// says which member it holds and whose call switches on that tag.
    bool retypeClosureSet(mlir::Value v);
    bool replaceClosureSetMember(mlir::Operation * op);
    std::string closureSetSpelling(llvm::ArrayRef<llvm::StringRef> targets);
    mlir::Type closureSetCarrierOf(mlir::Value v);
    mlir::Type joinClosureSetCarriers(mlir::Type a, mlir::Type b);
    mlir::Value convertClosureSet(mlir::OpBuilder & b, mlir::Location where, mlir::Value value,
                                  mlir::Type target);
    void declareClosureSetMembers(mlir::OpBuilder & b, llvm::ArrayRef<ec::FuncOp> lowered);
    void defineReceiverClosureSets(mlir::OpBuilder & b);
    /// One member of a closure set: its lifted function's C++ name, and the
    /// type and initializer of each capture field. `receiver` is set when a
    /// capture's type is spelled from the function's signature, as the pointer
    /// an arrow's lexical `this` is bound as is.
    struct closureSetMember {
        std::string function;
        llvm::SmallVector<std::pair<std::string, std::string>> captures;
        bool receiver = false;
    };
    const closureSetMember & closureSetMemberOf(llvm::StringRef target);
    llvm::StringMap<closureSetMember> closureSetMembers;
    /// Each set's struct definition by name, and whether a member binds a
    /// receiver, which puts the definition after the function prototypes.
    std::map<std::string, std::pair<std::string, bool>> closureSetDefinitions;
    /// Each closure-set member's target, mapped to its whole set's struct
    /// name, once computed.
    std::optional<llvm::StringMap<std::string>> closureSetGroups;
    static std::string spelled(mlir::Type type);

    std::string spelling(const siteShape & site) const;

    mlir::Type classType(const siteShape & site);

    mlir::Type receiverType(const siteShape & site);

    [[nodiscard]] llvm::StringRef memberName(mlir::Operation * access) const;

    [[nodiscard]] const siteShape & shapeAt(mlir::Value object) const;

    llvm::SmallVector<std::pair<std::string, mlir::Type>> fieldsOf(mlir::Value object);
    /// Gives each field of `object` stored as a String union carrier that
    /// carrier in `stored` (EmitC/VariantFields.cpp).
    void variantFields(mlir::Value object, llvm::StringMap<mlir::Type> & stored);

    void censusShapes(llvm::ArrayRef<ctjs::FuncOp> accepted);
    void censusScalars(llvm::ArrayRef<ctjs::FuncOp> accepted, const OwnedGlobalRoots * roots);
    mlir::Type joinedReturnType(ctjs::FuncOp fn) const;
    void convertBoundaries(ctjs::FuncOp fn);

    void nameFamilies();

    [[nodiscard]] std::string provenanceOf(const family & f) const;

    void collectVector(mlir::Value array);
    bool replaceStringValue(mlir::Operation * op);

    void finish();

    [[nodiscard]] mlir::Type typeOf(mlir::Value v) const;

    mlir::Value numberConstant(mlir::OpBuilder & b, mlir::Location where, double d);

    mlir::Value boolConstant(mlir::OpBuilder & b, mlir::Location where, bool v);
    mlir::Value absentConstant(mlir::OpBuilder & b, mlir::Location where, bool isNull = false);
    mlir::Value convertScalar(mlir::OpBuilder & b, mlir::Location where, mlir::Value value,
                              mlir::Type target);
    mlir::Value number(mlir::OpBuilder & b, mlir::Location where, mlir::Value value);

    mlir::Value stringConstant(mlir::OpBuilder & builder, mlir::Location where,
                               llvm::StringRef value);

    mlir::Type globalStorageType(llvm::StringRef name) const;
    mlir::Value lvalueOfGlobal(mlir::OpBuilder & b, mlir::Location where, llvm::StringRef name);

    mlir::Value truthyNumber(mlir::OpBuilder & b, mlir::Location where, mlir::Value x);

    mlir::Value truthy(mlir::OpBuilder & builder, mlir::Location where, mlir::Value value);

    void push(mlir::OpBuilder & b, mlir::Location where, mlir::Value into, mlir::Value element);

    mlir::Value libmCall(mlir::OpBuilder & b, mlir::Location where, llvm::StringRef fn,
                         mlir::ValueRange args);

    mlir::Value exponentiate(mlir::OpBuilder & b, mlir::Location where, mlir::Value base,
                             mlir::Value exponent);

    void retype(ctjs::FuncOp fn);
    /// Lowers a replace call with a specialised callback, and skips the plumbing
    /// it consumes (EmitC/StringCallbacks.cpp). False for any other operation.
    bool replaceStringCallback(mlir::Operation * o);
    /// Lowers an accepted `throw new TypeError(message)` to
    /// `ctnative::throw_type_error(message)`, and skips what it consumes
    /// (Exceptions/TypeErrors.cpp). False for any other operation.
    bool replaceTypeError(mlir::Operation * o);
    /// Gives each data-record call (TypeInference::markRecordReturns) its
    /// class by value and moves the result into a local (EmitC/Records.cpp).
    void passRecords(ctjs::FuncOp fn);
    /// The class a data-record function returns by value, or null.
    [[nodiscard]] mlir::Type recordReturnType(ctjs::FuncOp fn) const;

    static void eraseIfUnused(mlir::Operation * o);

    static mlir::Value cellPlace(mlir::OpBuilder & b, mlir::Location where, mlir::Value cell);

    mlir::Value memberAccess(mlir::OpBuilder & b, mlir::Location where, mlir::Value object,
                             llvm::StringRef member, mlir::Type type);

    void replace(mlir::Operation * o, bool isEntry, mlir::Type returnType);
    void printObservations(mlir::OpBuilder & b, mlir::Location where);
    bool replaceMap(mlir::Operation * o);

    void applyDeclarativeRules(ctjs::FuncOp fn);

    void lower(ctjs::FuncOp fn);

    void declareGlobals();
    /// Whether the lowered module uses an ordinary script's page views
    /// (Runtime/Page.hpp).
    bool usesPageViews();
    /// A verbatim C++ fragment at `b`, located at the module.
    void verbatim(mlir::OpBuilder & b, const llvm::Twine & text);
};

/// A StringMap's or StringSet's keys, sorted.
template <class Map> llvm::SmallVector<llvm::StringRef> sortedKeys(const Map & map) {
    llvm::SmallVector<llvm::StringRef> keys(map.keys().begin(), map.keys().end());
    llvm::sort(keys);
    return keys;
}

} // namespace ctcompile::ctnative::lowering_detail
