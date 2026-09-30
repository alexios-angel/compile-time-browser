#pragma once

#include "ctcompile/CTNative/Analysis/HostContract.h"
#include "ctcompile/CTNative/Analysis/NativeMap.h"
#include "mlir/IR/Dominance.h"
#include "mlir/IR/SymbolTable.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/STLFunctionalExtras.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringSet.h"

#include <initializer_list>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace ctcompile::ctnative::host_detail {

// Copy a recovered invocation's inert continuation tuple at the builder's
// insertion point. The supplied payload is the normal result or caught value;
// only unwind adds the original pre-call state. This does not execute/remove the
// call or select an exceptional edge: the consumer must prove that separately.
// Failure changes no IR. The invocation and both continuations remain intact.
mlir::FailureOr<llvm::SmallVector<mlir::Value>> projectInvocationContinuation(
    ctjs::InvokeOp invocation, bool unwind, mlir::Value payload, mlir::OpBuilder & at,
    unsigned & remaining);

inline constexpr llvm::StringLiteral classDefinedIntrinsic = "__ctbrowser_class_defined";

// Ordinary DOM identities; class preparation consumes its separate declarations.
inline constexpr unsigned maxDOMIntrinsicNameLength = 29; // __ctbrowser_set_function_name
const llvm::StringSet<> & domInitialIntrinsics();
inline bool domInitialIntrinsic(llvm::StringRef name) {
    return name.size() <= maxDOMIntrinsicNameLength && domInitialIntrinsics().contains(name);
}
// The intrinsic-entry declaration: Symbol, and optionally String, only.
inline bool symbolStringIntrinsics(const std::vector<std::string> & names) {
    return llvm::is_contained(names, "Symbol") && llvm::all_of(names, [](const auto & name) {
               return name == "Symbol" || name == "String";
           });
}

inline bool reservedCallbackBinding(llvm::StringRef name) {
    static const llvm::StringSet<> names{"globalThis", "undefined", "NaN", "Infinity"};
    return name.size() <= 10 && names.contains(name);
}

// The callee, receiver and explicit arguments of a ctjs.call or ctjs.call_direct
// (`direct` set only for the latter).
struct jsCall {
    mlir::Value callee, receiver;
    mlir::OperandRange args;
    ctjs::CallDirectOp direct;
};

inline std::optional<jsCall> jsCallOf(mlir::Operation * operation) {
    if (auto direct = llvm::dyn_cast<ctjs::CallDirectOp>(operation)) {
        return jsCall{direct.getCalleeValue(), direct.getReceiver(), direct.getArgs(), direct};
    }
    if (auto call = llvm::dyn_cast<ctjs::CallOp>(operation)) {
        return jsCall{call.getCallee(), call.getReceiver(), call.getArgs(), {}};
    }
    return std::nullopt;
}

// An i1-style 0/1 IntegerAttr as its Boolean, a wider integer as null, and any
// other literal unchanged.
inline mlir::Attribute booleanFromBit(mlir::Attribute literal) {
    auto integer = llvm::dyn_cast_if_present<mlir::IntegerAttr>(literal);
    if (!integer) { return literal; }
    if (!integer.getValue().isZero() && !integer.getValue().isOne()) { return {}; }
    return ctjs::BooleanAttr::get(literal.getContext(), integer.getValue().isOne());
}

// ToBoolean of an undefined, null, Boolean, String or 0/1 integer literal.
// Numbers are deliberately absent: callers that fold them test them first.
inline std::optional<bool> literalTruth(mlir::Attribute literal) {
    if (llvm::isa_and_nonnull<ctjs::UndefinedAttr, ctjs::NullAttr>(literal)) { return false; }
    if (auto boolean = llvm::dyn_cast_if_present<ctjs::BooleanAttr>(literal)) {
        return boolean.getValue();
    }
    if (auto text = llvm::dyn_cast_if_present<ctjs::StringAttr>(literal)) {
        return !text.getValue().empty();
    }
    if (auto integer = llvm::dyn_cast_if_present<mlir::IntegerAttr>(literal)) {
        if (integer.getValue().isZero() || integer.getValue().isOne()) {
            return integer.getValue().isOne();
        }
    }
    return std::nullopt;
}

