#include "Analysis.h"
#include "DOMSource/Proof.hpp"
#include "ctbrowser/core/algorithms.hpp"
#include "ctcompile/CTNative/Analysis/ClosedCallable.h"
#include "ctcompile/CTNative/Analysis/ImmutableCaptures.h"
#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/SCF/IR/SCF.h"
#include "mlir/Dialect/UB/IR/UBOps.h"
#include "mlir/IR/Dominance.h"
#include "mlir/IR/IRMapping.h"
#include "mlir/IR/SymbolTable.h"
#include "mlir/IR/Verifier.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/ScopeExit.h"
#include "llvm/ADT/StringMap.h"

#include <optional>

namespace ctcompile::ctnative {
using dom_source_detail::DOMSource;

mlir::FailureOr<llvm::SmallVector<mlir::Value>> host_detail::projectInvocationContinuation(
    ctjs::InvokeOp invocation, bool unwind, mlir::Value payload, mlir::OpBuilder & at,
    unsigned & remaining) {
    if (!invocation || !payload) { return mlir::failure(); }
    const auto spend = [&](uint64_t cost) {
        if (cost > remaining) { return false; }
        remaining -= static_cast<unsigned>(cost);
        return true;
    };
    // Reserve verification, mapping and copying before creating any operation.
    // Recovered continuations contain only tags/padding and a complete yield.
    for (auto & region : invocation->getRegions()) {
        if (!region.hasOneBlock() || region.front().empty() ||
            !spend(uint64_t(1) + region.front().getNumArguments())) {
            return mlir::failure();
        }
        for (auto & operation : region.front()) {
            if (operation.getNumRegions() || operation.getNumSuccessors() ||
                !spend(3 *
                       (uint64_t(1) + operation.getNumOperands() + operation.getNumResults()))) {
                return mlir::failure();
            }
            if (&region != &invocation.getBody() &&
                !llvm::isa<ctjs::ConstantOp, mlir::arith::ConstantOp, mlir::ub::PoisonOp,
                           ctjs::InvokeYieldOp>(operation)) {
                return mlir::failure();
            }
        }
    }
    if (mlir::failed(mlir::verify(invocation))) { return mlir::failure(); }
    auto & continuation =
        (unwind ? invocation.getUnwindBody() : invocation.getNormalBody()).front();
    if (payload.getType() != continuation.getArgument(0).getType()) { return mlir::failure(); }
    auto exit = llvm::cast<ctjs::InvokeExitOp>(invocation.getBody().front().back());
    mlir::IRMapping mapping;
    mapping.map(continuation.getArgument(0), payload);
    if (unwind) {
        for (auto [argument, value] :
             llvm::zip(continuation.getArguments().drop_front(), exit.getState())) {
            mapping.map(argument, value);
        }
    }
    for (auto & operation : continuation.without_terminator()) { at.clone(operation, mapping); }
    llvm::SmallVector<mlir::Value> values;
    for (auto value : llvm::cast<ctjs::InvokeYieldOp>(continuation.back()).getValues()) {
        values.push_back(mapping.lookupOrDefault(value));
    }
    return values;
}

bool host_detail::isLowercaseReplacement(ctjs::FuncOp function, llvm::function_ref<bool()> step) {
    if (!function.getBody().hasOneBlock() || function.getUpvalueCount() != 0) { return false; }
    auto & body = function.getBody().front();
    if (body.empty() || body.getNumArguments() != ctjs::implicit_arguments + 1) { return false; }
    for (auto argument : body.getArguments().take_front(ctjs::implicit_arguments)) {
        if (!step() || !argument.use_empty()) { return false; }
    }
    auto returned = llvm::dyn_cast<ctjs::ReturnOp>(body.back());
    auto concat = returned ? returned.getValue().getDefiningOp<ctjs::BinaryOp>() : ctjs::BinaryOp{};
    if (!concat || (concat.getKind() != ctjs::BinaryKind::Concat &&
                    concat.getKind() != ctjs::BinaryKind::Add)) {
        return false;
    }
    auto prefix = concat.getLhs().getDefiningOp<ctjs::ConstantOp>();
    auto text = prefix ? llvm::dyn_cast<ctjs::StringAttr>(prefix.getValue()) : ctjs::StringAttr{};
    auto call = concat.getRhs().getDefiningOp<ctjs::CallOp>();
    auto read =
        call ? call.getCallee().getDefiningOp<ctjs::GetPropertyOp>() : ctjs::GetPropertyOp{};
    if (!text || text.getValue() != "-" || !call || !read || !call.getArgs().empty() ||
        call.getReceiver() != body.getArgument(ctjs::implicit_arguments) ||
        read.getObject() != call.getReceiver() ||
        ctjs::constantKey(read.getKey()) != "toLowerCase") {
        return false;
    }
    // Prove the entire original callback, including discarded operations.
    // Its input is one matched ASCII uppercase unit; no coercion or script
    // reentry is needed for the initial lowercase method or concatenation.
    for (mlir::Operation & operation : body) {
        if (!step()) { return false; }
        if (&operation == read || &operation == call || &operation == concat ||
            llvm::isa<ctjs::ConstantOp, ctjs::FrameEnterOp, ctjs::FrameExitOp, ctjs::RootOp,
                      ctjs::ReturnOp>(operation)) {
            continue;
        }
        return false;
    }
    return true;
}

llvm::Error expandDOMHelpers(mlir::ModuleOp candidate, llvm::StringRef entry, unsigned maxSteps,
                             unsigned * workSteps, llvm::ArrayRef<mlir::Value> inactiveFillers) {
    DOMSource source(maxSteps);
    const llvm::scope_exit recordSteps([&] {
        if (workSteps) { *workSteps = maxSteps - source.remaining; }
    });
    for (mlir::Value filler : inactiveFillers) {
        if (!source.step()) { break; }
        source.inactiveFillers.insert(filler);
    }
    candidate.walk([&](mlir::Operation * operation) {
        if (!source.step()) { return mlir::WalkResult::interrupt(); }
        ++source.operationCount;
        if (auto closure = llvm::dyn_cast<ctjs::CreateClosureOp>(operation);
            closure && closure.getFunction() >= 0) {
            ++source.creations[static_cast<unsigned>(closure.getFunction())];
        }
        return mlir::WalkResult::advance();
    });
    auto target = candidate.lookupSymbol<ctjs::FuncOp>(entry);
    ctjs::FuncOp wrapper;
    for (mlir::Operation & operation : candidate.getBody()->getOperations()) {
        auto function = llvm::dyn_cast<ctjs::FuncOp>(operation);
        if (!source.step()) { break; }
        if (!function || !llvm::hasSingleElement(function.getBody()) ||
            (functionIndex(function) == 0 && function.getUpvalueCount() != 0) ||
            function->hasAttr("ctjs.skipped") ||
            function.getBody().front().getNumArguments() < ctjs::implicit_arguments ||
            !llvm::all_of(function.getBody().front().getArgumentTypes(),
                          [](mlir::Type type) { return llvm::isa<ctjs::ValueType>(type); })) {
            source.refuse(
                "DOM helper requires complete source functions and an uncaptured wrapper");
            break;
        }
        const auto index = functionIndex(function);
        if (!index || !source.functions.try_emplace(*index, function).second) {
            source.refuse("DOM helper source function identity is ambiguous");
            break;
        }
        if (*index == 0 && function != target) { wrapper = function; }
    }
    if (source.reason.empty() && !target) { source.refuse("DOM entry function is missing"); }
    bool initialized = false;
    if (source.reason.empty() && wrapper &&
        !host_detail::isInertEntryDeclaration(wrapper, target, [&] { return source.step(); })) {
        initialized = source.initializeEntry(wrapper, target);
    }
    if (source.reason.empty() && source.expand(initialized ? wrapper : target, 0, true)) {
        for (auto [index, function] : source.functions) {
            (void)index;
            if (!source.step()) { break; }
            if (function != wrapper && !source.expanded.contains(function)) {
                // Class lifting can retire an unread holder slot while retaining
                // its original body. Numeric closures and symbolic calls must
                // both be absent before its independent leaf proof can suffice.
                if (source.remaining / 2 < source.operationCount) {
                    source.refuse("DOM helper expansion work budget exhausted");
                    break;
                }
                source.remaining -= 2 * source.operationCount;
                if (source.creations.lookup(index) != 0 ||
                    !mlir::SymbolTable::symbolKnownUseEmpty(function, candidate.getOperation()) ||
                    !mlir::SymbolTable::symbolKnownUseEmpty(function, &candidate.getBodyRegion()) ||
                    !source.proveUnusedBody(function)) {
                    source.refuse("DOM helper source contains an unvisited function");
                    break;
                }
            }
        }
    }
    if (!source.reason.empty()) {
        return llvm::createStringError(llvm::inconvertibleErrorCode(), source.reason);
    }
    if (initialized) {
        target.getBody().takeBody(wrapper.getBody());
        target.setUpvalueCount(0);
        wrapper.erase();
        source.functions.erase(0);
        wrapper = {};
    }
    for (auto [index, function] : source.functions) {
        (void)index;
        if (function != target && function != wrapper &&
            !source.retainedCallbacks.contains(function)) {
            function.erase();
        }
    }
    return llvm::Error::success();
}
} // namespace ctcompile::ctnative
