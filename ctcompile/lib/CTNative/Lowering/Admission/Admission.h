//===- Admission/Admission.h - Native admission checks ----------*- C++ -*-===//
//
// Declares `admission`, the checker that decides whether one ctjs.func can be
// lowered to native C++ (proved CTJS to EmitC over plain C++ types). Its
// members are defined across the .cpp files in this directory.
//
//===----------------------------------------------------------------------===//

#pragma once

#include "../LoweringSupport.h"

namespace ctcompile::ctnative {
class OwnedMethodTableSlots;
class OwnedGlobalRoots;
class DOMEntryAnalysis;
} // namespace ctcompile::ctnative

namespace ctcompile::ctnative::lowering_detail {

/// Decides whether one ctjs function can be lowered to native C++. Each check
/// returns false after recording the first refusal reason in `why` (later
/// refusals are ignored); `function()` is the entry point.
struct admission {
    /// The solver holding the TypeLattice facts that typeOf() reads.
    mlir::DataFlowSolver & solver;
    /// The first refusal reason, or empty while everything has been admitted.
    std::string why;
    /// Null in unit tests that construct an admission directly; every path then
    /// treats each receiver as a local literal and inspects only its own uses.
    const receiverGroups * groups = nullptr;
    const OwnedMethodTableSlots * ownedTableSlots = nullptr;
    const OwnedGlobalRoots * ownedGlobals = nullptr;
    const DOMEntryAnalysis * domEntry = nullptr;
    /// The carrier every `return` in the function agrees on; `none` until the
    /// first return is seen. A function with no return at all returns NaN
    /// (undefined's carrier), which lower() picks when this stays `none`.
    carrier returns = carrier::none;
    /// The returned type, kept so that the source alternatives survive when a
    /// nullable carrier can also hold a boolean.
    mlir::Type returnedType = {};
    /// Whether this is an ordinary script, with no host contract: its DOM
    /// elements are the runtime's views (lowering::pageViews).
    [[nodiscard]] bool pageViews() const { return !domEntry && !ownedGlobals; }
    /// The inferred type of `v`, or null when the solver has no fact for it.
    [[nodiscard]] mlir::Type typeOf(mlir::Value v) const;

    /// Records `reason` if no refusal was recorded yet; always returns false.
    bool refuse(std::string reason);

    /// Admits `v` as an operand of `where` only if it has a scalar carrier.
    bool numeric(mlir::Value v, llvm::StringRef where);

    /// Admits `v` as a printed global observation only if its carrier is a
    /// supported primitive.
    bool printable(mlir::Value v, llvm::StringRef where);
    /// Admits a field read or write on a proved owned object identity.
    bool identityField(mlir::Operation * op);
    /// Admits a store into a slot of a proved owned method table.
    bool ownedTableField(ctjs::SetPropertyOp store);
    /// Admits an operation that OwnedGlobalRoots recorded for an owned global.
    bool ownedGlobalOperation(mlir::Operation * op);
    /// True when `value` is the proved allocation of an owned global binding.
    bool ownedGlobalValue(mlir::Value value) const;

    /// True for the store_global of a function declaration's closure.
    static bool isDeclarationStore(mlir::Operation * o);

    /// True for a ctjs.create_closure that closure lifting replaced with
    /// direct calls (it carries `ctnative.lifted`).
    static bool isLiftedClosure(mlir::Operation * o);

    /// True for a ctjs.create_cell proved constant, whose reads already use
    /// the value directly.
    static bool isUnboxedCell(mlir::Operation * o);

    /// True for a declaration or lifted closure, which emits no code.
    static bool closureLowersToNothing(mlir::Operation * o);

    /// The refusal message for a closure that does not lower to nothing,
    /// using the reason closure lifting recorded on it.
    static std::string closureRefusal(mlir::Operation * o);

    /// True when every use of `v` is the callee-value operand of a
    /// ctjs.call_direct.
    static bool feedsOnlyDirectCallees(mlir::Value v);

    /// True when `v` is an object with a closed shape, lowered to a struct.
    static bool isClosedObject(mlir::Value v);

    /// The entry-block indices that carry an object pointer
    /// (`ctnative.object_args`) on a ctjs.func or ctjs.call_direct.
    static llvm::ArrayRef<int32_t> objectArgsOf(mlir::Operation * o);

