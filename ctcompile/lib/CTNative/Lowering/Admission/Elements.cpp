//===- Admission/Elements.cpp - An ordinary script's element members ------===//
//
// Admits the element calls and reads TypeInference::elementMemberType types
// (Inference/Elements.cpp) when every operand has the carrier the runtime's
// js_element_t member takes (Runtime/Browser.hpp): an element, or an element
// that may be null, as the receiver, and Strings for selectors, attribute
// names and attribute values. A member of null throws the VM's TypeError in
// both engines.
//
//===----------------------------------------------------------------------===//

#include "Admission.h"

#include "mlir/Dialect/SCF/IR/SCF.h"

namespace ctcompile::ctnative::lowering_detail {

namespace {

/// Whether `value` is known non-null at `user`: `user` sits in the
/// then-branch of `if (value)`.
bool testedPresent(mlir::Value value, mlir::Operation * user) {
    for (mlir::Operation * at = user; at && at->getParentOp(); at = at->getParentOp()) {
        auto branch = llvm::dyn_cast<mlir::scf::IfOp>(at->getParentOp());
        if (!branch || at->getParentRegion() != &branch.getThenRegion()) { continue; }
        auto test = branch.getCondition().getDefiningOp<ctjs::TruthyOp>();
        if (test && test.getValue() == value) { return true; }
    }
    return false;
}

/// Why `x === undefined` cannot be told apart from `x === null` for
/// `value`, an element or null, or empty when it can. The carrier holds null
/// and undefined as one empty optional, so the test is exact only for a
/// parameter whose every caller passes a proved element, one it has just
/// tested, or nothing (the resolver pads a missing argument with
/// undefined): SelectorEngine's
/// `e = document.documentElement` default.
std::string whyUndefinedIsAmbiguous(admission & self, mlir::Value value) {
    auto parameter = llvm::dyn_cast<mlir::BlockArgument>(value);
    auto function = parameter ? llvm::dyn_cast<ctjs::FuncOp>(parameter.getOwner()->getParentOp())
                              : ctjs::FuncOp{};
    if (!function || !parameter.getOwner()->isEntryBlock() ||
        parameter.getArgNumber() < ctjs::implicit_arguments) {
        return "it is not a parameter";
    }
    auto module = function->getParentOfType<mlir::ModuleOp>();
    const auto uses = mlir::SymbolTable::getSymbolUses(function, module);
    if (!uses || uses->empty()) { return "its function has no direct caller"; }
    for (const mlir::SymbolTable::SymbolUse & use : *uses) {
        auto call = llvm::dyn_cast<ctjs::CallDirectOp>(use.getUser());
        if (!call) { return "its function is named by something other than a direct call"; }
        const mlir::Value passed = call.getArgOperands()[parameter.getArgNumber()];
        if (ctjs::isUndefinedConstant(passed) ||
            carrierOf(self.typeOf(passed)) == carrier::domElement || testedPresent(passed, call)) {
            continue;
        }
        return "a caller passes " + printed(self.typeOf(passed)) +
               ", which may be null, and null and undefined are one empty optional";
    }
    return {};
}

} // namespace

std::optional<bool> elementOperation(admission & self, mlir::Operation * o) {
    const auto element = [&](mlir::Value v) {
        const carrier c = carrierOf(self.typeOf(v));
        return c == carrier::domElement || c == carrier::nullableElement;
    };
    // The spread loop's iterator and `[].concat(...a)`'s copy, over an array
    // of elements the element-vector proof accepts.
    if (llvm::isa<ctjs::IterableOp>(o) || TypeInference::spreadCopySource(o)) {
        const mlir::Value array = o->getResult(0);
        if (carrierOf(self.typeOf(array)) != carrier::domElementVector) { return std::nullopt; }
        return isElementArray(array, self.typeOf(array), self.pageViews()) ||
               self.refuse("a spread of an array of elements that is not an element vector");
    }
    // `[i]` with `i` an element or null: admitted where it is present.
    if (auto append = llvm::dyn_cast<ctjs::AppendOp>(o);
        append && carrierOf(self.typeOf(append.getElement())) == carrier::nullableElement &&
        carrierOf(self.typeOf(append.getArray())) == carrier::domElementVector) {
        return TypeInference::presentElement(append.getElement(), o) ||
               self.refuse("an element or null stored into an array of elements where it may "
                           "be null");
    }
    if (auto compare = llvm::dyn_cast<ctjs::CompareOp>(o)) {
        const bool strict = compare.getKind() == ctjs::CompareKind::StrictEq;
        if (!strict && compare.getKind() != ctjs::CompareKind::Eq) { return std::nullopt; }
        mlir::Value tested = compare.getLhs(), other = compare.getRhs();
        if (carrierOf(self.typeOf(tested)) != carrier::nullableElement) {
            std::swap(tested, other);
        }
        if (carrierOf(self.typeOf(tested)) != carrier::nullableElement) { return std::nullopt; }
        // `x == null` and `x == undefined` ask the same question; `x ===
        // undefined` holds only where no null can reach x.
        const bool absent = ctjs::isUndefinedConstant(other) ||
                            (!strict && ctjs::constantAttr<ctjs::NullAttr>(other));
        if (!absent) {
            return self.refuse("an element or null compared with " + printed(self.typeOf(other)) +
                               " - only its absence is tested");
        }
        if (strict) {
            if (const std::string why = whyUndefinedIsAmbiguous(self, tested); !why.empty()) {
                return self.refuse("`=== undefined` on an element or null: " + why);
            }
        }
        return true;
    }
    const auto string = [&](mlir::Value v) { return carrierOf(self.typeOf(v)) == carrier::string; };
    if (const llvm::StringRef key = TypeInference::elementCallKey(o); !key.empty()) {
        auto call = llvm::cast<ctjs::CallOp>(o);
        if (!element(call.getReceiver())) { return std::nullopt; }
        for (mlir::Value argument : call.getArgs()) {
            if (!string(argument)) {
                return self.refuse(("an element's " + key + " given " +
                                    printed(self.typeOf(argument)) + ", not a String")
                                       .str());
            }
        }
        const carrier result = carrierOf(self.typeOf(call.getResult()));
        const bool typed = key == "matches"        ? result == carrier::boolean
                           : key == "getAttribute" ? result == carrier::nullableString
                           : (key == "closest" || key == "querySelector")
                               ? result == carrier::nullableElement
                               : call.getResult().use_empty();
        return typed || self.refuse(("an element's " + key + " whose result is " +
                                     printed(self.typeOf(call.getResult())))
                                        .str());
    }
    auto read = llvm::dyn_cast<ctjs::GetPropertyOp>(o);
    if (!read || !element(read.getObject())) { return std::nullopt; }
    const llvm::StringRef key = ctjs::constantKey(read.getKey());
    const carrier result = carrierOf(self.typeOf(read.getResult()));
    if (key == "parentNode") {
        return self.refuse("an element's parentNode, which is the Document for the document "
                           "element - no native carrier holds a node that is not an element "
                           "(parentElement is native)");
    }
    if (TypeInference::isElementLink(key)) {
        return result == carrier::nullableElement ||
               self.refuse(("an element's " + key + " that is not an element or null").str());
    }
    if (carrierOf(self.typeOf(read.getObject())) != carrier::nullableElement) {
        return std::nullopt;
    }
    if (key == "tagName" || key == "id" || key == "className") {
        return result == carrier::string ||
               self.refuse(("the `" + key + "` of an element or null that is not a String").str());
    }
    return self.refuse(("an element or null read through `" + key +
                        "` - it reads only tagName, id, className and the element links")
                           .str());
}

} // namespace ctcompile::ctnative::lowering_detail
