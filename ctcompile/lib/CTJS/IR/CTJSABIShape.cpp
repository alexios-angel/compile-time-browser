// DOES THIS OPERATION'S DECLARATION MATCH THE HELPER IT NAMES?
//
// CTJS_RuntimeOp already makes an operation unable to claim a helper the
// runtime does not declare: the helper is concatenated into a reference to a
// ctbrowser::aot::helper_id enumerator, so a wrong name stops the C++ build.
// That check is about the helper's IDENTITY and it says nothing about its
// SHAPE - and the shape is where the operations actually drifted.
//
// FOUR OF THE ELEVEN OPERATIONS THAT LOWERED TODAY WERE WRONG, each in a way
// that reads correctly in both files:
//
//   ctjs.load_upvalue  named ct_aot_cell_get and carried an $index attribute
//     the helper has no parameter for. It lowered, dropping the index - so
//     every read of a captured variable compiled to whichever cell the
//     closure value happened to be, and cell_get "no-ops on a non-cell",
//     yielding undefined rather than failing.
//   ctjs.store_upvalue  the same, for writes.
//   ctjs.instanceof     declared a !ctjs.value result against a uint32_t 0/1,
//     so `x instanceof C` would have produced value::from_bits(1) - a
//     subnormal double - instead of a boolean.
//   ctjs.delete_property declared a !ctjs.value result against a helper whose
//     int32_t is a STATUS, so `delete o[k]` would have evaluated to the status.
//
// None of those is a name error and none is a type error inside the dialect.
// Each is a disagreement between what ODS declares and what the ABI takes,
// which is exactly the class of defect this project keeps meeting: code that
// verifies, prints plausibly and is wrong.
//
// SO THE CHECK IS DERIVED, AND IT IS A TRAIT RATHER THAN A VERIFIER PER
// OPERATION. Per-operation is fifty chances to leave it out and fifty places
// for it to rot; on the base class every CTJS_RuntimeOp gets it, including the
// ones added after this file was last read. Nothing here names an operation.
#include "ctcompile/CTJS/IR/CTJSTraits.h"

#include "ctcompile/CTJS/IR/CTJSInterfaces.h"
#include "ctcompile/CTJS/IR/CTJSTypes.h"
#include "ctcompile/CTJS/Lowering/RuntimeHelpers.hpp"

#include "mlir/IR/BuiltinAttributes.h"

