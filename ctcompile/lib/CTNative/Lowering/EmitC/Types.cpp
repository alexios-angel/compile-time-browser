// EmitC/Types.cpp - native lowering implementation.
#include "../Admission/Admission.h"
#include "Emitter.h"

namespace ctcompile::ctnative::lowering_detail {

// Retype every JavaScript value in the function from the lattice. Done
// BEFORE any operation is replaced, so the replacements see carriers.
void lowering::retype(ctjs::FuncOp fn) {
    const auto retypeValue = [&](mlir::Value v) {
        needsNullable |= carrierOf(typeOf(v)) == carrier::nullable;
        if (!llvm::isa<ctjs::ValueType>(v.getType())) { return; }
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
            needsString |= carrierOf(typeOf(v)) == carrier::string;
            v.setType(ec::LValueType::get(carrierType(context, carrierOf(typeOf(v)))));
            return;
        }
        if (admission::isCellParameter(v)) {
            needsString |= carrierOf(typeOf(v)) == carrier::string;
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
            v.setType(methodTableCarrierType(llvm::cast<MethodTableType>(typeOf(v))));
            return;
        }
        if (c == carrier::closure) {
            v.setType(closureCarrierType(llvm::cast<ClosureType>(typeOf(v))));
            return;
        }
        if (c == carrier::map) {
            auto map = llvm::cast<MapType>(typeOf(v));
            needsMap = true;
            needsString |= mapNeedsString(map);
            v.setType(mapCarrierType(map));
            return;
        }
        if (c == carrier::string && admission::lowersToNothing(v)) {
            v.setType(mlir::Float64Type::get(context));
            return;
        }
        needsString |= c == carrier::string;
        // A DENSE ARRAY TAKES ITS OWN CARRIER, which is not one of the two
        // scalars carrierType() can spell: `std::vector<double>`, by value,
        // in this frame.
        if (c == carrier::vector) {
            v.setType(vectorCarrierType(context));
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
    // NOW THE SHAPES. The census gave every closed literal in the module
    // its family before anything was lowered - a site's spelling depends on
    // every OTHER site in the program - so here each object only takes the
    // type of its own site.
    fn.getBody().walk([&](ctjs::CreateObjectOp object) {
        if (!methodTableName(object).empty() || object->hasAttr(kNativeObjectIdentity)) { return; }
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
    // AND THE ARRAYS, whose type was taken above; what is left is which
    // reads are `length` and which are indices.
    fn.getBody().walk([&](mlir::Operation * op) {
        if (op->getNumResults() == 1 && TypeInference::isDenseVectorSite(op->getResult(0))) {
            collectVector(op->getResult(0));
        }
    });
}

} // namespace ctcompile::ctnative::lowering_detail
