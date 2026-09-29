#include "Proof.hpp"

namespace ctcompile::ctnative::class_detail {

// STATIC FACTORIES. `C.k(args)` runs k's body with `this` = C. When that body
// is one straight-line block that builds through its receiver - `new this(el)`
// - it is the same code at the call site with C for `this`: the construction
// becomes the ordinary local `new C(el)` every later proof already handles,
// and dropUnreadStatics then retires the definition no read reaches. `C.k`
// resolves to C's own static method or, when C writes no `k` at all, to the
// one its proved heritage base chain defines; examine still proves every
// class, slot and use afterwards.
// ponytail: one capture-free block. Conditional factories (getOrCreateInstance's
// `||`) need constructions inside scf.if arms; captured helpers need the
// capture census moved to the call site.
bool classInitialization::expandStaticFactories() {
    llvm::DenseMap<mlir::Value, ctjs::CallOp> setupOf;
    for (ctjs::CallOp call : calls) {
        if (!step()) { return false; }
        auto closure = call.getArgs().size() == 1
                           ? call.getArgs().front().getDefiningOp<ctjs::CreateClosureOp>()
                           : ctjs::CreateClosureOp{};
        if (closure) { setupOf.try_emplace(closure.getResult(), call); }
    }
    // The closure the exact slot write `cls.key = closure` stores before the
    // class setup, or a null op with `known` false when anything else may
    // define the key: a second or computed write, or an accessor.
    const auto ownStatic = [&](mlir::Value cls, llvm::StringRef key,
                               bool & known) -> ctjs::CreateClosureOp {
        known = false;
        const auto setup = setupOf.lookup(cls);
        if (!setup) { return {}; }
        ctjs::CreateClosureOp found;
        for (mlir::OpOperand * use : sourceUses(cls)) {
            if (!step()) { return {}; }
            if (use->getOperandNumber() != 0) { continue; }
            if (auto accessor = llvm::dyn_cast<ctjs::DefineAccessorOp>(use->getOwner())) {
                if (accessor.getName() == key) { return {}; }
                continue;
            }
            auto write = llvm::dyn_cast<ctjs::SetPropertyOp>(use->getOwner());
            if (!write) { continue; }
            const auto written = ctjs::constantKey(write.getKey());
            if (written.empty() && !ctjs::constantAttr<ctjs::NumberAttr>(write.getKey())) {
                return {};
            }
            if (written != key) { continue; }
            auto closure = write.getValue().getDefiningOp<ctjs::CreateClosureOp>();
            if (found || !closure || write->getBlock() != setup->getBlock() ||
                !write->isBeforeInBlock(setup)) {
                return {};
            }
            found = closure;
        }
        known = true;
        return found;
    };
    const auto resolve = [&](mlir::Value cls, llvm::StringRef key) -> ctjs::CreateClosureOp {
        for (unsigned depth = 0; depth < 16 && cls; ++depth) {
            bool known = false;
            if (auto closure = ownStatic(cls, key, known)) { return closure; }
            if (!known) { return {}; }
            auto inherited = heritage.lookup(cls);
            cls = inherited ? sourceValue(inherited.getArgs()[1]) : mlir::Value{};
        }
        return {};
    };
    // A body the call site can hold unchanged: every receiver use builds,
    // reads or calls through it, and at least one builds or calls - a call
    // through `this` may name the next factory, exposed by the next round.
    const auto factory = [&](ctjs::CreateClosureOp closure) {
        auto fn = target(closure);
        if (!fn || !closure.getUpvalues().empty() || fn.getUpvalueCount() != 0 ||
            !fn.getBody().hasOneBlock()) {
            return false;
        }
        auto & body = fn.getBody().front();
        if (!llvm::isa<ctjs::ReturnOp>(body.getTerminator()) ||
            !body.getArgument(ctjs::arg_new_target).use_empty() ||
            !body.getArgument(ctjs::arg_callee).use_empty()) {
            return false;
        }
        for (mlir::Operation & op : body) {
            if (!step() ||
                !llvm::isa<ctjs::FrameEnterOp, ctjs::FrameExitOp, ctjs::ReturnOp, ctjs::RootOp,
                           ctjs::ConstantOp, ctjs::ConstructOp, ctjs::GetPropertyOp, ctjs::CallOp>(
                    op) ||
                op.getNumRegions() != 0) {
                return false;
            }
        }
        const auto receiver = body.getArgument(ctjs::arg_receiver);
        bool builds = false;
        for (mlir::OpOperand & use : receiver.getUses()) {
            if (!step()) { return false; }
            auto * op = use.getOwner();
            if (auto made = llvm::dyn_cast<ctjs::ConstructOp>(op)) {
                if (made.getCallee() != receiver || made.getNewTarget() != receiver) {
                    return false;
                }
                builds = true;
                continue;
            }
            if (llvm::isa<ctjs::RootOp>(op) ||
                (llvm::isa<ctjs::GetPropertyOp>(op) && use.getOperandNumber() == 0)) {
                continue;
            }
            auto call = llvm::dyn_cast<ctjs::CallOp>(op);
            auto read = call ? call.getCallee().getDefiningOp<ctjs::GetPropertyOp>()
                             : ctjs::GetPropertyOp{};
            if (!call || use.getOperandNumber() != 1 || !read || read.getObject() != receiver) {
                return false;
            }
            builds = true;
        }
        return builds;
    };
    struct site {
        ctjs::GetPropertyOp read;
        ctjs::CallOp invocation;
        ctjs::FuncOp fn;
    };
    // A factory calling another through `this` exposes that call only once
    // expanded; each round checks every candidate before anything changes.
    for (unsigned round = 0; round < 8; ++round) {
        llvm::SmallVector<site> sites;
        for (auto [cls, setup] : setupOf) {
            for (mlir::OpOperand * use : sourceUses(cls)) {
                if (!step()) { return false; }
                auto read = llvm::dyn_cast<ctjs::GetPropertyOp>(use->getOwner());
                const auto key = read ? ctjs::constantKey(read.getKey()) : llvm::StringRef{};
                if (!read || use->getOperandNumber() != 0 || !ctjs::ordinaryKey(read.getKey()) ||
                    closureMetadataKey(key) || key == "prototype" ||
                    read->getBlock() != setup->getBlock() || !setup->isBeforeInBlock(read)) {
                    continue;
                }
                ctjs::CallOp invocation;
                bool exact = true;
                for (mlir::OpOperand & readUse : read.getResult().getUses()) {
                    if (!step()) { return false; }
                    if (llvm::isa<ctjs::RootOp>(readUse.getOwner())) { continue; }
                    auto call = llvm::dyn_cast<ctjs::CallOp>(readUse.getOwner());
                    exact &= !invocation && call && readUse.getOperandNumber() == 0 &&
                             sourceValue(call.getReceiver()) == cls;
                    invocation = call;
                }
                auto closure = exact && invocation ? resolve(cls, key) : ctjs::CreateClosureOp{};
                if (closure && factory(closure)) {
                    sites.push_back({read, invocation, target(closure)});
                }
            }
        }
        for (auto [read, invocation, fn] : sites) {
            auto & body = fn.getBody().front();
            if (!spendWalk(fn, cloneCost)) { return false; }
            mlir::OpBuilder at(invocation);
            mlir::IRMapping mapping;
            mapping.map(body.getArgument(ctjs::arg_receiver), invocation.getReceiver());
            mlir::Value absent;
            for (unsigned index = ctjs::implicit_arguments; index < body.getNumArguments();
                 ++index) {
                const unsigned actual = index - ctjs::implicit_arguments;
                if (actual < invocation.getArgs().size()) {
                    mapping.map(body.getArgument(index), invocation.getArgs()[actual]);
                    continue;
                }
                if (!absent) {
                    absent = ctjs::ConstantOp::create(
                        at, invocation.getLoc(), ctjs::UndefinedAttr::get(module.getContext()));
                }
                mapping.map(body.getArgument(index), absent);
            }
            for (mlir::Operation & op : body.without_terminator()) {
                if (llvm::isa<ctjs::FrameEnterOp, ctjs::FrameExitOp>(op)) { continue; }
                at.clone(op, mapping);
            }
            auto returned = llvm::cast<ctjs::ReturnOp>(body.getTerminator());
            invocation.getResult().replaceAllUsesWith(mapping.lookupOrDefault(returned.getValue()));
            invocation.erase();
            eraseRooted(read);
        }
        if (sites.empty()) { return true; }
    }
    return refuse("static factory expansion exceeds its nesting limit");
}

} // namespace ctcompile::ctnative::class_detail
