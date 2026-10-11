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

#include "mlir/Dialect/SCF/IR/SCF.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/MapVector.h"
#include "llvm/ADT/STLFunctionalExtras.h"
#include "llvm/ADT/SmallVector.h"

#include <cstddef>
#include <cstdint>
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

/// One proved use of a table: the operation and what it does.
struct tableUse {
    mlir::Operation * op;
    llvm::StringRef action;
};

/// The table operations `object` reaches, or why one of its uses is not one.
/// `computed` is set when some use is not a get or set under a literal key: a
/// literal with only those is a record's (TypeInference::hasClosedShape).
/// `isMember` says which values are tables, for a table stored into another.
std::string tableUses(mlir::Value object, llvm::SmallVectorImpl<tableUse> & uses, bool & computed,
                      llvm::function_ref<bool(mlir::Value)> isMember) {
    for (mlir::OpOperand & use : object.getUses()) {
        mlir::Operation * user = use.getOwner();
        if (auto set = llvm::dyn_cast<ctjs::SetPropertyOp>(user)) {
            // Stored as the value of another table: that table's to check.
            if (use.getOperandNumber() == 2 && isMember(set.getObject())) { continue; }
            if (use.getOperandNumber() != 0) {
                return "it is stored into something that is not a table, or used as a key";
            }
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
        // `o === undefined` or `o == undefined` (`!==` negates it): a table
        // read from a table lacking the key is undefined, and a table is
        // never null.
        if (auto compare = llvm::dyn_cast<ctjs::CompareOp>(user)) {
            const mlir::Value other =
                use.getOperandNumber() == 0 ? compare.getRhs() : compare.getLhs();
            const bool undefinedOther = ctjs::isUndefinedConstant(other) ||
                                        TypeInference::isUndefinedGlobal(other.getDefiningOp());
            if (undefinedOther && (compare.getKind() == ctjs::CompareKind::StrictEq ||
                                   compare.getKind() == ctjs::CompareKind::Eq)) {
                uses.push_back({user, "absent"});
                continue;
            }
            return "it is compared with something other than undefined";
        }
        // `o || d` tests it; an `if` that yields it is the same table.
        if (llvm::isa<ctjs::TruthyOp>(user)) { continue; }
        if (llvm::isa<mlir::scf::YieldOp>(user) &&
            llvm::isa<mlir::scf::IfOp>(user->getParentOp())) {
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

/// The values that name tables, grouped into families of one schema: a
/// literal, the `if` results it is yielded into, and, when a table holds
/// tables, every value stored into it and every read of it. A family may hold
/// several run-time tables; it is one C++ type, not one object.
class tableFamilies {
public:
    void add(mlir::Value value) {
        if (parent_.try_emplace(value, value).second) { members_.push_back(value); }
    }
    [[nodiscard]] bool contains(mlir::Value value) const { return parent_.contains(value); }
    mlir::Value find(mlir::Value value) {
        mlir::Value root = value;
        while (parent_[root] != root) { root = parent_[root]; }
        return parent_[value] = root;
    }
    bool unite(mlir::Value left, mlir::Value right) {
        add(left);
        add(right);
        const mlir::Value a = find(left), b = find(right);
        if (a == b) { return false; }
        parent_[b] = a;
        return true;
    }
    [[nodiscard]] llvm::ArrayRef<mlir::Value> members() const { return members_; }

private:
    llvm::DenseMap<mlir::Value, mlir::Value> parent_;
    llvm::SmallVector<mlir::Value> members_;
};

/// Grows the families to a fixpoint: a table yielded by an `if` makes the
/// `if`'s result one with it, and a family that holds a table holds tables
/// only, so everything stored into it and read from it is one child family.
/// Returns each family's child, by root.
llvm::DenseMap<mlir::Value, mlir::Value> growFamilies(tableFamilies & tables) {
    llvm::DenseMap<mlir::Value, mlir::Value> child;
    for (bool grew = true; grew;) {
        grew = false;
        // Roots move as families merge; the last pass, which grows nothing,
        // records every child under its final root.
        child.clear();
        for (std::size_t at = 0; at < tables.members().size(); ++at) {
            const mlir::Value member = tables.members()[at];
            for (mlir::OpOperand & use : member.getUses()) {
                auto yield = llvm::dyn_cast<mlir::scf::YieldOp>(use.getOwner());
                auto choice = yield ? llvm::dyn_cast<mlir::scf::IfOp>(yield->getParentOp())
                                    : mlir::scf::IfOp{};
                if (choice && use.getOperandNumber() < choice.getNumResults()) {
                    grew |= tables.unite(member, choice.getResult(use.getOperandNumber()));
                }
            }
        }
        // Per family, the values stored into it and the reads of it.
        llvm::MapVector<mlir::Value, llvm::SmallVector<mlir::Value>> payload;
        llvm::DenseSet<mlir::Value> holdsTables;
        for (const mlir::Value member : llvm::to_vector(tables.members())) {
            const mlir::Value root = tables.find(member);
            for (mlir::OpOperand & use : member.getUses()) {
                if (auto set = llvm::dyn_cast<ctjs::SetPropertyOp>(use.getOwner());
                    set && use.getOperandNumber() == 0) {
                    payload[root].push_back(set.getValue());
                    if (tables.contains(set.getValue())) { holdsTables.insert(root); }
                } else if (auto get = llvm::dyn_cast<ctjs::GetPropertyOp>(use.getOwner());
                           get && use.getOperandNumber() == 0) {
                    payload[root].push_back(get.getResult());
                }
            }
        }
        for (auto & [root, values] : payload) {
            if (!holdsTables.contains(root)) { continue; }
            for (const mlir::Value value : values) { grew |= tables.unite(values.front(), value); }
            child[root] = values.front();
        }
    }
    return child;
}

/// Why `member` is not made the way a table is: a literal, an `if` result
/// yielding only tables, or a read of a table that holds tables. Empty when
/// it is.
std::string producerProblem(mlir::Value member, const tableFamilies & tables,
                            const llvm::DenseSet<mlir::Value> & tableReads) {
    if (member.getDefiningOp<ctjs::CreateObjectOp>() || tableReads.contains(member)) { return {}; }
    if (auto choice = member.getDefiningOp<mlir::scf::IfOp>()) {
        const unsigned index = llvm::cast<mlir::OpResult>(member).getResultNumber();
        if (choice.elseBlock() && tables.contains(choice.thenYield().getOperand(index)) &&
            tables.contains(choice.elseYield().getOperand(index))) {
            return {};
        }
        return "an `if` chooses between a table and something else";
    }
    if (auto * op = member.getDefiningOp()) {
        return ("a table holds a value made by `" + op->getName().getStringRef() + "`").str();
    }
    return "a table holds a value that is not a table";
}

} // namespace

/// Tables are acyclic by the type rule: a family's value type is its child
/// family's type, so a table stored into itself, or into a table it holds,
/// would need a type that contains itself. growFamilies would make such a
/// family its own child, and this proof refuses it by name, so every
/// accepted table's values form a tree of families and shared_ptr ownership
/// cannot cycle.
void prepareNativeDictionaries(mlir::ModuleOp module) {
    // The attributes are this proof's output, never its input: clear a
    // previous run's.
    module.walk([](mlir::Operation * op) {
        op->removeAttr(kNativeDictionarySite);
        op->removeAttr(kNativeDictionaryAction);
        op->removeAttr(kNativeDictionaryReason);
        op->removeAttr(kNativeDictionaryGroup);
    });
    tableFamilies tables;
    module.walk([&](ctjs::CreateObjectOp made) { tables.add(made.getResult()); });
    const llvm::DenseMap<mlir::Value, mlir::Value> child = growFamilies(tables);
    llvm::DenseSet<mlir::Value> tableReads;
    for (const mlir::Value member : tables.members()) {
        for (mlir::Operation * user : member.getUsers()) {
            if (auto get = llvm::dyn_cast<ctjs::GetPropertyOp>(user);
                get && get.getObject() == member && tables.contains(get.getResult())) {
                tableReads.insert(get.getResult());
            }
        }
    }
    // Per family: its operations, whether it is a table at all, and why not.
    struct family {
        llvm::SmallVector<tableUse> uses;
        llvm::SmallVector<mlir::Operation *> sites;
        bool computed = false;
        std::string problem;
    };
    llvm::MapVector<mlir::Value, family> families;
    for (const mlir::Value member : tables.members()) {
        family & f = families[tables.find(member)];
        if (auto made = member.getDefiningOp<ctjs::CreateObjectOp>()) { f.sites.push_back(made); }
        std::string why = producerProblem(member, tables, tableReads);
        if (why.empty()) {
            why = tableUses(member, f.uses, f.computed,
                            [&](mlir::Value value) { return tables.contains(value); });
        }
        if (f.problem.empty()) { f.problem = why; }
    }
    // A family and every family it holds or is held by stand or fall
    // together: one C++ type spells them all.
    llvm::DenseMap<mlir::Value, mlir::Value> holder;
    for (const auto & [root, member] : child) { holder[tables.find(member)] = root; }
    const auto top = [&](mlir::Value root) {
        llvm::DenseSet<mlir::Value> seen;
        while (holder.contains(root) && seen.insert(root).second) { root = holder[root]; }
        return root;
    };
    llvm::MapVector<mlir::Value, llvm::SmallVector<mlir::Value>> trees;
    for (auto & [root, f] : families) { trees[top(root)].push_back(root); }
    int64_t group = 0;
    for (auto & [treeTop, roots] : trees) {
        std::string problem;
        bool computed = roots.size() > 1;
        for (const mlir::Value root : roots) {
            const family & f = families[root];
            if (problem.empty()) { problem = f.problem; }
            computed |= f.computed;
            // Each family holds at most one child family, so a cycle is a
            // chain of children that comes back to a family it passed.
            llvm::DenseSet<mlir::Value> chain{root};
            for (auto held = child.find(root); problem.empty() && held != child.end();
                 held = child.find(tables.find(held->second))) {
                if (!chain.insert(tables.find(held->second)).second) {
                    problem = "a table is stored into itself or into a table it holds";
                }
            }
        }
        auto * context = module.getContext();
        if (!problem.empty() || !computed) {
            if (computed) {
                for (const mlir::Value root : roots) {
                    for (mlir::Operation * site : families[root].sites) {
                        site->setAttr(kNativeDictionaryReason,
                                      mlir::StringAttr::get(
                                          context, "an object used as a table, but " + problem));
                    }
                }
            }
            continue;
        }
        for (const mlir::Value root : roots) {
            const auto id = mlir::IntegerAttr::get(mlir::IntegerType::get(context, 64), group++);
            family & f = families[root];
            for (mlir::Operation * site : f.sites) {
                site->setAttr(kNativeDictionarySite, mlir::UnitAttr::get(context));
                site->setAttr(kNativeDictionaryGroup, id);
            }
            for (const tableUse & use : f.uses) {
                use.op->setAttr(kNativeDictionaryAction,
                                mlir::StringAttr::get(context, use.action));
                use.op->setAttr(kNativeDictionaryGroup, id);
            }
        }
    }
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
        // A family's tables hold whatever is stored into any of them: one
        // value type V. The stores are indexed once per module.
        if (!dictionaryStoresIndexed_) {
            dictionaryStoresIndexed_ = true;
            op->getParentOfType<mlir::ModuleOp>().walk([&](ctjs::SetPropertyOp set) {
                if (auto group = set->getAttrOfType<mlir::IntegerAttr>(kNativeDictionaryGroup)) {
                    dictionaryStores_[group.getInt()].push_back(set.getValue());
                }
            });
        }
        const auto group = op->getAttrOfType<mlir::IntegerAttr>(kNativeDictionaryGroup).getInt();
        return DictType::get(c,
                             joinLattices(op, dictionaryStores_.lookup(group), BottomType::get(c)));
    }
    const llvm::StringRef action = nativeDictionaryAction(op);
    if (action.empty() || action == "set" || action == "plumbing") { return {}; }
    if (action == "get") {
        // A missing key reads undefined. The table itself may be a read of
        // another table, which may be undefined too (opt<dict<V>>).
        mlir::Type type = operands[0]->getValue().getType();
        if (auto optional = llvm::dyn_cast_or_null<OptType>(type)) {
            type = optional.getElementType();
        }
        auto table = llvm::dyn_cast_or_null<DictType>(type);
        if (!table) { return {}; }
        return meet(OptType::get(c, BottomType::get(c)), table.getElementType());
    }
    if (action == "object") { return operands[2]->getValue().getType(); }
    if (action == "key") { return defaultStringType(c); }
    // `delete`, `in` and a comparison with undefined answer a Boolean.
    return BoolType::get(c);
}

} // namespace ctcompile::ctnative
