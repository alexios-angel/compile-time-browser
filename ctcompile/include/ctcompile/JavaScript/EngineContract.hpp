//===- EngineContract.hpp - Engine layout ctcompile relies on ---*- C++ -*-===//
//
// What ctcompile assumes about the engine's bytecode and value layout, written
// as static_asserts so that a layout change in ctbrowser breaks the ctcompile
// build instead of silently drifting from a prose description.
//
// They live in ctcompile, not ctbrowser, because they are the compiler's
// assumptions about the runtime, not the runtime's own invariants; a
// runtime-only build must not carry them. The runtime declares, the compiler
// reads. The program-image serializer relies on every fact asserted here.
//
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <cstdint>
#include <type_traits>

#include <ctbrowser/script/bytecode.hpp>
#include <ctbrowser/script/value.hpp>

namespace ctcompile::engine_contract {

using ctbrowser::script::function_proto;
using ctbrowser::script::instruction;
using ctbrowser::script::op;
using ctbrowser::script::program;
using ctbrowser::script::upvalue_desc;
using value_t = ctbrowser::script::value;

// --- The instruction -----------------------------------------------------
// Eight bytes, and flat. This is what lets a compiled image hold the code
// array as bytes rather than as a decoded structure.
static_assert(sizeof(instruction) == 8, "an instruction is one 64-bit word");
static_assert(std::is_trivially_copyable_v<instruction>,
              "an instruction can be copied with memcpy");

// But it must not be serialized with memcpy. `instruction` is {op at 0,
// padding at 1, a at 2, b at 4, c at 6}: eight bytes, but alignof 2 and a hole.
// Measured on this toolchain, `instruction::with_bx(op::jump, 7, 0x12345678)`
// puts 0x02 in that hole at -O0 and 0x00 at -O2: the same instruction,
// different bytes, decided by how the code that built it was compiled.
//
// So a serializer that memcpy's a code array produces a different file for the
// same program depending on the build that wrote it, while every correctness
// test passes: the padding is not read by anything, so nothing is wrong except
// that two identical programs no longer have identical images. The image
// serializer therefore writes op, a, b and c as four explicit little-endian
// fields, and this assert refuses the memcpy shortcut at compile time rather
// than leaving it to be discovered when two builds disagree about a hash.
//
// The fix is in the writer, not the struct. Adding an explicit pad member
// would rebind ~215 positional aggregate initialisations across the compiler,
// and the ones whose second argument is small enough for a uint8 would compile
// silently and mean something else.
static_assert(!std::has_unique_object_representations_v<instruction>,
              "instruction has a padding byte at offset 1, so its object representation is NOT "
              "canonical - if this ever becomes true the layout changed and Phase 15's "
              "field-by-field writer can be reconsidered");
static_assert(std::is_same_v<std::underlying_type_t<op>, std::uint8_t>,
              "the opcode is one byte, which is what leaves three uint16 operands in eight");

// A register machine: a, b and c are frame slots or indices, and b+c can be
// read as one 32-bit field. Pinned by evaluation rather than by description,
// because this is the encoding the bytecode importer decodes.
static_assert(instruction{op::load_const, 0, 0x1234, 0x5678}.bx() == 0x12345678u,
              "bx() is b in the high half and c in the low half");
static_assert(instruction::with_bx(op::load_const, 7, 0x12345678u).b == 0x1234 &&
                  instruction::with_bx(op::load_const, 7, 0x12345678u).c == 0x5678,
              "with_bx() is the inverse of bx()");
static_assert(instruction{op::jump, 0, 0xFFFF, 0xFFFF}.sbx() == -1,
              "sbx() is the signed reading of the same field, which is how a backward jump "
              "is encoded");

// --- The value -----------------------------------------------------------
// NaN-boxed into one word. Generated C++ can name ctbrowser::script::value
// directly and let the host compiler resolve it. Any lowering that maps
// !ctjs.value to i64 must take the tag constants from value.hpp rather than
// restating them; a copy that drifts is a miscompile.
static_assert(sizeof(value_t) == 8, "a JS value is one machine word");
static_assert(std::is_trivially_copyable_v<value_t>, "a constant pool is copied as bytes");

// --- What is and is not flat ---------------------------------------------
// `constants` holds immediates only. A string literal cannot live there: a
// `value` for a string is a pointer into a VM heap that does not exist at
// compile time, so the text is kept in `strings` and the VM materializes it.
// That is why a compiled program is independent of any one VM instance, and
// it is the most important fact for the image serializer: the constant pool
// serializes as bytes, the string tables do not.
static_assert(std::is_trivially_copyable_v<upvalue_desc>, "the upvalue table serializes as bytes");
static_assert(!std::is_trivially_copyable_v<function_proto>,
              "a proto owns strings and vectors - Phase 15 must WALK it, not copy it. If "
              "this ever becomes trivially copyable the serializer can be simplified, and "
              "this assert is where that is noticed");
static_assert(!std::is_trivially_copyable_v<program>, "the same, one level up");

} // namespace ctcompile::engine_contract
