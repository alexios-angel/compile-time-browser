//===- ctbrowser/lib/Script/compile/helpers.cpp - Emit helpers ------------===//
//
// compiler_impl helpers: emitting a constant or a string, and patching a
// jump once its target is known. The class is declared in compiler_impl.hpp.
//
//===----------------------------------------------------------------------===//

#include "compiler_impl.hpp"

namespace ctbrowser::script::detail {

void compiler_impl::emit_string(std::uint16_t dst, std::string text) {
    proto().emit(instruction::with_bx(op::load_string, dst, intern_string(std::move(text))));
}

void compiler_impl::emit_const(std::uint16_t dst, value v) {
    proto().emit(instruction::with_bx(op::load_const, dst, proto().add_constant(v)));
}

void compiler_impl::patch_here(std::size_t at_index) {
    patch_jump(at_index, proto().code.size());
}

void compiler_impl::patch_jump(std::size_t at_index, std::size_t target) {
    const auto offset = static_cast<std::int32_t>(target) - static_cast<std::int32_t>(at_index) - 1;
    if (offset > jump_limit || offset < -jump_limit - 1) {
        fail(frame_name(fn().proto) + " needs a jump of " + std::to_string(offset) +
             " instructions; the displacement holds " + std::to_string(jump_limit) +
             ". Past that it branches to the WRONG address.");
    }
    instruction & jump = proto().code[at_index];
    // Must match instruction::with_bx: sbx() reads b (high 16 bits) and c
    // (low 16 bits) back as one 32-bit offset. A mismatch silently sends every
    // branch to the wrong address.
    const auto wide = static_cast<std::uint32_t>(offset);
    jump.b = static_cast<std::uint16_t>(wide >> 16);
    jump.c = static_cast<std::uint16_t>(wide & 0xFFFF);
}

} // namespace ctbrowser::script::detail
