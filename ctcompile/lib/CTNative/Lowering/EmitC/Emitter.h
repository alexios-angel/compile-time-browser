#pragma once

#include "../LoweringSupport.h"

namespace ctcompile::ctnative {
class OwnedGlobalRoots;
}

namespace ctcompile::ctnative::lowering_detail {

// Only for the compiler-owned helper ABIs below: non-lvalue operands are
// accepted by value or const reference. Mutable binding arguments (vec_push's
// vector, for example) must carry LValueType. This records a C++ const-acceptance
// contract, not a purity claim; Map handles may still mutate their pointees.
// Arbitrary external C++ calls must use CallOpaqueOp without this contract.
ec::CallOpaqueOp callWithConstValueOperands(mlir::OpBuilder & builder, mlir::Location where,
                                            mlir::TypeRange results, mlir::StringAttr callee,
                                            mlir::ValueRange operands);

struct lowering {
    mlir::DataFlowSolver & solver;
    mlir::MLIRContext * context;
    mlir::ModuleOp module;
    // UnaryPlusIsIdentity.pdll, frozen. Declared here so the member
    // initialisation order matches the list below and -Wreorder stays quiet.
    mlir::FrozenRewritePatternSet declarative;
    lowering(mlir::DataFlowSolver & s, mlir::MLIRContext * c, mlir::ModuleOp m)
        : solver(s), context(c), module(m), declarative(declarativePatterns(c)) {}
    llvm::StringSet<> globals; // numeric globals the emitted unit declares
    llvm::StringSet<> observations;
    bool explicitObservations = false;
    // Committed after whole-function admission while source operations still
    // exist. These are emission plans, never reusable ownership proofs.
    struct ownedGlobalStorage {
        std::string binding;
        std::string className;
        std::string field;
        mlir::Type type;
        mlir::Type fieldType;
    };
    llvm::SmallVector<ownedGlobalStorage> ownedGlobalStoragePlans;
    llvm::StringMap<mlir::Type> ownedGlobals;
    llvm::DenseMap<mlir::Value, mlir::Type> ownedObjectTypes;
    llvm::DenseMap<mlir::Operation *, unsigned> ownedGlobalOperations;
    void censusOwnedGlobals(const OwnedGlobalRoots & roots, llvm::ArrayRef<ctjs::FuncOp> accepted);
    bool replaceOwnedGlobal(mlir::Operation * operation);
    // ctjs symbol -> emitc symbol, decided for EVERY accepted function before
    // any is lowered, so a call lowered before its callee already names the
    // callee's new symbol and no symbol use is ever rewritten in place. A
    // rewrite would also reach a call_direct in a REFUSED caller, which must
    // keep naming a ctjs.func to verify.
    llvm::StringMap<std::string> names;
    // The hollowed ctjs.funcs, erased together in finish().
    llvm::SmallVector<ctjs::FuncOp> shells;
    // PHASE 56C: ONE SHAPE IS ONE DEFINITION, PROGRAM-WIDE.
    //
    // 56B emitted one class per creation SITE, so the shipped struct fixture
    // had seven classes for six shapes and two of them were byte for byte the
    // same. The key is the SHAPE instead - the ordered list of (field name,
    // field type) - so two sites with the same key get the same type and the
    // same NAME. That is what makes a generated struct passable between
    // functions at all, and it is the naming prerequisite for every later
    // phase.
    //
    // WHERE THE NAMES MATCH AND A TYPE DIFFERS, THE DEFINITION IS A TEMPLATE.
    // `{hit: false, at: 0}` and `{hit: 0, at: 0}` are one FAMILY at two
    // instantiations: `template <class T0> class ctn_at_hit { double at; T0
    // hit; };`. Only the positions the family disagrees on become parameters -
    // a position every site agrees on keeps its concrete type, which is more
    // information and not less, and a family that agrees everywhere is not a
    // template at all. NOT a variant field: each site is monomorphic and only
    // the union of the sites is not, so a variant would put a `std::visit` in
    // front of every read at both sites to pay for a polymorphism neither site
    // has (part 24 Phase 56C, steps 2 and 3).
    //
    // Admission permits scalar fields and checked owning method-table slots.
    // A shared field-name family can therefore vary between scalar carriers
    // and table schemas at different allocation sites. Each site's field
    // remains monomorphic; ordinary string/object fields are still refused.
    struct family {
        llvm::SmallVector<std::string> fields;     // the field names, sorted - the family key
        llvm::SmallVector<mlir::Type> types;       // the first site's carrier, per position
        llvm::BitVector varies;                    // a later site disagreed at this position
        llvm::SmallVector<std::string> parameters; // per position: "" or the template parameter
        llvm::SmallVector<std::string> where;      // the JavaScript sites, first sight first
        unsigned instantiations = 0;               // distinct (name, type) keys in this family
        std::string name;                          // ctn_at_hit, unique across the module
    };
    // ONE SITE: which family it belongs to, and the carriers ITS fields took.
    // The types are per site and the names are per family, which is the whole
    // of 56C in two lines.
    struct siteShape {
        unsigned family = 0;
        llvm::SmallVector<mlir::Type> types;
    };
    // N = 0: `family` is 320 bytes and SmallVector's default inline count
    // static_asserts above 256. There is one of these per distinct shape in the
    // whole program, so inline storage would buy nothing anyway.
    llvm::SmallVector<family, 0> families;
    llvm::StringMap<unsigned> familyIndex;          // the joined field names -> index into families
    llvm::DenseMap<mlir::Value, siteShape> shapeOf; // create_object result -> its site
    // Decided while the IR is still ctjs: by the time a key constant or an
    // access is replaced, the object it keys is already an emitc.variable and
    // no longer reads as a closed create_object.
    llvm::DenseMap<mlir::Operation *, std::string> accessKey; // get/set -> member name
    llvm::DenseMap<mlir::Operation *, mlir::Type> accessType;
    std::set<std::string> identityFields;
    llvm::DenseMap<mlir::Operation *, std::string> identityAccess;
    void censusIdentityFields(llvm::ArrayRef<ctjs::FuncOp> accepted);
    std::string identityDefinition() const;
    std::string identityFieldHelpers() const;
    bool replaceIdentityField(mlir::Operation * op);
    llvm::StringMap<mlir::Type> resultTypes;
    llvm::StringMap<llvm::SmallVector<mlir::Type>> parameterTypes;
    // PHASE 57A. Decided while the IR is still ctjs, for fieldsOf()'s reason:
    // by the time a read is replaced, the array it reads is already an
    // emitc.variable and no longer reads as a dense create_array.
    llvm::DenseSet<mlir::Operation *> vectorLengthReads;
    llvm::DenseSet<mlir::Operation *> vectorIndexReads;
    // THE RECEIVER LIFT. The alias groups, shared with admission, and per
    // lifted method the lvalue local its `%arg0` is copied into - the one
    // `ctn_x * self;` `self = v0;` pair that `emitc.member_of_ptr` needs.
    const receiverGroups * groups = nullptr;
    llvm::DenseSet<mlir::Value> receiverArgs;
    llvm::DenseMap<mlir::Value, mlir::Value> receiverLocal;
    // Set by the first array lowered; the include and the helper preamble ride
    // on it. An empty unit emits neither.
    bool needsVector = false;
    bool needsString = false;
    bool needsNullableString = false;
    bool needsBooleanString = false;
    bool needsStringVector = false;
    llvm::DenseMap<mlir::Operation *, MapType> mapSchemas;
    bool needsMap = false;
    bool needsNullableMapKeys = false;
    bool needsMapOrder = false;
    bool needsObjectIdentity = false;
    bool needsObjectValue = false;
    bool needsNullable = false;
    bool needsExceptions = false;
    struct exceptionStorage {
        mlir::Value result;
        llvm::SmallVector<mlir::Value> state;
    };
    llvm::DenseMap<mlir::Operation *, exceptionStorage> exceptionSlots;
    void prepareExceptions(ctjs::FuncOp fn);
    bool replaceException(mlir::Operation * op);
    llvm::SmallVector<std::string> environments;
    llvm::SmallVector<std::string> methodTables;
    llvm::SmallVector<std::string> callableBuilders;
    llvm::StringMap<mlir::DictionaryAttr> callableBodies;
    std::string callableTypeSpelling(mlir::Type type);
    bool hasConcreteCallableSignature(ctjs::CreateClosureOp made) const;
    void censusStoredCallable(ctjs::CreateClosureOp made, bool namedLambda = false);
    void censusMethodTables(llvm::ArrayRef<ctjs::FuncOp> accepted);
    bool replaceMethodTable(mlir::Operation * op);
    void censusEnvironments(llvm::ArrayRef<ctjs::FuncOp> accepted);
    bool replaceEnvironment(mlir::Operation * op);
    static std::string spelled(mlir::Type type);

