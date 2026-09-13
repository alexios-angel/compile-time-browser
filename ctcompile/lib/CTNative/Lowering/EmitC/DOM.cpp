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
            if ((edge->kind == HostDOMMethod::toggleClass ||
                 edge->kind == HostDOMMethod::toggleAttribute) &&
                call.getArgs().size() == 2) {
                if (auto constant = call.getArgs()[1].getDefiningOp<ctjs::ConstantOp>();
                    constant && llvm::isa<ctjs::UndefinedAttr>(constant.getValue())) {
                    domUndefinedForces.insert(call);
                }
            }
        }
    });
}

bool lowering::replaceDOM(mlir::Operation * operation) {
    if (domReads.contains(operation)) { return true; } // erased after their calls
    const auto found = domCalls.find(operation);
    if (found == domCalls.end()) { return false; }
    auto call = llvm::cast<ctjs::CallOp>(operation);
    const auto & edge = found->second;
    mlir::OpBuilder at(call);
    llvm::SmallVector<mlir::Value> arguments{edge.element};
    llvm::append_range(arguments, call.getArgs());
    if (edge.kind == HostDOMMethod::toggleAttribute && domUndefinedForces.contains(call)) {
        arguments.push_back(
            ec::ConstantOp::create(at, call.getLoc(), at.getI1Type(), at.getBoolAttr(false)));
    }
    llvm::StringRef callee;
    switch (edge.kind) {
    case HostDOMMethod::toggleClass: callee = "ctnative::toggle_class"; break;
    case HostDOMMethod::setAttribute: callee = "ctnative::set_attribute"; break;
    case HostDOMMethod::toggleAttribute: callee = "ctnative::toggle_attribute"; break;
    case HostDOMMethod::hasAttribute: callee = "ctnative::has_attribute"; break;
    case HostDOMMethod::removeAttribute: callee = "ctnative::remove_attribute"; break;
    }
    if (edge.returnsBoolean()) {
        auto value = callWithConstValueOperands(at, call.getLoc(), mlir::TypeRange{at.getI1Type()},
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
