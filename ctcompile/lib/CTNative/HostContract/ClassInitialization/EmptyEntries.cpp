#include "../../PartialEvaluation/Heap.h"
#include "Proof.hpp"

namespace ctcompile::ctnative::class_detail {

namespace {

constexpr llvm::StringLiteral freshObject =
    "class Object.entries requires a fresh empty static getter object";
constexpr llvm::StringLiteral closedPath =
    "class Object.entries requires a proved method with a closed effect-free path";

// Every evaluation of `getter` allocates a new object and returns it untouched.
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

// The most arguments any invocation passes `fn`, or nullopt when an invocation
// is not an ordinary call of a key `fn` is installed under. The complete class
// census runs after the fold and refuses every dynamic key that survives it; a
// folded body's own dynamic keys were on paths that never run.
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

// Run `fn` on the only path its invocations can take. Formals no call passes
// are undefined; receiver reads are the proved constructor/getter records.
// Returns the constant it always returns, or refuses.
// ponytail: control-flow bodies (the exception-carrying for-of) only; a lifted
// scf body needs clearOwnFieldLoop's region evaluator.
std::optional<mlir::Attribute> evaluate(classInitialization & proof, ctjs::FuncOp fn,
                                        const HostContract & contract) {
    using partial_eval::value;
    auto * context = proof.module.getContext();
    enum : unsigned {
        objectToken = 1,
        entriesToken,
        openToken,
        closeToken,
        constructorToken
    };
    unsigned next = constructorToken + 1;
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
    // An unproved operation that sees the fresh object or its entries could
    // add, keep or observe a property; anything else leaves the path open.
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
    mlir::Operation * op = &entry.front();
    // ponytail: a fixed bound on the executed path; loops over entries never run.
    for (unsigned executed = 0; executed != 4096; ++executed) {
        if (!proof.step()) { return std::nullopt; }
        mlir::Operation * following = op->getNextNode();
        const auto jump = [&](mlir::Block * target, mlir::ValueRange operands) {
            llvm::SmallVector<value> carried;
            for (mlir::Value operand : operands) { carried.push_back(get(operand)); }
            for (auto [argument, input] : llvm::zip(target->getArguments(), carried)) {
                values[argument] = input;
            }
            following = &target->front();
        };
        value result;
        if (llvm::isa<ctjs::FrameEnterOp, ctjs::FrameExitOp, ctjs::RootOp, ctjs::ConstantOp,
                      mlir::arith::ConstantOp>(op)) {
            op = following;
            continue;
        }
        if (auto branch = llvm::dyn_cast<mlir::cf::BranchOp>(op)) {
            jump(branch.getDest(), branch.getDestOperands());
            op = following;
            continue;
        }
        if (auto branch = llvm::dyn_cast<mlir::cf::CondBranchOp>(op)) {
            auto bit =
                llvm::dyn_cast_if_present<mlir::IntegerAttr>(get(branch.getCondition()).constant);
            if (!bit) { return fail(*op); }
            if (bit.getValue().isZero()) {
                jump(branch.getFalseDest(), branch.getFalseDestOperands());
            } else {
                jump(branch.getTrueDest(), branch.getTrueDestOperands());
            }
            op = following;
            continue;
        }
        if (auto returned = llvm::dyn_cast<ctjs::ReturnOp>(op)) {
            auto answer = get(returned.getValue());
            if (answer.tag != value::kind::constant ||
                llvm::isa<mlir::TypedAttr>(answer.constant)) {
                return fail(*op);
            }
            return answer.constant;
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
                return std::nullopt;
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
                if (proof.throwingGetters.contains(callee)) { return fail(*op); }
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
            if (is(callee, entriesToken) && is(receiver, objectToken) && arguments.size() == 1) {
                if (!in(arguments.front(), fresh)) {
                    proof.refuse(freshObject);
                    return std::nullopt;
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
            if (in(get(iterable->getOperand(0)), arrays)) { result = get(iterable->getOperand(0)); }
        }
        if (result.tag == value::kind::unknown ||
            (result.tag == value::kind::reference && result.node == 0) ||
            op->getNumResults() != 1) {
            return fail(*op);
        }
        values[op->getResult(0)] = result;
        op = following;
    }
    proof.refuse(closedPath);
    return std::nullopt;
}

bool foldMethod(classInitialization & proof, ctjs::FuncOp fn, const HostContract & contract) {
    if (!proof.methods.contains(fn) || fn.getBody().empty()) { return proof.refuse(closedPath); }
    const auto answer = evaluate(proof, fn, contract);
    if (!answer) { return false; }
    // The body goes. Operations other proofs still hold, or that carry
    // captures and publications of their own, keep it instead.
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
    // never runs. Fold each method that evaluates to one constant that way,
    // after every class has recorded its getter and constructor reads.
    // ponytail: whole methods; effects around the empty loop need a segment
    // fold that keeps them.
    if (!llvm::is_contained(contract.initialIntrinsics, "Object")) { return true; }
    llvm::SetVector<mlir::Operation *> scopes;
    const auto walked = module.walk([&](ctjs::GetPropertyOp read) {
        if (!step()) { return mlir::WalkResult::interrupt(); }
        auto load = read.getObject().getDefiningOp<ctjs::LoadGlobalOp>();
        if (load && load.getName() == "Object" && ctjs::constantKey(read.getKey()) == "entries") {
            scopes.insert(read->getParentOfType<ctjs::FuncOp>());
        }
        return mlir::WalkResult::advance();
    });
    if (walked.wasInterrupted()) { return false; }
    for (mlir::Operation * scope : scopes) {
        if (!foldMethod(*this, llvm::cast<ctjs::FuncOp>(scope), contract)) { return false; }
    }
    return true;
}

} // namespace ctcompile::ctnative::class_detail
