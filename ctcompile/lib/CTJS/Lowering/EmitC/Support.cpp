#include "Lowering.h"
#include "ctcompile/Support/CppLiterals.hpp"

namespace ctcompile::ctjs::emitc_detail {
// THE C++ SPELLINGS, IN ONE PLACE.
//
// A JavaScript value is `uint64_t` HERE and nothing else. It is not
// ctbrowser::script::value - that class keeps its bits private and offers no
// conversion, so `value *` where the ABI wants `uint64_t *out` is a compile
// error - and it is not i64, which this emitter prints as `int64_t` and which
// fails the same way against `uint64_t *`.
ec::OpaqueType opaque(mlir::MLIRContext * context, llvm::StringRef spelling) {
    return ec::OpaqueType::get(context, spelling);
}

ec::PointerType pointer_to(mlir::MLIRContext * context, llvm::StringRef spelling) {
    return ec::PointerType::get(opaque(context, spelling));
}

// AN OPCODE'S C++ ENUMERATOR NAME.
//
// `uint32_t op_kind` in the ABI is a ctbrowser::script::op and aot_bridge/operators.cpp
// casts it back, so the emitted call must name the OPERATOR. Spelling it as a
// number would survive the renumbering Phases 13 and 14 do deliberately and
// silently mean something else; spelling it as an enumerator makes that a build
// error in the generated translation unit.
std::string opcode_spelling(ctbrowser::script::op which) {
    return "static_cast<uint32_t>(ctbrowser::script::op::" + std::string(opcode_name(which)) + ")";
}

// EVERY HELPER IS CALLED BY ITS QUALIFIED C++ NAME.
//
// aot.hpp puts the extern "C" prototypes INSIDE namespace ctbrowser::aot, so
// they have C linkage and an unmangled linker symbol while their C++ name is
// qualified. An unqualified call compiles only by argument-dependent lookup off
// a ct_aot_frame * argument, which the frameless rows do not have. The helper
// table's `symbol` is the LINKER name and is not this string.
std::string callee(llvm::StringRef helper) {
    return ("ctbrowser::aot::" + helper).str();
}

// A C++ EXPRESSION WITH NO SSA SOURCE - an enumerator, a literal, nullptr.
mlir::Value literal(mlir::OpBuilder & build, mlir::Location where, mlir::Type type,
                    llvm::StringRef text) {
    return ec::LiteralOp::create(build, where, type, text);
}

// A NAME AS BYTES AND A LENGTH, not a NUL-terminated string: a name is bytes,
// and one containing a zero byte is legal JavaScript that strlen stops at.
std::pair<mlir::Value, mlir::Value> bytes_and_length(mlir::OpBuilder & build, mlir::Location where,
                                                     llvm::StringRef text) {
    const mlir::Value bytes = literal(build, where, pointer_to(build.getContext(), "const char"),
                                      cpp::c_string_literal(text));
    return {bytes, literal(build, where, opaque(build.getContext(), "uint32_t"),
                           std::to_string(text.size()))};
}

// THE INTERNED NAME RECORD a helper taking `const ct_aot_name *` wants.
mlir::Value intern(mlir::OpBuilder & build, mlir::Location where, llvm::StringRef name) {
    const auto [bytes, length] = bytes_and_length(build, where, name);
    return ec::CallOpaqueOp::create(
               build, where,
               mlir::TypeRange{pointer_to(build.getContext(), "const ctbrowser::aot::ct_aot_name")},
               callee("ct_aot_intern_name"), mlir::ValueRange{bytes, length})
        .getResult(0);
}

// A uint32_t 0-or-1 answer as an i1. The ABI has no bool; testing against
// zero IS the conversion.
mlir::Value nonzero(mlir::OpBuilder & build, mlir::Location where, mlir::Value u32) {
    return ec::CmpOp::create(build, where, mlir::IntegerType::get(build.getContext(), 1),
                             ec::CmpPredicate::ne, u32,
                             literal(build, where, opaque(build.getContext(), "uint32_t"), "0"))
        .getResult();
}

// THE SYMBOL, AS C++ ACTUALLY SPELLS IDENTIFIERS.
//
// The importer suffixes every function with `$index` because a real program has
// many functions sharing a name - p5.js has dozens called `constructor` - and
// without it the module fails to verify on a duplicate symbol. `$` is not in
// C++'s basic character set: GCC and Clang accept it as an extension, so the
// emitted unit compiles here today and would stop compiling the first time
// anything is built with a compiler that does not.
//
// THE SUFFIX STILL SEPARATES, so replacing it loses nothing. Every name the
// importer produces is `<sanitised>$<index>` where the sanitised part already
// has no `$` and the index is decimal, so mapping `$` to `_` keeps distinct
// names distinct: `a$1` becomes `a_1` and `a_1$2` becomes `a_1_2`.
std::string c_identifier(llvm::StringRef symbol) {
    std::string spelled = symbol.str();
    std::ranges::replace(spelled, '$', '_');
    return spelled;
}

} // namespace ctcompile::ctjs::emitc_detail
