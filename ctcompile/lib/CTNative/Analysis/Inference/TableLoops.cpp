//===- TableLoops.cpp - Unroll loops over a constant table's entries ------===//
//
// Bootstrap's Config type check loops over a component's type table:
//
//     for (const [key, expected] of Object.entries(this.constructor.DefaultType))
//         check(config[key], expected);
//
// The table is a constant (StaticTables.cpp), so its keys and values are known
// at compile time, but the config struct's fields have different C++ types,
// so `config[key]` has no single type an index loop could give it. Each
// iteration is therefore written out once per key, with that key and value
// as constants: `config[key]` becomes a constant-key read typed by its own
// field, as a C++ fold over the struct's fields would be.
//
// Only the loop the importer and --ctjs-lift-to-scf make for a for-of over a
// marked enumeration (kNativeStaticTableEnumeration) is unrolled: an
// scf.while whose counter starts at 0, steps by 1 and runs while it is below
// the enumeration's length, whose after-region only forwards its arguments,
// and whose body reads the enumeration only at the counter. Each iteration's
// clone keeps the loop's own exit test, so a `break`, `return` or throw in the
// body leaves after that key, as it would in JavaScript.
//
//===----------------------------------------------------------------------===//
#include "ctcompile/CTNative/Analysis/StaticTables.h"

#include "ctcompile/CTJS/IR/CTJSDialect.h"
#include "ctcompile/CTJS/IR/CTJSOps.h"
#include "ctcompile/CTNative/Analysis/ClosedCallable.h"
#include "ctcompile/CTNative/Analysis/TypeInference.h"

#include "mlir/Dialect/SCF/IR/SCF.h"
#include "mlir/IR/Builders.h"
#include "mlir/IR/IRMapping.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringSet.h"

#include <cmath>
#include <functional>
#include <optional>
#include <utility>

