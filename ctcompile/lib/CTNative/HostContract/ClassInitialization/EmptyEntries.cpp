//===- EmptyEntries.cpp - Empty entry evaluation ----------------*- C++ -*-===//
//
// Part of the class-initialization proof: folds methods that loop over
// Object.entries() of a fresh empty object. Such a for-of never runs, so a
// method whose only path returns one constant is replaced by that constant.
//
//===----------------------------------------------------------------------===//

#include "../../PartialEvaluation/Heap.h"
#include "Proof.hpp"
#include "mlir/Dialect/SCF/IR/SCF.h"
#include "mlir/Dialect/UB/IR/UBOps.h"

#include <functional>

namespace ctcompile::ctnative::class_detail {

namespace {

constexpr llvm::StringLiteral freshObject =
    "class Object.entries requires a fresh empty static getter object";
constexpr llvm::StringLiteral closedPath =
    "class Object.entries requires a proved method with a closed effect-free path";

/// True if every evaluation of `getter` allocates a new object and returns it
/// untouched.
bool freshEmptyGetter(ctjs::FuncOp getter) {
    if (!getter || !getter.getBody().hasOneBlock()) { return false; }
    ctjs::CreateObjectOp made;
    for (mlir::Operation & op : getter.getBody().front()) {
        if (llvm::isa<ctjs::FrameEnterOp, ctjs::FrameExitOp, ctjs::ConstantOp>(op)) { continue; }
        if (auto object = llvm::dyn_cast<ctjs::CreateObjectOp>(op); object && !made) {
            made = object;
            continue;
        }
        auto returned = llvm::dyn_cast<ctjs::ReturnOp>(op);
        return returned && made && returned.getValue() == made.getResult() &&
               made.getResult().hasOneUse();
    }
    return false;
}

/// Returns the most arguments any invocation passes `fn`, or nullopt when an
/// invocation is not an ordinary call through a key `fn` is installed under.
/// Dynamic keys are not handled here: the check of every class operation runs
/// after the fold and refuses any dynamic key that survives it, and a folded
/// body's own dynamic keys were on paths that never run.
std::optional<size_t> passedArguments(classInitialization & proof, ctjs::FuncOp fn) {
    llvm::StringSet<> keys;
    const auto installed = proof.module.walk([&](ctjs::CreateClosureOp closure) {
        if (!proof.step()) { return mlir::WalkResult::interrupt(); }
        if (proof.target(closure) != fn) { return mlir::WalkResult::advance(); }
        for (mlir::OpOperand & use : closure.getResult().getUses()) {
            auto write = llvm::dyn_cast<ctjs::SetPropertyOp>(use.getOwner());
            if (!write || use.getOperandNumber() != 2) { continue; }
            if (ctjs::constantKey(write.getKey()).empty()) { return mlir::WalkResult::interrupt(); }
            keys.insert(ctjs::constantKey(write.getKey()));
        }
        return mlir::WalkResult::advance();
    });
    if (installed.wasInterrupted()) { return std::nullopt; }
    // A Number key names the property its canonical string does.
    const bool numeric = llvm::any_of(keys, [](const auto & item) {
        const auto key = item.first();
        return key.front() == '-' || (key.front() >= '0' && key.front() <= '9') || key == "NaN" ||
               key == "Infinity";
    });
    size_t most = 0;
    const auto calls = proof.module.walk([&](mlir::Operation * op) {
        if (!proof.step()) { return mlir::WalkResult::interrupt(); }
        if (auto direct = llvm::dyn_cast<ctjs::CallDirectOp>(op);
            direct && direct.getTarget() == fn) {
            most = std::max(most, direct.getArgs().size());
        }
        auto key = ctjs::propertyKey(op);
        if (!key) { return mlir::WalkResult::advance(); }
        auto text = ctjs::constantAttr<ctjs::StringAttr>(key);
        if (!text) {
            return ctjs::constantAttr<ctjs::NumberAttr>(key) && numeric
                       ? mlir::WalkResult::interrupt()
                       : mlir::WalkResult::advance();
        }
        auto read = llvm::dyn_cast<ctjs::GetPropertyOp>(op);
        if (!read || !keys.contains(text.getValue())) { return mlir::WalkResult::advance(); }
        for (mlir::OpOperand & use : read.getResult().getUses()) {
            if (llvm::isa<ctjs::RootOp>(use.getOwner())) { continue; }
            auto call = llvm::dyn_cast<ctjs::CallOp>(use.getOwner());
            if (!call || use.getOperandNumber() != 0) { return mlir::WalkResult::interrupt(); }
            most = std::max(most, call.getArgs().size());
        }
        return mlir::WalkResult::advance();
    });
    if (calls.wasInterrupted()) { return std::nullopt; }
    return most;
}

/// Runs `fn` on the only path its invocations can take and returns the
/// constant it always returns, or refuses (nullopt). Formals that no call
/// passes are undefined; receiver reads resolve to the proved constructor and
/// getter records. It runs cf branches, and the structured form a lifted
/// entries loop takes: scf.if, an scf.while whose body never runs, and the
/// cf.switch on its exit.
std::optional<mlir::Attribute> evaluate(classInitialization & proof, ctjs::FuncOp fn,
                                        const HostContract & contract) {
    using partial_eval::value;
    auto * context = proof.module.getContext();
    // Abstract heap references: fixed tokens for the globals this evaluator
    // understands, then one fresh number per allocated object or entries
    // array. A reference with node 0 is an unknown object and fails the proof.
    enum : unsigned {
        objectToken = 1,
        entriesToken,
        openToken,
        closeToken,
        constructorToken,
        poisonToken
    };
    unsigned next = poisonToken + 1;
    llvm::DenseSet<unsigned> fresh, arrays;
    llvm::DenseMap<mlir::Value, value> values;
    auto & entry = fn.getBody().front();
    const auto undefined = value::primitive(ctjs::UndefinedAttr::get(context));
    if (auto passed = passedArguments(proof, fn)) {
        for (auto [index, formal] :
             llvm::enumerate(entry.getArguments().drop_front(ctjs::implicit_arguments))) {
            if (index >= *passed) { values[formal] = undefined; }
        }
    }
    if (!proof.reason.empty()) { return std::nullopt; }
    const auto get = [&](mlir::Value input) {
        if (auto found = values.find(input); found != values.end()) { return found->second; }
        if (auto constant = input.getDefiningOp<ctjs::ConstantOp>()) {
            return value::primitive(constant.getValue());
        }
        if (auto constant = input.getDefiningOp<mlir::arith::ConstantOp>()) {
            return value::primitive(constant.getValue());
        }
        return value{};
    };
    const auto is = [](value input, unsigned token) {
        return input.tag == value::kind::reference && input.node == token;
    };
    const auto in = [](value input, const llvm::DenseSet<unsigned> & set) {
        return input.tag == value::kind::reference && set.contains(input.node);
    };
    const auto absent = [&](value input) {
        return input.tag == value::kind::constant && input.constant == undefined.constant;
    };
    // Refuses at `op`. If `op` sees the fresh object or its entries array, it
    // could add, keep or observe a property (freshObject); otherwise the path
    // simply could not be decided (closedPath).
    const auto fail = [&](mlir::Operation & op) -> std::optional<mlir::Attribute> {
        const bool touched = llvm::any_of(op.getOperands(), [&](mlir::Value operand) {
            return in(get(operand), fresh) || in(get(operand), arrays);
        });
        proof.refuse(touched ? freshObject : closedPath);
        return std::nullopt;
    };
    const bool iterators = llvm::all_of(
        std::initializer_list<llvm::StringRef>{"Array", "__ctbrowser_for_of_open",
                                               "__ctbrowser_iter_next", "__ctbrowser_iter_close"},
        [&](llvm::StringRef name) { return llvm::is_contained(contract.initialIntrinsics, name); });
    // Runs `block` from its first operation. A ctjs.return ends the method
    // (`answer`); an scf.yield or scf.condition hands its operands back to the
    // structured operation that ran the block (`yielded`).
    enum class flow {
        failed,
        returned,
        yielded
    };
    std::optional<mlir::Attribute> answer;
    llvm::SmallVector<value> yielded;
    // A ub.poison placeholder: carried through loop arguments, never read.
    const auto poison = value::reference(poisonToken);
    // ponytail: at most 4096 executed operations. Loops over the empty entries
    // never run, so a real path is short; a longer one refuses.
    unsigned executed = 0;
    std::function<flow(mlir::Block &)> run = [&](mlir::Block & block) -> flow {
        mlir::Operation * op = &block.front();
        while (true) {
            if (++executed > 4096) {
                proof.refuse(closedPath);
                return flow::failed;
            }
            if (!proof.step()) { return flow::failed; }
            mlir::Operation * following = op->getNextNode();
            const auto jump = [&](mlir::Block * target, mlir::ValueRange operands) {
                llvm::SmallVector<value> carried;
                for (mlir::Value operand : operands) { carried.push_back(get(operand)); }
                for (auto [argument, input] : llvm::zip(target->getArguments(), carried)) {
                    values[argument] = input;
                }
                following = &target->front();
            };
            const auto failAt = [&](mlir::Operation & at) {
                (void)fail(at);
                return flow::failed;
            };
            const auto bitOf = [&](mlir::Value condition) {
                return llvm::dyn_cast_if_present<mlir::IntegerAttr>(get(condition).constant);
            };
            value result;
            if (llvm::isa<ctjs::FrameEnterOp, ctjs::FrameExitOp, ctjs::RootOp, ctjs::ConstantOp,
                          mlir::arith::ConstantOp>(op)) {
                op = following;
                continue;
            }
            if (llvm::isa<mlir::ub::PoisonOp>(op)) {
                values[op->getResult(0)] = poison;
                op = following;
                continue;
            }
            if (auto branch = llvm::dyn_cast<mlir::cf::BranchOp>(op)) {
                jump(branch.getDest(), branch.getDestOperands());
                op = following;
                continue;
            }
            if (auto branch = llvm::dyn_cast<mlir::cf::CondBranchOp>(op)) {
                auto bit = bitOf(branch.getCondition());
                if (!bit) { return failAt(*op); }
                if (bit.getValue().isZero()) {
                    jump(branch.getFalseDest(), branch.getFalseDestOperands());
                } else {
                    jump(branch.getTrueDest(), branch.getTrueDestOperands());
                }
                op = following;
                continue;
            }
            if (auto choice = llvm::dyn_cast<mlir::cf::SwitchOp>(op)) {
                auto flag = bitOf(choice.getFlag());
                if (!flag) { return failAt(*op); }
                mlir::Block * target = choice.getDefaultDestination();
                mlir::ValueRange operands = choice.getDefaultOperands();
                if (auto cases = choice.getCaseValues()) {
                    for (auto [index, label] : llvm::enumerate(cases->getValues<llvm::APInt>())) {
                        if (label == flag.getValue()) {
                            target = choice.getCaseDestinations()[index];
                            operands = choice.getCaseOperands(static_cast<unsigned>(index));
                        }
                    }
                }
                jump(target, operands);
                op = following;
                continue;
            }
            if (auto returned = llvm::dyn_cast<ctjs::ReturnOp>(op)) {
                auto constant = get(returned.getValue());
                if (constant.tag != value::kind::constant ||
                    llvm::isa<mlir::TypedAttr>(constant.constant)) {
                    return failAt(*op);
                }
                answer = constant.constant;
                return flow::returned;
            }
            if (llvm::isa<mlir::scf::YieldOp, mlir::scf::ConditionOp>(op)) {
                yielded.clear();
                for (mlir::Value operand : op->getOperands()) { yielded.push_back(get(operand)); }
                return flow::yielded;
            }
            // Structured control flow runs the region its condition picks and
            // takes what that region yields as its results.
            if (auto branch = llvm::dyn_cast<mlir::scf::IfOp>(op)) {
                auto bit = bitOf(branch.getCondition());
                if (!bit) { return failAt(*op); }
                mlir::Region & taken =
                    bit.getValue().isZero() ? branch.getElseRegion() : branch.getThenRegion();
                if (taken.empty()) {
                    op = following;
                    continue;
                }
                if (const flow inner = run(taken.front()); inner != flow::yielded) { return inner; }
                for (auto [output, input] : llvm::zip(op->getResults(), yielded)) {
                    values[output] = input;
                }
                op = following;
                continue;
            }
            // A loop over the empty entries ends at its first test: the
            // condition is false and the loop yields its arguments as they
            // were. One whose body would run refuses.
            if (auto loop = llvm::dyn_cast<mlir::scf::WhileOp>(op)) {
                mlir::Block & before = loop.getBefore().front();
                for (auto [argument, input] : llvm::zip(before.getArguments(), loop.getInits())) {
                    values[argument] = get(input);
                }
                if (const flow inner = run(before); inner != flow::yielded) { return inner; }
                auto bit = llvm::dyn_cast_if_present<mlir::IntegerAttr>(yielded.front().constant);
                if (!bit || !bit.getValue().isZero()) { return failAt(*op); }
                for (auto [output, input] :
                     llvm::zip(op->getResults(), llvm::ArrayRef(yielded).drop_front())) {
                    values[output] = input;
                }
                op = following;
                continue;
            }
            if (auto narrow = llvm::dyn_cast<mlir::arith::TruncIOp>(op)) {
                auto bit = bitOf(narrow.getIn());
                if (!bit) { return failAt(*op); }
                const auto width = narrow.getType().getIntOrFloatBitWidth();
                values[op->getResult(0)] = value::primitive(
                    mlir::IntegerAttr::get(narrow.getType(), bit.getValue().trunc(width)));
                op = following;
                continue;
            }
            if (auto compare = llvm::dyn_cast<ctjs::CompareOp>(op)) {
                result = partial_eval::compare(compare.getKind(), get(compare.getLhs()),
                                               get(compare.getRhs()), context);
            } else if (auto binary = llvm::dyn_cast<ctjs::BinaryOp>(op)) {
                result = partial_eval::binary(binary.getKind(), get(binary.getLhs()),
                                              get(binary.getRhs()), context);
            } else if (auto binary = llvm::dyn_cast<ctjs::BinaryStaticOp>(op)) {
                result = partial_eval::binary(binary.getKind(), get(binary.getLhs()),
                                              get(binary.getRhs()), context, true);
            } else if (auto unary = llvm::dyn_cast<ctjs::UnaryOp>(op)) {
                result = partial_eval::unary(unary.getKind(), get(unary.getOperand()), context);
            } else if (auto truth = llvm::dyn_cast<ctjs::TruthyOp>(op)) {
                if (auto bit = partial_eval::truthy(get(truth.getValue()))) {
                    result = value::primitive(
                        mlir::IntegerAttr::get(mlir::IntegerType::get(context, 1), *bit));
                }
            } else if (auto load = llvm::dyn_cast<ctjs::LoadGlobalOp>(op)) {
                const auto name = load.getName();
                if (!iterators &&
                    (name == "__ctbrowser_for_of_open" || name == "__ctbrowser_iter_close")) {
                    proof.refuse("class own-key loop requires original Array iterator identities");
                    return flow::failed;
                }
                result = value::reference(name == "Object"                    ? objectToken
                                          : name == "__ctbrowser_for_of_open" ? openToken
                                          : name == "__ctbrowser_iter_close"  ? closeToken
                                                                              : 0);
            } else if (auto read = llvm::dyn_cast<ctjs::GetPropertyOp>(op)) {
                const auto object = get(read.getObject());
                const auto key = ctjs::constantKey(read.getKey());
                // Each getter read allocates its own object, as its expansion would.
                bool getter = false, empty = true;
                for (auto [site, callee] : proof.getterReads) {
                    if (site != read) { continue; }
                    if (proof.throwingGetters.contains(callee)) { return failAt(*op); }
                    getter = true;
                    empty &= freshEmptyGetter(callee);
                }
                if (llvm::is_contained(proof.constructorReads, read)) {
                    result = value::reference(constructorToken);
                } else if (getter && empty) {
                    fresh.insert(next);
                    result = value::reference(next++);
                } else if (getter) {
                    values[read.getResult()] = value{}; // A proved closed getter: no effect.
                    op = following;
                    continue;
                } else if (is(object, objectToken) && key == "entries") {
                    result = value::reference(entriesToken);
                } else if (in(object, arrays) && key == "length") {
                    result = partial_eval::numeric(context, 0);
                }
            } else if (auto call = llvm::dyn_cast<ctjs::CallOp>(op)) {
                const auto callee = get(call.getCallee());
                const auto receiver = get(call.getReceiver());
                llvm::SmallVector<value> arguments;
                for (mlir::Value argument : call.getArgs()) { arguments.push_back(get(argument)); }
                if (is(callee, entriesToken) && is(receiver, objectToken) &&
                    arguments.size() == 1) {
                    if (!in(arguments.front(), fresh)) {
                        proof.refuse(freshObject);
                        return flow::failed;
                    }
                    arrays.insert(next);
                    result = value::reference(next++);
                } else if (is(callee, openToken) && absent(receiver) && arguments.size() == 1 &&
                           in(arguments.front(), arrays)) {
                    result = undefined; // An array iterates by index.
                } else if (is(callee, closeToken) && absent(receiver) && arguments.size() == 2 &&
                           absent(arguments.front()) &&
                           partial_eval::truthy(arguments.back()) == false) {
                    result = undefined;
                }
            } else if (auto iterable = llvm::dyn_cast<ctjs::IterableOp>(op)) {
                if (in(get(iterable->getOperand(0)), arrays)) {
                    result = get(iterable->getOperand(0));
                }
            }
            if (result.tag == value::kind::unknown ||
                (result.tag == value::kind::reference && result.node == 0) ||
                op->getNumResults() != 1) {
                return failAt(*op);
            }
            values[op->getResult(0)] = result;
            op = following;
        }
    };
    if (run(entry) == flow::returned) { return answer; }
    if (proof.reason.empty()) { proof.refuse(closedPath); }
    return std::nullopt;
}

/// Replaces the body of method `fn` with `return <constant>` when evaluate()
/// proves it always returns that constant, and drops the records other proofs
/// kept for the erased operations.
bool foldMethod(classInitialization & proof, ctjs::FuncOp fn, const HostContract & contract) {
    if (!proof.methods.contains(fn) || fn.getBody().empty()) { return proof.refuse(closedPath); }
    const auto answer = evaluate(proof, fn, contract);
    if (!answer) { return false; }
    // The whole body is erased, unless it holds an operation another proof
    // still records, or one that creates closures, cells, globals or accessors;
    // then the method refuses.
    llvm::DenseSet<mlir::Operation *> erased;
    bool held = false;
    const auto scanned = fn.getBody().walk([&](mlir::Operation * op) {
        if (!proof.step()) { return mlir::WalkResult::interrupt(); }
        erased.insert(op);
        held |= llvm::isa<ctjs::CreateClosureOp, ctjs::CreateCellOp, ctjs::StoreGlobalOp,
                          ctjs::DefineAccessorOp>(op) ||
                proof.setup.contains(op) || proof.retainedSetup.contains(op) ||
                proof.globalHolderLoads.contains(op) || proof.domDataOperations.contains(op) ||
                proof.instanceOfResults.contains(op) ||
                llvm::is_contained(proof.inertCalleeCalls, op) ||
                (op->getNumResults() == 1 && proof.holderReads.count(op->getResult(0)));
        return mlir::WalkResult::advance();
    });
    if (scanned.wasInterrupted()) { return false; }
    if (held) { return proof.refuse(closedPath); }
    if (!proof.spend(erased.size() + proof.getterReads.size() + proof.constructorReads.size() +
                     proof.staticMethodReads.size() + proof.cellReads.size() +
                     proof.captureReads.size() + 8)) {
        return false;
    }
    proof.forgetRecords(fn.getBody());
    const auto gone = [&](mlir::Operation * op) { return erased.contains(op); };
    llvm::erase_if(proof.getterReads, [&](auto & item) { return gone(item.first); });
    llvm::erase_if(proof.staticMethodReads, [&](auto & item) { return gone(item.first); });
    llvm::erase_if(proof.constructorReads, [&](ctjs::GetPropertyOp read) { return gone(read); });
    llvm::erase_if(proof.cellReads, [&](ctjs::CellGetOp read) { return gone(read); });
    for (mlir::Operation * op : erased) {
        proof.methodCalls.erase(op);
        proof.errorOperations.erase(op);
        proof.cellOperations.erase(op);
    }
    auto & entry = fn.getBody().front();
    auto enter = llvm::dyn_cast<ctjs::FrameEnterOp>(entry.front());
    mlir::OwningOpRef<ctjs::FuncOp> copy(llvm::cast<ctjs::FuncOp>(fn->cloneWithoutRegions()));
    auto * body = new mlir::Block;
    copy->getBody().push_back(body);
    for (mlir::BlockArgument argument : entry.getArguments()) {
        body->addArgument(argument.getType(), argument.getLoc());
    }
    auto at = mlir::OpBuilder::atBlockEnd(body);
    mlir::Value frame = enter ? at.clone(*enter.getOperation())->getResult(0) : mlir::Value{};
    auto returned = ctjs::ConstantOp::create(at, fn.getLoc(), *answer);
    if (frame) { ctjs::FrameExitOp::create(at, fn.getLoc(), frame); }
    ctjs::ReturnOp::create(at, fn.getLoc(), returned.getResult());
    fn.getBody().takeBody(copy->getBody());
    fn->removeAttr("ctjs.not_structured");
    return true;
}

} // namespace

bool classInitialization::foldEmptyEntries(const HostContract & contract) {
    // Object.entries of a fresh empty object is empty, so a for-of over it
    // never runs. Fold each method that reads Object.entries to the constant it
    // returns. This runs after every class has recorded its getter and
    // constructor reads.
    // ponytail: folds whole methods only; keeping effects around the empty
    // loop would need a fold of just the loop segment.
    if (!llvm::is_contained(contract.initialIntrinsics, "Object")) { return true; }
    llvm::SetVector<mlir::Operation *> scopes;
    if (!walkSteps(module, [&](ctjs::GetPropertyOp read) {
            auto load = read.getObject().getDefiningOp<ctjs::LoadGlobalOp>();
            if (load && load.getName() == "Object" &&
                ctjs::constantKey(read.getKey()) == "entries") {
                scopes.insert(read->getParentOfType<ctjs::FuncOp>());
            }
            return mlir::WalkResult::advance();
        })) {
        return false;
    }
    for (mlir::Operation * scope : scopes) {
        if (!foldMethod(*this, llvm::cast<ctjs::FuncOp>(scope), contract)) { return false; }
    }
    return true;
}

} // namespace ctcompile::ctnative::class_detail
