//===- Dictionaries.cpp - Objects used as string-keyed tables -------------===//
//
// The proof that an object literal is used only as a table
// (prepareNativeDictionaries, declared in Analysis/NativeDictionary.h) and the
// types of its operations (TypeInference::dictionaryType). Admission checks
// what this leaves open: the value type, the key types and Object.prototype's
// keys (Lowering/Admission/Dictionaries.cpp).
//
//===----------------------------------------------------------------------===//

#include "ctcompile/CTJS/IR/CTJSOps.h"
#include "ctcompile/CTNative/Analysis/NativeDictionary.h"
#include "ctcompile/CTNative/Analysis/TypeInference.h"

#include "llvm/ADT/SmallVector.h"

#include <optional>
#include <string>

namespace ctcompile::ctnative {

namespace {

constexpr llvm::StringLiteral kDelete = "__ctbrowser_delete";
constexpr llvm::StringLiteral kInObject = "__ctbrowser_in_object";
constexpr llvm::StringLiteral kPropertyKey = "__ctbrowser_to_property_key";

/// `name(...)` with `undefined` as the receiver, the importer's spelling of a
/// call of one of its intrinsics.
ctjs::CallOp intrinsicCall(mlir::Operation * op, llvm::StringRef name) {
    auto call = llvm::dyn_cast_or_null<ctjs::CallOp>(op);
    return call && ctjs::calleeGlobalName(call) == name &&
                   ctjs::isUndefinedConstant(call.getReceiver())
               ? call
               : ctjs::CallOp{};
}

bool isFalse(mlir::Value value) {
    auto flag = ctjs::constantAttr<ctjs::BooleanAttr>(value);
    return flag && !flag.getValue();
}

/// Whether a key operand is a literal (`o.a`, `o[1]`): a literal with only
/// such keys is a record's to prove (or refuse), not a table.
bool constantKeyOperand(mlir::Value key) {
    return static_cast<bool>(ctjs::constantAttr<ctjs::StringAttr>(key)) ||
           static_cast<bool>(ctjs::constantAttr<ctjs::NumberAttr>(key));
}

/// One proved use of a dictionary: the operation and what it does.
struct tableUse {
    mlir::Operation * op;
    llvm::StringRef action;
};

/// The table operations `object` reaches, or why one of its uses is not one.
/// `computed` is set when some use is not a get or set under a literal key: a
/// literal with only those is a record's (TypeInference::hasClosedShape).
std::string tableUses(mlir::Value object, llvm::SmallVectorImpl<tableUse> & uses, bool & computed) {
    for (mlir::OpOperand & use : object.getUses()) {
        mlir::Operation * user = use.getOwner();
        if (auto set = llvm::dyn_cast<ctjs::SetPropertyOp>(user)) {
            if (use.getOperandNumber() != 0) { return "it is stored as a value or used as a key"; }
            computed |= !constantKeyOperand(set.getKey());
            uses.push_back({user, "set"});
            continue;
        }
        if (auto get = llvm::dyn_cast<ctjs::GetPropertyOp>(user)) {
            if (use.getOperandNumber() != 0) { return "it is used as a key"; }
            computed |= !constantKeyOperand(get.getKey());
            uses.push_back({user, "get"});
            continue;
        }
        // `delete o[k]`: (object, key, strict, super), so the object is the
        // first argument (operand 2) and `super` must be false.
        if (auto erase = intrinsicCall(user, kDelete)) {
            if (use.getOperandNumber() != 2 || erase.getArgs().size() != 4 ||
                !isFalse(erase.getArgs()[3])) {
                return "it reaches `delete` other than as the object deleted from";
            }
            computed = true;
            uses.push_back({user, "delete"});
            continue;
        }
        // `k in o` checks that `o` is an object, then asks has_property with
        // the checked object and ToPropertyKey(k).
        if (auto checked = intrinsicCall(user, kInObject)) {
            if (use.getOperandNumber() != 2 || checked.getArgs().size() != 1) {
                return "it reaches the `in` check other than as its object";
            }
            for (mlir::OpOperand & test : checked.getResult().getUses()) {
                auto has = llvm::dyn_cast<ctjs::HasPropertyOp>(test.getOwner());
                if (!has || has.getObject() != checked.getResult()) {
                    return "its `in` check is used other than by `in`";
                }
                auto key = intrinsicCall(has.getKey().getDefiningOp(), kPropertyKey);
                if (!key || key.getArgs().size() != 1 || !key.getResult().hasOneUse()) {
                    return "its `in` key is not one ToPropertyKey of its own";
                }
                uses.push_back({has, "has"});
                uses.push_back({key, "key"});
            }
            computed = true;
            uses.push_back({user, "object"});
            continue;
        }
        // JSON.stringify(o): the runtime serializes a table's own keys
        // (Runtime/Json.hpp); the call is the built-in statics row's.
        if (TypeInference::passesARecord(use)) { continue; }
        return ("it reaches `" + user->getName().getStringRef() + "`").str();
    }
    return {};
}

/// Marks a read of an intrinsic as plumbing when every use of it is the
/// callee of a call this proof marked.
void markPlumbing(mlir::ModuleOp module) {
    module.walk([&](ctjs::LoadGlobalOp load) {
        if (load.getName() != kDelete && load.getName() != kInObject &&
            load.getName() != kPropertyKey) {
            return;
        }
        const bool plumbing = !load.getResult().use_empty() &&
                              llvm::all_of(load.getResult().getUses(), [](mlir::OpOperand & use) {
                                  return use.getOperandNumber() == 0 &&
                                         !nativeDictionaryAction(use.getOwner()).empty();
                              });
        if (plumbing) {
            load->setAttr(kNativeDictionaryAction,
                          mlir::StringAttr::get(load.getContext(), "plumbing"));
        }
    });
}

} // namespace

void prepareNativeDictionaries(mlir::ModuleOp module) {
    // The attributes are this proof's output, never its input: clear a
    // previous run's.
    module.walk([](mlir::Operation * op) {
        op->removeAttr(kNativeDictionarySite);
        op->removeAttr(kNativeDictionaryAction);
        op->removeAttr(kNativeDictionaryReason);
    });
    module.walk([&](ctjs::CreateObjectOp made) {
        llvm::SmallVector<tableUse> uses;
        bool computed = false;
        const std::string why = tableUses(made.getResult(), uses, computed);
        auto * context = made.getContext();
        if (!why.empty()) {
            if (computed) {
                made->setAttr(
                    kNativeDictionaryReason,
                    mlir::StringAttr::get(context, "an object used as a table, but " + why));
            }
            return;
        }
        if (!computed) { return; }
        made->setAttr(kNativeDictionarySite, mlir::UnitAttr::get(context));
        for (const tableUse & use : uses) {
            use.op->setAttr(kNativeDictionaryAction, mlir::StringAttr::get(context, use.action));
        }
    });
    markPlumbing(module);
}

llvm::StringRef nativeDictionaryAction(mlir::Operation * op) {
    auto action = op ? op->getAttrOfType<mlir::StringAttr>(kNativeDictionaryAction) : nullptr;
    return action ? action.getValue() : llvm::StringRef{};
}

bool isNativeDictionarySite(mlir::Operation * op) {
    return op && op->hasAttr(kNativeDictionarySite);
}

mlir::Type TypeInference::dictionaryType(mlir::Operation * op,
                                         llvm::ArrayRef<const TypeLattice *> operands) {
    mlir::MLIRContext * c = op->getContext();
    if (isNativeDictionarySite(op)) {
        // The table holds whatever is stored into it; one value type V.
        llvm::SmallVector<mlir::Value> stored;
        for (mlir::Operation * user : op->getResult(0).getUsers()) {
            if (auto set = llvm::dyn_cast<ctjs::SetPropertyOp>(user)) {
                stored.push_back(set.getValue());
            }
        }
        return DictType::get(c, joinLattices(op, stored, BottomType::get(c)));
    }
    const llvm::StringRef action = nativeDictionaryAction(op);
    if (action.empty() || action == "set" || action == "plumbing") { return {}; }
    if (action == "get") {
        // A missing key reads undefined.
        auto table = llvm::dyn_cast_or_null<DictType>(operands[0]->getValue().getType());
        if (!table) { return {}; }
        return meet(OptType::get(c, BottomType::get(c)), table.getElementType());
    }
    if (action == "object") { return operands[2]->getValue().getType(); }
    if (action == "key") { return defaultStringType(c); }
    // `delete` and `in` answer a Boolean.
    return BoolType::get(c);
}

} // namespace ctcompile::ctnative
