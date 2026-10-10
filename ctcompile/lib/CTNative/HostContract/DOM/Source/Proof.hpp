//===- Proof.hpp - DOM helper source proof state ----------------*- C++ -*-===//
//
// DOMSource, the state shared by the files in this directory, which together
// implement expandDOMHelpers (../Source.cpp): inlining a DOM entry's local
// helpers into one body that the typed DOM entry proof can then check. Every
// rewrite here assumes that later proof (called "the DOM entry proof" in
// comments) rejects any throw, coercion, reentry or mutation it relies on.
//
//===----------------------------------------------------------------------===//

#pragma once

#include "../../Analysis.h"
#include "../Snapshot/Bounds.h"
#include "ctbrowser/core/algorithms.hpp"
#include "ctcompile/CTNative/Analysis/ClosedCallable.h"
#include "ctcompile/CTNative/Analysis/ImmutableCaptures.h"
#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/SCF/IR/SCF.h"
#include "mlir/Dialect/UB/IR/UBOps.h"
#include "mlir/IR/Dominance.h"
#include "mlir/IR/IRMapping.h"
#include "mlir/IR/SymbolTable.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/StringMap.h"

#include <optional>

namespace ctcompile::ctnative::dom_source_detail {

/// Mutable state for proving that a DOM entry function's helpers can be
/// expanded and simplified for native lowering. Every step is charged against
/// `remaining`; any failure records `reason` via refuse() and the proof stops.
struct DOMSource {
    explicit DOMSource(unsigned maxSteps) : remaining(maxSteps) {}

    /// Work steps left; step() and drain() charge against it.
    unsigned remaining;
    /// Operations in the module, kept current as rewrites add operations;
    /// used to price whole-module scans.
    unsigned operationCount = 0;
    /// The first refusal message; empty while the proof is still going.
    std::string reason;
    /// Every source function, by its function index.
    llvm::DenseMap<unsigned, ctjs::FuncOp> functions;
    /// How many create_closure operations name each function index.
    llvm::DenseMap<unsigned, unsigned> creations;
    /// Functions being expanded (recursion guard), functions fully expanded,
    /// and confined callbacks that must survive the final cleanup.
    llvm::DenseSet<mlir::Operation *> active, expanded, retainedCallbacks;
    /// A captured cell and its single write (null if only initialized).
    /// `enclosingIndex` >= 0 means the capture is instead forwarded from that
    /// upvalue slot of the enclosing closure.
    struct Capture {
        ctjs::CreateCellOp cell;
        ctjs::CellSetOp write;
        int32_t enclosingIndex = -1;
        mlir::Value value() { return write ? write.getValue() : cell.getInitial(); }
    };
    /// Cells resolved by resolveCell, by cell value.
    llvm::DenseMap<mlir::Value, Capture> cells;
    /// Call depth already inlined above each cloned call; bounds call trees.
    llvm::DenseMap<mlir::Operation *, unsigned> callDepth;
    /// Every string a parameter can receive, from bindConstantArguments.
    llvm::DenseMap<mlir::Value, llvm::SmallVector<ctjs::StringAttr>> stringInputs;
    /// Undefined constants inserted as padding for result slots no path
    /// observes; repairInactiveCompletion may later replace them.
    llvm::DenseSet<mlir::Value> inactiveFillers;

    /// Records `message` as the reason unless one is already set; returns false.
    bool refuse(llvm::StringRef message);
    /// refuse(), then the empty result or a stopped walk.
    std::nullopt_t fail(llvm::StringRef message) {
        refuse(message);
        return std::nullopt;
    }
    mlir::WalkResult halt(llvm::StringRef message) {
        refuse(message);
        return mlir::WalkResult::interrupt();
    }
    /// Charges `cost` steps; refuses (leaving the budget as is) if they do not
    /// fit.
    bool step(uint64_t cost = 1);
    /// One all-or-nothing charge; one that does not fit drains the budget, as
    /// `cost` separate steps would.
    bool drain(uint64_t cost);
    /// Charges for one immutableClosureTarget query (six module scans).
    bool chargeCaptureQuery();
    static bool undefined(mlir::Value value) { return ctjs::isUndefinedConstant(value); }

