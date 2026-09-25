#include "SymbolTransport.h"
#include "../LoweringSupport.h"

namespace ctcompile::ctnative::lowering_detail {

void transportSymbols(ec::FuncOp function) {
    auto * context = function.getContext();
    const auto symbol = carrierType(context, carrier::symbol);
    constexpr llvm::StringLiteral storageName = "std::optional<ctnative::js_symbol_t>";
    const auto storage = ec::OpaqueType::get(context, storageName);
    const auto wrap = [&](mlir::OpOperand & operand) {
        if (operand.get().getType() != symbol) { return; }
        mlir::OpBuilder at(operand.getOwner());
        auto value =
            ec::CallOpaqueOp::create(at, operand.getOwner()->getLoc(), mlir::TypeRange{storage},
                                     at.getStringAttr(storageName), mlir::ArrayAttr{},
                                     mlir::ArrayAttr{}, mlir::ValueRange{operand.get()});
        operand.set(value.getResult(0));
    };
    const auto unwrap = [&](mlir::Value value, mlir::OpBuilder & at) {
        if (value.getType() != symbol) { return; }
        value.setType(storage);
        if (value.use_empty()) { return; }
        // Every SCF edge supplies a Symbol. Empty storage only permits C++
        // declarations before assignment; it is never a JavaScript alternative.
        // Copy on read so the next loop state cannot change saved identities.
        auto read = ec::MemberCallOpaqueOp::create(
            at, value.getLoc(), mlir::TypeRange{symbol}, value, at.getStringAttr("value"),
            mlir::ArrayAttr{}, mlir::ArrayAttr{}, mlir::ValueRange{});
        value.replaceAllUsesExcept(read.getResult(0), read);
    };
    function.walk<mlir::WalkOrder::PostOrder>([&](mlir::Operation * operation) {
        if (auto branch = llvm::dyn_cast<mlir::scf::IfOp>(operation)) {
            for (mlir::Region & region : branch->getRegions()) {
                if (region.empty()) { continue; }
                for (mlir::OpOperand & operand : region.front().back().getOpOperands()) {
                    wrap(operand);
                }
            }
            mlir::OpBuilder after(operation);
            after.setInsertionPointAfter(operation);
            for (mlir::Value result : branch.getResults()) { unwrap(result, after); }
        } else if (auto loop = llvm::dyn_cast<mlir::scf::WhileOp>(operation)) {
            for (mlir::OpOperand & operand : loop->getOpOperands()) { wrap(operand); }
            for (mlir::Region & region : loop->getRegions()) {
                mlir::OpBuilder start = mlir::OpBuilder::atBlockBegin(&region.front());
                for (mlir::BlockArgument argument : region.front().getArguments()) {
                    unwrap(argument, start);
                }
                for (mlir::OpOperand & operand : region.front().back().getOpOperands()) {
                    wrap(operand);
                }
            }
            mlir::OpBuilder after(operation);
            after.setInsertionPointAfter(operation);
            for (mlir::Value result : loop.getResults()) { unwrap(result, after); }
        }
    });
}

} // namespace ctcompile::ctnative::lowering_detail
