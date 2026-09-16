// EmitC/Types.cpp - native lowering implementation.
#include "../Admission/Admission.h"
#include "Emitter.h"

namespace ctcompile::ctnative::lowering_detail {

// Retype every JavaScript value in the function from the lattice. Done
// BEFORE any operation is replaced, so the replacements see carriers.
void lowering::retype(ctjs::FuncOp fn) {
    // Current contents evidence is invalidated even by retyping operands.
    // Capture every admitted vector access before changing the source IR.
    fn.getBody().walk([&](mlir::Operation * op) {
        for (mlir::Value result : op->getResults()) {
            if (TypeInference::isDenseVectorSite(result)) { collectVector(result); }
        }
        for (mlir::Region & region : op->getRegions()) {
            for (mlir::Block & block : region) {
                for (mlir::BlockArgument argument : block.getArguments()) {
                    if (TypeInference::isDenseVectorSite(argument)) { collectVector(argument); }
                }
            }
        }
    });
    // Preserve complete receiver schemas before replacements erase solver
    // identities. Read result facts must never choose the storage schema.
    fn.getBody().walk([&](ctjs::CallOp call) {
        if (nativeMapAction(call).empty()) { return; }
        auto map = llvm::dyn_cast_or_null<MapType>(typeOf(call.getReceiver()));
        if (map) { mapSchemas[call] = map; }
    });
    const auto retypeValue = [&](mlir::Value v) {
        if (domStringResults.contains(v)) {
            v.setType(carrierType(context, carrier::string));
            return;
        }
        if (domReads.contains(v.getDefiningOp()) || domUnusedPayloads.contains(v)) {
            // Checked builtin/receiver bookkeeping never needs a value carrier.
            v.setType(mlir::Float64Type::get(context));
            return;
        }
        if (domNulls.contains(v.getDefiningOp()) || domOptionalStrings.contains(v)) {
            v.setType(ec::OpaqueType::get(context, kDOMOptionalStringType));
            return;
        }
        if (auto call = domCalls.find(v.getDefiningOp());
            call != domCalls.end() && call->second.returnsOptionalString()) {
            v.setType(ec::OpaqueType::get(context, kDOMOptionalStringType));
            return;
        }
        if (auto call = domCalls.find(v.getDefiningOp());
            call != domCalls.end() && !call->second.returnsBoolean() &&
            !call->second.returnsElement() && !call->second.returnsNumber() &&
            !call->second.returnsString() && !call->second.returnsJSON()) {
            // The proof requires this result to be unused, and the effect is
            // emitted as a void call. This placeholder never reaches C++.
            v.setType(mlir::Float64Type::get(context));
            return;
        }
        needsObjectValue |= carrierOf(typeOf(v)) == carrier::objectValue;
        if (!llvm::isa<ctjs::ValueType>(v.getType())) { return; }
        if (auto found = ownedObjectTypes.find(v); found != ownedObjectTypes.end()) {
            v.setType(found->second);
            return;
        }
        // A closed object keeps its ctjs type until its shape is known
        // below; everything else takes its carrier now.
        if (admission::isClosedObject(v)) { return; }
        // PHASE 59 SLICE 2 STEP 2: A SHARED BINDING IS NOT ITS CARRIER, it
        // is a PLACE holding one. The box in the owning frame becomes an
        // `emitc.lvalue` - the type an emitc.variable has, which load and
        // assign both take - and the capture parameter that reaches it
        // from a lifted call becomes an `emitc.ptr` to the same carrier,
        // which is the receiver's convention (`ctn_x * self`) one operand
        // along. Both are asked BEFORE the scalar row below, because the
        // lattice type of either is the carrier of what is INSIDE.
        if (admission::isCarriedCell(v.getDefiningOp())) {
            v.setType(ec::LValueType::get(carrierType(context, carrierOf(typeOf(v)))));
            return;
        }
        if (admission::isCellParameter(v)) {
            v.setType(ec::PointerType::get(carrierType(context, carrierOf(typeOf(v)))));
            return;
        }
        const carrier c = carrierOf(typeOf(v));
        if (c == carrier::objectIdentity) {
            needsObjectIdentity = true;
            v.setType(carrierType(context, c));
            return;
        }
        if (c == carrier::methodTable) {
            v.setType(tableType(llvm::cast<MethodTableType>(typeOf(v))));
            return;
        }
        if (c == carrier::closure) {
            v.setType(closureCarrierType(llvm::cast<ClosureType>(typeOf(v))));
            return;
        }
        if (c == carrier::map) {
            auto map = llvm::cast<MapType>(typeOf(v));
            needsObjectValue |= mapNeedsObjectValues(map);
            v.setType(mapCarrierType(map));
            return;
        }
        if (c == carrier::string && admission::lowersToNothing(v)) {
            v.setType(mlir::Float64Type::get(context));
            return;
        }
        // A DENSE ARRAY TAKES ITS OWN CARRIER, which is not one of the two
        // scalars carrierType() can spell: an owning vector in this frame,
        // or an SCF-carried address whose owners outlive every use.
        if (isVectorCarrier(c)) {
            auto owner =
                llvm::cast<ec::LValueType>(vectorCarrierType(context, c == carrier::stringVector));
            const bool borrowed =
                llvm::isa<mlir::BlockArgument>(v) ||
                (v.getDefiningOp() &&
                 llvm::isa<mlir::scf::IfOp, mlir::scf::WhileOp>(v.getDefiningOp()));
            v.setType(borrowed ? mlir::Type(ec::PointerType::get(owner.getValueType()))
                               : mlir::Type(owner));
            return;
        }
        // NO CARRIER IS FATAL, NOT A DOUBLE. This fell through to f64
        // for anything that was not a boolean, so a value admission
        // never looked at - a boxed object threaded through a loop, say
        // - would have been retyped to a number and lowered as one, and
        // the miscompile would have surfaced as a wrong answer at the
        // gate rather than here. Admission refuses every such function;
        // reaching this line is a bug in admission, and says so.
        if (c == carrier::none) {
            if (!admission::lowersToNothing(v)) {
                llvm::report_fatal_error(llvm::Twine("ctnative lowering: `") + fn.getSymName() +
                                         "` holds a value of type " + printed(typeOf(v)) +
                                         " that has no native carrier - admission should "
                                         "have refused it (a literal that reaches a loop is "
                                         "obligation O-3, and whyOpen names it)");
            }
            // A PLACEHOLDER, AND ONLY FOR VALUES THAT ARE ABOUT TO BE
            // ERASED: the lift's poison, a key constant, a declaration
            // closure, the three implicit arguments. Nothing ever reads
            // this type - replace() removes each of them - but the IR has
            // to stay verifiable in between, and `!ctjs.value` among
            // retyped operands does not. It is a double for the same
            // reason `undefined` is: it is the type this tier can always
            // spell.
            //
            // It is written HERE rather than left to carrierType(), which
            // now aborts on a carrier it cannot represent. That default
            // used to answer f64 for everything non-boolean, and the
            // difference matters: a value admission never looked at got a
            // representation and was lowered as a number, so the
            // miscompile surfaced as a wrong answer at the gate instead of
            // as a diagnostic here.
            v.setType(mlir::Float64Type::get(context));
            return;
        }
        v.setType(carrierType(context, c));
    };
    for (mlir::Block & block : fn.getBody()) {
        for (mlir::BlockArgument a : block.getArguments()) { retypeValue(a); }
    }
    fn.getBody().walk([&](mlir::Operation * o) {
        for (mlir::Value r : o->getResults()) { retypeValue(r); }
        for (mlir::Region & region : o->getRegions()) {
            for (mlir::Block & block : region) {
                for (mlir::BlockArgument a : block.getArguments()) { retypeValue(a); }
            }
        }
    });
    // SCF carries an address to the proved entry-scope owner. Do this
    // before scalar boundary conversion, which would otherwise load/copy it.
    fn.getBody().walk([&](mlir::Operation * op) {
        const auto borrow = [&](unsigned index, mlir::Type target) {
            auto pointer = llvm::dyn_cast<ec::PointerType>(target);
            auto owner = llvm::dyn_cast<ec::LValueType>(op->getOperand(index).getType());
            if (!pointer || !owner || pointer.getPointee() != owner.getValueType()) { return; }
            mlir::OpBuilder at(op);
            op->setOperand(
                index, ec::AddressOfOp::create(at, op->getLoc(), pointer, op->getOperand(index)));
        };
        if (auto loop = llvm::dyn_cast<mlir::scf::WhileOp>(op)) {
            for (unsigned index = 0; index < op->getNumOperands(); ++index) {
                borrow(index, loop.getBeforeArguments()[index].getType());
            }
        } else if (auto yield = llvm::dyn_cast<mlir::scf::YieldOp>(op)) {
            auto * parent = yield->getParentOp();
            auto loop = llvm::dyn_cast<mlir::scf::WhileOp>(parent);
            if (!loop && !llvm::isa<mlir::scf::IfOp>(parent)) { return; }
            for (unsigned index = 0; index < op->getNumOperands(); ++index) {
                borrow(index, loop ? loop.getBeforeArguments()[index].getType()
                                   : parent->getResult(index).getType());
            }
        } else if (auto condition = llvm::dyn_cast<mlir::scf::ConditionOp>(op)) {
            for (unsigned index = 1; index < op->getNumOperands(); ++index) {
                borrow(index, condition->getParentOp()->getResult(index - 1).getType());
            }
        }
    });
    // NOW THE SHAPES. The census gave every closed literal in the module
    // its family before anything was lowered - a site's spelling depends on
    // every OTHER site in the program - so here each object only takes the
    // type of its own site.
    fn.getBody().walk([&](ctjs::CreateObjectOp object) {
        if (!methodTableName(object).empty() || object->hasAttr(kNativeObjectIdentity)) { return; }
        if (ownedObjectTypes.contains(object.getResult())) { return; }
        mlir::Value(object.getResult()).setType(classType(shapeAt(object.getResult())));
    });
    // AND THE RECEIVER, which is the same shape one indirection away. It is
    // done here rather than in the loop above because `retypeValue` skips
    // a closed object - "keeps its ctjs type until its shape is known" -
    // and `%arg0` of a lifted method is exactly that.
    if (fn->hasAttr("ctnative.receiver")) {
        mlir::Value self = fn.getBody().front().getArgument(0);
        self.setType(receiverType(shapeAt(self)));
    }
    // AND THE OBJECT PARAMETERS, WHICH ARE THE SAME TYPE IN THE SAME PLACE.
    for (int32_t index : admission::objectArgsOf(fn.getOperation())) {
        mlir::Value parameter = fn.getBody().front().getArgument(static_cast<unsigned>(index));
        parameter.setType(receiverType(shapeAt(parameter)));
    }
}

} // namespace ctcompile::ctnative::lowering_detail
