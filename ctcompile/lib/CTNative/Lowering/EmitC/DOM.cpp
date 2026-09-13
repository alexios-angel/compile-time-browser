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
    if (edge.kind == HostDOMMethod::toggleClass) {
        auto value =
            callWithConstValueOperands(at, call.getLoc(), mlir::TypeRange{at.getI1Type()},
                                       at.getStringAttr("ctnative::toggle_class"), arguments);
        call.getResult().replaceAllUsesWith(value.getResult(0));
    } else {
        callWithConstValueOperands(at, call.getLoc(), mlir::TypeRange{},
                                   at.getStringAttr("ctnative::set_attribute"), arguments);
        if (!call.getResult().use_empty()) {
            call.getResult().replaceAllUsesWith(absentConstant(at, call.getLoc()));
        }
    }
    eraseIfUnused(call);
    return true;
}

} // namespace ctcompile::ctnative::lowering_detail