    std::string spelling(const siteShape & site) const;

    mlir::Type classType(const siteShape & site);

    mlir::Type receiverType(const siteShape & site);

    mlir::Type receiverLocalType(const siteShape & site);

    [[nodiscard]] llvm::StringRef memberName(mlir::Operation * access) const;

    [[nodiscard]] const siteShape & shapeAt(mlir::Value object) const;

    llvm::SmallVector<std::pair<std::string, mlir::Type>> fieldsOf(mlir::Value object);

    void censusShapes(llvm::ArrayRef<ctjs::FuncOp> accepted);
    void censusScalars(llvm::ArrayRef<ctjs::FuncOp> accepted);
    mlir::Type joinedReturnType(ctjs::FuncOp fn) const;
    void convertBoundaries(ctjs::FuncOp fn);

    void nameFamilies();

    [[nodiscard]] std::string provenanceOf(const family & f) const;

    void collectVector(mlir::Value array);
    bool replaceStringValue(mlir::Operation * op);

    void finish();

    [[nodiscard]] mlir::Type typeOf(mlir::Value v) const;

    mlir::Value f64Constant(mlir::OpBuilder & b, mlir::Location where, double d);

    mlir::Value boolConstant(mlir::OpBuilder & b, mlir::Location where, bool v);
    mlir::Value absentConstant(mlir::OpBuilder & b, mlir::Location where, bool isNull = false);
    mlir::Value convertScalar(mlir::OpBuilder & b, mlir::Location where, mlir::Value value,
                              mlir::Type target);
    mlir::Value number(mlir::OpBuilder & b, mlir::Location where, mlir::Value value);