// `typeof` of a primitive literal; empty for anything else.
inline llvm::StringRef typeofLiteral(mlir::Attribute literal) {
    if (llvm::isa_and_nonnull<ctjs::UndefinedAttr>(literal)) { return "undefined"; }
    if (llvm::isa_and_nonnull<ctjs::NullAttr>(literal)) { return "object"; }
    if (llvm::isa_and_nonnull<ctjs::StringAttr>(literal)) { return "string"; }
    if (llvm::isa_and_nonnull<ctjs::BooleanAttr>(literal)) { return "boolean"; }
    if (llvm::isa_and_nonnull<ctjs::NumberAttr>(literal)) { return "number"; }
    return {};
}

// Whether `name` differs from every name in `lists` and every root binding of
// `contract`, charging `charge(name)` for each compared name first (false stops).
inline bool unreservedBinding(llvm::StringRef name, const HostContract & contract,
                              std::initializer_list<const std::vector<std::string> *> lists,
                              llvm::function_ref<bool(llvm::StringRef)> charge) {
    for (const auto * names : lists) {
        for (const auto & reserved : *names) {
            if (!charge(reserved) || reserved == name) { return false; }
        }
    }
    for (const auto & root : contract.roots) {
        if (!charge(root.binding) || root.binding == name) { return false; }
    }
    return true;
}

// The source function a closure creates. Its numeric index names a function
// only inside the enclosing source program, whose callee the closure carries.
inline ctjs::FuncOp closureTarget(ctjs::CreateClosureOp closure,
                                  const llvm::DenseMap<unsigned, ctjs::FuncOp> & functions) {
    auto scope = closure->getParentOfType<ctjs::FuncOp>();
    if (closure.getFunction() < 0 || !scope || scope.getBody().empty() ||
        scope.getBody().front().getNumArguments() < 3 ||
        closure.getEnclosingClosure() != scope.getBody().front().getArgument(ctjs::arg_callee)) {
        return {};
    }
    return functions.lookup(static_cast<unsigned>(closure.getFunction()));
}

// Invocation shape only: identity does not prove heritage, receiver rebinding,
// field initialization or super lookup, and never authorizes erasing a call.
inline unsigned classIntrinsicArity(llvm::StringRef name) {
    static const llvm::StringMap<unsigned> arities{{classDefinedIntrinsic, 1},
                                                   {"__ctbrowser_bind_this", 1},
                                                   {"__ctbrowser_init_fields", 2},
                                                   {"__ctbrowser_class_heritage", 3},
                                                   {"__ctbrowser_super_get", 3}};
    return name.size() <= 26 ? arities.lookup(name) : 0;
}

inline unsigned iteratorIntrinsicArity(llvm::StringRef name) {
    static const llvm::StringMap<unsigned> arities{{"__ctbrowser_for_of_open", 1},
                                                   {"__ctbrowser_iter_next", 1},
                                                   {"__ctbrowser_iter_close", 2}};
    return name.size() <= 23 ? arities.lookup(name) : 0;
}

// Compiler source helpers a bare context installs and the importer emits as a
// direct call with an undefined receiver. Identity plus this arity is what lets
// a declared binding stay closed; anything else still refuses.
inline unsigned sourceHelperArity(llvm::StringRef name) {
    static const llvm::StringMap<unsigned> arities{
        {"__ctbrowser_define_own", 4},      {"__ctbrowser_define_accessor", 4},
        {"__ctbrowser_to_property_key", 1}, {"__ctbrowser_in_object", 1},
        {"__ctbrowser_require_object", 1},
    };
    return name.size() <= 28 ? arities.lookup(name) : 0;
}

// Complete original Bootstrap F callback; callers still prove enclosure, uses,
// intrinsic identity and the actual replacement receiver.
bool isLowercaseReplacement(ctjs::FuncOp function, llvm::function_ref<bool()> step);

