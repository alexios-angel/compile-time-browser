//===- Constants.cpp - Lower ctjs.constant --------------------------------===//
//
// Spells ctjs constants and boxed machine values as C++ expressions over
// ctbrowser::script::value, for the VM-backed EmitC backend.
//
//===----------------------------------------------------------------------===//
#include "Lowering.h"

namespace ctcompile::ctjs::emitc_detail {

/// Boxes a machine quantity (a bool or a double) as a JavaScript value.
///
/// This goes through one of the shims the module's prelude defines, because no
/// runtime helper boxes a bool or a double, and `value::boolean(b).bits()` is
/// a member call on a temporary, which emitc.call_opaque (whose whole output
/// is `callee(args)`) cannot spell.
mlir::Value lowering::box(mlir::OpBuilder & build, mlir::Location where, mlir::Type value,
                          shim which, mlir::Value machine) {
    // Only the shims a module actually uses are emitted. An unused function
    // in the translation unit is dead code the host compiler warns about,
    // and this project builds with -Werror.
    const auto index = static_cast<std::size_t>(which);
    used_shims.set(index);
    return ec::CallOpaqueOp::create(build, where, mlir::TypeRange{value}, shim_rows[index].name,
                                    mlir::ValueRange{machine})
        .getResult(0);
}

/// A JavaScript `undefined`, spelled the way the runtime spells it.
mlir::Value lowering::undefined(mlir::OpBuilder & build, mlir::Location where, mlir::Type value) {
    return literal(build, where, value, "ctbrowser::script::value::undefined().bits()");
}

/// A constant, spelled the way the runtime spells it.
///
/// Through value::bits() rather than a bit pattern written here. The encoding
/// is script::value's, a NaN-boxing scheme it is free to change, and a backend
/// that baked the pattern would keep compiling and silently produce garbage
/// the day it did.
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
        // Emit the bits, not a decimal literal. The attribute carries the
        // double's bit pattern (`bit_cast<uint64_t>(double)`), because -0.0
        // and 0.0 print the same and no decimal spelling keeps a NaN payload.
        // bit_cast restores the exact double and value::number boxes it, so
        // nothing here depends on the NaN-boxing scheme.
        return literal(build, where, value,
                       "ctbrowser::script::value::number(std::bit_cast<double>(UINT64_C(" +
                           std::to_string(number.getBits()) + "))).bits()");
    }
    // body_is_supported admits nothing else.
    return undefined(build, where, value);
}

} // namespace ctcompile::ctjs::emitc_detail
