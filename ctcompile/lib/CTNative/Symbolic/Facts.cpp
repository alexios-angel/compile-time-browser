//===- Facts.cpp - Symbolic primitive-value facts -------------------------===//
//
// Implements symbolic::Analysis (see Facts.h). Constant folding reuses the
// partial evaluator's primitive operations, so a folded value matches the
// interpreter exactly.
//
//===----------------------------------------------------------------------===//

#include "Facts.h"

#include "../PartialEvaluation/Heap.h"
#include "ctcompile/CTNative/Analysis/ClosedCallable.h"
#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/SCF/IR/SCF.h"
#include "mlir/IR/SymbolTable.h"
#include "llvm/ADT/STLExtras.h"

namespace ctcompile::ctnative::symbolic {

Fact literalFact(mlir::Attribute literal) {
    if (llvm::isa_and_nonnull<ctjs::NumberAttr>(literal)) { return {Number, literal}; }
    if (llvm::isa_and_nonnull<ctjs::BooleanAttr>(literal)) { return {Boolean, literal}; }
    if (llvm::isa_and_nonnull<ctjs::StringAttr>(literal)) { return {String, literal}; }
    if (llvm::isa_and_nonnull<ctjs::NullAttr>(literal)) { return {Null, literal}; }
    if (llvm::isa_and_nonnull<ctjs::UndefinedAttr>(literal)) { return {Undefined, literal}; }
    if (auto integer = llvm::dyn_cast_or_null<mlir::IntegerAttr>(literal)) {
        if (integer.getValue().getBitWidth() > 64) { return {}; }
        return {integer.getType().isInteger(1) ? Boolean : Number, literal};
    }
    return {};
}

Fact join(Fact left, Fact right) {
    if (!left.domains || !right.domains) { return {}; }
    return {left.domains | right.domains,
            left.literal == right.literal ? left.literal : mlir::Attribute{}};
}

llvm::StringRef domainName(Fact fact) {
    switch (fact.domains) {
    case Unknown: return "unknown";
    case Number: return "number";
    case Boolean: return "boolean";
    case String: return "string";
    case Null: return "null";
    case Undefined: return "undefined";
    default: return "primitive union";
    }
}

std::optional<bool> condition(Fact fact) {
    if (!fact.literal) { return {}; }
    return partial_eval::truthy(partial_eval::value::primitive(fact.literal));
}

Fact Analysis::get(mlir::Value value) const {
    if (auto constant = value.getDefiningOp<ctjs::ConstantOp>()) {
        if (llvm::isa<mlir::IntegerAttr>(constant.getValue())) { return {}; }
        return literalFact(constant.getValue());
    }
    if (auto constant = value.getDefiningOp<mlir::arith::ConstantOp>()) {
        return literalFact(constant.getValue());
    }
    return facts.lookup(value);
}

namespace {
Fact exported(partial_eval::value value) {
    return value.tag == partial_eval::value::kind::constant ? literalFact(value.constant) : Fact{};
}
/// The constant the evaluator folded `value` to, or nullopt.
std::optional<Fact> folded(partial_eval::value value) {
    const auto fact = exported(value);
    return fact.literal ? std::optional(fact) : std::nullopt;
}
partial_eval::value imported(Fact fact) {
    return fact.literal ? partial_eval::value::primitive(fact.literal) : partial_eval::value{};
}
/// The `typeof` result for a single known domain, or empty.
llvm::StringRef typeName(unsigned domains) {
    switch (domains) {
    case Number: return "number";
    case Boolean: return "boolean";
    case String: return "string";
    case Null: return "object";
    case Undefined: return "undefined";
    default: return {};
    }
}
} // namespace

Fact Analysis::operation(mlir::Operation * op) {
    auto * context = module.getContext();
    if (auto constant = llvm::dyn_cast<ctjs::ConstantOp>(op)) { return get(constant.getResult()); }
    if (auto constant = llvm::dyn_cast<mlir::arith::ConstantOp>(op)) {
        return literalFact(constant.getValue());
    }
    if (auto binary = llvm::dyn_cast<ctjs::BinaryOp>(op)) {
        const Fact left = get(binary.getLhs()), right = get(binary.getRhs());
        if (auto fact = folded(
                partial_eval::binary(binary.getKind(), imported(left), imported(right), context))) {
            return *fact;
        }
        if (binary.getKind() == ctjs::BinaryKind::Concat) {
            return {String, {}};
        }
        if (!left.domains || !right.domains) { return {}; }
        if (binary.getKind() == ctjs::BinaryKind::Add) {
            if (left.domains == String || right.domains == String) {
                return {String, {}};
            }
            return {(left.domains | right.domains) & String ? Number | String : Number, {}};
        }
        // Known primitive domains exclude BigInt, so the other arithmetic and
        // bitwise kinds produce a Number here, as ctjs.binary_static's do
        // below. Any other kind stays unknown.
        if (!ctjs::isArithmetic(binary.getKind()) && !ctjs::isBitwise(binary.getKind())) {
            return {};
        }
        return {Number, {}};
    }
    if (auto binary = llvm::dyn_cast<ctjs::BinaryStaticOp>(op)) {
        const Fact left = get(binary.getLhs()), right = get(binary.getRhs());
        if (auto fact = folded(partial_eval::binary(binary.getKind(), imported(left),
                                                    imported(right), context, true))) {
            return *fact;
        }
        return left.domains && right.domains ? Fact{Number, {}} : Fact{};
    }
    if (auto unary = llvm::dyn_cast<ctjs::UnaryOp>(op)) {
        const Fact input = get(unary.getOperand());
        if (auto fact = folded(partial_eval::unary(unary.getKind(), imported(input), context))) {
            return *fact;
        }
        if (unary.getKind() == ctjs::UnaryKind::TypeOf) {
            if (auto name = typeName(input.domains); !name.empty()) {
                return literalFact(ctjs::StringAttr::get(context, name));
            }
            return {String, {}};
        }
        if (unary.getKind() == ctjs::UnaryKind::Not) {
            return {Boolean, {}};
        }
        if (unary.getKind() == ctjs::UnaryKind::Plus || input.domains) {
            return {Number, {}};
        }
        return {};
    }
    if (auto compare = llvm::dyn_cast<ctjs::CompareOp>(op)) {
        const Fact left = get(compare.getLhs()), right = get(compare.getRhs());
        if (auto fact = folded(partial_eval::compare(compare.getKind(), imported(left),
                                                     imported(right), context))) {
            return *fact;
        }
        // `x == x` is not always true for a number, because NaN is not equal
        // to itself. Boolean, string and nullish values are always equal to
        // themselves, so the fold applies only when Number is excluded.
        if (compare.getLhs() == compare.getRhs() && left.domains && !(left.domains & Number) &&
            (compare.getKind() == ctjs::CompareKind::StrictEq ||
             compare.getKind() == ctjs::CompareKind::Eq)) {
            return literalFact(ctjs::BooleanAttr::get(context, true));
        }
        return {Boolean, {}};
    }
    if (auto truth = llvm::dyn_cast<ctjs::TruthyOp>(op)) {
        if (auto known = condition(get(truth.getValue()))) {
            return literalFact(ctjs::BooleanAttr::get(context, *known));
        }
        return {Boolean, {}};
    }
    if (auto trunc = llvm::dyn_cast<mlir::arith::TruncIOp>(op)) {
        auto integer = llvm::dyn_cast_or_null<mlir::IntegerAttr>(get(trunc.getIn()).literal);
        auto type = llvm::cast<mlir::IntegerType>(trunc.getType());
        if (integer) {
            return literalFact(
                mlir::IntegerAttr::get(type, integer.getValue().trunc(type.getWidth())));
        }
    }
    if (auto call = llvm::dyn_cast<ctjs::CallDirectOp>(op)) {
        if (!llvm::isa_and_nonnull<ctjs::UndefinedAttr>(get(call.getNewTarget()).literal)) {
            return {};
        }
        auto target = call.getTarget();
        return target ? returns.lookup(target) : Fact{};
    }
    return {};
}

bool Analysis::region(mlir::Region & body) {
    for (mlir::Block & block : body) {
        // Only closed function entry blocks have argument facts. Other block
        // and scf region arguments stay unknown: a loop's initial value is not
        // a fact about every iteration.
        for (mlir::BlockArgument arg : block.getArguments()) { facts[arg] = arguments.lookup(arg); }
        for (mlir::Operation & op : block) {
            if (!budget.spend()) { return false; }
            for (mlir::Region & nested : op.getRegions()) {
                if (!region(nested)) { return false; }
            }
            if (auto branch = llvm::dyn_cast<mlir::scf::IfOp>(op)) {
                const auto take = condition(get(branch.getCondition()));
                for (auto [index, result] : llvm::enumerate(op.getResults())) {
                    const auto position = static_cast<unsigned>(index);
                    const Fact yes = get(branch.thenYield().getOperand(position));
                    const Fact no = get(branch.elseYield().getOperand(position));
                    facts[result] = take ? (*take ? yes : no) : join(yes, no);
                }
            } else {
                const Fact result = operation(&op);
                for (mlir::Value value : op.getResults()) { facts[value] = result; }
            }
        }
    }
    return true;
}

bool Analysis::collectCallers(llvm::ArrayRef<ctjs::FuncOp> functions) {
    for (ctjs::FuncOp fn : functions) {
        if (fn->getParentOp() != module || !fn.isPrivate() || functionIndex(fn) == 0 ||
            fn.getBody().empty() || fn.getUpvalueCount() != 0 ||
            !fn.getBody().front().hasNoPredecessors() ||
            fn.getBody().front().getNumArguments() <= ctjs::implicit_arguments) {
            continue;
        }
        if (llvm::any_of(fn.getBody().front().getArgument(ctjs::arg_callee).getUsers(),
                         [](mlir::Operation * user) { return !llvm::isa<ctjs::RootOp>(user); })) {
            continue;
        }
        // Spend budget for one full walk of the module (one unit per operation
        // plus one per operand) before recording this function's callers, so a
        // module too large for the budget fails here instead of being scanned
        // for free below. Report attributes already on the IR are not used as
        // evidence.
        auto charged = module.walk([&](mlir::Operation * op) {
            for (unsigned i = 0; i <= op->getNumOperands(); ++i) {
                if (!budget.spend()) { return mlir::WalkResult::interrupt(); }
            }
            return mlir::WalkResult::advance();
        });
        if (charged.wasInterrupted()) { return false; }
        if (!closedCallableProblem(fn, module).empty()) { continue; }
        bool argumentsObject = false;
        fn.walk([&](ctjs::MakeArgumentsOp) { argumentsObject = true; });
        // In non-strict code, arguments.callee exposes this function as a
        // value, so it may have callers this analysis cannot see.
        if (argumentsObject) { continue; }
        // Specialization may leave this closure as the callee value of a call
        // redirected to another symbol. Those calls' arguments reach the
        // original function too, so leave it generic rather than trust only
        // its remaining direct calls.
        auto sites = directCallers(fn, module);
        if (sites && !sites->empty() && !hasAlternateCalleeCalls(fn, module)) {
            callers[fn] = std::move(*sites);
        }
    }
    return true;
}

void Analysis::run() {
    llvm::SmallVector<ctjs::FuncOp> functions;
    module.walk([&](ctjs::FuncOp fn) { functions.push_back(fn); });
    if (!collectCallers(functions)) { return; }
    // Iterate argument and return summaries to a fixed point, starting from
    // unknown. A recursive dependency cannot invent a primitive result, while
    // literal returns can ground a summary even when the call has effects or
    // might not return. Facts only widen (join), so functions.size() + 1
    // rounds is enough; when there are callers, at least two rounds run,
    // because round 0 computes argument facts before any return fact exists.
    for (size_t round = 0; round <= functions.size(); ++round) {
        bool changed = false;
        for (const auto & [operation, sites] : callers) {
            auto fn = llvm::cast<ctjs::FuncOp>(operation);
            for (auto arg :
                 llvm::drop_begin(fn.getBody().front().getArguments(), ctjs::implicit_arguments)) {
                std::optional<Fact> common;
                for (auto call : sites) {
                    if (!budget.spend()) { return; }
                    auto actual = get(call->getOperand(arg.getArgNumber()));
                    common = common ? join(*common, actual) : actual;
                }
                changed |= arguments.lookup(arg) != common.value_or(Fact{});
                arguments[arg] = common.value_or(Fact{});
            }
        }
        facts.clear();
        for (ctjs::FuncOp fn : functions) {
            if (!region(fn.getBody())) { return; }
            std::optional<Fact> result;
            fn.getBody().walk([&](ctjs::ReturnOp ret) {
                if (ret->getParentOfType<ctjs::FuncOp>() != fn) { return; }
                result = result ? join(*result, get(ret.getValue())) : get(ret.getValue());
            });
            changed |= returns.lookup(fn) != result.value_or(Fact{});
            returns[fn] = result.value_or(Fact{});
        }
        if (!changed && (round != 0 || callers.empty())) { return; }
    }
}

} // namespace ctcompile::ctnative::symbolic
