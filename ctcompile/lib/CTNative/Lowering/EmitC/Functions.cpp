// EmitC/Functions.cpp - native lowering implementation.
#include "../Admission/Admission.h"
#include "Emitter.h"

namespace ctcompile::ctnative::lowering_detail {

// THE ONE DECLARATIVE STEP OF THIS PASS, AND WHERE IT HAS TO GO.
//
// UnaryPlusIsIdentity.pdll replaces `ctjs.unary plus %x` with `%x`. It runs
// BEFORE retype() because that is the only window in which a PDL driver can
// touch this function at all: retype() sets every value's type to its
// carrier, and a `ctjs.unary` whose operand is f64 does not satisfy its own
// ODS (`CTJS_ValueType`), so from that line onwards the function does not
// verify and no driver may be pointed at it. Before it, the rewrite is
// `!ctjs.value` for `!ctjs.value` and the IR stays valid throughout.
//
// applyOpPatternsGreedily AND NOT applyPatternsGreedily, with the worklist
// seeded by name. Every greedy entry point "performs simple dead-code
// elimination before attempting to match", no configuration option turns
// that off, and this pass has its own erasure discipline - eraseIfUnused()
// is fatal on an operation with uses, and PruneDeadStores already lost a
// test COUNT to the driver taking over an erasure the pass used to make
// itself. Handing the driver a list of `ctjs.unary` operations and
// ExistingOps strictness keeps its worklist to exactly those.
void lowering::applyDeclarativeRules(ctjs::FuncOp fn) {
    llvm::SmallVector<mlir::Operation *> unaries;
    fn.getBody().walk([&](ctjs::UnaryOp u) { unaries.push_back(u.getOperation()); });
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
    const bool isEntry = fn.getSymName().starts_with("_script_$");
    mlir::Block & entry = fn.getBody().front();
    applyDeclarativeRules(fn);
    retype(fn);

    // The signature takes the parameters after the three implicit
    // arguments and returns the proved carrier. A function that returns
    // nothing returns NaN, which is undefined's carrier.
    // THE RECEIVER IS THE FIRST PARAMETER, and this is the whole of the
    // signature change: `double bump_3(ctn_x * self, double n)`. It comes
    // first because ctjs.call_direct's operand 0 is the receiver, so the
    // caller already passes it there.
    const bool carriesReceiver = fn->hasAttr("ctnative.receiver");
    llvm::SmallVector<mlir::Type> params;
    if (carriesReceiver) { params.push_back(entry.getArgument(0).getType()); }
    for (unsigned i = 3; i < entry.getNumArguments(); ++i) {
        params.push_back(entry.getArgument(i).getType());
    }
    const mlir::Type f64 = mlir::Float64Type::get(context);
    const mlir::Type i32 = mlir::IntegerType::get(context, 32);
    // THE RETURN TYPE IS WHAT THE RETURNS CARRY - retyped already, so any
    // ctjs.return's operand type is the answer; a function that never
    // returns a value returns undefined, carried as a NaN double.
    mlir::Type returnType = isEntry ? i32 : f64;
    if (!isEntry) {
        fn.getBody().walk([&](ctjs::ReturnOp ret) { returnType = ret.getValue().getType(); });
    }
    if (isEntry) { params.clear(); }

    mlir::OpBuilder b(fn);
    const auto named = names.find(fn.getSymName());
    const std::string symbol = isEntry                ? std::string{"main"}
                               : named != names.end() ? named->second
                                                      : cIdentifier(fn.getSymName());
    auto made = ec::FuncOp::create(b, fn.getLoc(), symbol, b.getFunctionType(params, {returnType}));
    // The JavaScript name is the symbol before the importer's `$index`.
    const llvm::StringRef jsName = fn.getSymName().split('$').first;
    made->setAttr("ctnative.provenance",
                  b.getStringAttr((isEntry ? "the top level" : "function " + jsName.str()) + ", " +
                                  siteOfFunction(fn)));
    made.getBody().takeBody(fn.getBody());
    mlir::Block & body = made.getBody().front();

    // THE ONE LOCAL A RECEIVER COSTS, built by memberAccess() at the first
    // field it reads: `emitc.member_of_ptr` wants an lvalue HOLDING the
    // pointer and a parameter is not one, so a method that touches a field
    // opens with `ctn_x * self; self = v0;` and every `this.x` after it is
    // `self->x`. A method that only FORWARDS the receiver gets neither.
    if (carriesReceiver) { receiverArgs.insert(body.getArgument(0)); }
    // AN OBJECT PARAMETER IS A RECEIVER IN EVERY WAY THAT MATTERS HERE: it
    // arrived as a pointer, so `memberAccess` has to give it the same
    // `ctn_x * p; p = v3;` local and the same `p->x`.
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

    // SWEEP THE CONSTANTS THE REWRITE ORPHANED - the NaN for an `undefined`
    // that main no longer returns, a literal folded into nothing. The
    // canonicalizer will not: emitc.constant carries no memory-effect
    // interface, so dead-code elimination keeps it, and the C++ it prints
    // is an unused variable that -Werror rejects. Reverse order, so a
    // constant whose only user was another dead constant goes too.
    llvm::SmallVector<mlir::Operation *> dead;
    made.getBody().walk([&](mlir::Operation * o) {
        if (o->hasAttr(kNativeStoredRead)) {
            dead.push_back(o);
            return;
        }
        // PHASE 59 SLICE 1 ADDS TWO, AND THE REVERSE ORDER IS WHY THEY
        // WORK. A lifted ctjs.create_closure loses its last use when the
        // call arm drops the call_direct's callee value; the constant
        // ctjs.create_cell it captured loses ITS last use when that
        // closure goes. Program order puts the cell first, so the reversed
        // sweep erases the closure, then the cell, then the `undefined`
        // constant the importer made for its `$enclosing_this`.
        //
        // AND SLICE 1b'S CAPTURE PLACEHOLDERS GO THE SAME WAY, which is
        // worth saying because they are new and they are DEAD BY
        // CONSTRUCTION. A slot the enclosing closure fills carries an
        // `undefined` operand nothing reads - the index is on
        // `enclosing_indices` - so once the closure is erased the constant
        // has no users at all. It costs the emitted C++ nothing: over
        // native-nested-closure-fixture.js, whose lifted `mid` functions
        // hold exactly these placeholders, the module this pass writes
        // holds ZERO `emitc.constant` of 0x7FF8000000000000, and the
        // emitted C++ is byte for byte what slice 1b emitted when the same
        // slot held a live ctjs.load_upvalue instead.
        //
        // MEASURED BETWEEN 0bf7501 AND 384dbc6, and the citation matters
        // because the figure does not reproduce on THIS tree: both of
        // those commits emit 9,665 bytes, sha256 36b671c54ab13025b5a4d971
        // 181d221b24a4fc217fb3e24c70f422404a3c292d, while the tree
        // carrying this comment emits 9,668 - three bytes more, because
        // the same commit's doc edit to native-nested-closure-fixture.js
        // shifted the JS line numbers that go into the provenance
        // comments. A bare number here would read as false to the next
        // person who checked it.
        //
        // Erasing the placeholder in lift() would therefore buy nothing,
        // which is why it is not erased there.
        if (llvm::isa<ec::ConstantOp, ec::LiteralOp, ctjs::FrameEnterOp, ctjs::LoadGlobalOp,
                      ctjs::ConstantOp, ctjs::CreateClosureOp, ctjs::CreateCellOp>(o)) {
            dead.push_back(o);
        }
    });
    for (mlir::Operation * o : llvm::reverse(dead)) {
        if (o->use_empty()) { eraseIfUnused(o); }
    }

    // An unused owning Map parameter still receives and releases a handle.
    // Keep that signature and evaluate the argument at every call site;
    // explicitly discard the parameter to satisfy -Wunused-parameter.
    // Do this after sweeping capture placeholders, which can be its last
    // apparent use before lowering.
    if (!isEntry) {
        mlir::OpBuilder at = mlir::OpBuilder::atBlockBegin(&body);
        for (unsigned i = 3; i < body.getNumArguments(); ++i) {
            mlir::Value arg = body.getArgument(i);
            if (arg.use_empty() && (carrierOf(typeOf(arg)) == carrier::methodTable ||
                                    carrierOf(typeOf(arg)) == carrier::map ||
                                    carrierOf(typeOf(arg)) == carrier::closure)) {
                ec::CallOpaqueOp::create(at, made.getLoc(), mlir::TypeRange{},
                                         at.getStringAttr("static_cast<void>"),
                                         mlir::ValueRange{arg});
            }
        }
    }

    // AND NOTHING OF THE ctjs DIALECT SURVIVED, WHICH IS THE WHOLE CLAIM.
    // replace() is an if-chain over operation names with no fatal default,
    // and the sweep above erases five kinds by name - so an operation that
    // neither arm handles is simply still there. `ctjs.create_closure` is
    // the live example: it is erased only by the declaration-store arm, so
    // a closure whose store did not take that route rides into the emitted
    // function and is discovered by the C++ emitter three steps later, or
    // by nobody. Asserted here, on the function that was just built, where
    // the message can name it.
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

    // THE IMPLICIT ARGUMENTS GO LAST, once the declaration closures that
    // named `callee` and `this` have been erased with their stores - not
    // before, as they once did: erasing a block argument that still has
    // uses is the same silent use-after-free as erasing an operation
    // with uses, and it surfaced as a crash three passes later in a fold
    // of an operation that did not exist. Same invariant, same fatal.
    //
    // AND A LIFTED METHOD KEEPS %arg0, which is why this counts from a
    // first index rather than always from zero: the receiver is a real
    // parameter now and new.target and the callee are the two that go.
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

    // THE OLD ctjs.func STAYS FOR NOW, hollow: an accepted caller lowered
    // after this one still holds a call_direct naming it, and erasing it
    // here left `is_between` with a live symbol use once - the invariant
    // in finish() caught it. finish() runs after every accepted function
    // has been rewritten, when no call_direct names any of them.
    shells.push_back(fn);
}

} // namespace ctcompile::ctnative::lowering_detail
