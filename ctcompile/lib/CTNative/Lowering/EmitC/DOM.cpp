#include "Emitter.h"

namespace ctcompile::ctnative::lowering_detail {

void lowering::censusDOM(const DOMEntryAnalysis & entry) {
    if (!entry.proved()) { return; }
    needsDOM = true;
    for (mlir::BlockArgument parameter : entry.parameters()) { domParameters.insert(parameter); }
    entry.entry().walk([&](ctjs::GetPropertyOp read) {
        if (entry.method(read) || entry.isTokenList(read.getResult())) { domReads.insert(read); }
    });
    entry.entry().walk([&](ctjs::CallOp call) {
        if (const auto * edge = entry.call(call)) {
            domCalls[call] = *edge;
            needsDOMToggle |= edge->kind == HostDOMMethod::toggleClass;
            needsDOMAttributes |= edge->kind == HostDOMMethod::setAttribute;
            needsDOMAttributeToggle |= edge->kind == HostDOMMethod::toggleAttribute;
            needsDOMAttributePresence |= edge->kind == HostDOMMethod::hasAttribute;
            needsDOMAttributeRemoval |= edge->kind == HostDOMMethod::removeAttribute;
            needsDOMContains |= edge->kind == HostDOMMethod::contains;
            needsDOMMatches |= edge->kind == HostDOMMethod::matches;
            needsDOMClosest |= edge->kind == HostDOMMethod::closest;
        }
    });
    for (mlir::BlockArgument parameter : entry.parameters()) {
        if (llvm::any_of(domCalls, [&](const auto & item) {
                return item.second.usesStyle() && item.second.element == parameter;
            })) {
            domStyleParameters.push_back(parameter);
        }
    }
}

bool lowering::replaceDOM(mlir::Operation * operation) {
    if (domReads.contains(operation)) { return true; } // erased after their calls
    const auto found = domCalls.find(operation);
    if (found == domCalls.end()) { return false; }
    auto call = llvm::cast<ctjs::CallOp>(operation);
    const auto & edge = found->second;
    mlir::OpBuilder at(call);
    llvm::SmallVector<mlir::Value> arguments{edge.element};
    if (edge.usesStyle()) { arguments.push_back(domStyles.lookup(edge.element)); }
    llvm::append_range(arguments, call.getArgs());
    llvm::StringRef callee;
    switch (edge.kind) {
    case HostDOMMethod::toggleClass: callee = "ctnative::toggle_class"; break;
    case HostDOMMethod::setAttribute: callee = "ctnative::set_attribute"; break;
    case HostDOMMethod::toggleAttribute: callee = "ctnative::toggle_attribute"; break;
    case HostDOMMethod::hasAttribute: callee = "ctnative::has_attribute"; break;
    case HostDOMMethod::removeAttribute: callee = "ctnative::remove_attribute"; break;
    case HostDOMMethod::contains: callee = "ctnative::contains"; break;
    case HostDOMMethod::matches: callee = "ctnative::matches"; break;
    case HostDOMMethod::closest: callee = "ctnative::closest"; break;
    }
    if (edge.returnsBoolean() || edge.returnsElement()) {
        const mlir::Type type =
            edge.returnsElement() ? carrierType(context, carrier::domElement) : at.getI1Type();
        auto value = callWithConstValueOperands(at, call.getLoc(), mlir::TypeRange{type},
                                                at.getStringAttr(callee), arguments);
        call.getResult().replaceAllUsesWith(value.getResult(0));
    } else {
        callWithConstValueOperands(at, call.getLoc(), mlir::TypeRange{}, at.getStringAttr(callee),
                                   arguments);
        if (!call.getResult().use_empty()) {
            call.getResult().replaceAllUsesWith(absentConstant(at, call.getLoc()));
        }
    }
    eraseIfUnused(call);
    return true;
}

} // namespace ctcompile::ctnative::lowering_detail
