//===- Functions.cpp - Lower one function to EmitC ------------ -*- C++ -*-===//
//
// Lowers one accepted ctjs.func into an emitc.func: the signature, the body
// rewrite, the dead-constant sweep, parameter-use suppression and removal of
// the implicit arguments.
//
//===----------------------------------------------------------------------===//
#include "../Admission/Admission.h"
#include "Emitter.h"
#include "SymbolTransport.h"

namespace ctcompile::ctnative::lowering_detail {

/// Runs the pass's only rewrite pattern, UnaryPlusIsIdentity (in
/// LoweringSupport.cpp), which replaces `ctjs.unary plus %x` with `%x`.
///
/// It must run before retype(), the only window in which a pattern driver can
/// touch this function: retype() sets every value's type to its carrier, and a
/// `ctjs.unary` whose operand is f64 does not satisfy its own ODS
/// (`CTJS_ValueType`), so afterwards the function does not verify. Before it,
/// the rewrite is `!ctjs.value` for `!ctjs.value` and the IR stays valid.
///
/// It uses applyOpPatternsGreedily, not applyPatternsGreedily, with the
/// worklist seeded with the `ctjs.unary` operations. Every greedy entry point
/// "performs simple dead-code elimination before attempting to match", and no
/// option turns that off, while this pass has its own erasure discipline
/// (eraseIfUnused() is fatal on an operation with uses; PruneDeadStores once
/// lost a test count to the driver erasing what the pass meant to erase).
/// An explicit operation list with ExistingOps strictness keeps the driver's
/// worklist to exactly those operations.
void lowering::applyDeclarativeRules(ctjs::FuncOp fn) {
    llvm::SmallVector<mlir::Operation *> unaries;
    fn.getBody().walk([&](ctjs::UnaryOp u) {
        if (llvm::isa_and_nonnull<NumType>(typeOf(u.getOperand()))) {
            unaries.push_back(u.getOperation());
        }
    });
    if (unaries.empty()) { return; }
    mlir::GreedyRewriteConfig config;
    config.setStrictness(mlir::GreedyRewriteStrictness::ExistingOps)
        .enableFolding(false)
        .enableConstantCSE(false);
    if (mlir::failed(mlir::applyOpPatternsGreedily(unaries, declarative, config))) {
        llvm::report_fatal_error("ctnative lowering: the declarative pattern driver did not "
                                 "converge over a function admission had accepted");
    }
}

void lowering::lower(ctjs::FuncOp fn) {
    const bool hostEntry = hasHostEntry || needsDOM;
    const bool isEntry = !hostEntry && isScriptEntry(fn);
    mlir::Block & entry = fn.getBody().front();
    if (hostEntry) {
        // A host or DOM entry runs with no garbage collector and keeps no
        // rooted values, so drop the ctjs.root bookkeeping and unused constants
        // before choosing carriers; otherwise a dead `undefined` placeholder
        // could force a scalar value model into an ordinary DOM action.
        fn.walk([](ctjs::RootOp root) { root.erase(); });
        fn.walk([](ctjs::ConstantOp constant) {
            if (constant.getResult().use_empty()) { constant.erase(); }
        });
    }
    applyDeclarativeRules(fn);
    retype(fn);
    prepareDOMStrings();
    convertBoundaries(fn);
    prepareExceptions(fn);

    // JavaScript parameters follow the three implicit arguments (receiver,
    // new.target, callee). A lifted receiver comes first, as a borrowed pointer
    // to its concrete shape.
    const bool carriesReceiver = fn->hasAttr("ctnative.receiver");
    llvm::SmallVector<mlir::Type> params;
    if (carriesReceiver) { params.push_back(entry.getArgument(0).getType()); }
    for (unsigned i = 3; i < entry.getNumArguments(); ++i) {
        params.push_back(entry.getArgument(i).getType());
    }
    const auto styleType = ec::OpaqueType::get(context, "ctbrowser::style::engine &");
    for (mlir::BlockArgument parameter : domStyleParameters) {
        if (parameter.getOwner() == &entry) { params.push_back(styleType); }
    }
    const mlir::Type numeric = carrierType(context, carrier::number);
    const mlir::Type i32 = mlir::IntegerType::get(context, 32);
    // The result type is the retyped return's type, or the joined return type
    // from the census when there is one.
    mlir::Type returnType = isEntry ? i32 : numeric;
    if (!isEntry) {
        fn.getBody().walk([&](ctjs::ReturnOp ret) { returnType = ret.getValue().getType(); });
        if (const auto joined = resultTypes.lookup(fn.getSymName())) { returnType = joined; }
    }
    if (isEntry) { params.clear(); }

    mlir::OpBuilder b(fn);
    const auto named = names.find(fn.getSymName());
    const std::string symbol = isEntry                ? std::string{"main"}
                               : named != names.end() ? named->second
                                                      : cIdentifier(fn.getSymName());
    auto made = ec::FuncOp::create(b, fn.getLoc(), symbol,
                                   b.getFunctionType(params, llvm::isa<mlir::NoneType>(returnType)
                                                                 ? mlir::TypeRange{}
                                                                 : mlir::TypeRange{returnType}));
    // The JavaScript name is the symbol before the importer's `$index`.
    const llvm::StringRef jsName = fn.getSymName().split('$').first;
    made->setAttr("ctnative.provenance",
                  b.getStringAttr((isEntry ? "the top level" : "function " + jsName.str()) + ", " +
                                  siteOfFunction(fn)));
    made.getBody().takeBody(fn.getBody());
    mlir::Block & body = made.getBody().front();
    for (mlir::BlockArgument parameter : domStyleParameters) {
        if (parameter.getOwner() == &body) {
            domStyles[parameter] = body.addArgument(styleType, made.getLoc());
        }
    }
    prepareDOMStyles(made);

    // A receiver costs one local, built by memberAccess() at the first field
    // it reads: `emitc.member_of_ptr` wants an lvalue holding the pointer and a
    // parameter is not one, so a method that touches a field opens with
    // `ctn_x * self; self = v0;` and every `this.x` after it is `self->x`. A
    // method that only forwards the receiver gets neither.
    if (carriesReceiver) { receiverArgs.insert(body.getArgument(0)); }
    // An object parameter is treated like a receiver: it arrives as a pointer,
    // so memberAccess() gives it the same `ctn_x * p; p = v3;` local and the
    // same `p->x`.
    for (int32_t index : admission::objectArgsOf(fn.getOperation())) {
        receiverArgs.insert(body.getArgument(static_cast<unsigned>(index)));
    }

    llvm::SmallVector<mlir::Operation *> ops;
    made.getBody().walk([&](mlir::Operation * o) {
        if (o->getDialect() == fn->getDialect() || o->getName().getStringRef() == "ub.poison") {
            ops.push_back(o);
        }
    });
    for (mlir::Operation * o : ops) { replace(o, isEntry, returnType); }
    transportSymbols(made);

    // ponytail: the pinned SCFToEmitC lowerDoWhile drops backedge assignments
    // when after contains only a yield. Keep a printing no-op through later
    // canonicalization, which can empty that region too. Remove after the
    // upstream converter always lowers the yield of carried while loops.
    made.getBody().walk([&](mlir::scf::WhileOp loop) {
        if (loop.getInits().empty()) { return; }
        mlir::OpBuilder after(loop.getYieldOp());
        ec::VerbatimOp::create(after, loop.getLoc(), after.getStringAttr(""));
    });

    // Sweep the constants the rewrite orphaned: the NaN for an `undefined`
    // that main no longer returns, a literal folded into nothing. The
    // canonicalizer will not: emitc.constant carries no memory-effect
    // interface, so dead-code elimination keeps it, and the C++ it prints
    // is an unused variable that -Werror rejects. Reverse order, so a
    // constant whose only user was another dead constant goes too.
    llvm::SmallVector<mlir::Operation *> dead;
    made.getBody().walk([&](mlir::Operation * o) {
        if (domReads.contains(o)) {
            dead.push_back(o);
            return;
        }
        if (o->hasAttr(kNativeStoredRead)) {
            dead.push_back(o);
            return;
        }
        // Lifted closures depend on the reverse order. When the call arm drops
        // a call_direct's callee value, the lifted ctjs.create_closure loses
        // its last use, and the ctjs.create_cell it captured loses its last
        // use once the closure goes. The cell comes first in program order, so
        // the reversed sweep erases the closure, then the cell, then the
        // `undefined` the importer made for its `$enclosing_this`.
        //
        // Capture slots that the enclosing closure fills carry an `undefined`
        // placeholder operand nothing reads (the index is in
        // `enclosing_indices`). It dies the same way once the closure is
        // erased and emits no C++, so lift() does not erase it itself.
        if (llvm::isa<ec::ConstantOp, ec::LiteralOp, ctjs::FrameEnterOp, ctjs::LoadGlobalOp,
                      ctjs::ConstantOp, ctjs::CreateClosureOp, ctjs::CreateCellOp>(o)) {
            dead.push_back(o);
        }
    });
    for (mlir::Operation * o : llvm::reverse(dead)) {
        if (o->use_empty()) { eraseIfUnused(o); }
    }

    // Later canonicalization can erase a parameter's last use (for example,
    // identical Map stores in both arms erase the boolean branch). Preserve
    // a tagged void use for every emitted parameter. The final printer omits
    // it when another use survives, after every cleanup pass has run. This
    // keeps unused parameters warning-clean without cluttering used ones.
    if (!isEntry) {
        mlir::OpBuilder at = mlir::OpBuilder::atBlockBegin(&body);
        for (unsigned i = 0; i < body.getNumArguments(); ++i) {
            if (i < 3 && !(i == 0 && carriesReceiver)) { continue; }
            mlir::Value arg = body.getArgument(i);
            if (domParameters.contains(arg)) {
                // A Data session's inputs are only keys; a DOM entry reads its
                // inputs' members, which a form's controls shadow.
                callWithConstValueOperands(
                    at, made.getLoc(), mlir::TypeRange{},
                    at.getStringAttr(domDatasetParameters.contains(arg)
                                         ? "ctnative::require_dataset_element"
                                     : domDataEntry.empty() ? "ctnative::require_entry_element"
                                                            : "ctnative::require_element"),
                    mlir::ValueRange{arg});
            }
            auto suppression = callWithConstValueOperands(at, made.getLoc(), mlir::TypeRange{},
                                                          at.getStringAttr("static_cast<void>"),
                                                          mlir::ValueRange{arg});
            suppression->setAttr("ctnative.parameter_suppression", at.getUnitAttr());
        }
        for (mlir::BlockArgument parameter : domStyleParameters) {
            if (parameter.getOwner() == &body) {
                callWithConstValueOperands(
                    at, made.getLoc(), mlir::TypeRange{},
                    at.getStringAttr("ctnative::require_style"),
                    mlir::ValueRange{parameter, domStyles.lookup(parameter)});
            }
        }
        if (domDocumentParameter && domDocumentParameter.getOwner() == &body) {
            // A lexical RAII declaration must survive unused-call cleanup and
            // top-declaration printing. Bind only after every input validates.
            ec::VerbatimOp::create(
                at, made.getLoc(),
                "const ctnative::document_scope ctn_document_scope(*{}.owner, {});",
                mlir::ValueRange{domDocumentParameter, domStyles.lookup(domDocumentParameter)});
        }
    }

    // No ctjs operation may survive lowering. replace() is an if-chain over
    // operation names with no fatal default, and the sweep above erases only a
    // fixed list of kinds, so an operation neither handles would silently
    // remain (for example a ctjs.create_closure whose store did not go through
    // the declaration-store arm) and only fail later in the C++ emitter. Check
    // here, where the message can name the function.
    made.getBody().walk([&](mlir::Operation * o) {
        if (o->getDialect() != nullptr && o->getDialect()->getNamespace() == "ctjs") {
            llvm::report_fatal_error(llvm::Twine("ctnative lowering: `") +
                                     o->getName().getStringRef() + "` survived into `" +
                                     made.getSymName() +
                                     "` - every ctjs operation in an accepted function has to "
                                     "be replaced or swept, and this one is handled by no arm "
                                     "of replace()");
        }
    });

    // Remove the implicit arguments last, after the declaration closures that
    // use `callee` and `this` have been erased with their stores: erasing a
    // block argument that still has uses is a silent use-after-free, so it is
    // fatal. A lifted method keeps %arg0 (its receiver is a real parameter),
    // so only new.target and the callee are dropped and counting starts at 1.
    const unsigned first = carriesReceiver ? 1u : 0u;
    const unsigned drop = isEntry ? body.getNumArguments() : 3u - first;
    for (unsigned i = 0; i < drop; ++i) {
        if (!body.getArgument(first).use_empty()) {
            llvm::report_fatal_error(
                llvm::Twine("ctnative lowering: implicit argument of `") + fn.getSymName() +
                "` still has uses after lowering - admission should have refused it");
        }
        body.eraseArgument(first);
    }

    // Keep the source ctjs.func as an empty shell: a caller lowered after this
    // one may still hold a call_direct naming it. finish() erases the shells
    // once every accepted function has been rewritten and no call_direct
    // names them.
    shells.push_back(fn);
}

} // namespace ctcompile::ctnative::lowering_detail