    /// Whether `use` runs after `definition` on every path: `use` is in the
    /// same block later on, or nested only in structured arms and invocation
    /// outcomes of an operation that is.
    bool precedesInStructuredBody(mlir::Operation * definition, mlir::Operation * use);

    /// The source function `closure` creates, or null.
    ctjs::FuncOp closureTarget(ctjs::CreateClosureOp closure) {
        return closure && closure.getFunction() >= 0
                   ? functions.lookup(static_cast<unsigned>(closure.getFunction()))
                   : ctjs::FuncOp{};
    }
    /// Whether `creations` counted exactly one creation of this closure's
    /// function.
    bool uniqueCreation(ctjs::CreateClosureOp closure) {
        return closure.getFunction() >= 0 &&
               creations.lookup(static_cast<unsigned>(closure.getFunction())) == 1;
    }
    /// Erases every root of `value`; refuses any other remaining use.
    bool eraseRootedUses(mlir::Value value, llvm::StringRef reason);

    /// Whether `closure` is a capture-free filter predicate or uppercase
    /// replacement, invoked only as the callback of its own Array filter or
    /// String replace call.
    bool confinedCallback(ctjs::CreateClosureOp closure, ctjs::FuncOp target);

    /// When every call of `target` (through `closure`, or by symbol when
    /// `closure` is null) is known, replaces each string parameter that always
    /// gets the same constant with that constant, and records the possible
    /// strings of the others in stringInputs. Returns false only on refusal.
    bool bindConstantArguments(ctjs::CreateClosureOp closure, ctjs::FuncOp target);

    /// Folds `s.replace(/[X-Y]/g, cb)` in `function`'s top block when every
    /// possible `s` is known: to `s` when nothing matches, or, for `[A-Z]`
    /// with the camelCase-to-kebab-case callback, to the converted strings.
    bool foldConstantReplacements(ctjs::FuncOp function);

    /// Checks that `closure`'s captures are safe to substitute at its calls:
    /// `target` is expanded and every capture is immutable, or forwarded from
    /// an exact enclosing upvalue slot to a body that never stores upvalues.
    bool captureTarget(ctjs::CreateClosureOp closure, ctjs::FuncOp target);

    /// Whether `use` stores a value into a resolved cell (as its initial value
    /// or its single write).
    bool captureStorage(mlir::OpOperand & use);

    /// Proves `cell` is written at most once, before every read, and only
    /// captured by closures captureTarget accepts; then replaces its reads
    /// with the stored value and records it in `cells`.
    bool resolveCell(ctjs::CreateCellOp cell);

    /// Removes the one local cell in `function` that holds a Boolean flag and
    /// is passed by reference to private helper functions (minified
    /// Bootstrap's `!0`/`!1` flags). Proves that every store is a Boolean
    /// literal or !0/!1, that no read comes before initialization, and that
    /// the helpers only read and write the cell under Boolean conditions. Then
    /// inlines the helpers and carries the flag through scf.if results as a
    /// plain SSA value. Returns true without changes when there is no such
    /// cell, and false after refuse() otherwise.
    bool normalizeBooleanCell(ctjs::FuncOp function, unsigned depth);

    /// Replaces each read of a field on a function-local object with the value
    /// last written to it and erases the object, when the object is only read
    /// and written with constant keys in source order and never escapes or
    /// holds a closure.
    bool forwardFields(ctjs::FuncOp function);

    /// Whether `object` is a plain data object (no closure fields) whose uses
    /// the DOM entry proof can check, so it may stay in the expanded body.
    bool dataObject(ctjs::CreateObjectOp object);

    /// Resolves a local object of methods: each read of a method becomes the
    /// closure stored there, the stores are erased, and the method calls are
    /// added to `methods`.
    bool resolveMethods(ctjs::CreateObjectOp object, llvm::DenseSet<mlir::Operation *> & methods);

    /// If `wrapper` publishes `target` through a one-shot factory call
    /// (`g = (function(){ ...; return entry; })()`, or a returned table
    /// selected by property), inlines the factory body into `wrapper` and
    /// erases the factory, so the entry's captured cells live in the wrapper.
    /// Returns true and changes nothing when there is no factory call.
    bool flattenEntryFactory(ctjs::FuncOp wrapper, ctjs::FuncOp target);

