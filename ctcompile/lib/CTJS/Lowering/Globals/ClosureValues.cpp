//===- ClosureValues.cpp - Which closures a value can hold ----------------===//
//
// Implements ClosureValues (ClosureValues.h). A query walks backward from a
// value to everything that can flow into it and collects the closures, array
// literals and object literals it meets:
//
//  - A block argument takes what each predecessor passes it.
//  - A parameter takes the matching argument of every call that may enter its
//    function (the callers, below), or nothing where a call passes fewer, as
//    the parameter is then undefined. The callee argument is each closure
//    made of the function.
//  - A binding read (ctjs.cell_get, or ctjs.load_upvalue of the cell a
//    capture slot holds) takes the cell's initial value and every value
//    written to it: each ctjs.cell_set, and each ctjs.store_upvalue in a
//    function that captured it, through nested captures.
//  - A call takes what the functions its callee may hold return, and a direct
//    call what its symbol returns. Those values are walked in the call's
//    frame first: there the function's parameters take this call's
//    arguments, not every caller's, and the walk goes back to the steps
//    above where it leaves the frame (inFrame).
//  - A step of an array destructuring (the k-th `__ctbrowser_iter_next` on an
//    `__ctbrowser_iter_open` in the same block) takes the k-th element of each
//    array literal the source may be. The literal must be used only by its
//    own appends, by destructurings, and by `return` into calls whose results
//    are only destructured, so nothing changes it before it is read; and the
//    program must write no prototype table the realm made
//    (read_prototype_writes), so it iterates its elements. When the source is
//    a call's result, a literal made in that call's frame has its element
//    walked in that frame.
//  - An operator answers a primitive, which is no closure.
//
// Anything else is unknown: a property read, a global, `this`, a value a
// branch produces, or a parameter of a function whose callers are not all
// seen.
//
// The callers of a function are found forward from each closure made of it
// and from its own callee argument. Each use must call it, bind it to a
// binding whose every read is again followed, or define it as a method of an
// object literal. The callers of such a method are the calls through each
// read of its key off the literal, and the literal must be used only to read
// keys it owns (a key it does not own is looked up on its prototype, where an
// inherited getter would see it), as the receiver of its own methods that
// leave `this` unused, and through bindings. A direct call of the symbol is a
// caller too.
//
// What the realm's prototype tables hold when the script starts is taken to
// be the realm's own, as read_prototype_writes takes it.
//
//===----------------------------------------------------------------------===//
#include "ClosureValues.h"

#include "Effects.h"
#include "RegisterFlow.h"

#include "mlir/IR/SymbolTable.h"
#include "mlir/Interfaces/ControlFlowInterfaces.h"
#include "llvm/ADT/STLExtras.h"

