//===- ClosureValues.h - Which closures a value can hold --------*- C++ -*-===//
//
// Answers, for a value of the importer's IR, which ctjs.create_closure ops it
// may hold, by following it back to everything that can flow into it. A call
// through a value (`f(x)`, `f.call(r, x)`, `f.apply(r, [x])`) then enters one
// of those closures' functions. The answer is sound in the may direction: a
// value that may come from anything the query does not follow is unknown,
// with the reason, and a value it does follow contributes every closure it
// can hold. ClosureValues.cpp lists the steps it follows.
//
// It takes the realm's built-ins to be untouched when the script starts, which
// holds only because the module is the whole page (the model the window fold
// uses); do not use it on a module that is only part of a page.
//
//===----------------------------------------------------------------------===//
#pragma once

#include "ctcompile/CTJS/IR/CTJSOps.h"
#include "mlir/IR/BuiltinOps.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringMap.h"

#include <cstdint>
#include <map>
#include <optional>
#include <string>

namespace ctcompile::ctjs::globals_detail {

/// The closures the values of one module can hold. It indexes the module when
/// built, so it must not outlive a change to the module's operations.
class ClosureValues {
public:
    explicit ClosureValues(mlir::ModuleOp module);

    /// The closures a value may hold, or why that is not known.
    struct Answer {
        /// Each ctjs.create_closure the value may be, when `unknown` is empty.
        llvm::SmallVector<CreateClosureOp> closures;
        /// Why the value may hold something else; empty when `closures` is
        /// complete.
        std::string unknown;
    };
    Answer closuresOf(mlir::Value value);

    /// The values `value` may hold that the query follows no further (a
    /// constant, a binary result, a made object), when it followed every one,
    /// or why it could not. The setAttribute name rule reads this: a name is
    /// known at build time when every leaf is one the rule admits.
    struct Leaves {
        /// Each value the walk stopped at.
        llvm::SmallVector<mlir::Value> values;
        /// Why the value may hold something else; empty when `values` is
        /// complete.
        std::string unknown;
    };
    Leaves leafValuesOf(mlir::Value value);

    /// The value `call` calls: `f` when the callee is a constant-key read of
    /// `call` or `apply` off `f` and `f` is also the receiver, else the callee.
    static mlir::Value calledValue(CallOp call);

    /// The function `made` makes a closure of, or null when the module does
    /// not hold it.
    FuncOp functionOf(CreateClosureOp made) const;

private:
    /// The closures, array literals and object literals a value may be, or
    /// why it may be something else. `leaves` holds every value the walk
    /// stopped at without following further (a constant, a binary result):
    /// the values leafValuesOf() reports.
    struct objects {
        llvm::SetVector<mlir::Operation *> made;
        llvm::SetVector<mlir::Value> leaves;
        std::string unknown;
    };
    /// Every call that may enter a function, or why some may be unseen.
    struct callers {
        llvm::SetVector<mlir::Operation *> calls;
        std::string unknown;
    };
    /// Everything a binding can hold and every read of it, or why something
    /// else may touch it.
    struct binding {
        llvm::SmallVector<mlir::Value> values;
        llvm::SmallVector<mlir::Value> reads;
        std::string unknown;
    };
    /// An object literal's own keys and the values its literal defines there.
    using ownKeys = llvm::StringMap<llvm::SmallVector<mlir::Value, 1>>;

    objects objectsOf(mlir::Value root);
    std::string step(mlir::Value value, llvm::SmallVectorImpl<mlir::Value> & work, objects & into);
    std::string parameterSources(FuncOp function, unsigned index,
                                 llvm::SmallVectorImpl<mlir::Value> & work);
    std::string resultSources(mlir::Operation * call, llvm::SmallVectorImpl<mlir::Value> & work);
    /// The functions `call` (a ctjs.call or ctjs.call_direct) may enter.
    std::string calleesOf(mlir::Operation * call, llvm::SmallVectorImpl<FuncOp> & functions);
    /// Walks `values` of `function` in the frame `call` enters it with: the
    /// function's parameters take the call's arguments. What leaves the frame
    /// goes to `work`; the literals and closures made in it go to `made`.
    std::string inFrame(FuncOp function, mlir::Operation * call,
                        llvm::SmallVector<mlir::Value> values,
                        llvm::SmallVectorImpl<mlir::Value> & work,
                        llvm::SetVector<mlir::Operation *> & made);
    std::string elementSources(CallOp next, llvm::SmallVectorImpl<mlir::Value> & work);
    std::string onlyDestructured(CreateArrayOp array);

    const callers & callersOf(FuncOp function);
    std::string callsThrough(mlir::Value value, FuncOp function, callers & into,
                             llvm::DenseSet<mlir::Value> & seen);
    std::string methodCalls(CreateObjectOp object, llvm::StringRef key, callers & into);
    std::string objectReads(mlir::Value value, const ownKeys & own, llvm::StringRef key,
                            callers & into, llvm::DenseSet<mlir::Value> & seen);
    std::string receiverUnused(CallOp call, const ownKeys & own);

    const binding & bindingOf(CreateCellOp cell);
    std::string slotAccess(CreateClosureOp made, std::size_t slot, binding & into,
                           llvm::DenseSet<std::pair<mlir::Operation *, std::size_t>> & seen);
    std::string slotCells(FuncOp function, std::uint32_t slot,
                          llvm::SmallVectorImpl<CreateCellOp> & cells, unsigned depth = 0);
    std::string cellsOf(mlir::Value value, llvm::SmallVectorImpl<CreateCellOp> & cells);
    std::optional<std::string> boundReads(mlir::OpOperand & use,
                                          llvm::SmallVectorImpl<mlir::Value> & reads);

    mlir::ModuleOp module;
    llvm::DenseMap<unsigned, FuncOp> byIndex;
    llvm::DenseMap<mlir::Operation *, llvm::SmallVector<CreateClosureOp, 1>> creators;
    /// Why the program may write a prototype table the realm made, which
    /// could change how an array literal iterates; empty when it cannot.
    std::string prototypes;
    llvm::DenseMap<mlir::Value, objects> objectsMemo;
    llvm::DenseSet<mlir::Value> active;
    // Node-based, so a reference handed out survives the nested queries that
    // add entries.
    std::map<mlir::Operation *, callers> callersMemo;
    std::map<mlir::Operation *, binding> bindingMemo;
    llvm::DenseMap<mlir::Operation *, std::string> destructuredMemo;
};

} // namespace ctcompile::ctjs::globals_detail
