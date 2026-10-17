//===- Elements.cpp - Types of an ordinary script's element members -------===//
//
// The element calls and reads Bootstrap's SelectorEngine and Manipulator
// make, typed for an ordinary script (no host contract), where an element is
// the runtime's js_element_t (Runtime/Browser.hpp):
//
//   el.matches(s)                               Boolean
//   el.closest(s), el.querySelector(s)          an element or null
//   el.getAttribute(s)                          a String or null
//   el.setAttribute(s, v), el.removeAttribute(s)  undefined
//   el.previousElementSibling, el.nextElementSibling, el.parentElement
//                                               an element or null
//   [].concat(...a), a an array of elements      a copy of `a`
//
// An element or null is `opt<dom_element>`. Its members are typed as an
// element's: reading one off null throws the VM's TypeError, and the runtime
// throws the same text.
//
//===----------------------------------------------------------------------===//

#include "ctcompile/CTJS/IR/CTJSOps.h"
#include "ctcompile/CTNative/Analysis/TypeInference.h"
#include "ctcompile/CTNative/IR/CTNativeDialect.h"
#include "ctcompile/CTNative/IR/CTNativeLattice.h"

#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/SCF/IR/SCF.h"
#include "mlir/IR/Matchers.h"

namespace ctcompile::ctnative {

namespace {

/// One element method an ordinary script may call: its key and its number of
/// arguments.
struct elementMethod {
    llvm::StringLiteral key;
    unsigned arity;
};
constexpr elementMethod elementMethods[] = {
    {"matches", 1},      {"closest", 1},      {"querySelector", 1},
    {"getAttribute", 1}, {"setAttribute", 2}, {"removeAttribute", 1},
};

/// Whether `type` is an element or an element that may be null.
bool elementOrNull(mlir::Type type) {
    if (auto opt = llvm::dyn_cast_or_null<OptType>(type)) { type = opt.getElementType(); }
    return llvm::isa_and_nonnull<DOMElementType>(type);
}

} // namespace

llvm::StringRef TypeInference::elementCallKey(mlir::Operation * op) {
    auto call = llvm::dyn_cast_or_null<ctjs::CallOp>(op);
    auto read = call ? ctjs::receiverMethod(call) : ctjs::GetPropertyOp{};
    if (!read || read.getObject() != call.getReceiver() || !read.getResult().hasOneUse()) {
        return {};
    }
    const llvm::StringRef key = ctjs::methodKey(call);
    for (const elementMethod & method : elementMethods) {
        if (key == method.key && call.getArgs().size() == method.arity) { return method.key; }
    }
    return {};
}

bool TypeInference::isElementLink(llvm::StringRef key) {
    return key == "previousElementSibling" || key == "nextElementSibling" || key == "parentElement";
}

mlir::Value TypeInference::spreadCopySource(mlir::Operation * op) {
    auto spread = llvm::dyn_cast_or_null<ctjs::CallSpreadOp>(op);
    auto read = spread ? spread.getCallee().getDefiningOp<ctjs::GetPropertyOp>() : nullptr;
    auto empty = read ? read.getObject().getDefiningOp<ctjs::CreateArrayOp>() : nullptr;
    if (!empty || ctjs::constantKey(read.getKey()) != "concat" || !empty.getElements().empty() ||
        spread.getReceiver() != empty.getResult() || !read.getResult().hasOneUse()) {
        return {};
    }
    // The literal is the receiver and nothing else, and the spread arguments
    // are a literal the spread loop filled and nothing else reads.
    for (mlir::Operation * user : empty->getUsers()) {
        if (user != read && user != spread && !llvm::isa<ctjs::RootOp>(user)) { return {}; }
    }
    auto arguments = spread.getArgs().getDefiningOp<ctjs::CreateArrayOp>();
    if (!arguments || !arguments.getElements().empty()) { return {}; }
    for (mlir::OpOperand & use : arguments->getUses()) {
        mlir::Operation * user = use.getOwner();
        if (user != spread && !llvm::isa<ctjs::RootOp>(user) &&
            !(llvm::isa<ctjs::AppendOp>(user) && use.getOperandNumber() == 0)) {
            return {};
        }
    }
    return arguments.getResult();
}

bool TypeInference::isSpreadCopyPlumbing(mlir::Operation * op) {
    const auto copies = [](mlir::Operation * read) {
        return llvm::isa_and_nonnull<ctjs::GetPropertyOp>(read) && read->hasOneUse() &&
               spreadCopySource(*read->getUsers().begin());
    };
    if (llvm::isa_and_nonnull<ctjs::GetPropertyOp>(op)) { return copies(op); }
    auto empty = llvm::dyn_cast_or_null<ctjs::CreateArrayOp>(op);
    return empty && llvm::any_of(empty->getUsers(), copies);
}

namespace {

/// Whether `op` sits in the then-branch of an `scf.if` testing `value`.
bool insideTest(mlir::Value value, mlir::Operation * op) {
    for (mlir::Operation * at = op; at && at->getParentOp(); at = at->getParentOp()) {
        auto branch = llvm::dyn_cast<mlir::scf::IfOp>(at->getParentOp());
        if (!branch || at->getParentRegion() != &branch.getThenRegion()) { continue; }
        auto test = branch.getCondition().getDefiningOp<ctjs::TruthyOp>();
        if (test && test.getValue() == value) { return true; }
    }
    return false;
}

/// Every (constant, yield) pair that can produce `value` through nested
/// `scf.if` results, or false when some producer is not an integer constant.
bool exitCodes(mlir::Value value,
               llvm::SmallVectorImpl<std::pair<int64_t, mlir::Operation *>> & out,
               mlir::Operation * at, unsigned depth = 0) {
    if (depth > 8) { return false; }
    if (auto branch = value.getDefiningOp<mlir::scf::IfOp>()) {
        const unsigned index = llvm::cast<mlir::OpResult>(value).getResultNumber();
        for (mlir::scf::YieldOp yield : {branch.thenYield(), branch.elseYield()}) {
            if (!exitCodes(yield.getOperand(index), out, yield, depth + 1)) { return false; }
        }
        return true;
    }
    llvm::APInt code;
    if (!mlir::matchPattern(value, mlir::m_ConstantInt(&code))) { return false; }
    out.emplace_back(code.getSExtValue(), at);
    return true;
}

} // namespace

bool TypeInference::presentElement(mlir::Value value, mlir::Operation * user) {
    if (insideTest(value, user)) { return true; }
    // `for (; i;) { if (...) return [i]; ... }`: the importer leaves the loop
    // carrying `i` and an exit code, and builds `[i]` in the switch arm for
    // that code. `i` is present there when every producer of that code sits
    // in the loop's `if (i)` arm.
    auto result = llvm::dyn_cast<mlir::OpResult>(value);
    auto loop = result ? llvm::dyn_cast<mlir::scf::WhileOp>(result.getOwner()) : nullptr;
    if (!loop) { return false; }
    // The exit code the use sits under: an arm of `scf.index_switch` on the
    // code, or (once canonicalized) the then-branch of `code == C`.
    mlir::Value selected;
    std::optional<int64_t> wanted;
    for (mlir::Operation * at = user; at && at->getParentOp() && !wanted; at = at->getParentOp()) {
        if (auto cases = llvm::dyn_cast<mlir::scf::IndexSwitchOp>(at->getParentOp())) {
            for (auto [code, region] : llvm::zip(cases.getCases(), cases.getCaseRegions())) {
                if (&region == at->getParentRegion()) {
                    wanted = code;
                    selected = cases.getArg();
                }
            }
            if (!wanted) { return false; }
        } else if (auto branch = llvm::dyn_cast<mlir::scf::IfOp>(at->getParentOp());
                   branch && at->getParentRegion() == &branch.getThenRegion()) {
            auto equal = branch.getCondition().getDefiningOp<mlir::arith::CmpIOp>();
            llvm::APInt code;
            if (!equal || equal.getPredicate() != mlir::arith::CmpIPredicate::eq) { continue; }
            if (mlir::matchPattern(equal.getRhs(), mlir::m_ConstantInt(&code))) {
                selected = equal.getLhs();
            } else if (mlir::matchPattern(equal.getLhs(), mlir::m_ConstantInt(&code))) {
                selected = equal.getRhs();
            } else {
                continue;
            }
            wanted = code.getSExtValue();
        }
    }
    if (!wanted || !selected) { return false; }
    auto cast = selected.getDefiningOp<mlir::arith::IndexCastUIOp>();
    auto selector = cast ? llvm::dyn_cast<mlir::OpResult>(cast.getIn()) : nullptr;
    if (!selector || selector.getOwner() != loop) { return false; }
    mlir::scf::ConditionOp condition = loop.getConditionOp();
    const mlir::Value carried = condition.getArgs()[result.getResultNumber()];
    auto before = llvm::dyn_cast<mlir::BlockArgument>(carried);
    if (!before || before.getOwner() != &loop.getBefore().front()) { return false; }
    llvm::SmallVector<std::pair<int64_t, mlir::Operation *>> codes;
    if (!exitCodes(condition.getArgs()[selector.getResultNumber()], codes, condition)) {
        return false;
    }
    bool seen = false;
    for (const auto & [code, producer] : codes) {
        if (code != *wanted) { continue; }
        if (!insideTest(before, producer)) { return false; }
        seen = true;
    }
    return seen;
}

mlir::Type TypeInference::presentElements(mlir::Operation * op, llvm::ArrayRef<mlir::Value> members,
                                          mlir::Type joined) {
    auto opt = llvm::dyn_cast_or_null<OptType>(joined);
    if (!opt || !llvm::isa<DOMElementType>(opt.getElementType())) { return joined; }
    for (mlir::Value member : members) {
        for (mlir::OpOperand & use : member.getUses()) {
            auto append = llvm::dyn_cast<ctjs::AppendOp>(use.getOwner());
            if (!append || use.getOperandNumber() != 0) { continue; }
            const mlir::Type stored = joinLattices(op, append.getElement(), mlir::Type{});
            if (!llvm::isa_and_nonnull<DOMElementType>(stored) &&
                !presentElement(append.getElement(), append)) {
                return joined;
            }
        }
        // Elements written into a literal itself are not narrowed.
        if (auto literal = member.getDefiningOp<ctjs::CreateArrayOp>();
            literal && !literal.getElements().empty()) {
            return joined;
        }
    }
    return opt.getElementType();
}

mlir::Type TypeInference::elementMemberType(mlir::Operation * op,
                                            llvm::ArrayRef<const TypeLattice *> operands) {
    mlir::MLIRContext * c = op->getContext();
    const auto element = DOMElementType::get(c);
    // The spread loop's iterator over an array of elements is that array, and
    // `[].concat(...a)` copies `a` (operands: callee, arguments, receiver).
    const auto elements = [&](const TypeLattice * lattice) -> mlir::Type {
        auto vector = llvm::dyn_cast_or_null<VecType>(lattice->getValue().getType());
        return vector && llvm::isa<DOMElementType>(vector.getElementType()) ? vector : nullptr;
    };
    if (llvm::isa<ctjs::IterableOp>(op)) { return elements(operands[0]); }
    if (spreadCopySource(op)) { return elements(operands[1]); }
    if (const llvm::StringRef key = elementCallKey(op); !key.empty()) {
        // Operands are the callee, the receiver, then the arguments.
        if (!elementOrNull(operands[1]->getValue().getType())) { return {}; }
        if (key == "matches") { return BoolType::get(c); }
        if (key == "closest" || key == "querySelector") { return OptType::get(c, element); }
        if (key == "getAttribute") { return OptType::get(c, defaultStringType(c)); }
        return OptType::get(c, BottomType::get(c));
    }
    auto read = llvm::dyn_cast<ctjs::GetPropertyOp>(op);
    if (!read || !elementOrNull(operands[0]->getValue().getType())) { return {}; }
    const llvm::StringRef key = ctjs::constantKey(read.getKey());
    if (isElementLink(key)) { return OptType::get(c, element); }
    // An element that may be null reads the members an element does.
    if (llvm::isa<OptType>(operands[0]->getValue().getType()) &&
        (key == "tagName" || key == "id" || key == "className")) {
        return defaultStringType(c);
    }
    return {};
}

} // namespace ctcompile::ctnative
