//===- ClosureLifting/SpreadParameters.cpp - Spread object parameters -----===//
//
// Finds the parameters whose every call passes an object literal of one
// shape, and writes their `typeof` tests and spreads out for that shape
// (SpreadParameters.h).
//
//===----------------------------------------------------------------------===//

#include "SpreadParameters.h"

#include "mlir/Dialect/SCF/IR/SCF.h"
#include "mlir/IR/Builders.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringSet.h"

#include <functional>
#include <optional>
#include <string>

namespace ctcompile::ctnative::lowering_detail {
namespace {

/// A literal's own keys, in property order.
using shape = llvm::SmallVector<std::string, 4>;

/// True for a canonical array index ("0", "17"): it is enumerated before every
/// string key (OrdinaryOwnPropertyKeys, ECMA-262 10.1.11.1), so the order the
/// keys are written in would not be property order.
bool arrayIndexKey(llvm::StringRef key) {
    return !key.empty() && llvm::all_of(key, llvm::isDigit) && (key == "0" || key[0] != '0');
}

/// The own keys `literal` has when `call` runs, in property order, or nullopt
/// when they are not all known. Before the call, every use must be a
/// constant-key read or write on the literal itself, directly in the call's
/// block, so each runs exactly once and no other name could add a key. The
/// first write of each key must define it (ctjs.define_own): a plain
/// assignment to a key the object does not have yet consults its prototype
/// chain, which code outside this program can change. Uses after the call do
/// not change what the call sees.
std::optional<shape> literalShape(ctjs::CreateObjectOp literal, mlir::Operation * call) {
    mlir::Block * block = call->getBlock();
    if (literal->getBlock() != block) { return std::nullopt; }
    struct write {
        mlir::Operation * op;
        llvm::StringRef key;
        bool defines;
    };
    llvm::SmallVector<write> writes;
    for (mlir::OpOperand & use : literal.getResult().getUses()) {
        mlir::Operation * user = block->findAncestorOpInBlock(*use.getOwner());
        if (user && (user == call || call->isBeforeInBlock(user))) { continue; }
        if (!user || user != use.getOwner() || use.getOperandNumber() != 0) { return std::nullopt; }
        const mlir::Value key = ctjs::propertyKey(user);
        if (!key || !ctjs::ordinaryKey(key) || arrayIndexKey(ctjs::constantKey(key))) {
            return std::nullopt;
        }
        if (llvm::isa<ctjs::SetPropertyOp>(user)) {
            writes.push_back({user, ctjs::constantKey(key), user->hasAttr(ctjs::defineOwnMarker)});
        } else if (!llvm::isa<ctjs::GetPropertyOp>(user)) {
            return std::nullopt;
        }
    }
    llvm::sort(writes,
               [](const write & a, const write & b) { return a.op->isBeforeInBlock(b.op); });
    shape keys;
    llvm::StringSet<> seen;
    for (const write & each : writes) {
        // Writing an existing key again keeps its place in property order.
        if (!seen.insert(each.key).second) { continue; }
        if (!each.defines) { return std::nullopt; }
        keys.push_back(each.key.str());
    }
    return keys;
}

/// `op` when it is `typeof parameter`, else null.
ctjs::UnaryOp typeOf(mlir::Operation * op, mlir::Value parameter) {
    auto unary = llvm::dyn_cast_or_null<ctjs::UnaryOp>(op);
    return unary && unary.getKind() == ctjs::UnaryKind::TypeOf && unary.getOperand() == parameter
               ? unary
               : ctjs::UnaryOp{};
}

/// The arm `branch` takes when its condition is `typeof parameter` compared
/// (`==` or `===`, either way round, possibly negated) with a constant string,
/// given that `typeof parameter` is "object": true for the then arm. Nullopt
/// for any other condition.
std::optional<bool> armOfTypeofTest(mlir::scf::IfOp branch, mlir::Value parameter) {
    auto truthy = branch.getCondition().getDefiningOp<ctjs::TruthyOp>();
    mlir::Value tested = truthy ? truthy.getValue() : mlir::Value{};
    bool negated = false;
    if (auto unary = tested ? tested.getDefiningOp<ctjs::UnaryOp>() : ctjs::UnaryOp{};
        unary && unary.getKind() == ctjs::UnaryKind::Not) {
        negated = true;
        tested = unary.getOperand();
    }
    auto compare = tested ? tested.getDefiningOp<ctjs::CompareOp>() : ctjs::CompareOp{};
    if (!compare || (compare.getKind() != ctjs::CompareKind::Eq &&
                     compare.getKind() != ctjs::CompareKind::StrictEq)) {
        return std::nullopt;
    }
    mlir::Value other = typeOf(compare.getLhs().getDefiningOp(), parameter)   ? compare.getRhs()
                        : typeOf(compare.getRhs().getDefiningOp(), parameter) ? compare.getLhs()
                                                                              : mlir::Value{};
    auto text = other ? ctjs::constantAttr<ctjs::StringAttr>(other) : ctjs::StringAttr{};
    if (!text) { return std::nullopt; }
    return (text.getValue() == "object") != negated;
}

/// Whether the arm of `branch` that `then` does not select holds nothing but
/// constants, fresh object literals, constant-key writes into them and its
/// yield, so dropping it erases no call and nothing the program can observe.
/// The literals it would erase are added to `literals`.
bool droppableArm(mlir::scf::IfOp branch, bool then,
                  llvm::SmallVectorImpl<mlir::Operation *> * literals = nullptr) {
    mlir::Region & region = then ? branch.getElseRegion() : branch.getThenRegion();
    if (region.empty()) { return true; }
    mlir::Block & arm = region.front();
    for (mlir::Operation & op : arm) {
        auto write = llvm::dyn_cast<ctjs::SetPropertyOp>(op);
        auto owner = write ? write.getObject().getDefiningOp<ctjs::CreateObjectOp>()
                           : ctjs::CreateObjectOp{};
        if (llvm::isa<ctjs::ConstantOp, ctjs::CreateObjectOp, mlir::scf::YieldOp>(op) ||
            (owner && owner->getBlock() == &arm && ctjs::constantKey(write.getKey()).size())) {
            continue;
        }
        return false;
    }
    if (literals) {
        for (mlir::Operation & op : arm) {
            if (llvm::isa<ctjs::CreateObjectOp>(op)) { literals->push_back(&op); }
        }
    }
    return true;
}

/// Replaces `branch` by the operations and yielded values of one arm.
void keepArm(mlir::scf::IfOp branch, bool then) {
    mlir::Region & region = then ? branch.getThenRegion() : branch.getElseRegion();
    if (!region.empty()) {
        mlir::Block & arm = region.front();
        auto yield = llvm::cast<mlir::scf::YieldOp>(arm.getTerminator());
        branch->getResults().replaceAllUsesWith(yield.getOperands());
        yield.erase();
        branch->getBlock()->getOperations().splice(branch->getIterator(), arm.getOperations());
    }
    branch.erase();
}

/// Erases `value`'s defining operation, and then its operands' defining
/// operations, while each is a constant, a comparison, a negation, a truthy
/// test or a `typeof` with no use left.
void eraseDeadTest(mlir::Value value) {
    mlir::Operation * op = value ? value.getDefiningOp() : nullptr;
    if (!op || !op->use_empty() ||
        !llvm::isa<ctjs::ConstantOp, ctjs::CompareOp, ctjs::UnaryOp, ctjs::TruthyOp>(op)) {
        return;
    }
    llvm::SmallVector<mlir::Value> operands(op->getOperands());
    op->erase();
    for (mlir::Value operand : operands) { eraseDeadTest(operand); }
}

/// Whether every use of `value` (the tested value, or the result of an scf.if
/// that selects it) is one the rewrite turns into constant-key reads:
/// `typeof tested`, a constant-key read, a spread into a fresh literal, the
/// arm an scf.if testing `typeof tested` keeps, or a use `accepted` allows.
bool usesFit(mlir::Value value, mlir::Value tested,
             llvm::function_ref<bool(mlir::OpOperand &)> accepted) {
    for (mlir::OpOperand & use : value.getUses()) {
        mlir::Operation * user = use.getOwner();
        if (typeOf(user, tested)) { continue; }
        if (llvm::isa<ctjs::GetPropertyOp>(user) && use.getOperandNumber() == 0 &&
            !ctjs::constantKey(ctjs::propertyKey(user)).empty()) {
            continue;
        }
        if (auto copy = llvm::dyn_cast<ctjs::CopyPropsOp>(user);
            copy && copy.getSource() == value && copy.getTarget() != value &&
            copy.getTarget().getDefiningOp<ctjs::CreateObjectOp>()) {
            continue;
        }
        if (auto yield = llvm::dyn_cast<mlir::scf::YieldOp>(user)) {
            auto branch = llvm::dyn_cast<mlir::scf::IfOp>(yield->getParentOp());
            const std::optional<bool> arm = branch ? armOfTypeofTest(branch, tested) : std::nullopt;
            if (!arm || !droppableArm(branch, *arm)) { return false; }
            // The arm the test drops goes, and this use with it; the arm it
            // keeps hands the value to the branch's result.
            if ((yield->getParentRegion() == &branch.getThenRegion()) != *arm) { continue; }
            if (!usesFit(branch.getResult(use.getOperandNumber()), tested, accepted)) {
                return false;
            }
            continue;
        }
        if (accepted(use)) { continue; }
        return false;
    }
    return true;
}

/// Writes out, in the function holding `value`, what `value` having the own
/// keys `keys` decides: each scf.if testing `typeof value` keeps the arm the
/// test selects, `typeof value` is "object", and each spread of it into a
/// fresh literal is one read and one own-property definition per key.
/// Returns whether anything changed.
bool rewriteValue(mlir::Value value, const shape & keys,
                  llvm::SmallVectorImpl<mlir::Operation *> & erasedLiterals) {
    mlir::Operation * anchor =
        value.getDefiningOp() ? value.getDefiningOp() : value.getParentBlock()->getParentOp();
    auto fn = llvm::isa<ctjs::FuncOp>(anchor) ? llvm::cast<ctjs::FuncOp>(anchor)
                                              : anchor->getParentOfType<ctjs::FuncOp>();
    bool changed = false;
    // The tests first, innermost first, while `typeof` still names the value.
    llvm::SmallVector<std::pair<mlir::scf::IfOp, bool>> tests;
    fn.walk([&](mlir::scf::IfOp branch) {
        auto arm = armOfTypeofTest(branch, value);
        if (arm && droppableArm(branch, *arm)) { tests.emplace_back(branch, *arm); }
    });
    for (auto [branch, arm] : tests) {
        const mlir::Value condition = branch.getCondition();
        (void)droppableArm(branch, arm, &erasedLiterals);
        keepArm(branch, arm);
        eraseDeadTest(condition);
        changed = true;
    }
    // `typeof` of an object literal is "object".
    llvm::SmallVector<ctjs::UnaryOp> typeofs;
    for (mlir::Operation * user : value.getUsers()) {
        if (auto test = typeOf(user, value)) { typeofs.push_back(test); }
    }
    for (ctjs::UnaryOp test : typeofs) {
        mlir::OpBuilder at(test);
        auto object = ctjs::ConstantOp::create(at, test.getLoc(), test.getResult().getType(),
                                               ctjs::StringAttr::get(at.getContext(), "object"));
        test.getResult().replaceAllUsesWith(object.getResult());
        test.erase();
        changed = true;
    }
    // Each spread into a fresh literal copies the keys in order.
    llvm::SmallVector<ctjs::CopyPropsOp> copies;
    for (mlir::Operation * user : value.getUsers()) {
        if (auto copy = llvm::dyn_cast<ctjs::CopyPropsOp>(user);
            copy && copy.getSource() == value) {
            copies.push_back(copy);
        }
    }
    for (ctjs::CopyPropsOp copy : copies) {
        mlir::OpBuilder at(copy);
        const mlir::Type type = value.getType();
        for (const std::string & key : keys) {
            const mlir::Value name = ctjs::ConstantOp::create(
                at, copy.getLoc(), type, ctjs::StringAttr::get(at.getContext(), key));
            const mlir::Value read =
                ctjs::GetPropertyOp::create(at, copy.getLoc(), type, value, name);
            ctjs::SetPropertyOp::create(at, copy.getLoc(), copy.getTarget(), name, read)
                ->setAttr(ctjs::defineOwnMarker, at.getUnitAttr());
        }
        copy.erase();
        changed = true;
    }
    return changed;
}

/// The own keys `literal` keeps for the rest of the function, in property
/// order, when every write of it is a constant-key write directly in its
/// block that comes before every other use, the first write of each key
/// defines it, and no key is an array index or a reserved name; nullopt
/// otherwise. `writes` receives those writes.
std::optional<shape> fixedShape(ctjs::CreateObjectOp literal,
                                llvm::SmallPtrSetImpl<mlir::Operation *> & writes) {
    mlir::Block * block = literal->getBlock();
    mlir::Operation * firstRead = nullptr;
    llvm::SmallVector<std::pair<mlir::Operation *, bool>> ordered;
    for (mlir::OpOperand & use : literal.getResult().getUses()) {
        mlir::Operation * user = block->findAncestorOpInBlock(*use.getOwner());
        if (!user) { return std::nullopt; }
        auto write = llvm::dyn_cast<ctjs::SetPropertyOp>(use.getOwner());
        if (write && user == use.getOwner() && use.getOperandNumber() == 0) {
            const mlir::Value key = write.getKey();
            if (!ctjs::ordinaryKey(key) || arrayIndexKey(ctjs::constantKey(key))) {
                return std::nullopt;
            }
            ordered.emplace_back(user, user->hasAttr(ctjs::defineOwnMarker));
            continue;
        }
        if (!firstRead || user->isBeforeInBlock(firstRead)) { firstRead = user; }
    }
    llvm::sort(ordered,
               [](const auto & a, const auto & b) { return a.first->isBeforeInBlock(b.first); });
    shape keys;
    llvm::StringSet<> seen;
    for (const auto & [write, defines] : ordered) {
        if (firstRead && !write->isBeforeInBlock(firstRead)) { return std::nullopt; }
        writes.insert(write);
        const llvm::StringRef key =
            ctjs::constantKey(llvm::cast<ctjs::SetPropertyOp>(write).getKey());
        if (!seen.insert(key).second) { continue; }
        if (!defines) { return std::nullopt; }
        keys.push_back(key.str());
    }
    return keys;
}

} // namespace

unsigned rewriteSpreadParameters(llvm::ArrayRef<calledFunction> functions,
                                 llvm::SmallVectorImpl<mlir::Operation *> & erasedLiterals) {
    // Which function each call reaches, and which list a function's calls are in.
    llvm::DenseMap<mlir::Operation *, ctjs::FuncOp> calleeOf;
    llvm::DenseMap<mlir::Operation *, const calledFunction *> rowOf;
    for (const calledFunction & row : functions) {
        ctjs::FuncOp function = row.function;
        rowOf[function.getOperation()] = &row;
        for (const auto & call : row.calls) { calleeOf[call.first] = row.function; }
    }
    // Every JavaScript parameter of a function whose calls are all listed, in
    // a function that does not read `arguments` (whose elements alias the
    // parameters in sloppy mode).
    llvm::SetVector<mlir::Value> tracked;
    for (const calledFunction & row : functions) {
        ctjs::FuncOp fn = row.function;
        if (!fn || fn.getBody().empty() || row.calls.empty()) { continue; }
        const bool readsArguments = fn.walk([](ctjs::MakeArgumentsOp) {
                                          return mlir::WalkResult::interrupt();
                                      }).wasInterrupted();
        if (readsArguments) { continue; }
        mlir::Block & entry = fn.getBody().front();
        for (unsigned i = ctjs::implicit_arguments; i < entry.getNumArguments(); ++i) {
            if (!entry.getArgument(i).use_empty()) { tracked.insert(entry.getArgument(i)); }
        }
    }
    // A use a tracked parameter forwards to another tracked parameter.
    const auto forwards = [&](mlir::OpOperand & use) {
        mlir::Operation * call = use.getOwner();
        ctjs::FuncOp callee = calleeOf.lookup(call);
        const unsigned first = llvm::isa<ctjs::CallDirectOp>(call) ? 3 : 2;
        if (!callee || callee.getBody().empty() ||
            !llvm::isa<ctjs::CallOp, ctjs::CallDirectOp>(call) || use.getOperandNumber() < first) {
            return false;
        }
        mlir::Block & entry = callee.getBody().front();
        const unsigned index = ctjs::implicit_arguments + use.getOperandNumber() - first;
        return index < entry.getNumArguments() && tracked.contains(entry.getArgument(index));
    };
    // A greatest fixpoint: drop a parameter with a use that does not fit or a
    // call that passes something else, until nothing changes. A shape is
    // known once every call's argument has one and they all agree; a
    // parameter forwarded around a cycle keeps no shape and is left as it is.
    llvm::DenseMap<mlir::Value, shape> shapes;
    for (bool changed = true; changed;) {
        changed = false;
        // `tracked` stays as it is during a round, since the use checks read
        // it; what fails is dropped after.
        llvm::SmallVector<mlir::Value> dropped;
        for (mlir::Value parameter : tracked) {
            auto argument = llvm::cast<mlir::BlockArgument>(parameter);
            const calledFunction & row = *rowOf.lookup(argument.getOwner()->getParentOp());
            const unsigned j = argument.getArgNumber() - ctjs::implicit_arguments;
            std::optional<shape> common;
            bool pending = false;
            bool fits = usesFit(parameter, parameter, forwards);
            for (const auto & [call, args] : row.calls) {
                if (!fits) { break; }
                const mlir::Value arg = j < args.size() ? args[j] : mlir::Value{};
                std::optional<shape> passed;
                if (auto literal =
                        arg ? arg.getDefiningOp<ctjs::CreateObjectOp>() : ctjs::CreateObjectOp{}) {
                    passed = literalShape(literal, call);
                } else if (arg && tracked.contains(arg)) {
                    const auto known = shapes.find(arg);
                    if (known == shapes.end()) {
                        pending = true;
                        continue;
                    }
                    passed = known->second;
                }
                fits = passed && (!common || *common == *passed);
                if (fits) { common = passed; }
            }
            if (!fits) {
                dropped.push_back(parameter);
                continue;
            }
            if (!pending && common && !shapes.contains(parameter)) {
                shapes[parameter] = *common;
                changed = true;
            }
        }
        for (mlir::Value parameter : dropped) {
            tracked.remove(parameter);
            shapes.erase(parameter);
            changed = true;
        }
    }
    unsigned rewritten = 0;
    for (mlir::Value parameter : tracked) {
        const auto known = shapes.find(parameter);
        if (known != shapes.end() && rewriteValue(parameter, known->second, erasedLiterals)) {
            ++rewritten;
        }
    }
    return rewritten;
}

unsigned rewriteLiteralLocals(mlir::ModuleOp module,
                              llvm::SmallVectorImpl<mlir::Operation *> & erasedLiterals) {
    // A candidate is used by `typeof`, by a spread, or by an scf.if arm.
    const auto tested = [](mlir::Value value) {
        return llvm::any_of(value.getUsers(), [&](mlir::Operation * user) {
            return typeOf(user, value) || llvm::isa<ctjs::CopyPropsOp, mlir::scf::YieldOp>(user);
        });
    };
    // The shape of `op`'s result when `op` is a fresh literal that keeps its
    // keys, or of result `k` of an scf.if whose arms each make it as a fresh
    // literal of one shape (yielded once, complete when yielded), and the
    // result fits the rewrite.
    const auto candidate =
        [&](mlir::Operation * op) -> std::optional<std::pair<mlir::Value, shape>> {
        if (auto literal = llvm::dyn_cast<ctjs::CreateObjectOp>(op)) {
            llvm::SmallPtrSet<mlir::Operation *, 8> writes;
            const std::optional<shape> keys = fixedShape(literal, writes);
            if (keys && tested(literal.getResult()) &&
                usesFit(literal.getResult(), literal.getResult(),
                        [&](mlir::OpOperand & use) { return writes.contains(use.getOwner()); })) {
                return std::pair{literal.getResult(), *keys};
            }
            return std::nullopt;
        }
        auto branch = llvm::dyn_cast<mlir::scf::IfOp>(op);
        if (!branch || branch.getElseRegion().empty()) { return std::nullopt; }
        for (unsigned k = 0; k < branch.getNumResults(); ++k) {
            const mlir::Value value = branch.getResult(k);
            if (!tested(value)) { continue; }
            std::optional<shape> common;
            bool fits = true;
            for (mlir::Region * arm : {&branch.getThenRegion(), &branch.getElseRegion()}) {
                mlir::Operation * yield = arm->front().getTerminator();
                const mlir::Value made = yield->getOperand(k);
                auto literal = made.getDefiningOp<ctjs::CreateObjectOp>();
                const std::optional<shape> keys =
                    literal && literal->getBlock() == &arm->front() &&
                            llvm::count(yield->getOperands(), made) == 1
                        ? literalShape(literal, yield)
                        : std::nullopt;
                fits = fits && keys && (!common || *common == *keys);
                if (fits) { common = keys; }
            }
            if (fits && common && usesFit(value, value, [](mlir::OpOperand &) { return false; })) {
                return std::pair{value, *common};
            }
        }
        return std::nullopt;
    };
    llvm::SmallVector<ctjs::FuncOp> functions;
    module.walk([&](ctjs::FuncOp fn) { functions.push_back(fn); });
    unsigned rewritten = 0;
    for (ctjs::FuncOp fn : functions) {
        // One at a time, found afresh each time: folding one value's test can
        // erase another candidate. Each rewrite removes the uses that made its
        // value a candidate, so the loop ends; the bound is a backstop.
        for (unsigned round = 0; round < 1000; ++round) {
            std::optional<std::pair<mlir::Value, shape>> found;
            fn.walk([&](mlir::Operation * op) {
                found = candidate(op);
                return found ? mlir::WalkResult::interrupt() : mlir::WalkResult::advance();
            });
            if (!found || !rewriteValue(found->first, found->second, erasedLiterals)) { break; }
            ++rewritten;
        }
    }
    return rewritten;
}

} // namespace ctcompile::ctnative::lowering_detail
