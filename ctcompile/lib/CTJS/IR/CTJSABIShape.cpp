//===- ctcompile/lib/CTJS/IR/CTJSABIShape.cpp - Op/helper ABI shape check -===//
//
// Checks that a runtime operation's declaration matches the shape of the AOT
// helper it names.
//
// CTJS_RuntimeOp already stops an operation from claiming a helper the runtime
// does not declare: the helper is concatenated into a reference to a
// ctbrowser::aot::helper_id enumerator, so a wrong name stops the C++ build.
// That checks the helper's identity, not its shape. Shape mismatches this
// verifier catches, each of which looks correct in both the ODS file and the
// helper ABI tables (ctbrowser/include/ctbrowser/aot/helpers/*.def):
//   - an attribute the helper has no parameter for, which the lowering silently
//     drops (ctjs.load_upvalue's $index against ct_aot_cell_get's single cell
//     parameter would read whichever cell the closure value happened to be);
//   - a !ctjs.value result declared against a helper that returns a uint32_t
//     0/1 (instanceof, whose 1 would become a subnormal double) or a status
//     code (delete_property, which would evaluate to the status).
// None is a name error or a type error inside the dialect, so the IR verifies,
// prints plausibly and is wrong.
//
// The check is derived from the helper's parameter roles and runs as a trait
// on every CTJS_RuntimeOp, rather than as one verifier per operation that each
// new operation could forget. Nothing here names an operation.
//
//===----------------------------------------------------------------------===//
#include "ctcompile/CTJS/IR/CTJSTraits.h"

#include "ctcompile/CTJS/IR/CTJSInterfaces.h"
#include "ctcompile/CTJS/IR/CTJSTypes.h"
#include "ctcompile/CTJS/Lowering/RuntimeHelpers.hpp"

#include "mlir/IR/BuiltinAttributes.h"

namespace ctcompile::ctjs {

namespace {

/// How a helper's parameters divide into the three things a caller must find.
struct role_census {
    /// Supplied by an SSA value: the frame, the operands, an argv span.
    std::size_t ssa = 0;
    /// Written through by the callee: these are results, never operands.
    std::size_t out = 0;
    /// Immediates with no SSA source (an opcode, a count, a literal's bytes).
    /// These are what an operation carries as an attribute.
    std::size_t immediate = 0;
    /// Whether the first parameter is the frame handle, which the lowering may
    /// supply itself rather than the operation carrying it.
    bool leads_with_frame = false;
};

/// Counts `helper`'s parameters by role.
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
    census.leads_with_frame = helper.role_count > 0 && (helper.roles[0] == param_role::frame ||
                                                        helper.roles[0] == param_role::context);
    return census;
}

/// Whether the helper's return value is one of the operation's results.
///
/// A returned status is not a result: a ct_aot_status return is the exception
/// edge, not the operation's value, and a void return carries nothing. Any
/// other return type is a result. Many helpers have no out-parameter at all,
/// so counting only out-parameters would lose their result.
bool return_carries_a_result(const runtime_helper & helper) {
    return helper.ret != return_role::nothing && helper.ret != return_role::status;
}

} // namespace

mlir::LogicalResult verifyABIShape(mlir::Operation * op) {
    auto call = mlir::dyn_cast<RuntimeCallOpInterface>(op);
    if (!call) { return mlir::success(); }
    const runtime_helper & helper = helper_for(call.getHelperID());
    const role_census census = census_of(helper);

    // There is deliberately no operand-count rule. The dialect is higher level
    // than the ABI: ctjs.get_property carries an object and a key but not the
    // inline cache the lowering attaches; ctjs.call carries a callee and a
    // receiver but not the argv span, argc, key or baked site; ctjs.frame_enter
    // carries nothing. The lowering supplies the rest, so an operation normally
    // has fewer operands than the helper has parameters. Only an excess is
    // checkable, and the attribute rule below checks it.

    // The results: out-parameters, plus the return value unless it is a
    // status or void.
    const std::size_t expected = census.out + (return_carries_a_result(helper) ? 1 : 0);
    if (op->getNumResults() != expected) {
        return op->emitOpError() << "declares " << op->getNumResults() << " result(s), but "
                                 << helper.symbol << " answers with " << expected << " ("
                                 << census.out << " out-parameter(s) and a "
                                 << (return_carries_a_result(helper) ? "value" : "status or void")
                                 << " return)";
    }

    // The type of the return value's result, a separate question from how many
    // results there are. ct_aot_instance_of returns a uint32_t 0 or 1;
    // declaring that as a !ctjs.value makes `x instanceof C` evaluate to
    // value::from_bits(1), a subnormal double, and every later pass would be
    // right to believe the type.
    if (return_carries_a_result(helper) && op->getNumResults() > 0) {
        const mlir::Type first = op->getResult(0).getType();
        // A closure and a coroutine are JavaScript values at run time; their
        // distinct types (CTJSTypes.td) only mark the place that builds one.
        // They refine !ctjs.value, so ct_aot_make_closure's uint64_t may be
        // declared as !ctjs.closure. !ctjs.program is not a value: it is the
        // importer's input, a compiled script::program, and no helper returns
        // one.
        const bool is_js_value = mlir::isa<ValueType, ClosureType, CoroutineType>(first);
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

    // The immediates, checked as an upper bound for the same reason there is no
    // operand rule. An attribute on a runtime operation supplies a parameter
    // with no SSA source: an opcode, a count, a literal's bytes. Carrying fewer
    // than the helper has is normal: ctjs.load_global's one $name attribute
    // stands for ct_aot_global_get's `const char *name` and its `uint32_t
    // name_len`, and ctjs.create_array supplies ct_aot_new_array's count from
    // its variadic element list. Carrying more means the operation spells
    // something the helper has nowhere to put, and the lowering would drop it
    // silently (ctjs.load_upvalue's $index against ct_aot_cell_get's single
    // `uint64_t cell`).
    //
    // Inherent attributes only. The dialect sets usePropertiesForAttributes, so
    // an operation's own attributes live in its properties; getAttrDictionary()
    // returns the discardable ones, which anybody may attach and which say
    // nothing about the ABI.
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
