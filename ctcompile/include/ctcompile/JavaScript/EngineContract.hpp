#pragma once

#include <cstddef>
#include <cstdint>
#include <type_traits>

#include <ctbrowser/script/bytecode.hpp>
#include <ctbrowser/script/value.hpp>

// WHAT ctcompile ASSUMES ABOUT THE ENGINE, as build errors.
//
// Phase 0's Program Representation Inventory. The inventory is not a document -
// a prose description of a layout drifts from the layout within weeks and
// nothing can check it - so it is a header of `static_assert`s that the
// compiler includes. When the engine changes one of these facts, ctcompile
// STOPS BUILDING, which is the earliest and cheapest place to find out.
//
// It lives in ctcompile rather than in ctbrowser on purpose. These are the
// COMPILER's assumptions about the runtime, not the runtime's own invariants,
// and a runtime-only build must not carry them (Principle 2). The direction of
// ownership is the same one Principle 11 states for the AOT ABI: the runtime
// declares, the compiler reads.
//
// Phase 15 serializes exactly these structures. Every assert here is a fact
// that serializer will depend on.

namespace ctcompile::engine_contract {

using ctbrowser::script::function_proto;
using ctbrowser::script::instruction;
using ctbrowser::script::op;
using ctbrowser::script::program;
using ctbrowser::script::upvalue_desc;
using value_t = ctbrowser::script::value;

// --- THE INSTRUCTION -----------------------------------------------------
// Eight bytes, and flat. This is what lets a compiled image hold the code
// array as bytes rather than as a decoded structure.
static_assert(sizeof(instruction) == 8, "an instruction is one 64-bit word");
static_assert(std::is_trivially_copyable_v<instruction>,
              "Phase 15 copies code arrays as bytes; a non-trivial instruction would need "
              "a per-element serializer");
static_assert(std::is_same_v<std::underlying_type_t<op>, std::uint8_t>,
              "the opcode is one byte, which is what leaves three uint16 operands in eight");

// A REGISTER MACHINE: a, b and c are frame slots or indices, and b+c can be
// read as one 32-bit field. Pinned by evaluation rather than by description,
// because this is the encoding the importer in Phase 9 decodes.
static_assert(instruction{op::load_const, 0, 0x1234, 0x5678}.bx() == 0x12345678u,
              "bx() is b in the high half and c in the low half");
static_assert(instruction::with_bx(op::load_const, 7, 0x12345678u).b == 0x1234 &&
                  instruction::with_bx(op::load_const, 7, 0x12345678u).c == 0x5678,
              "with_bx() is the inverse of bx()");
static_assert(instruction{op::jump, 0, 0xFFFF, 0xFFFF}.sbx() == -1,
              "sbx() is the signed reading of the same field, which is how a backward jump "
              "is encoded");

// --- THE VALUE -----------------------------------------------------------
// NaN-boxed into one word. Phase 10A can name `ctbrowser::script::value`
// directly in generated C++ and let the host compiler resolve it; Phase 11's
// type converter maps !ctjs.value to i64 and MUST take the constants from
// value.hpp rather than restating them - a second copy that drifts is a
// miscompile.
static_assert(sizeof(value_t) == 8, "a JS value is one machine word");
static_assert(std::is_trivially_copyable_v<value_t>, "a constant pool is copied as bytes");

// --- WHAT IS AND IS NOT FLAT ---------------------------------------------
// `constants` holds IMMEDIATES ONLY. A string literal cannot live there: a
// `value` for a string is a pointer into a VM heap that does not exist at
// compile time, so the TEXT is kept in `strings` and the VM materializes it.
// That is why a compiled program is independent of any one VM instance, and it
// is the single most important fact for Phase 15: the constant pool
// serializes as bytes, the string tables do not.
static_assert(std::is_trivially_copyable_v<upvalue_desc>, "the upvalue table serializes as bytes");
static_assert(!std::is_trivially_copyable_v<function_proto>,
              "a proto owns strings and vectors - Phase 15 must WALK it, not copy it. If "
              "this ever becomes trivially copyable the serializer can be simplified, and "
              "this assert is where that is noticed");
static_assert(!std::is_trivially_copyable_v<program>, "the same, one level up");

// --- THE POD HEADER OF A PROTO -------------------------------------------
// The fields a serialized proto can write as a fixed record, before the
// variable-length tables. Named here so Phase 15's record and this list cannot
// drift silently: adding a field to function_proto without adding it here
// leaves it unserialized, and nothing else would say so.
struct proto_header {
    std::uint16_t param_count;
    std::uint16_t frame_size;
    bool is_arrow;
    bool is_generator;
    std::uint32_t source_begin;
    std::uint32_t source_end;
};
static_assert(std::is_trivially_copyable_v<proto_header>);

// The entry point of a program is functions[0] - the top-level script. Stated
// as a named constant because Phase 15's image format needs an entry index and
// hard-coding 0 in three places is how they drift.
inline constexpr std::size_t entry_function_index = 0;

} // namespace ctcompile::engine_contract
