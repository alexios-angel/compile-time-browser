#include "Facts.h"

#include "../PartialEvaluation/Heap.h"
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
partial_eval::value imported(Fact fact) {
    return fact.literal ? partial_eval::value::primitive(fact.literal) : partial_eval::value{};
}
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
Fact yielded(mlir::Region & region, unsigned index, const Analysis & analysis) {
    if (!llvm::hasSingleElement(region)) { return {}; }
    auto yield = llvm::dyn_cast<mlir::scf::YieldOp>(region.front().getTerminator());
    return yield && index < yield.getNumOperands() ? analysis.get(yield.getOperand(index)) : Fact{};
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
        if (auto folded = exported(
                partial_eval::binary(binary.getKind(), imported(left), imported(right), context));
            folded.literal) {
            return folded;
        }
        if (binary.getKind() == ctjs::BinaryKind::Concat) {
            return {String, {}};
        }
        if (!left.domains || !right.domains) { return {}; }
        switch (binary.getKind()) {
        case ctjs::BinaryKind::Add:
            if (left.domains == String || right.domains == String) {
                return {String, {}};
            }
            return {(left.domains | right.domains) & String ? Number | String : Number, {}};
        case ctjs::BinaryKind::Sub:
        case ctjs::BinaryKind::Mul:
        case ctjs::BinaryKind::Div:
        case ctjs::BinaryKind::Mod:
        case ctjs::BinaryKind::Pow: return {Number, {}};
        default: return {};
        }
    }
    if (auto binary = llvm::dyn_cast<ctjs::BinaryStaticOp>(op)) {
        const Fact left = get(binary.getLhs()), right = get(binary.getRhs());
        if (auto folded = exported(partial_eval::binary(binary.getKind(), imported(left),
                                                        imported(right), context, true));
            folded.literal) {
            return folded;
        }
        return left.domains && right.domains ? Fact{Number, {}} : Fact{};
    }
    if (auto unary = llvm::dyn_cast<ctjs::UnaryOp>(op)) {
        const Fact input = get(unary.getOperand());
        if (auto folded = exported(partial_eval::unary(unary.getKind(), imported(input), context));
            folded.literal) {
            return folded;
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
        if (auto folded = exported(
                partial_eval::compare(compare.getKind(), imported(left), imported(right), context));
            folded.literal) {
            return folded;
        }
        // No arithmetic identity is implied by a numeric type: NaN is not
        // equal to itself. Primitive boolean/string/nullish domains exclude it.
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
        auto target =
            mlir::SymbolTable::lookupNearestSymbolFrom<ctjs::FuncOp>(call, call.getCalleeAttr());
        return target ? returns.lookup(target) : Fact{};
    }
    return {};
}

bool Analysis::region(mlir::Region & body) {
    for (mlir::Block & block : body) {
        // Region and CFG arguments are runtime values. Loop initializers are
        // not facts about arbitrary iterations, and input annotations are inert.
        for (mlir::BlockArgument arg : block.getArguments()) { facts[arg] = {}; }
        for (mlir::Operation & op : block) {
            if (!budget.take()) { return false; }
            for (mlir::Region & nested : op.getRegions()) {
                if (!region(nested)) { return false; }
            }
            if (auto branch = llvm::dyn_cast<mlir::scf::IfOp>(op)) {
                const auto take = condition(get(branch.getCondition()));
                for (auto [index, result] : llvm::enumerate(op.getResults())) {
                    facts[result] =
                        take ? yielded(*take ? branch.getThenRegion() : branch.getElseRegion(),
                                       static_cast<unsigned>(index), *this)
                             : join(yielded(branch.getThenRegion(), static_cast<unsigned>(index),
                                            *this),
                                    yielded(branch.getElseRegion(), static_cast<unsigned>(index),
                                            *this));
                }
            } else {
                const Fact result = operation(&op);
                for (mlir::Value value : op.getResults()) { facts[value] = result; }
            }
        }
    }
    return true;
}

void Analysis::run() {
    llvm::SmallVector<ctjs::FuncOp> functions;
    module.walk([&](ctjs::FuncOp fn) { functions.push_back(fn); });
    // Unknown summaries are the starting point. A recursive dependency cannot
    // invent a primitive result, while normal literal returns can ground a
    // summary even when the call has effects or might not return.
    for (size_t round = 0; round <= functions.size(); ++round) {
        bool changed = false;
        facts.clear();
        for (ctjs::FuncOp fn : functions) {
            if (!region(fn.getBody())) { return; }
            Fact result;
            bool first = true;
            fn.getBody().walk([&](ctjs::ReturnOp ret) {
                if (ret->getParentOfType<ctjs::FuncOp>() != fn) { return; }
                result = first ? get(ret.getValue()) : join(result, get(ret.getValue()));
                first = false;
            });
            changed |= returns.lookup(fn) != result;
            returns[fn] = result;
        }
        if (!changed) { return; }
    }
}

} // namespace ctcompile::ctnative::symbolic