namespace ctcompile::ctjs::globals_detail {
namespace {

/// The symbol of `function`, quoted for a reason.
std::string quoted(FuncOp function) {
    return "`" + function.getSymName().str() + "`";
}

/// Pushes what each predecessor passes to `argument`, a block argument that
/// is not an entry block's, or says why that is not known.
std::string incoming(mlir::BlockArgument argument, llvm::SmallVectorImpl<mlir::Value> & work) {
    mlir::Block & block = *argument.getOwner();
    for (auto pred = block.pred_begin(), end = block.pred_end(); pred != end; ++pred) {
        mlir::Operation * terminator = (*pred)->getTerminator();
        auto branch = mlir::dyn_cast<mlir::BranchOpInterface>(terminator);
        if (!branch) { return "a value " + describe(terminator) + " passes"; }
        auto operands = branch.getSuccessorOperands(pred.getSuccessorIndex());
        const unsigned index = argument.getArgNumber();
        if (index >= operands.size() || operands.isOperandProduced(index)) {
            return "a value " + describe(terminator) + " produces";
        }
        work.push_back(operands[index]);
    }
    return {};
}

/// The constant-key read `call` takes its callee from when its receiver is
/// the object read from (`o.m(...)`), both followed through block arguments;
/// null otherwise.
GetPropertyOp methodRead(CallOp call) {
    const mlir::Value callee = registerFlowOrigin(call.getCallee());
    auto read = callee ? callee.getDefiningOp<GetPropertyOp>() : GetPropertyOp{};
    const mlir::Value receiver = registerFlowOrigin(call.getReceiver());
    return read && receiver && registerFlowOrigin(read.getObject()) == receiver ? read
                                                                                : GetPropertyOp{};
}

/// The argument `call` passes to parameter `index` of the function it
/// enters, or null where it passes fewer, as the parameter is then undefined.
/// A direct call passes the entry block's arguments in order; a plain call
/// passes the parameters after its callee and receiver.
mlir::Value argumentFor(mlir::Operation * call, unsigned index) {
    if (mlir::isa<CallDirectOp>(call)) {
        return index < call->getNumOperands() ? call->getOperand(index) : mlir::Value{};
    }
    const mlir::OperandRange args = mlir::cast<CallOp>(call).getArgs();
    return index - implicit_arguments < args.size() ? args[index - implicit_arguments]
                                                    : mlir::Value{};
}

/// What `function` returns, from each of its returns.
llvm::SmallVector<mlir::Value> returnsOf(FuncOp function) {
    llvm::SmallVector<mlir::Value> values;
    function.walk([&](ReturnOp ret) { values.append(ret->operand_begin(), ret->operand_end()); });
    return values;
}

/// Whether `use` hands an array to an array destructuring.
bool destructures(mlir::OpOperand & use) {
    auto open = mlir::dyn_cast<CallOp>(use.getOwner());
    return open && calleeGlobalName(open) == "__ctbrowser_iter_open" &&
           open.getArgs().size() == 1 && use.getOperandNumber() == 2;
}

} // namespace

ClosureValues::ClosureValues(mlir::ModuleOp module) : module(module) {
    module.walk([&](FuncOp function) {
        if (const std::optional<unsigned> index = functionIndex(function)) {
            byIndex[*index] = function;
        }
    });
    module.walk([&](CreateClosureOp made) {
        if (FuncOp target = functionOf(made)) { creators[target.getOperation()].push_back(made); }
    });
    prototypes = read_prototype_writes(module).prototypes;
}

FuncOp ClosureValues::functionOf(CreateClosureOp made) const {
    const std::optional<unsigned> index = closureFunction(made);
    return index ? byIndex.lookup(*index) : FuncOp{};
}

mlir::Value ClosureValues::calledValue(CallOp call) {
    auto read = methodRead(call);
    const llvm::StringRef key = read ? constantKey(read.getKey()) : llvm::StringRef{};
    return key == "call" || key == "apply" ? call.getReceiver() : call.getCallee();
}

ClosureValues::Answer ClosureValues::closuresOf(mlir::Value value) {
    objects found = objectsOf(value);
    Answer answer;
    answer.unknown = std::move(found.unknown);
    for (mlir::Operation * made : found.made) {
        if (auto closure = mlir::dyn_cast<CreateClosureOp>(made)) {
            answer.closures.push_back(closure);
        }
    }
    return answer;
}

ClosureValues::objects ClosureValues::objectsOf(mlir::Value root) {
    if (auto found = objectsMemo.find(root); found != objectsMemo.end()) { return found->second; }
    // ponytail: a query that meets its own root again through a call is
    // unknown, and remembered so; a fixpoint over such cycles would answer
    // more of them.
    if (!active.insert(root).second) {
        return {{}, "a value that depends on itself through a call"};
    }
    objects answer;
    llvm::SmallVector<mlir::Value> work{root};
    llvm::DenseSet<mlir::Value> seen;
    while (!work.empty() && answer.unknown.empty()) {
        const mlir::Value value = work.pop_back_val();
        if (seen.insert(value).second) { answer.unknown = step(value, work, answer); }
    }
    if (!answer.unknown.empty()) { answer.made.clear(); }
    active.erase(root);
    objectsMemo[root] = answer;
    return answer;
}

std::string ClosureValues::step(mlir::Value value, llvm::SmallVectorImpl<mlir::Value> & work,
                                objects & into) {
    if (auto argument = mlir::dyn_cast<mlir::BlockArgument>(value)) {
        mlir::Block & block = *argument.getOwner();
        if (!block.isEntryBlock()) { return incoming(argument, work); }
        auto function = mlir::dyn_cast<FuncOp>(block.getParentOp());
        if (!function) { return "an argument of " + describe(block.getParentOp()); }
        return parameterSources(function, argument.getArgNumber(), work);
    }
    mlir::Operation * op = value.getDefiningOp();
    if (mlir::isa<CreateClosureOp, CreateArrayOp, CreateObjectOp>(op)) {
        into.made.insert(op);
        return {};
    }
    if (mlir::isa<ConstantOp, UnaryOp, BinaryOp, BinaryStaticOp, CompareOp, InstanceOfOp,
                  HasPropertyOp, DeletePropertyOp, FromBoolOp>(op)) {
        return {};
    }
    if (mlir::isa<CallOp, CallDirectOp>(op)) { return resultSources(op, work); }
    llvm::SmallVector<CreateCellOp> cells;
    std::string why;
    if (auto read = mlir::dyn_cast<CellGetOp>(op)) {
        why = cellsOf(read.getCell(), cells);
    } else if (auto read = mlir::dyn_cast<LoadUpvalueOp>(op)) {
        why = slotCells(op->getParentOfType<FuncOp>(), read.getIndex(), cells);
    } else {
        return "the result of " + describe(op);
    }
    for (CreateCellOp cell : cells) {
        if (!why.empty()) { break; }
        const binding & held = bindingOf(cell);
        why = held.unknown;
        work.append(held.values.begin(), held.values.end());
    }
    return why;
}

std::string ClosureValues::parameterSources(FuncOp function, unsigned index,
                                            llvm::SmallVectorImpl<mlir::Value> & work) {
    if (index == arg_callee) {
        if (functionIndex(function) == 0u) { return "the closure the host runs the script as"; }
        for (CreateClosureOp made : creators.lookup(function.getOperation())) {
            work.push_back(made.getResult());
        }
        return {};
    }
    if (index == arg_receiver) { return "the receiver of " + quoted(function); }
    if (index == arg_new_target) { return "new.target in " + quoted(function); }
    if (readsRawArguments(function)) { return quoted(function) + " reads its raw arguments"; }
    const callers & sites = callersOf(function);
    if (!sites.unknown.empty()) { return sites.unknown; }
    for (mlir::Operation * site : sites.calls) {
        if (const mlir::Value argument = argumentFor(site, index)) { work.push_back(argument); }
    }
    return {};
}

std::string ClosureValues::resultSources(mlir::Operation * call,
                                         llvm::SmallVectorImpl<mlir::Value> & work) {
    if (auto plain = mlir::dyn_cast<CallOp>(call);
        plain && calleeGlobalName(plain) == "__ctbrowser_iter_next") {
        return elementSources(plain, work);
    }
    llvm::SmallVector<FuncOp> functions;
    if (std::string why = calleesOf(call, functions); !why.empty()) { return why; }
    for (FuncOp function : functions) {
        llvm::SetVector<mlir::Operation *> made;
        if (std::string why = inFrame(function, call, returnsOf(function), work, made);
            !why.empty()) {
            return why;
        }
        for (mlir::Operation * literal : made) { work.push_back(literal->getResult(0)); }
    }
    return {};
}

std::string ClosureValues::calleesOf(mlir::Operation * call,
                                     llvm::SmallVectorImpl<FuncOp> & functions) {
    if (auto direct = mlir::dyn_cast<CallDirectOp>(call)) {
        FuncOp target = direct.getTarget();
        if (!target) { return "the result of " + describe(call); }
        functions.push_back(target);
        return {};
    }
    auto plain = mlir::cast<CallOp>(call);
    const llvm::StringRef global = calleeGlobalName(plain);
    if (!global.empty()) {
        return "the result of " + describe(call) + " through `" + global.str() + "`";
    }
    const objects callees = objectsOf(plain.getCallee());
    if (!callees.unknown.empty()) { return callees.unknown; }
    for (mlir::Operation * made : callees.made) {
        // Calling an array or an object literal throws.
        auto closure = mlir::dyn_cast<CreateClosureOp>(made);
        if (!closure) { continue; }
        FuncOp target = functionOf(closure);
        if (!target) {
            return "the result of a function the module does not hold (" + describe(made) + ")";
        }
        functions.push_back(target);
    }
    return {};
}

std::string ClosureValues::inFrame(FuncOp function, mlir::Operation * call,
                                   llvm::SmallVector<mlir::Value> values,
                                   llvm::SmallVectorImpl<mlir::Value> & work,
                                   llvm::SetVector<mlir::Operation *> & made) {
    // ponytail: one frame deep. A call met inside the frame leaves it, so its
    // own callee is walked in its frame, but its arguments take every caller's
    // values; nesting frames would answer more.
    llvm::DenseSet<mlir::Value> seen;
    while (!values.empty()) {
        mlir::Value value = values.pop_back_val();
        if (!seen.insert(value).second) { continue; }
        // Another function's value, a call's result, or an upvalue (whose cell
        // every call of the function shares) is not this call's alone.
        mlir::Operation * op = value.getDefiningOp();
        if (value.getParentRegion()->getParentOfType<FuncOp>() != function ||
            mlir::isa_and_present<CallOp, CallDirectOp, LoadUpvalueOp>(op)) {
            work.push_back(value);
            continue;
        }
        auto argument = mlir::dyn_cast<mlir::BlockArgument>(value);
        if (argument && argument.getOwner() == &function.getBody().front() &&
            argument.getArgNumber() >= implicit_arguments) {
            if (readsRawArguments(function)) {
                return quoted(function) + " reads its raw arguments";
            }
            if (const mlir::Value passed = argumentFor(call, argument.getArgNumber())) {
                work.push_back(passed);
            }
            continue;
        }
        // The rest stays in the frame: a binding cell the function makes is
        // made again by each call, and only that call's code writes it here.
        objects found;
        if (std::string why = step(value, values, found); !why.empty()) { return why; }
        made.insert(found.made.begin(), found.made.end());
    }
    return {};
}

std::string ClosureValues::elementSources(CallOp next, llvm::SmallVectorImpl<mlir::Value> & work) {
    if (!prototypes.empty()) {
        return "an array destructuring, and the program may change how arrays iterate: " +
               prototypes;
    }
    auto open =
        next.getArgs().size() == 1 ? next.getArgs().front().getDefiningOp<CallOp>() : CallOp{};
    if (!open || calleeGlobalName(open) != "__ctbrowser_iter_open" || open.getArgs().size() != 1) {
        return "an iterator step this does not follow (" + describe(next) + ")";
    }
    // The steps are the iterator's `__ctbrowser_iter_next` calls in the open's
    // block, in order, as ForOf.cpp's destructuring fold reads them.
    llvm::SmallVector<CallOp> steps;
    for (mlir::Operation * user : open.getResult().getUsers()) {
        auto step = mlir::dyn_cast<CallOp>(user);
        const llvm::StringRef helper = step ? calleeGlobalName(step) : llvm::StringRef{};
        if (helper == "__ctbrowser_iter_close") { continue; }
        if (helper != "__ctbrowser_iter_next" || step->getBlock() != open->getBlock() ||
            step.getArgs().size() != 1 || step.getArgs().front() != open.getResult()) {
            return "an array destructuring whose iterator is also used by " + describe(user);
        }
        steps.push_back(step);
    }
    llvm::sort(steps, [](CallOp a, CallOp b) { return a->isBeforeInBlock(b); });
    const auto index = static_cast<std::size_t>(llvm::find(steps, next) - steps.begin());
    // The literals the source may be. When the source is a call's result, a
    // literal made in that call's frame keeps the function it was made in, so
    // its element is walked in that frame too; any other keeps null.
    llvm::SmallVector<std::pair<mlir::Operation *, FuncOp>> rows;
    const mlir::Value source = open.getArgs().front();
    mlir::Operation * site = source.getDefiningOp();
    auto plain = mlir::dyn_cast_if_present<CallOp>(site);
    if (mlir::isa_and_present<CallDirectOp>(site) || (plain && calleeGlobalName(plain).empty())) {
        llvm::SmallVector<FuncOp> functions;
        if (std::string why = calleesOf(site, functions); !why.empty()) { return why; }
        for (FuncOp function : functions) {
            llvm::SmallVector<mlir::Value> outside;
            llvm::SetVector<mlir::Operation *> made;
            if (std::string why = inFrame(function, site, returnsOf(function), outside, made);
                !why.empty()) {
                return why;
            }
            for (mlir::Operation * literal : made) { rows.push_back({literal, function}); }
            for (mlir::Value value : outside) {
                const objects found = objectsOf(value);
                if (!found.unknown.empty()) { return found.unknown; }
                for (mlir::Operation * literal : found.made) {
                    rows.push_back({literal, FuncOp{}});
                }
            }
        }
    } else {
        const objects found = objectsOf(source);
        if (!found.unknown.empty()) { return found.unknown; }
        for (mlir::Operation * literal : found.made) { rows.push_back({literal, FuncOp{}}); }
    }
    for (auto [made, function] : rows) {
        auto array = mlir::dyn_cast<CreateArrayOp>(made);
        if (!array) { return "an array destructuring of " + describe(made); }
        if (std::string why = onlyDestructured(array); !why.empty()) { return why; }
        // The elements in order: the operands, then each append.
        llvm::SmallVector<mlir::Value> elements(array.getElements().begin(),
                                                array.getElements().end());
        llvm::SmallVector<AppendOp> appends;
        for (mlir::Operation * user : array.getResult().getUsers()) {
            if (auto append = mlir::dyn_cast<AppendOp>(user)) { appends.push_back(append); }
        }
        llvm::sort(appends, [](AppendOp a, AppendOp b) { return a->isBeforeInBlock(b); });
        for (AppendOp append : appends) { elements.push_back(append.getElement()); }
        // A shorter literal answers undefined.
        if (index >= elements.size()) { continue; }
        if (!function) {
            work.push_back(elements[index]);
            continue;
        }
        llvm::SetVector<mlir::Operation *> inner;
        if (std::string why = inFrame(function, site, {elements[index]}, work, inner);
            !why.empty()) {
            return why;
        }
        for (mlir::Operation * literal : inner) { work.push_back(literal->getResult(0)); }
    }
    return {};
}

std::string ClosureValues::onlyDestructured(CreateArrayOp array) {
    if (auto found = destructuredMemo.find(array.getOperation()); found != destructuredMemo.end()) {
        return found->second;
    }
    destructuredMemo[array.getOperation()] = "an array literal returned into its own destructuring";
    const std::string why = [&]() -> std::string {
        const std::string literal = "the array literal " + describe(array);
        auto uses = registerFlowUses(array.getResult());
        if (!uses) { return literal + " moves through too many registers"; }
        bool returned = false;
        for (mlir::OpOperand * use : *uses) {
            mlir::Operation * user = use->getOwner();
            if (mlir::isa<AppendOp>(user) && use->getOperandNumber() == 0 &&
                use->get() == array.getResult() && user->getBlock() == array->getBlock()) {
                continue;
            }
            if (mlir::isa<RootOp>(user) || destructures(*use)) { continue; }
            if (mlir::isa<ReturnOp>(user)) {
                returned = true;
                continue;
            }
            return literal + " is also used by " + describe(user);
        }
        if (!returned) { return {}; }
        FuncOp function = array->getParentOfType<FuncOp>();
        const callers & sites = callersOf(function);
        if (!sites.unknown.empty()) { return sites.unknown; }
        for (mlir::Operation * site : sites.calls) {
            auto results = registerFlowUses(site->getResult(0));
            if (!results) {
                return "what " + quoted(function) + " returns moves through too many registers";
            }
            for (mlir::OpOperand * use : *results) {
                if (!mlir::isa<RootOp>(use->getOwner()) && !destructures(*use)) {
                    return "what " + quoted(function) + " returns is also used by " +
                           describe(use->getOwner());
                }
            }
        }
        return {};
    }();
    destructuredMemo[array.getOperation()] = why;
    return why;
}

const ClosureValues::callers & ClosureValues::callersOf(FuncOp function) {
    auto [found, fresh] = callersMemo.try_emplace(function.getOperation());
    callers & sites = found->second;
    if (!fresh) { return sites; }
    // What a query needing these callers while they are being found gets.
    sites.unknown = quoted(function) + " may be called through its own callers";
    callers answer;
    answer.unknown = [&]() -> std::string {
        if (functionIndex(function) == 0u) { return "the host runs " + quoted(function); }
        const auto uses = mlir::SymbolTable::getSymbolUses(function.getOperation(), module);
        if (!uses) { return "the uses of " + quoted(function) + " are not known"; }
        for (const mlir::SymbolTable::SymbolUse & use : *uses) {
            auto direct = mlir::dyn_cast<CallDirectOp>(use.getUser());
            if (!direct || direct.getCallee() != function.getSymName()) {
                return quoted(function) + " is named by " + describe(use.getUser());
            }
            answer.calls.insert(direct);
        }
        llvm::DenseSet<mlir::Value> seen;
        for (CreateClosureOp made : creators.lookup(function.getOperation())) {
            if (std::string why = callsThrough(made.getResult(), function, answer, seen);
                !why.empty()) {
                return why;
            }
        }
        mlir::Block * entry = implicitEntry(function);
        return entry ? callsThrough(entry->getArgument(arg_callee), function, answer, seen)
                     : std::string{};
    }();
    sites = std::move(answer);
    return sites;
}

std::string ClosureValues::callsThrough(mlir::Value value, FuncOp function, callers & into,
                                        llvm::DenseSet<mlir::Value> & seen) {
    if (!seen.insert(value).second) { return {}; }
    auto uses = registerFlowUses(value);
    if (!uses) { return quoted(function) + " moves through too many registers"; }
    for (mlir::OpOperand * use : *uses) {
        mlir::Operation * user = use->getOwner();
        const unsigned at = use->getOperandNumber();
        if (auto call = mlir::dyn_cast<CallOp>(user); call && at == 0) {
            into.calls.insert(call);
            continue;
        }
        // A direct call of the symbol is counted from the symbol's uses, and
        // building a nested closure or reaching an upvalue through the closure
        // does not call it.
        auto direct = mlir::dyn_cast<CallDirectOp>(user);
        if ((direct && at == 2 && direct.getCallee() == function.getSymName()) ||
            mlir::isa<RootOp>(user) ||
            (at == 0 && mlir::isa<CreateClosureOp, LoadUpvalueOp, StoreUpvalueOp>(user))) {
            continue;
        }
        if (auto define = mlir::dyn_cast<SetPropertyOp>(user);
            define && at == 2 && define->hasAttr(defineOwnMarker)) {
            const mlir::Value holder = registerFlowOrigin(define.getObject());
            auto object = holder ? holder.getDefiningOp<CreateObjectOp>() : CreateObjectOp{};
            const llvm::StringRef key = constantKey(define.getKey());
            if (!object || key.empty() || key == "__proto__") {
                return quoted(function) + " is stored by " + describe(user);
            }
            if (std::string why = methodCalls(object, key, into); !why.empty()) { return why; }
            continue;
        }
        llvm::SmallVector<mlir::Value> reads;
        const std::optional<std::string> bound = boundReads(*use, reads);
        if (!bound) { return quoted(function) + " is used as a value by " + describe(user); }
        if (!bound->empty()) { return *bound; }
        for (mlir::Value read : reads) {
            if (std::string why = callsThrough(read, function, into, seen); !why.empty()) {
                return why;
            }
        }
    }
    return {};
}

std::string ClosureValues::methodCalls(CreateObjectOp object, llvm::StringRef key, callers & into) {
    ownKeys own;
    auto uses = registerFlowUses(object.getResult());
    if (!uses) {
        return "the object literal " + describe(object) + " moves through too many registers";
    }
    for (mlir::OpOperand * use : *uses) {
        auto define = mlir::dyn_cast<SetPropertyOp>(use->getOwner());
        const llvm::StringRef name = define ? constantKey(define.getKey()) : llvm::StringRef{};
        if (define && use->getOperandNumber() == 0 && define->hasAttr(defineOwnMarker) &&
            !name.empty() && name != "__proto__") {
            own[name].push_back(define.getValue());
        }
    }
    llvm::DenseSet<mlir::Value> seen;
    return objectReads(object.getResult(), own, key, into, seen);
}

std::string ClosureValues::objectReads(mlir::Value value, const ownKeys & own, llvm::StringRef key,
                                       callers & into, llvm::DenseSet<mlir::Value> & seen) {
    if (!seen.insert(value).second) { return {}; }
    const std::string literal = "the object literal holding `" + key.str() + "`";
    auto uses = registerFlowUses(value);
    if (!uses) { return literal + " moves through too many registers"; }
    for (mlir::OpOperand * use : *uses) {
        mlir::Operation * user = use->getOwner();
        const unsigned at = use->getOperandNumber();
        if (mlir::isa<RootOp>(user)) { continue; }
        if (auto define = mlir::dyn_cast<SetPropertyOp>(user);
            define && at == 0 && define->hasAttr(defineOwnMarker)) {
            continue;
        }
        if (auto read = mlir::dyn_cast<GetPropertyOp>(user); read && at == 0) {
            const llvm::StringRef name = constantKey(read.getKey());
            if (!own.contains(name)) {
                return literal + " is read with a key it does not own (" + describe(user) + ")";
            }
            if (name != key) { continue; }
            auto results = registerFlowUses(read.getResult());
            if (!results) {
                return "the method `" + key.str() + "` moves through too many registers";
            }
            for (mlir::OpOperand * result : *results) {
                auto call = mlir::dyn_cast<CallOp>(result->getOwner());
                if (call && result->getOperandNumber() == 0) {
                    into.calls.insert(call);
                } else if (!mlir::isa<RootOp>(result->getOwner())) {
                    return "the method `" + key.str() + "` is used as a value by " +
                           describe(result->getOwner());
                }
            }
            continue;
        }
        if (auto call = mlir::dyn_cast<CallOp>(user); call && at == 1) {
            if (std::string why = receiverUnused(call, own); !why.empty()) { return why; }
            continue;
        }
        llvm::SmallVector<mlir::Value> reads;
        const std::optional<std::string> bound = boundReads(*use, reads);
        if (!bound) { return literal + " is used by " + describe(user); }
        if (!bound->empty()) { return *bound; }
        for (mlir::Value read : reads) {
            if (std::string why = objectReads(read, own, key, into, seen); !why.empty()) {
                return why;
            }
        }
    }
    return {};
}

std::string ClosureValues::receiverUnused(CallOp call, const ownKeys & own) {
    // `o.m(...)` hands the literal to m as `this`.
    auto read = methodRead(call);
    const auto found = read ? own.find(constantKey(read.getKey())) : own.end();
    if (found == own.end()) { return "an object literal is the receiver of " + describe(call); }
    for (mlir::Value method : found->second) {
        const Answer made = closuresOf(method);
        if (!made.unknown.empty()) { return made.unknown; }
        for (CreateClosureOp closure : made.closures) {
            FuncOp target = functionOf(closure);
            mlir::Block * entry = target ? implicitEntry(target) : nullptr;
            if (!entry) { return "an object literal is the receiver of " + describe(call); }
            if (!llvm::all_of(entry->getArgument(arg_receiver).getUsers(),
                              [](mlir::Operation * user) { return mlir::isa<RootOp>(user); })) {
                return quoted(target) +
                       " uses `this`, which may be an object literal it is a method of";
            }
        }
    }
    return {};
}

const ClosureValues::binding & ClosureValues::bindingOf(CreateCellOp cell) {
    auto [found, fresh] = bindingMemo.try_emplace(cell.getOperation());
    binding & held = found->second;
    if (!fresh) { return held; }
    held.values.push_back(cell.getInitial());
    auto uses = registerFlowUses(cell.getResult());
    if (!uses) {
        held.unknown = "a binding that moves through too many registers (" + describe(cell) + ")";
        return held;
    }
    llvm::DenseSet<std::pair<mlir::Operation *, std::size_t>> seen;
    for (mlir::OpOperand * use : *uses) {
        mlir::Operation * user = use->getOwner();
        const unsigned at = use->getOperandNumber();
        if (auto read = mlir::dyn_cast<CellGetOp>(user)) {
            held.reads.push_back(read.getResult());
        } else if (auto write = mlir::dyn_cast<CellSetOp>(user); write && at == 0) {
            held.values.push_back(write.getValue());
        } else if (auto made = mlir::dyn_cast<CreateClosureOp>(user)) {
            const std::size_t first = made->getNumOperands() - made.getUpvalues().size();
            held.unknown = at < first ? "a binding used by " + describe(user)
                                      : slotAccess(made, at - first, held, seen);
        } else if (!mlir::isa<RootOp>(user)) {
            held.unknown = "a binding used by " + describe(user);
        }
        if (!held.unknown.empty()) { break; }
    }
    return held;
}

std::string ClosureValues::slotAccess(
    CreateClosureOp made, std::size_t slot, binding & into,
    llvm::DenseSet<std::pair<mlir::Operation *, std::size_t>> & seen) {
    if (!seen.insert({made.getOperation(), slot}).second) { return {}; }
    FuncOp target = functionOf(made);
    if (!target) {
        return "a binding captured by a function the module does not hold (" + describe(made) + ")";
    }
    std::string why;
    target.walk([&](mlir::Operation * op) {
        if (auto read = mlir::dyn_cast<LoadUpvalueOp>(op); read && read.getIndex() == slot) {
            into.reads.push_back(read.getResult());
        } else if (auto write = mlir::dyn_cast<StoreUpvalueOp>(op);
                   write && write.getIndex() == slot) {
            into.values.push_back(write.getValue());
        } else if (auto nested = mlir::dyn_cast<CreateClosureOp>(op)) {
            if (const mlir::DenseI32ArrayAttr indices = nested.getEnclosingIndicesAttr()) {
                for (auto [at, from] : llvm::enumerate(indices.asArrayRef())) {
                    if (from >= 0 && static_cast<std::size_t>(from) == slot && why.empty()) {
                        why = slotAccess(nested, at, into, seen);
                    }
                }
            }
        }
        return why.empty() ? mlir::WalkResult::advance() : mlir::WalkResult::interrupt();
    });
    return why;
}

std::string ClosureValues::slotCells(FuncOp function, std::uint32_t slot,
                                     llvm::SmallVectorImpl<CreateCellOp> & cells, unsigned depth) {
    if (!function || depth > 64) { return "an upvalue this does not follow"; }
    for (CreateClosureOp made : creators.lookup(function.getOperation())) {
        if (slot >= made.getUpvalues().size()) {
            return "an upvalue " + describe(made) + " does not capture";
        }
        if (const mlir::DenseI32ArrayAttr indices = made.getEnclosingIndicesAttr()) {
            if (const std::int32_t from = indices[slot]; from >= 0) {
                if (std::string why = slotCells(made->getParentOfType<FuncOp>(),
                                                static_cast<std::uint32_t>(from), cells, depth + 1);
                    !why.empty()) {
                    return why;
                }
                continue;
            }
        }
        // A slot the VM fills from nothing holds undefined.
        const mlir::Value captured = made.getUpvalues()[slot];
        if (isUndefinedConstant(captured)) { continue; }
        if (std::string why = cellsOf(captured, cells); !why.empty()) { return why; }
    }
    return {};
}

std::string ClosureValues::cellsOf(mlir::Value value, llvm::SmallVectorImpl<CreateCellOp> & cells) {
    llvm::SmallVector<mlir::Value> work{value};
    llvm::DenseSet<mlir::Value> seen;
    while (!work.empty()) {
        value = work.pop_back_val();
        if (!seen.insert(value).second) { continue; }
        if (auto cell = value.getDefiningOp<CreateCellOp>()) {
            cells.push_back(cell);
            continue;
        }
        auto argument = mlir::dyn_cast<mlir::BlockArgument>(value);
        if (!argument || argument.getOwner()->isEntryBlock()) {
            return "a binding cell that comes from " +
                   (argument ? std::string("a parameter") : describe(value.getDefiningOp()));
        }
        if (std::string why = incoming(argument, work); !why.empty()) { return why; }
    }
    return {};
}

std::optional<std::string> ClosureValues::boundReads(mlir::OpOperand & use,
                                                     llvm::SmallVectorImpl<mlir::Value> & reads) {
    mlir::Operation * user = use.getOwner();
    llvm::SmallVector<CreateCellOp> cells;
    std::string why;
    if (auto cell = mlir::dyn_cast<CreateCellOp>(user)) {
        cells.push_back(cell);
    } else if (auto write = mlir::dyn_cast<CellSetOp>(user); write && use.getOperandNumber() == 1) {
        why = cellsOf(write.getCell(), cells);
    } else if (auto write = mlir::dyn_cast<StoreUpvalueOp>(user);
               write && use.getOperandNumber() == 1) {
        why = slotCells(user->getParentOfType<FuncOp>(), write.getIndex(), cells);
    } else {
        return std::nullopt;
    }
    for (CreateCellOp cell : cells) {
        if (!why.empty()) { break; }
        const binding & held = bindingOf(cell);
        why = held.unknown;
        reads.append(held.reads.begin(), held.reads.end());
    }
    return why;
}

} // namespace ctcompile::ctjs::globals_detail