namespace ctcompile::ctnative {
namespace {

/// The most keys a table loop is unrolled for.
constexpr unsigned maxUnrolledKeys = 64;

/// One field of a table: its key and its constant.
using tableField = std::pair<std::string, mlir::Attribute>;

/// The fields of the table literal marked `name`, in property order, or
/// nullopt when one is not a constant.
std::optional<llvm::SmallVector<tableField>> tableFields(mlir::ModuleOp module,
                                                         llvm::StringRef name) {
    llvm::SmallVector<tableField> fields;
    bool constant = true;
    module.walk([&](ctjs::CreateObjectOp literal) {
        auto marked = literal->getAttrOfType<mlir::StringAttr>(kNativeStaticTable);
        if (!marked || marked.getValue() != name) { return; }
        llvm::SmallVector<ctjs::SetPropertyOp> stores;
        for (mlir::Operation * user : literal.getResult().getUsers()) {
            if (auto store = llvm::dyn_cast<ctjs::SetPropertyOp>(user)) { stores.push_back(store); }
        }
        llvm::sort(stores, [](ctjs::SetPropertyOp a, ctjs::SetPropertyOp b) {
            return a->isBeforeInBlock(b);
        });
        for (ctjs::SetPropertyOp store : stores) {
            auto value = store.getValue().getDefiningOp<ctjs::ConstantOp>();
            if (!value) {
                constant = false;
                continue;
            }
            fields.emplace_back(tableKey(store.getKey()), value.getValue());
        }
    });
    if (!constant || fields.empty()) { return std::nullopt; }
    sortOwnKeyOrder(fields, [](const tableField & field) { return llvm::StringRef(field.first); });
    return fields;
}

/// The constant number `value` holds, or nullopt.
std::optional<double> constantNumber(mlir::Value value) {
    auto number = ctjs::constantAttr<ctjs::NumberAttr>(value);
    return number ? std::optional(number.getDouble()) : std::nullopt;
}

/// One loop over an enumeration, as matched: the counter is argument
/// `counter` of the before-region and, when the before-region passes it on
/// unchanged through scf.condition (the body is in the after-region), argument
/// `forwarded` of the after-region. Otherwise the body is in the
/// before-region and the after-region only forwards its arguments.
struct tableLoop {
    ctjs::CallOp enumeration;
    ctjs::GetPropertyOp length;
    ctjs::CompareOp test;
    mlir::scf::WhileOp loop;
    unsigned counter = 0;
    std::optional<unsigned> forwarded;
};

/// Matches the for-of over `enumeration` that unrollTableLoops can unroll.
std::optional<tableLoop> matchLoop(ctjs::CallOp enumeration) {
    tableLoop match{enumeration, {}, {}, {}, 0, std::nullopt};
    llvm::SmallVector<ctjs::GetPropertyOp> elements;
    for (mlir::Operation * user : enumeration.getResult().getUsers()) {
        auto read = llvm::dyn_cast<ctjs::GetPropertyOp>(user);
        if (!read || read.getObject() != enumeration.getResult()) { return std::nullopt; }
        if (ctjs::constantKey(read.getKey()) == "length") {
            if (match.length) { return std::nullopt; }
            match.length = read;
        } else {
            elements.push_back(read);
        }
    }
    if (!match.length || elements.empty()) { return std::nullopt; }
    // The loop: the one whose before-region compares its counter with the
    // length (`counter < length`), the counter starting at 0.
    for (mlir::Operation * user : match.length.getResult().getUsers()) {
        auto compare = llvm::dyn_cast<ctjs::CompareOp>(user);
        auto counter =
            compare ? llvm::dyn_cast<mlir::BlockArgument>(compare.getLhs()) : mlir::BlockArgument{};
        auto loop = counter ? llvm::dyn_cast<mlir::scf::WhileOp>(counter.getOwner()->getParentOp())
                            : mlir::scf::WhileOp{};
        if (!loop || compare.getKind() != ctjs::CompareKind::Lt ||
            compare.getRhs() != match.length.getResult() ||
            counter.getOwner() != &loop.getBefore().front()) {
            continue;
        }
        match.loop = loop;
        match.test = compare;
        match.counter = counter.getArgNumber();
    }
    mlir::scf::WhileOp loop = match.loop;
    if (!loop || constantNumber(loop.getInits()[match.counter]) != 0.0) { return std::nullopt; }
    mlir::Block & before = loop.getBefore().front();
    mlir::Block & after = loop.getAfter().front();
    const mlir::Value counter = before.getArgument(match.counter);
    auto condition = llvm::cast<mlir::scf::ConditionOp>(before.getTerminator());
    auto forward = llvm::cast<mlir::scf::YieldOp>(after.getTerminator());
    const auto passed = llvm::find(condition.getArgs(), counter);
    mlir::Value forwarded;
    if (passed != condition.getArgs().end()) {
        match.forwarded = static_cast<unsigned>(passed - condition.getArgs().begin());
        forwarded = after.getArgument(*match.forwarded);
    } else if (after.getOperations().size() != 1 ||
               !llvm::equal(forward.getOperands(), after.getArguments())) {
        return std::nullopt;
    }
    // Every element read is at the counter, and the counter steps by one.
    for (ctjs::GetPropertyOp read : elements) {
        if (read.getKey() != counter && (!forwarded || read.getKey() != forwarded)) {
            return std::nullopt;
        }
    }
    bool stepped = false;
    for (mlir::Value value : {counter, forwarded}) {
        if (!value) { continue; }
        for (mlir::Operation * user : value.getUsers()) {
            auto step = llvm::dyn_cast<ctjs::BinaryStaticOp>(user);
            stepped |= step && step.getLhs() == value && constantNumber(step.getRhs()) == 1.0;
        }
    }
    if (!stepped) { return std::nullopt; }
    return match;
}

/// Replaces, in the ops `iteration` cloned for key `index`, the read of the
/// enumeration's element and the reads of its two positions by constants.
/// The clone after the last key reads past the end on a path its exit test
/// never takes; there (`field` is nullopt) each read is undefined.
void foldElementReads(ctjs::CallOp enumeration, llvm::StringRef method, unsigned index,
                      std::optional<tableField> field,
                      llvm::ArrayRef<mlir::Operation *> iteration) {
    llvm::SmallVector<ctjs::GetPropertyOp> reads;
    for (mlir::Operation * op : iteration) {
        op->walk([&](ctjs::GetPropertyOp read) {
            if (read.getObject() == enumeration.getResult() &&
                constantNumber(read.getKey()) == static_cast<double>(index)) {
                reads.push_back(read);
            }
        });
    }
    auto * context = enumeration.getContext();
    const mlir::Attribute undefined = ctjs::UndefinedAttr::get(context);
    const mlir::Attribute keyAttr =
        field ? mlir::Attribute(ctjs::StringAttr::get(context, field->first)) : undefined;
    const mlir::Attribute valueAttr = field ? field->second : undefined;
    for (ctjs::GetPropertyOp read : reads) {
        mlir::OpBuilder builder{read};
        const auto constant = [&](mlir::Attribute value) {
            return ctjs::ConstantOp::create(builder, read.getLoc(), read.getResult().getType(),
                                            value)
                .getResult();
        };
        if (method != "entries") {
            read.getResult().replaceAllUsesWith(constant(method == "keys" ? keyAttr : valueAttr));
            read.erase();
            continue;
        }
        // A [key, value] pair is read at 0 and 1 by the destructuring.
        for (mlir::Operation * user : llvm::make_early_inc_range(read.getResult().getUsers())) {
            auto position = llvm::dyn_cast<ctjs::GetPropertyOp>(user);
            const auto at = position ? constantNumber(position.getKey()) : std::nullopt;
            if (!position || position.getObject() != read.getResult() || (at != 0.0 && at != 1.0)) {
                continue;
            }
            mlir::OpBuilder here{position};
            position.getResult().replaceAllUsesWith(
                ctjs::ConstantOp::create(here, position.getLoc(), position.getResult().getType(),
                                         *at == 0.0 ? keyAttr : valueAttr)
                    .getResult());
            position.erase();
        }
        // Anything else reading the pair sees undefined only where no key is.
        if (!read->use_empty() && !field) {
            read.getResult().replaceAllUsesWith(constant(undefined));
        }
        if (read->use_empty()) { read.erase(); }
    }
}

/// Replaces `branch` by the region `taken` selects, recording in `replaced`
/// what each of its results became.
void keepRegion(mlir::scf::IfOp branch, bool taken,
                llvm::DenseMap<mlir::Value, mlir::Value> & replaced) {
    mlir::Block & kept = (taken ? branch.getThenRegion() : branch.getElseRegion()).front();
    auto yield = llvm::cast<mlir::scf::YieldOp>(kept.getTerminator());
    for (auto [result, value] : llvm::zip(branch->getResults(), yield.getOperands())) {
        replaced[result] = value;
    }
    branch->getResults().replaceAllUsesWith(yield.getOperands());
    yield.erase();
    branch->getBlock()->getOperations().splice(branch->getIterator(), kept.getOperations());
    branch.erase();
}

/// In the clone after the last key, the loop's test `counter < length` is
/// false: keep only the branch it selects, so the body (which would read past
/// the end) is not written out at all.
llvm::DenseMap<mlir::Value, mlir::Value> decideLastTest(mlir::Value test) {
    llvm::DenseMap<mlir::Value, mlir::Value> replaced;
    llvm::SmallVector<mlir::scf::IfOp> branches;
    for (mlir::Operation * user : test.getUsers()) {
        auto truthy = llvm::dyn_cast<ctjs::TruthyOp>(user);
        if (!truthy) { continue; }
        for (mlir::Operation * branch : truthy.getResult().getUsers()) {
            if (auto decided = llvm::dyn_cast<mlir::scf::IfOp>(branch);
                decided && decided.getCondition() == truthy.getResult()) {
                branches.push_back(decided);
            }
        }
    }
    for (mlir::scf::IfOp branch : branches) { keepRegion(branch, false, replaced); }
    return replaced;
}

/// Writes out `match` once per field. Iteration `index` runs the
/// before-region with the counter replaced by `index`; if its test passes, the
/// after-region with the same counter, and then the next iteration, nested in
/// an scf.if; otherwise the loop answers what the test passed on.
void unroll(const tableLoop & match, llvm::StringRef method, llvm::ArrayRef<tableField> fields) {
    mlir::scf::WhileOp loop = match.loop;
    mlir::Block & before = loop.getBefore().front();
    mlir::Block & after = loop.getAfter().front();
    auto condition = llvm::cast<mlir::scf::ConditionOp>(before.getTerminator());
    auto forward = llvm::cast<mlir::scf::YieldOp>(after.getTerminator());
    const mlir::TypeRange types = loop.getResultTypes();
    const mlir::Value lastTest = ctjs::CompareOp(match.test).getResult();
    const auto constantCounter = [&](mlir::OpBuilder & builder, unsigned index) {
        return ctjs::ConstantOp::create(builder, loop.getLoc(),
                                        before.getArgument(match.counter).getType(),
                                        ctjs::numberAttr(loop.getContext(), index))
            .getResult();
    };
    const auto field = [&](unsigned index) {
        return index < fields.size() ? std::optional(fields[index]) : std::nullopt;
    };
    // Iteration `index` runs at `builder` with `current` as the loop-carried
    // values and returns what the loop answers from there on.
    std::function<llvm::SmallVector<mlir::Value>(mlir::OpBuilder &, unsigned,
                                                 llvm::ArrayRef<mlir::Value>)>
        iteration =
            [&](mlir::OpBuilder & builder, unsigned index, llvm::ArrayRef<mlir::Value> current) {
                mlir::IRMapping test;
                for (auto [argument, value] : llvm::zip(before.getArguments(), current)) {
                    test.map(argument, value);
                }
                test.map(before.getArgument(match.counter), constantCounter(builder, index));
                llvm::SmallVector<mlir::Operation *> cloned;
                for (mlir::Operation & op : before.without_terminator()) {
                    cloned.push_back(builder.clone(op, test));
                }
                foldElementReads(match.enumeration, method, index, field(index), cloned);
                // After the folds: deciding erases ops `cloned` lists, and its
                // replacements are what the condition passes on.
                llvm::DenseMap<mlir::Value, mlir::Value> replaced;
                if (index == fields.size()) { replaced = decideLastTest(test.lookup(lastTest)); }
                llvm::SmallVector<mlir::Value> passed;
                for (mlir::Value value : condition.getArgs()) {
                    const mlir::Value cloned = test.lookup(value);
                    passed.push_back(replaced.lookup(cloned) ? replaced.lookup(cloned) : cloned);
                }
                // After the last key the test fails: `index < length` is false
                // there whatever the body did.
                if (index == fields.size()) { return passed; }
                auto branch = mlir::scf::IfOp::create(builder, loop.getLoc(), types,
                                                      test.lookup(condition.getCondition()), true);
                {
                    mlir::OpBuilder then =
                        mlir::OpBuilder::atBlockBegin(&branch.getThenRegion().front());
                    mlir::IRMapping body;
                    for (auto [argument, value] : llvm::zip(after.getArguments(), passed)) {
                        body.map(argument, value);
                    }
                    if (match.forwarded) {
                        body.map(after.getArgument(*match.forwarded), constantCounter(then, index));
                    }
                    llvm::SmallVector<mlir::Operation *> ran;
                    for (mlir::Operation & op : after.without_terminator()) {
                        ran.push_back(then.clone(op, body));
                    }
                    foldElementReads(match.enumeration, method, index, field(index), ran);
                    llvm::SmallVector<mlir::Value> next;
                    for (mlir::Value value : forward.getOperands()) {
                        next.push_back(body.lookup(value));
                    }
                    const auto answer = iteration(then, index + 1, next);
                    mlir::scf::YieldOp::create(then, loop.getLoc(), answer);
                }
                {
                    mlir::OpBuilder otherwise =
                        mlir::OpBuilder::atBlockBegin(&branch.getElseRegion().front());
                    mlir::scf::YieldOp::create(otherwise, loop.getLoc(), passed);
                }
                return llvm::SmallVector<mlir::Value>(branch.getResults());
            };
    mlir::OpBuilder builder{loop};
    const auto answer = iteration(builder, 0, llvm::to_vector(loop.getInits()));
    loop.replaceAllUsesWith(answer);
    loop.erase();
}

/// Replaces the length read of `match`'s enumeration by the key count and
/// erases the enumeration and its method read once nothing reads them.
void dropEnumeration(tableLoop match, std::size_t keys) {
    mlir::OpBuilder builder{match.length};
    match.length.getResult().replaceAllUsesWith(
        ctjs::ConstantOp::create(builder, match.length.getLoc(), match.length.getResult().getType(),
                                 ctjs::numberAttr(builder.getContext(), static_cast<double>(keys)))
            .getResult());
    match.length.erase();
    ctjs::CallOp enumeration = match.enumeration;
    if (!enumeration->use_empty()) { return; }
    mlir::Operation * method = enumeration.getCallee().getDefiningOp();
    enumeration.erase();
    if (method && method->use_empty()) {
        mlir::Operation * object = method->getOperand(0).getDefiningOp();
        method->erase();
        if (object && object->use_empty()) { object->erase(); }
    }
}

/// The fields of `literal`, in property order, when it is a table of
/// constants (finite numbers, booleans, strings, null) defined with distinct
/// constant keys in `before`'s block before it, and every other use is an
/// operand `passes` accepts.
std::optional<llvm::SmallVector<tableField>> literalTable(
    ctjs::CreateObjectOp literal, mlir::Operation * before,
    llvm::function_ref<bool(mlir::OpOperand &)> passes) {
    llvm::SmallVector<ctjs::SetPropertyOp> stores;
    for (mlir::OpOperand & use : literal.getResult().getUses()) {
        auto store = llvm::dyn_cast<ctjs::SetPropertyOp>(use.getOwner());
        if (store && use.getOperandNumber() == 0) {
            stores.push_back(store);
        } else if (!passes(use)) {
            return std::nullopt;
        }
    }
    if (llvm::any_of(stores, [&](ctjs::SetPropertyOp store) {
            return store->getBlock() != before->getBlock();
        })) {
        return std::nullopt;
    }
    llvm::sort(stores,
               [](ctjs::SetPropertyOp a, ctjs::SetPropertyOp b) { return a->isBeforeInBlock(b); });
    llvm::SmallVector<tableField> fields;
    llvm::StringSet<> seen;
    for (ctjs::SetPropertyOp store : stores) {
        const std::string key = tableKey(store.getKey());
        const auto constant = ctjs::constantAttr<mlir::Attribute>(store.getValue());
        const auto number = llvm::dyn_cast_or_null<ctjs::NumberAttr>(constant);
        if (key.empty() || key == "__proto__" || !seen.insert(key).second ||
            !store->hasAttr(ctjs::defineOwnMarker) || !store->isBeforeInBlock(before) ||
            !llvm::isa_and_nonnull<ctjs::NumberAttr, ctjs::BooleanAttr, ctjs::StringAttr,
                                   ctjs::NullAttr>(constant) ||
            (number && !std::isfinite(number.getDouble()))) {
            return std::nullopt;
        }
        fields.emplace_back(key, constant);
    }
    if (fields.empty()) { return std::nullopt; }
    sortOwnKeyOrder(fields, [](const tableField & field) { return llvm::StringRef(field.first); });
    return fields;
}

/// The constant table every caller passes as `parameter`, a parameter of a
/// function whose callers are all known direct calls: each passes an object
/// literal of constants, all with the same fields, that it uses for nothing
/// but those calls. None otherwise.
std::optional<llvm::SmallVector<tableField>> parameterTable(mlir::BlockArgument parameter) {
    auto function = llvm::dyn_cast<ctjs::FuncOp>(parameter.getOwner()->getParentOp());
    if (!function || !parameter.getOwner()->isEntryBlock() ||
        parameter.getArgNumber() < ctjs::implicit_arguments) {
        return std::nullopt;
    }
    auto module = function->getParentOfType<mlir::ModuleOp>();
    bool argumentsObject = false;
    function.walk([&](ctjs::MakeArgumentsOp) { argumentsObject = true; });
    const auto callers = directCallers(function, module);
    if (argumentsObject || !callers || callers->empty() ||
        !closedCallableProblem(function, module).empty() ||
        hasAlternateCalleeCalls(function, module)) {
        return std::nullopt;
    }
    const unsigned index = parameter.getArgNumber();
    std::optional<llvm::SmallVector<tableField>> fields;
    for (ctjs::CallDirectOp call : *callers) {
        auto literal = call->getOperand(index).getDefiningOp<ctjs::CreateObjectOp>();
        if (!literal) { return std::nullopt; }
        // The literal may reach only this parameter, at any of the calls.
        const auto table = literalTable(literal, call, [&](mlir::OpOperand & use) {
            auto other = llvm::dyn_cast<ctjs::CallDirectOp>(use.getOwner());
            return other && use.getOperandNumber() == index && llvm::is_contained(*callers, other);
        });
        if (!table || (fields && *fields != *table)) { return std::nullopt; }
        fields = table;
    }
    return fields;
}

} // namespace

void unrollParameterTableLoops(mlir::ModuleOp module) {
    llvm::SmallVector<ctjs::CallOp> enumerations;
    module.walk([&](ctjs::CallOp call) {
        const auto * row = TypeInference::builtinStaticOf(call);
        if (row && row->params[0] == TypeInference::BuiltinStatic::Param::staticTable &&
            call.getArgs().size() == 1 && llvm::isa<mlir::BlockArgument>(call.getArgs()[0]) &&
            call.getArgs()[0].hasOneUse()) {
            enumerations.push_back(call);
        }
    });
    for (ctjs::CallOp enumeration : enumerations) {
        auto parameter = llvm::cast<mlir::BlockArgument>(enumeration.getArgs()[0]);
        const auto fields = parameterTable(parameter);
        auto match = matchLoop(enumeration);
        if (!fields || !match || fields->size() > maxUnrolledKeys) { continue; }
        auto function = llvm::cast<ctjs::FuncOp>(parameter.getOwner()->getParentOp());
        unroll(*match, ctjs::methodKey(enumeration), *fields);
        dropEnumeration(*match, fields->size());
        // The parameter is read by nothing now: every caller passes undefined
        // instead, and its literal, used for nothing else, goes.
        for (ctjs::CallDirectOp call : *directCallers(function, module)) {
            mlir::OpBuilder builder{call};
            mlir::Value passed = call->getOperand(parameter.getArgNumber());
            call->setOperand(parameter.getArgNumber(),
                             ctjs::ConstantOp::create(builder, call.getLoc(), passed.getType(),
                                                      ctjs::UndefinedAttr::get(module.getContext()))
                                 .getResult());
            auto literal = passed.getDefiningOp<ctjs::CreateObjectOp>();
            if (!literal || !llvm::all_of(literal.getResult().getUsers(), [](mlir::Operation * u) {
                    return llvm::isa<ctjs::SetPropertyOp>(u);
                })) {
                continue;
            }
            for (mlir::Operation * store :
                 llvm::make_early_inc_range(literal.getResult().getUsers())) {
                store->erase();
            }
            literal.erase();
        }
    }
}

void unrollTableLoops(mlir::ModuleOp module) {
    llvm::SmallVector<ctjs::CallOp> enumerations;
    module.walk([&](ctjs::CallOp call) {
        if (call->hasAttr(kNativeStaticTableEnumeration)) { enumerations.push_back(call); }
    });
    for (ctjs::CallOp enumeration : enumerations) {
        const auto name =
            enumeration->getAttrOfType<mlir::StringAttr>(kNativeStaticTableEnumeration);
        const auto fields = tableFields(module, name.getValue());
        auto match = matchLoop(enumeration);
        if (!fields || !match) { continue; }
        if (fields->size() > maxUnrolledKeys) {
            enumeration->setAttr(
                kNativeStaticTableLoopRefusal,
                mlir::StringAttr::get(module.getContext(),
                                      "a loop over the " + llvm::Twine(fields->size()) +
                                          " keys of a constant table - more than " +
                                          llvm::Twine(maxUnrolledKeys) + " are not unrolled"));
            continue;
        }
        unroll(*match, ctjs::stringAttrOf(enumeration, kNativeStaticTableMethod), *fields);
        // The length is the table's key count, and the enumeration itself is
        // no longer read.
        dropEnumeration(*match, fields->size());
    }
}

} // namespace ctcompile::ctnative