using CapturedMapOrigin = std::pair<mlir::Value, mlir::Operation *>;

struct CapturedMapEntry {
    mlir::Value key;
    PrimitiveAlternatives payload;
    bool present = true;
    mlir::Value object;
    bool absent = false;
    CapturedMapOrigin map{};
};

struct CapturedMapState {
    llvm::SmallVector<CapturedMapEntry> entries;
    bool completeKeys = false;
    llvm::SmallVector<mlir::Value> possibleKeys;
    std::optional<unsigned> currentSize;
    llvm::DenseSet<mlir::Operation *> observations;
};

// Optional entry-order facts, used only after every reusable body has passed
// the independent complete-family proof. Child identity includes this call.
struct CapturedMapInvocation {
    mlir::Value root;
    mlir::Operation * call = nullptr;
    llvm::DenseMap<mlir::Value, mlir::Value> arguments;
    // One exhaustively enumerated alias partition of checked DOM entry inputs.
    // These classes never narrow the reusable family proof.
    llvm::DenseMap<mlir::Value, unsigned> elementClasses;
    llvm::DenseMap<CapturedMapOrigin, CapturedMapState> states;
    mlir::Value returnedLeaf;
};

struct analyzer {
    mlir::ModuleOp module;
    const HostContract & contract;
    unsigned remaining;
    bool exhausted = false;
    bool ambiguousFunctions = false;
    ctjs::FuncOp entry;
    ctjs::FuncOp declaration;
    mlir::DominanceInfo dominance;
    llvm::StringMap<llvm::SmallVector<ctjs::StoreGlobalOp>> globals;
    llvm::DenseMap<mlir::Operation *, llvm::SmallVector<mlir::Operation *>> callers;
    llvm::DenseMap<mlir::Operation *, ctjs::FuncOp> indirectFactories;
    llvm::DenseMap<mlir::Operation *, llvm::SmallVector<ctjs::ReturnOp>> returns;
    llvm::DenseMap<unsigned, ctjs::FuncOp> functions;
    llvm::DenseSet<mlir::Value> evaluating;
    std::vector<HostCallableEdge> checkedCalls;
    llvm::DenseMap<mlir::Operation *, HostCallableEdge> capturedCalls;
    llvm::DenseSet<mlir::Operation *> capturedOperations;
    llvm::DenseMap<mlir::Value, PrimitiveAlternatives> capturedResults;
    // Private read-time categories, reconstructed independently by class
    // preparation and the final provider. These never authorize native ownership.
    llvm::DenseMap<mlir::Value, PrimitiveAlternatives> classScalarReads;
    HostLocalRecords localRecords;
    llvm::DenseSet<mlir::Operation *> mutableScalarReads;