    mlir::Value stringConstant(mlir::OpBuilder & builder, mlir::Location where,
                               llvm::StringRef value);

    mlir::Value lvalueOfGlobal(mlir::OpBuilder & b, mlir::Location where, llvm::StringRef name);

    mlir::Value truthyNumber(mlir::OpBuilder & b, mlir::Location where, mlir::Value x);

    mlir::Value truthy(mlir::OpBuilder & builder, mlir::Location where, mlir::Value value);

    void push(mlir::OpBuilder & b, mlir::Location where, mlir::Value into, mlir::Value element);

    mlir::Value libmCall(mlir::OpBuilder & b, mlir::Location where, llvm::StringRef fn,
                         mlir::ValueRange args);

    mlir::Value exponentiate(mlir::OpBuilder & b, mlir::Location where, mlir::Value base,
                             mlir::Value exponent);

    void retype(ctjs::FuncOp fn);

    static void eraseIfUnused(mlir::Operation * o);

    static mlir::Value cellPlace(mlir::OpBuilder & b, mlir::Location where, mlir::Value cell);

    mlir::Value memberAccess(mlir::OpBuilder & b, mlir::Location where, mlir::Value object,
                             llvm::StringRef member, mlir::Type type);

    void replace(mlir::Operation * o, bool isEntry, mlir::Type returnType);
    bool replaceMap(mlir::Operation * o);

    void applyDeclarativeRules(ctjs::FuncOp fn);

    void lower(ctjs::FuncOp fn);

    void declareGlobals();
};

} // namespace ctcompile::ctnative::lowering_detail