namespace ctcompile::ctjs {

namespace {

// How the ABI's parameters divide into the three things a caller must find.
struct role_census {
    // Supplied by an SSA value: the frame, the operands, an argv span.
    std::size_t ssa = 0;
    // Written through by the callee: these are RESULTS, never operands.
    std::size_t out = 0;
    // Immediates with no SSA source - an opcode, a count, a literal's bytes.
    // These are what an operation carries as an ATTRIBUTE.
    std::size_t immediate = 0;
    // Whether the first parameter is the frame handle, which the lowering may
    // supply itself rather than the operation carrying it.
    bool leads_with_frame = false;
};

role_census census_of(const runtime_helper & helper) {
    role_census census;
    for (std::size_t i = 0; i < helper.role_count; ++i) {
        switch (helper.roles[i]) {
        case param_role::out_value:
        case param_role::out_other: ++census.out; break;
        case param_role::opcode:
        case param_role::count:
        case param_role::number:
        case param_role::text: ++census.immediate; break;
        default: ++census.ssa; break;
        }
    }
    census.leads_with_frame =
        helper.role_count > 0 &&
        (helper.roles[0] == param_role::frame || helper.roles[0] == param_role::context);
    return census;
}

// WHETHER THE RETURN REGISTER CARRIES A RESULT.
//
// A STATUS IS NOT A RESULT and this is the distinction that catches
// ctjs.delete_property. 24 rows return a ct_aot_status, which is the exception
// edge and belongs to the throwing tier, not to the operation's value; 11
// return void. Everything else answers in the return register, and 33 rows
// answer ONLY there - no out-parameter at all - so a rule that counted only
// out-parameters would delete the result of half the table.
bool return_carries_a_result(const runtime_helper & helper) {
    return helper.ret != return_role::nothing && helper.ret != return_role::status;
}

} // namespace

mlir::LogicalResult verifyABIShape(mlir::Operation * op) {
    auto call = mlir::dyn_cast<RuntimeCallOpInterface>(op);
    if (!call) { return mlir::success(); }
    const runtime_helper & helper = helper_for(call.getHelperID());
    const role_census census = census_of(helper);

    // THERE IS NO OPERAND-COUNT RULE HERE, AND THAT IS A FINDING RATHER THAN
    // AN OMISSION.
    //
    // The obvious check - "operands must equal the arguments the ABI takes
    // from the IR" - was written, and it rejected six operations that are
    // right. The dialect is DELIBERATELY HIGHER-LEVEL than the ABI:
    // ctjs.get_property carries an object and a key and not the inline cache
    // Phase 26 attaches, ctjs.call carries a callee and a receiver and not the
    // argv span, the argc, the key or the baked site, and ctjs.frame_enter
    // carries nothing at all. Every one of those arguments is materialised by
    // the lowering, which is the whole reason the operation is not just a
    // spelling of the call.
    //
    // So an operation supplying FEWER arguments than the ABI takes is the
    // normal case and cannot be an error. What remains checkable is EXCESS -
    // an operation carrying something the helper has nowhere to put - and that
    // is what the attribute rule below tests. A sound weaker check beats a
    // strict one that must be suppressed everywhere it fires.

    // THE RESULTS. Out-parameters are results, and so is the return register
    // unless it carries a status or nothing.
    const std::size_t expected = census.out + (return_carries_a_result(helper) ? 1 : 0);
    if (op->getNumResults() != expected) {
        return op->emitOpError() << "declares " << op->getNumResults() << " result(s), but "
                                 << helper.symbol << " answers with " << expected
                                 << " (" << census.out << " out-parameter(s) and a "
                                 << (return_carries_a_result(helper) ? "value" : "status or void")
                                 << " return)";
    }

    // AND WHAT THE RETURN REGISTER'S RESULT IS, which is a different question
    // from how many there are. ct_aot_instance_of returns a uint32_t 0 or 1;
    // declaring that as a !ctjs.value makes `x instanceof C` evaluate to
    // value::from_bits(1), a subnormal double, and every later pass would be
    // right to believe the type.
    if (return_carries_a_result(helper) && op->getNumResults() > 0) {
        const mlir::Type first = op->getResult(0).getType();
        // A CLOSURE AND A COROUTINE ARE JAVASCRIPT VALUES, and the dialect says
        // so where it introduces them: "a closure is a value at run time; the
        // distinct type is for the one place that BUILDS one". They are
        // refinements of !ctjs.value, not alternatives to it, so
        // ct_aot_make_closure's uint64_t is honestly declared as !ctjs.closure.
        // !ctjs.program is NOT one - it is the importer's input, a compiled
        // script::program, and no helper returns one.
        const bool is_js_value =
            mlir::isa<ValueType>(first) || mlir::isa<ClosureType>(first) ||
            mlir::isa<CoroutineType>(first);
        if (helper.ret == return_role::value && !is_js_value) {
            return op->emitOpError() << helper.symbol
                                     << " returns a JavaScript value, but the result is not "
                                        "!ctjs.value";
        }
        if (helper.ret != return_role::value && is_js_value) {
            return op->emitOpError()
                   << helper.symbol
                   << " does not return a JavaScript value - declaring !ctjs.value here would "
                      "reinterpret its bits as one";
        }
    }

    // THE IMMEDIATES, WHICH IS THE RULE THAT CATCHES load_upvalue - AS AN
    // UPPER BOUND, for the reason the operand rule was dropped.
    //
    // An attribute on a runtime operation exists to supply a parameter with no
    // SSA source: an opcode, a count, a literal's bytes. Carrying FEWER than
    // the helper has is normal - ctjs.load_global's one $name attribute stands
    // for ct_aot_global_get's `const char *name` AND its `uint32_t name_len`,
    // because a string knows its own length, and ctjs.create_array supplies
    // ct_aot_new_array's count from its variadic element list instead.
    //
    // Carrying MORE is not normal. It means the operation is spelling
    // something the call it lowers to has nowhere to put, and the lowering
    // will drop it in silence - which is precisely what ctjs.load_upvalue's
    // $index did against ct_aot_cell_get's single `uint64_t cell`.
    //
    // INHERENT ATTRIBUTES ONLY. The dialect sets usePropertiesForAttributes,
    // so an operation's own attributes live in its properties and
    // getAttrDictionary() returns the DISCARDABLE ones - which anybody may
    // attach and which say nothing about the ABI.
    std::size_t inherent = 0;
    if (const mlir::Attribute properties = op->getPropertiesAsAttribute()) {
        if (const auto dictionary = mlir::dyn_cast<mlir::DictionaryAttr>(properties)) {
            for (const mlir::NamedAttribute & entry : dictionary) {
                // The segment sizes are ODS bookkeeping for variadic operand
                // and result lists, not an ABI argument.
                const llvm::StringRef name = entry.getName().strref();
                if (name == "operandSegmentSizes" || name == "resultSegmentSizes") { continue; }
                ++inherent;
            }
        }
    }
    if (inherent > census.immediate) {
        return op->emitOpError()
               << "carries " << inherent << " attribute(s), but " << helper.symbol << " has only "
               << census.immediate
               << " parameter(s) an attribute could supply - an attribute with no parameter to "
                  "reach is dropped by the lowering in silence";
    }

    return mlir::success();
}

} // namespace ctcompile::ctjs