    /// Turns the wrapper that declares and publishes the DOM entry into a
    /// function that takes the entry's parameters and calls it: the global
    /// store of the entry closure is removed and the wrapper's
    /// `return undefined` becomes `return entry(args...)`. Refuses unless the
    /// wrapper only builds local state.
    bool initializeEntry(ctjs::FuncOp wrapper, ctjs::FuncOp target);

    /// Rewrites `function`'s body so each completion path (scf.if,
    /// scf.index_switch and scf.while arms, protected try/invoke regions) is
    /// copied separately, choosing an arm whenever its condition is a proved
    /// constant. Each return or throw then receives exact values, and inactive
    /// (poison) result slots become recorded undefined fillers
    /// (inactiveFillers). Refuses when a poison slot could be observed or a
    /// source operation would be left unvisited. Returns true without changes
    /// when the body has no such dispatch.
    bool normalizeCompletion(ctjs::FuncOp function);
    /// Erases the unused inert arithmetic under `root`, users before operands.
    template <typename Root> bool eraseUnusedArithmetic(Root & root) {
        llvm::SmallVector<mlir::Operation *> arithmetic;
        const auto scanned = root.walk([&](mlir::Operation * operation) {
            if (!step()) { return mlir::WalkResult::interrupt(); }
            if (llvm::isa<mlir::arith::ConstantOp, mlir::arith::IndexCastUIOp>(operation)) {
                arithmetic.push_back(operation);
            }
            return mlir::WalkResult::advance();
        });
        if (scanned.wasInterrupted()) { return false; }
        for (mlir::Operation * operation : llvm::reverse(arithmetic)) {
            if (!step()) { return false; }
            if (operation->use_empty()) { operation->erase(); }
        }
        return true;
    }
    /// Replaces each undefined filler that normalizeCompletion inserted for an
    /// unobserved result slot with a real value of the same type that
    /// dominates it and reaches it through the same scf.if result slot, so a
    /// later expansion sees the actual value. Never moves that value's
    /// producer.
    bool repairInactiveCompletion(ctjs::FuncOp function);

    /// Checks that `function`'s body is a structured, self-contained DOM
    /// helper: only scf.if, scf.while, scf.execute_region, invoke and try
    /// nesting (at most 64 deep), every operand defined locally and dominating
    /// its use, one balanced shadow frame, and closures and upvalues with
    /// exact identities. `entry` marks the DOM entry, whose implicit arguments
    /// may be used freely; `directReceiver` allows uses of the receiver
    /// argument; `beforeReplacement` allows upvalue stores, which only exist
    /// before callee replacement.
    bool checkBody(ctjs::FuncOp function, bool entry, bool directReceiver = false,
                   bool beforeReplacement = false);

    /// Proves a helper that is never called is inert for any input, so it can
    /// be erased; nested closures must be provable the same way.
    bool proveUnusedBody(ctjs::FuncOp function, unsigned depth = 0);

    /// Inlines helper `target` at `call` inside `function`, binding the callee,
    /// receiver, `arguments` (missing ones become undefined) and `captures`. A
    /// call inside a protected invoke is accepted only in shapes whose
    /// exception edge is preserved (a forwarded leaf call, an inert body, or an
    /// attribute read/write chain). Returns false and sets `reason` when it
    /// refuses. The caller erases `call` afterwards.
    bool inlineCall(ctjs::FuncOp function, ctjs::FuncOp target, mlir::Operation * call,
                    mlir::ValueRange arguments, mlir::Value receiver, mlir::Value callee,
                    llvm::MutableArrayRef<Capture> captures, unsigned depth);

    /// Normalizes helper `function` and everything it calls into a form the
    /// DOM entry proof can check: runs normalizeCompletion and checkBody,
    /// resolves local cells, inlines direct calls to private uncaptured
    /// helpers (expanding each first) and expands capturing closures. Refuses
    /// recursion and call trees deeper than 64. `entry` marks the program
    /// entry; `directReceiver` is set when the caller supplies the actual
    /// receiver.
    bool expand(ctjs::FuncOp function, unsigned depth, bool entry = false,
                bool directReceiver = false);
};

} // namespace ctcompile::ctnative::dom_source_detail
