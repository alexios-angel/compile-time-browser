//===- Types.cpp - Native carrier retyping ----------------------*- C++ -*-===//
//
// lowering::retype() gives every ctjs value in a function its native EmitC
// carrier type before any operation is rewritten, so the rewrites in
// Operations.cpp see carriers rather than `!ctjs.value`.
//
//===----------------------------------------------------------------------===//
#include "../Admission/Admission.h"
#include "Emitter.h"
#include "Vectors.h"
#include "ctcompile/CTNative/Analysis/StaticTables.h"

namespace ctcompile::ctnative::lowering_detail {

/// The type that operand `index` of SCF terminator or loop `op` flows into, or
/// null when the operand is not a carried edge. A while's operands and its
/// `after`-region yield feed the before-region arguments; an scf.for's
/// operands 0-2 are bounds and step, so iter-arg `index` feeds result
/// `index - 3`; an scf.condition's operand 0 is the condition, so `index`
/// feeds result `index - 1`; an if/for yield feeds the parent's result.
mlir::Type edgeTarget(mlir::Operation * op, unsigned index) {
    if (llvm::isa<mlir::scf::YieldOp>(op)) {
        if (auto loop = llvm::dyn_cast<mlir::scf::WhileOp>(op->getParentOp())) {
            op = loop;
        } else {
            return op->getParentOp()->getResult(index).getType();
        }
    }
    if (auto loop = llvm::dyn_cast<mlir::scf::WhileOp>(op)) {
        return loop.getBefore().front().getArgument(index).getType();
    }
    if (llvm::isa<mlir::scf::ForOp>(op) && index >= 3) {
        return op->getResult(index - 3).getType();
    }
    if (llvm::isa<mlir::scf::ConditionOp>(op) && index > 0) {
        return op->getParentOp()->getResult(index - 1).getType();
    }
    return {};
}

/// Visits each value an operation nested in `region` defines, in walk order:
/// its results, then its regions' block arguments.
template <class Visit> static void forEachNestedValue(mlir::Region & region, Visit && visit) {
    region.walk([&](mlir::Operation * op) {
        for (mlir::Value result : op->getResults()) { visit(result); }
        for (mlir::Region & nested : op->getRegions()) {
            for (mlir::Block & block : nested) {
                for (mlir::BlockArgument argument : block.getArguments()) { visit(argument); }
            }
        }
    });
}

/// Retypes every JavaScript value in the function from the lattice. This runs
/// before any operation is replaced, so the replacements see carriers.
void lowering::retype(ctjs::FuncOp fn) {
    // The proof of a vector's current contents is invalidated even by
    // retyping operands, so every admitted vector access is collected before
    // the source IR changes.
    // Arrays of Strings are planned the same way, and first: a dense array
    // that holds Strings belongs to that plan, not to the numeric one.
    const llvm::DenseSet<mlir::Value> stringArrays = planStringArrays(*this, fn);
    forEachNestedValue(fn.getBody(), [&](mlir::Value value) {
        if (TypeInference::isDenseVectorSite(value) && !stringArrays.contains(value)) {
            collectVector(value);
        }
    });
    // Preserve complete receiver schemas before replacements erase solver
    // identities. Read result facts must never choose the storage schema.
    fn.getBody().walk([&](ctjs::CallOp call) {
        if (nativeMapAction(call).empty()) { return; }
        auto map = llvm::dyn_cast_or_null<MapType>(typeOf(call.getReceiver()));
        if (map) { mapSchemas[call] = map; }
        if (auto payload = nativeMapRecordPayload(call.getReceiver())) {
            recordMapPointers[call] = spelling(shapeAt(payload)) + " *";
        }
    });
    const auto retypeValue = [&](mlir::Value v) {
        if (retypeClosureSet(v)) { return; } // ClosureSets.cpp
        if (retypeBorrowed(v)) { return; }
        if (v.getDefiningOp<ctjs::CreateObjectOp>() && carrierOf(typeOf(v)) == carrier::json) {
            v.setType(ec::LValueType::get(carrierType(context, carrier::json)));
            return;
        }
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
        if (auto found = domCalls.find(v.getDefiningOp()); found != domCalls.end()) {
            const HostDOMCall & call = found->second;
            if (call.returnsOptionalString()) {
                v.setType(ec::OpaqueType::get(context, kDOMOptionalStringType));
                return;
            }
            if (call.kind == HostDOMMethod::documentQuerySelectorAll) {
                // The local snapshot owns typed views. Existing proved indexed
                // uses extract a raw element only where it is consumed.
                v.setType(ec::OpaqueType::get(context, kDOMElementViewVectorType));
                return;
            }
            if (!call.returnsBoolean() && !call.returnsElement() && !call.returnsNumber() &&
                !call.returnsSymbol() && !call.returnsString() && !call.returnsJSON() &&
                !call.returnsStringVector() && !call.returnsElementVector()) {
                // The effect is emitted as a void call. Any live JavaScript
                // undefined consumer is replaced separately during emission.
                v.setType(mlir::Float64Type::get(context));
                return;
            }
            if (call.returnsStringVector()) {
                v.setType(ec::OpaqueType::get(context, kStringVectorType));
                return;
            }
        }
        needsObjectValue |= carrierOf(typeOf(v)) == carrier::objectValue;
        if (!llvm::isa<ctjs::ValueType>(v.getType())) { return; }
        if (auto payload = nativeMapRecordPayload(v)) {
            const auto pointer = spelling(shapeAt(payload)) + " *";
            const auto key = mapKeySpelling(llvm::cast<MapType>(typeOf(v)).getKeyType());
            recordMapPointers[v.getDefiningOp()] = pointer;
            v.setType(ec::OpaqueType::get(context, "std::shared_ptr<ctnative::map_storage<" +
                                                       key.str() + ", " + pointer + ">>"));
            return;
        }
        if (auto found = ownedObjectTypes.find(v); found != ownedObjectTypes.end()) {
            v.setType(found->second);
            return;
        }
        // A closed object keeps its ctjs type until its shape is known
        // below; everything else takes its carrier now.
        if (admission::isClosedObject(v) && carrierOf(typeOf(v)) != carrier::json) { return; }
        // A variable shared with a closure is a storage location, not a
        // value. In the frame that owns it, its cell becomes an `emitc.lvalue`
        // (the type an `emitc.variable` has, which load and assign both
        // accept). In a lifted closure that captures it, the capture parameter
        // becomes an `emitc.ptr` to the same carrier, like a method's
        // `ctn_x * self` receiver. Both checks must run before the scalar case
        // below, because the lattice type of either value describes what the
        // cell holds, not the cell itself.
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
        // An element array is a std::vector of its element's spelling, owned
        // where it is made and borrowed by address through SCF, like the
        // dense arrays below.
        if (stringArrays.contains(v)) {
            // ponytail: one alias query per value; cache the groups per
            // function if large functions make this slow.
            const auto members = TypeInference::elementVectorAliases(v);
            // A parameter is a read-only borrow of the caller's array, held
            // by value as a span, so scf carries the span itself.
            if (TypeInference::elementVectorParameter(members)) {
                v.setType(ec::OpaqueType::get(context, elementSpanSpelling(typeOf(v))));
                return;
            }
            // A direct call returns its array by value; the fix-up after this
            // walk moves it into a local, which is the owner.
            if (v.getDefiningOp<ctjs::CallDirectOp>()) {
                v.setType(ec::OpaqueType::get(context, elementArraySpelling(typeOf(v))));
                return;
            }
            const auto owner = ec::OpaqueType::get(context, elementArraySpelling(typeOf(v)));
            const bool borrowed =
                llvm::isa<mlir::BlockArgument>(v) ||
                (v.getDefiningOp() &&
                 llvm::isa<mlir::scf::IfOp, mlir::scf::WhileOp>(v.getDefiningOp()));
            v.setType(borrowed ? mlir::Type(ec::PointerType::get(owner))
                               : mlir::Type(ec::LValueType::get(owner)));
            return;
        }
        // An ordinary script's DOM element is the runtime's view of it.
        if (c == carrier::domElement && pageViews) {
            v.setType(ec::OpaqueType::get(context, kPageElementType));
            return;
        }
        // A tuple (a tuple literal, or an element read from an array of them)
        // is a std::pair of its positions.
        if (carrierOf(typeOf(v)) == carrier::tuple) {
            v.setType(ec::OpaqueType::get(context, elementSpelling(typeOf(v))));
            return;
        }
        // A dense array takes its own vector carrier, which carrierType()
        // cannot spell: an owning vector in this frame,
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
        // A value with no native carrier is a fatal error, not a double.
        // Admission refuses every function holding one, so reaching here is
        // an admission bug; silently typing it f64 would turn that bug into a
        // wrong answer at run time.
        if (c == carrier::none) {
            if (!admission::lowersToNothing(v)) {
                llvm::report_fatal_error(llvm::Twine("ctnative lowering: `") + fn.getSymName() +
                                         "` holds a value of type " + printed(typeOf(v)) +
                                         " that has no native carrier - admission should "
                                         "have refused it (a literal that reaches a loop is "
                                         "obligation O-3, and whyOpen names it)");
            }
            // Placeholder type for values replace() is about to erase (the
            // closure lifter's poison, a key constant, a declaration closure,
            // the three implicit arguments). Nothing reads it, but the IR must
            // verify in between and `!ctjs.value` among retyped operands does
            // not. f64 is used because this lowering can always spell it;
            // carrierType() would abort on carrier::none.
            v.setType(mlir::Float64Type::get(context));
            return;
        }
        v.setType(carrierType(context, c));
    };
    for (mlir::Block & block : fn.getBody()) {
        for (mlir::BlockArgument a : block.getArguments()) { retypeValue(a); }
    }
    forEachNestedValue(fn.getBody(), retypeValue);
    passElementArrays(*this, fn, stringArrays);
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
        // Only while/if yields borrow, and a for loop's operands never do.
        if (llvm::isa<mlir::scf::ForOp>(op) ||
            (llvm::isa<mlir::scf::YieldOp>(op) &&
             !llvm::isa<mlir::scf::WhileOp, mlir::scf::IfOp>(op->getParentOp()))) {
            return;
        }
        for (unsigned index = 0; index < op->getNumOperands(); ++index) {
            if (auto target = edgeTarget(op, index)) { borrow(index, target); }
        }
    });
    // Object shapes. Before lowering, censusShapes() assigned every closed
    // object literal its shape family (a site's C++ type name depends on every
    // other site in the program), so here each object only takes the type
    // already chosen for its own site.
    fn.getBody().walk([&](ctjs::CreateObjectOp object) {
        if (carrierOf(typeOf(object.getResult())) == carrier::json) { return; }
        if (!methodTableName(object).empty() || object->hasAttr(kNativeObjectIdentity)) { return; }
        // A static table's literal is its address (EmitC/StaticTables.cpp).
        if (object->hasAttr(kNativeStaticTable)) { return; }
        if (ownedObjectTypes.contains(object.getResult())) { return; }
        mlir::Value(object.getResult()).setType(classType(shapeAt(object.getResult())));
    });
    // The receiver of a lifted method has the same shape, behind a pointer.
    // It is retyped here because retypeValue leaves closed objects alone until
    // their shape is known, and `%arg0` of a lifted method is one.
    if (fn->hasAttr("ctnative.receiver")) {
        mlir::Value self = fn.getBody().front().getArgument(0);
        self.setType(receiverType(shapeAt(self)));
    }
    // Object parameters are passed the same way as the receiver: a pointer to
    // their shape's class.
    for (int32_t index : admission::objectArgsOf(fn.getOperation())) {
        mlir::Value parameter = fn.getBody().front().getArgument(static_cast<unsigned>(index));
        parameter.setType(receiverType(shapeAt(parameter)));
    }
    fn.getBody().walk([&](ctjs::CallOp call) {
        if (nativeMapRecordOrigin(call.getResult())) {
            mlir::Value(call.getResult()).setType(receiverType(shapeAt(call.getResult())));
        }
    });
    passRecords(fn);
}

} // namespace ctcompile::ctnative::lowering_detail