    /// True when argument `index` of `o` is listed in objectArgsOf().
    static bool isObjectArg(mlir::Operation * o, unsigned index);

    /// The entry-block indices that carry a shared cell by pointer
    /// (`ctnative.cell_args`) on a ctjs.func or ctjs.call_direct.
    static llvm::ArrayRef<int32_t> cellArgsOf(mlir::Operation * o);

    /// True when argument `index` of `o` is listed in cellArgsOf().
    static bool isCellArg(mlir::Operation * o, unsigned index);

    /// True for a ctjs.create_cell that closure lifting made a frame-local
    /// variable.
    static bool isCarriedCell(mlir::Operation * o);

    /// True for an entry-block argument that receives a shared cell by pointer.
    static bool isCellParameter(mlir::Value v);

    /// True when `v` is a shared binding: a carried cell or a cell parameter.
    static bool namesASharedCell(mlir::Value v);

    /// True when `key` is a valid C identifier.
    static bool isCIdentifier(llvm::StringRef key);

    /// True when `key` is a C++ keyword or a macro the generated headers may
    /// define, so it cannot be a struct field name.
    static bool isReservedInCpp(llvm::StringRef key);

    /// True when `key` names a member inherited from Object.prototype.
    static bool namesObjectPrototypeMember(llvm::StringRef key);

    /// True for a string constant with uses, every one of which `key` accepts.
    static bool stringConstantUsedOnlyAs(mlir::Operation * o,
                                         llvm::function_ref<bool(mlir::OpOperand &)> key);

    /// True for a string constant used only as a closed object's field key.
    static bool isKeyOnlyString(mlir::Operation * o);

    /// True when `v` is an array literal proved to be a dense vector.
    static bool isVectorSite(mlir::Value v);

    /// The refusal message naming why array literal `array` is not dense.
    static std::string whyNotDense(mlir::Value array);

    /// True for a string constant used only as the `length` key of a dense
    /// array.
    static bool isVectorKeyString(mlir::Operation * o);

    /// The reason the object-parameter rule recorded on `object`'s literal, or
    /// empty.
    static std::string argumentReason(mlir::Value object);

    /// The escape routes an object and an array literal share, in one wording.
    static std::optional<std::string> escapeRoute(llvm::StringRef literal, mlir::OpOperand & use);
    /// The generic escape message for a literal that reaches `user`.
    static std::string escapesTo(llvm::StringRef literal, mlir::Operation * user);

    /// The refusal message naming why object literal `object` is not closed.
    static std::string whyOpen(mlir::Value object);

    /// The refusal message naming why a lifted method's `this` is not closed.
    static std::string whyOpenReceiver(mlir::Value self);

    /// True when `v` emits no code and therefore needs no carrier.
    static bool lowersToNothing(mlir::Value v);

    /// Admits or refuses one operation of the function.
    bool op(mlir::Operation * o);
    // op()'s larger arms, one member each.
    std::optional<bool> domOperation(mlir::Operation * o);
    /// A run-once static's constant table: its literal, fields and reads, and a
    /// read of a static whose literal is not one (Admission/StaticTables.cpp).
    std::optional<bool> staticTableOperation(mlir::Operation * o);
    bool nativeMapCall(mlir::Operation * o, llvm::StringRef action);
    bool closedObject(ctjs::CreateObjectOp object);
    /// One access `user` to field `key` of a closed object whose field
    /// accesses are `accesses`, when the field is stored as a String union
    /// carrier; nullopt for any other field (Admission/VariantFields.cpp).
    std::optional<bool> variantField(llvm::StringRef key, mlir::Operation * user,
                                     llvm::ArrayRef<mlir::Operation *> accesses);
    bool directCall(ctjs::CallDirectOp call);
    bool returned(ctjs::ReturnOp ret);

    /// The entry point: admits `fn` if its arguments, every operation and every
    /// value it defines can be lowered to native C++.
    bool function(ctjs::FuncOp fn);
    /// Admits a ctjs.try region.
    bool exceptionRegion(ctjs::TryOp attempt);
};

} // namespace ctcompile::ctnative::lowering_detail
