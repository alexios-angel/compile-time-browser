// Boxed EmitC constants lowering.
#include "Lowering.h"

namespace ctcompile::ctjs::emitc_detail {

// A MACHINE QUANTITY, MADE INTO A JAVASCRIPT VALUE.
//
// Through one of the shims the module's prelude defines, because the ABI
// has no row that boxes a bool or a double and `value::boolean(b).bits()`
// is a member call on a temporary - which emitc.call_opaque, whose whole
// output is `callee(args)`, cannot spell.
mlir::Value lowering::box(mlir::OpBuilder & build, mlir::Location where, mlir::Type value,
                          llvm::StringRef shim, mlir::Value machine) {
    // ONLY THE SHIMS A MODULE ACTUALLY USES ARE EMITTED. A translation unit
    // carrying a function nothing calls is dead code the host compiler will
    // warn about - and this project builds with -Werror, so emitting all of
    // them unconditionally would make the generated output unbuildable
    // under the same flags as everything else.
    if (shim == "ctc_box_bool") { boxes_bool = true; }
    if (shim == "ctc_box_number") { boxes_number = true; }
    return ec::CallOpaqueOp::create(build, where, mlir::TypeRange{value}, shim,
                                    mlir::ValueRange{machine})
        .getResult(0);
}

// A JavaScript `undefined`, spelled the way the runtime spells it.
mlir::Value lowering::undefined(mlir::OpBuilder & build, mlir::Location where, mlir::Type value) {
    return literal(build, where, value, "ctbrowser::script::value::undefined().bits()");
}

// A CONSTANT, SPELLED THE WAY THE RUNTIME SPELLS IT.
//
// Through value::bits() rather than a bit pattern written here. The
// encoding is script::value's - a NaN-boxing scheme it is free to change -
// and a backend that baked the pattern would keep compiling and start
// producing garbage the day it did.
mlir::Value lowering::constant_value(mlir::OpBuilder & build, mlir::Location where,
                                     mlir::Type value, ConstantOp constant) {
    const mlir::Attribute what = constant.getValue();
    if (mlir::isa<NullAttr>(what)) {
        return literal(build, where, value, "ctbrowser::script::value::null().bits()");
    }
    if (auto boolean = mlir::dyn_cast<BooleanAttr>(what)) {
        return literal(build, where, value,
                       boolean.getValue() ? "ctbrowser::script::value::boolean(true).bits()"
                                          : "ctbrowser::script::value::boolean(false).bits()");
    }
    if (auto number = mlir::dyn_cast<NumberAttr>(what)) {
        // THE BITS, NOT A DECIMAL LITERAL, and that is what makes it exact.
        //
        // The attribute carries `bit_cast<uint64_t>(double)` - the double's
        // pattern, not the value's - because "-0.0 and NaN are the reason
        // for APFloat and for a dedicated attribute". Printing it back as
        // decimal would reintroduce exactly what the attribute exists to
        // avoid: 0.0 and -0.0 print identically and are different
        // JavaScript values, and no decimal spelling round-trips a NaN
        // payload. bit_cast reconstructs the double exactly, and
        // value::number boxes it however script::value chooses to - so
        // nothing here depends on the NaN-boxing scheme either.
        return literal(build, where, value,
                       "ctbrowser::script::value::number(std::bit_cast<double>(UINT64_C(" +
                           std::to_string(number.getBits()) + "))).bits()");
    }
    // body_is_supported admits nothing else.
    return undefined(build, where, value);
}

} // namespace ctcompile::ctjs::emitc_detail