    analyzer(mlir::ModuleOp module, const HostContract & contract, unsigned steps);
    bool step();
    // `count` steps at once; one that does not fit spends the rest, as `count` steps would.
    bool spend(uint64_t count);
    // One step per operand; false at the first that does not dominate `operation`.
    bool operandsDominate(mlir::Operation * operation);
    // One step; then `use` lies in `function` and `value` dominates it.
    bool localUse(mlir::Value value, mlir::OpOperand & use, mlir::Operation * function);
    // One step per user; false at the first that is not a ctjs.root.
    bool rootedUsers(mlir::Value value);
    mlir::BlockArgument elementInput(mlir::Value value) const;
    ctjs::FuncOp target(mlir::Operation * call) const;
    ctjs::FuncOp callable(mlir::Value value, unsigned depth = 0);
    ctjs::SetPropertyOp currentWrite(ctjs::GetPropertyOp read, unsigned depth = 0);
    std::optional<HostCallableEdge> propertyCall(mlir::Operation * call);
    std::optional<HostCapturedMap> capturedMap(ctjs::CreateClosureOp closure, ctjs::FuncOp function,
                                               mlir::Operation * call, ctjs::GetPropertyOp read);
    bool capturedMapCalls(ctjs::FuncOp function, ctjs::SetPropertyOp publication, bool prepared,
                          llvm::SmallVectorImpl<mlir::Operation *> & calls);
    bool capturedMapParameters(ctjs::FuncOp function, bool prepared,
                               llvm::ArrayRef<mlir::Operation *> calls,
                               const llvm::DenseSet<mlir::Operation *> & familyCalls,
                               const llvm::DenseMap<mlir::Value, PrimitiveAlternatives> & results,
                               HostMethodParameters & result, HostCapturedMap * capture = nullptr);
    PrimitiveAlternatives entryCategories(
        mlir::Value value, const llvm::DenseMap<mlir::Value, PrimitiveAlternatives> & results,
        mlir::Operation * consumer = nullptr, unsigned depth = 0,
        std::vector<mlir::Value> * dependencies = nullptr);
    std::optional<HostScalarGlobalRead> scalarGlobalRead(ctjs::LoadGlobalOp read);
    bool scalarCallbacks(llvm::ArrayRef<HostMethodParameters> family, HostCapturedMap & result);
    std::optional<HostObjectGlobalRead> objectGlobalRead(ctjs::LoadGlobalOp read);
    // The fresh object a value names: its allocation or a proved global alias's.
    ctjs::CreateObjectOp namedObject(mlir::Value value);
    std::optional<std::vector<HostObjectGlobalRead>> objectGlobalReads(ctjs::CreateObjectOp made);
    bool capturedMapBody(ctjs::FuncOp function, bool prepared, bool primitiveContents,
                         const HostMethodParameters & parameters, HostCapturedMap & result,
                         PrimitiveAlternatives & returnAlternatives,
                         CapturedMapInvocation * invocation = nullptr);
    bool capturedMapOuterKeys(bool prepared,
                              llvm::ArrayRef<llvm::SmallVector<mlir::Operation *>> familyCalls,
                              HostCapturedMap & result);
    ctjs::CreateObjectOp object(mlir::Value value, unsigned depth = 0);
    mlir::Attribute primitive(mlir::Value value, unsigned depth = 0);
    std::optional<bool> truth(mlir::Value value, unsigned depth = 0);
    bool active(mlir::Operation * operation);
    bool before(mlir::Operation * first, mlir::Operation * second);
    mlir::Operation * anchor(mlir::Operation * operation);
    bool exactCall(mlir::Operation * call);
    bool transportedCallable(ctjs::FuncOp function);
    bool singleInvocation(ctjs::FuncOp function);
    std::string localRecordProblem();
    std::string environmentProblem();
    HostSlotReport slot(const HostRootRequest & root, llvm::StringRef key);
};

bool isInertEntryDeclaration(ctjs::FuncOp wrapper, ctjs::FuncOp target,
                             llvm::function_ref<bool()> spend);

// Visit each symbol use in `module`'s attributes and body with the function it
// names (null if none), one `step` each. A value stops the census: `unenumerable`
// when a use list cannot be built, false when a step fails, else what `visit` returned.
inline std::optional<bool> forEachSymbolUse(
    mlir::ModuleOp module, bool unenumerable, llvm::function_ref<bool()> step,
    llvm::function_ref<std::optional<bool>(const mlir::SymbolTable::SymbolUse &, ctjs::FuncOp)>
        visit) {
    for (const auto & uses : {mlir::SymbolTable::getSymbolUses(module.getOperation()),
                              mlir::SymbolTable::getSymbolUses(&module.getBodyRegion())}) {
        if (!uses) { return unenumerable; }
        for (const auto & use : *uses) {
            if (!step()) { return false; }
            if (auto stop = visit(use, mlir::SymbolTable::lookupNearestSymbolFrom<ctjs::FuncOp>(
                                           use.getUser(), use.getSymbolRef()))) {
                return stop;
            }
        }
    }
    return std::nullopt;
}

std::string initialBindingProblem(mlir::ModuleOp module, const HostContract & contract);

} // namespace ctcompile::ctnative::host_detail
