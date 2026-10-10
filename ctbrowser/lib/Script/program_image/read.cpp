//===- program_image/read.cpp - Program image reader ------------*- C++ -*-===//
//
// The program image reader: the bounds-checked byte source, the operand
// shapes every instruction is validated against, the fixed prefix, and
// read_image_header / load_image. An image is untrusted input, so everything
// the VM later indexes without a check is validated here. The format's
// constants and the checks shared with the writer (write.cpp) live in
// internal.hpp so the two cannot drift.
//
//===----------------------------------------------------------------------===//

#include "internal.hpp"

namespace ctbrowser::script {

using detail::format_version;
using detail::magic;
using detail::why_not_a_constant;

namespace {

/// A cursor over the image bytes. Every read is bounds-checked and the first
/// failure sets `bad` and `why`; later reads return zero. A reader that
/// returned garbage past the end would turn a truncated file into a program.
struct source_bytes {
    std::span<const std::byte> bytes;
    std::size_t at = 0;
    bool bad = false;
    std::string why;

    void fail(std::string reason) {
        if (!bad) {
            bad = true;
            why = std::move(reason);
        }
    }
    [[nodiscard]] bool need(std::size_t n) {
        if (bad) { return false; }
        if (at + n > bytes.size()) {
            fail("the image ends in the middle of a value");
            return false;
        }
        return true;
    }
    std::uint8_t u8() {
        if (!need(1)) { return 0; }
        return static_cast<std::uint8_t>(bytes[at++]);
    }
    /// Reads one little-endian T with a single bounds check (a check per byte
    /// measured 13% of the load of a 7 MB image). Assembled by shifts rather
    /// than a cast, so the result does not depend on the host's byte order.
    template <typename T> T little() {
        if (!need(sizeof(T))) { return 0; }
        T v = 0;
        for (std::size_t i = 0; i < sizeof(T); ++i) {
            v |= static_cast<T>(static_cast<std::uint8_t>(bytes[at + i])) << (8 * i);
        }
        at += sizeof(T);
        return v;
    }
    std::uint16_t u16() { return little<std::uint16_t>(); }
    std::uint32_t u32() { return little<std::uint32_t>(); }
    std::uint64_t u64() { return little<std::uint64_t>(); }
    std::string text() {
        const std::uint32_t n = u32();
        // A length prefix is the classic way to ask a reader to allocate four
        // gigabytes: check it against what is actually left before reserving.
        if (!need(n)) { return {}; }
        std::string out(reinterpret_cast<const char *>(bytes.data() + at), n);
        at += n;
        return out;
    }
};

/// Operand kinds, taken straight from the columns of bytecode_opcodes.def, so
/// validation cannot disagree with the opcode table about which pool an
/// operand indexes: reg (register), kidx (constant), sidx (string), nidx
/// (name), fidx (function), jump (relative target), count, bx_hi (high half
/// of a wide b operand) and unused.
enum class slot_kind : std::uint8_t {
    reg,
    kidx,
    sidx,
    nidx,
    fidx,
    jump,
    count,
    bx_hi,
    unused
};

[[nodiscard]] constexpr slot_kind kind_of(std::string_view text) {
    if (text == "reg") { return slot_kind::reg; }
    if (text == "kidx") { return slot_kind::kidx; }
    if (text == "sidx") { return slot_kind::sidx; }
    if (text == "nidx") { return slot_kind::nidx; }
    if (text == "fidx") { return slot_kind::fidx; }
    if (text == "jump") { return slot_kind::jump; }
    if (text == "count") { return slot_kind::count; }
    if (text == "bx_hi") { return slot_kind::bx_hi; }
    return slot_kind::unused;
}

struct opcode_shape {
    slot_kind a, b, c;
};

#define CT_OPCODE(name_, a_kind_, b_kind_, c_kind_, writes_a_, allocates_, may_throw_,             \
                  may_reenter_, is_safepoint_, may_suspend_, resumable_, impl_)                    \
    opcode_shape{kind_of(#a_kind_), kind_of(#b_kind_), kind_of(#c_kind_)},
constexpr opcode_shape shapes[] = {
#include <ctbrowser/script/bytecode_opcodes.def>
};
static_assert(std::size(shapes) == opcode_count, "one shape per opcode");

/// The five opcodes that place a callee's frame at the caller's argument
/// window, where `b` is an argument count rather than an index. `a + 1 + b`
/// must fit the caller's frame or the argument gather reads past it.
[[nodiscard]] constexpr bool is_call_shape(op code) {
    return code == op::call || code == op::call_method || code == op::call_computed ||
           code == op::construct || code == op::call_receiver;
}

/// The fixed prefix: magic, format version, engine fingerprint, image
/// option, script kind and source hash. Read in one place (read_prefix)
/// because `load_image` and `read_image_header` both need it.
struct prefix {
    image_option option = image_option::keep_source;
    script_kind kind = script_kind::classic;
    std::uint64_t source_hash = 0;
};

/// Reads the fixed prefix into \p out. Returns an empty string when the
/// prefix is one this build accepts, and otherwise the reason it is not.
[[nodiscard]] std::string read_prefix(source_bytes & in, prefix & out) {
    if (in.u32() != magic) { return "not a ctbrowser program image"; }
    if (const std::uint32_t v = in.u32(); v != format_version) {
        return "image format version " + std::to_string(v) + ", this build reads " +
               std::to_string(format_version);
    }
    if (const std::uint64_t got = in.u64(); got != image_fingerprint()) {
        // The most important refusal in the file. An image whose opcodes were
        // numbered differently would otherwise load and run at full speed,
        // executing different instructions than the ones that were compiled.
        (void)got;
        return "image was written by a different engine build - opcode numbering, value "
               "layout, instruction layout, or what the compiler EMITS for a given source has "
               "changed since it was written";
    }
    const std::uint32_t option = in.u32();
    if (option > static_cast<std::uint32_t>(image_option::drop_source)) {
        return "unknown image option " + std::to_string(option);
    }
    out.option = static_cast<image_option>(option);
    const std::uint8_t kind_byte = in.u8();
    if (kind_byte > static_cast<std::uint8_t>(script_kind::module_)) {
        return "unknown script kind " + std::to_string(kind_byte);
    }
    out.kind = static_cast<script_kind>(kind_byte);
    out.source_hash = in.u64();
    if (in.bad) { return in.why; }
    return {};
}

} // namespace

std::optional<image_header> read_image_header(std::span<const std::byte> bytes) {
    source_bytes in{bytes, 0, false, {}};
    prefix got;
    if (!read_prefix(in, got).empty()) { return std::nullopt; }
    return image_header{got.source_hash, got.kind, got.option};
}

load_result load_image(std::span<const std::byte> bytes,
                       std::optional<std::uint64_t> expect_source_hash, script_kind expect_kind) {
    load_result out;
    source_bytes in{bytes, 0, false, {}};

    prefix head;
    if (std::string why = read_prefix(in, head); !why.empty()) {
        out.error = std::move(why);
        return out;
    }
    out.kind = head.kind;
    if (out.kind != expect_kind) {
        // Checked before the source hash because it is the more specific
        // answer: the text is what the caller asked for, but it was compiled
        // differently. "Built from different source" would send a reader
        // looking for an edit that never happened.
        out.error = out.kind == script_kind::module_
                        ? "the image is a module, and a classic script was asked for"
                        : "the image is a classic script, and a module was asked for";
        return out;
    }
    out.source_hash = head.source_hash;
    if (expect_source_hash && *expect_source_hash != out.source_hash) {
        // The refusal that makes a cache safe. A stale image is not a slow
        // path: it is different JavaScript running at full speed, with the
        // page behaving as it did before an edit nobody can see.
        out.error = "the image was built from different source";
        return out;
    }

    const auto read_pool = [&in](const char * what) {
        std::vector<std::string> pool;
        const std::uint32_t n = in.u32();
        if (!in.need(n)) { // A count larger than the file cannot be honest.
            in.fail(std::string{"the "} + what + " pool claims more entries than the image holds");
            return pool;
        }
        pool.reserve(n);
        for (std::uint32_t i = 0; i < n && !in.bad; ++i) { pool.push_back(in.text()); }
        return pool;
    };
    const std::vector<std::string> names = read_pool("name");
    const std::vector<std::string> strings = read_pool("string");

    program result;
    const auto read_list = [&in](std::vector<std::string> & into, const char * what) {
        const std::uint32_t n = in.u32();
        if (!in.need(static_cast<std::size_t>(n) * 4u)) { // Each entry has a 4-byte length.
            in.fail(std::string{"the "} + what + " list claims more entries than the image holds");
            return;
        }
        into.reserve(n);
        for (std::uint32_t i = 0; i < n && !in.bad; ++i) { into.push_back(in.text()); }
    };
    read_list(result.imports, "import");
    read_list(result.exports, "export");
    {
        const std::uint32_t n = in.u32();
        // At least 12 bytes each: a re-export is three length-prefixed
        // strings. The struct is 96 bytes, so without this check a corrupt
        // count would reserve gigabytes.
        if (in.need(static_cast<std::size_t>(n) * 12u)) {
            result.reexports.reserve(n);
            for (std::uint32_t i = 0; i < n && !in.bad; ++i) {
                program::reexport r;
                r.exported = in.text();
                r.source = in.text();
                r.from = in.text();
                result.reexports.push_back(std::move(r));
            }
        } else {
            in.fail("the re-export list claims more entries than the image holds");
        }
    }
    read_list(result.hoisted_vars, "hoisted var");
    read_list(result.lexical_names, "lexical name");
    read_list(result.function_names, "function name");
    read_list(result.annex_b_names, "Annex B name");
    result.source = in.text();
    result.kind = out.kind;

    const std::uint32_t function_count = in.u32();
    if (in.bad) {
        out.error = in.why;
        return out;
    }
    if (function_count == 0) {
        out.error = "an image with no functions - functions[0] is the entry point and every "
                    "caller assumes it exists";
        return out;
    }
    // Bound the function count by the bytes left, as the pools do: the
    // smallest encodable function is 42 bytes of fields plus one 7-byte
    // instruction (a function with no code is refused below), so a count the
    // remaining bytes cannot pay for is corrupt, and resizing on it would
    // allocate unbounded memory.
    constexpr std::size_t least_bytes_per_function = 49;
    if (!in.need(static_cast<std::size_t>(function_count) * least_bytes_per_function)) {
        out.error = "the image claims more functions than its remaining bytes could describe";
        return out;
    }

    result.functions.resize(function_count);
    for (std::uint32_t fi = 0; fi < function_count && !in.bad; ++fi) {
        function_proto & fn = result.functions[fi];
        // Built lazily, only on failure: building "function N: " for every
        // function (and, in the operand pass below, a message for every
        // instruction) measured 22% of the load time.
        const auto where = [fi] { return "function " + std::to_string(fi) + ": "; };
        fn.module = in.text();
        fn.name = in.text();
        fn.inferred_name = in.text();
        fn.param_count = in.u16();
        fn.length = in.u16();
        fn.frame_size = in.u16();
        const std::uint8_t arrow = in.u8();
        const std::uint8_t flags = in.u8();
        const std::uint8_t generator = flags & 3u;
        if (arrow > 3 || generator > 2 || (flags & ~7u) != 0 || (flags == 4u)) {
            in.fail(where() + "a function flag out of range");
            break;
        }
        fn.is_arrow = (arrow & 1u) != 0;
        fn.is_strict = (arrow & 2u) != 0;
        fn.is_generator = generator != 0;
        fn.is_async = generator == 2; // Only an async generator records it (write.cpp).
        fn.eager_prologue = (flags & 4u) != 0;
        fn.source_begin = in.u32();
        fn.source_end = in.u32();

        // frame_size 0 is legal: an empty body needs no registers. It is safe
        // because the operand pass below rejects every `reg` operand against
        // a zero-size frame.
        //
        // param_count must not exceed frame_size: op::call fills
        // [b, param_count) in the callee's window without its own bound, so a
        // forged image would write past the register vector. The compiler
        // never produces this, because parameters are allocated first.
        if (fn.param_count > fn.frame_size) {
            in.fail(where() + "param_count " + std::to_string(fn.param_count) +
                    " exceeds "
                    "frame_size " +
                    std::to_string(fn.frame_size) +
                    " - the parameter fill would write past the register window");
            break;
        }

        const std::uint32_t code_count = in.u32();
        if (!in.need(static_cast<std::size_t>(code_count) * 7u)) {
            in.fail(where() + "the code array claims more instructions than the image holds");
            break;
        }
        if (code_count == 0) {
            in.fail(where() + "no instructions - a frame entered here would run off the end");
            break;
        }
        fn.code.reserve(code_count);
        for (std::uint32_t i = 0; i < code_count && !in.bad; ++i) {
            const std::uint8_t raw = in.u8();
            if (raw >= opcode_count) {
                in.fail(where() + "opcode byte " + std::to_string(raw) + " is not an instruction");
                break;
            }
            instruction one;
            one.code = static_cast<op>(raw);
            one.a = in.u16();
            one.b = in.u16();
            one.c = in.u16();
            fn.code.push_back(one);
        }
        if (in.bad) { break; }

        const std::uint32_t constant_count = in.u32();
        if (!in.need(static_cast<std::size_t>(constant_count) * 8u)) {
            in.fail(where() + "the constant pool claims more entries than the image holds");
            break;
        }
        fn.constants.reserve(constant_count);
        for (std::uint32_t i = 0; i < constant_count && !in.bad; ++i) {
            // Not every 64-bit pattern is a valid value; the rule and its
            // reasons are documented at `why_not_a_constant` (internal.hpp).
            const std::uint64_t bits = in.u64();
            if (const char * why = why_not_a_constant(bits); why != nullptr) {
                in.fail(where() + "constant " + std::to_string(i) + " " + why);
                break;
            }
            fn.constants.push_back(value::from_bits(bits));
        }
        if (in.bad) { break; }

        const auto read_indices = [&](std::vector<std::string> & into,
                                      const std::vector<std::string> & pool, const char * what) {
            const std::uint32_t n = in.u32();
            if (!in.need(static_cast<std::size_t>(n) * 4u)) {
                in.fail(where() + "the " + what +
                        " table claims more entries than the image holds");
                return;
            }
            into.reserve(n);
            for (std::uint32_t i = 0; i < n && !in.bad; ++i) {
                const std::uint32_t id = in.u32();
                if (id >= pool.size()) {
                    in.fail(where() + what + " entry " + std::to_string(i) +
                            " points outside the " + what + " pool");
                    return;
                }
                into.push_back(pool[id]);
            }
        };
        read_indices(fn.strings, strings, "string");
        if (in.bad) { break; }
        read_indices(fn.names, names, "name");
        if (in.bad) { break; }

        const std::uint32_t upvalue_count = in.u32();
        if (!in.need(static_cast<std::size_t>(upvalue_count) * 3u)) {
            in.fail(where() + "the upvalue table claims more entries than the image holds");
            break;
        }
        fn.upvalues.reserve(upvalue_count);
        for (std::uint32_t i = 0; i < upvalue_count && !in.bad; ++i) {
            const std::uint8_t from_local = in.u8();
            if (from_local > 1) {
                in.fail(where() + "upvalue " + std::to_string(i) +
                        " has a boolean that is "
                        "neither 0 nor 1");
                break;
            }
            upvalue_desc up;
            up.from_parent_local = from_local != 0;
            up.index = in.u16();
            fn.upvalues.push_back(up);
        }
        if (in.bad) { break; }

        // The debug side tables. Nothing executes them, but they are still
        // validated: an out-of-range `reg` or a `last_pc` past the end of the
        // code would make their readers (a stack trace, the AOT importer)
        // index off the end of a vector, typically while something has
        // already gone wrong.
        const std::uint32_t local_count = in.u32();
        if (!in.need(static_cast<std::size_t>(local_count) * 15u)) {
            in.fail(where() + "the local table claims more entries than the image holds");
            break;
        }
        fn.locals.reserve(local_count);
        for (std::uint32_t i = 0; i < local_count && !in.bad; ++i) {
            const std::uint32_t id = in.u32();
            if (id >= names.size()) {
                in.fail(where() + "local " + std::to_string(i) + " points outside the name pool");
                break;
            }
            local_desc d;
            d.name = names[id];
            d.reg = in.u16();
            d.first_pc = in.u32();
            d.last_pc = in.u32();
            const std::uint8_t boxed = in.u8();
            if (boxed > 1) {
                in.fail(where() + "local " + std::to_string(i) +
                        " has a boolean that is neither 0 nor 1");
                break;
            }
            d.boxed = boxed != 0;
            if (d.reg >= fn.frame_size) {
                in.fail(where() + "local " + std::to_string(i) + " names register " +
                        std::to_string(d.reg) + " in a frame of " + std::to_string(fn.frame_size));
                break;
            }
            if (d.first_pc > d.last_pc || d.last_pc > fn.code.size()) {
                in.fail(where() + "local " + std::to_string(i) + " is live for [" +
                        std::to_string(d.first_pc) + ", " + std::to_string(d.last_pc) + ") of " +
                        std::to_string(fn.code.size()) + " instructions");
                break;
            }
            fn.locals.push_back(std::move(d));
        }
        if (in.bad) { break; }

        const std::uint32_t offset_count = in.u32();
        // Either one entry per instruction or none. A half-filled table would
        // silently attribute every instruction past its end to nowhere while
        // looking like working debug information.
        if (offset_count != 0 && offset_count != fn.code.size()) {
            in.fail(where() + "the source-offset table has " + std::to_string(offset_count) +
                    " entries for " + std::to_string(fn.code.size()) +
                    " instructions - it is parallel to the code or it is absent");
            break;
        }
        if (!in.need(static_cast<std::size_t>(offset_count) * 4u)) {
            in.fail(where() + "the source-offset table claims more entries than the image holds");
            break;
        }
        fn.code_offsets.reserve(offset_count);
        for (std::uint32_t i = 0; i < offset_count && !in.bad; ++i) {
            fn.code_offsets.push_back(in.u32());
        }
        // Only searched (note_throw_site), never indexed by, so a bad entry
        // misattributes one throw's position and cannot read out of bounds.
        const std::uint32_t rethrow_count = in.u32();
        if (!in.need(static_cast<std::size_t>(rethrow_count) * 4u)) {
            in.fail(where() + "the rethrow table claims more entries than the image holds");
            break;
        }
        fn.rethrows.reserve(rethrow_count);
        for (std::uint32_t i = 0; i < rethrow_count && !in.bad; ++i) {
            fn.rethrows.push_back(in.u32());
        }
        if (in.bad) { break; }
    }
    if (in.bad) {
        out.error = in.why;
        return out;
    }

    // Operands last, because they are checked against tables that must all
    // be present first. Every one is validated against the kind the opcode
    // table gives it, because the VM reads them with unchecked operator[].
    for (std::uint32_t fi = 0; fi < function_count; ++fi) {
        const function_proto & fn = result.functions[fi];
        // One bound per operand kind, in a table, so each check is a load and
        // a compare instead of an unpredictable switch (this is hot: p5 has
        // about half a million instructions). Kinds that need no check (count,
        // jump, bx_hi, unused) get UINT32_MAX, which no uint16 operand can
        // reach. The error message is built by a switch on the failure path
        // only.
        const std::uint32_t reg_bound = fn.frame_size;
        const auto constant_bound = static_cast<std::uint32_t>(fn.constants.size());
        const auto string_bound = static_cast<std::uint32_t>(fn.strings.size());
        const auto name_bound = static_cast<std::uint32_t>(fn.names.size());
        constexpr std::uint32_t unchecked = 0xFFFFFFFFu;
        std::uint32_t bound_of[static_cast<std::size_t>(slot_kind::unused) + 1];
        for (std::uint32_t & b : bound_of) { b = unchecked; }
        bound_of[static_cast<std::size_t>(slot_kind::reg)] = reg_bound;
        bound_of[static_cast<std::size_t>(slot_kind::kidx)] = constant_bound;
        bound_of[static_cast<std::size_t>(slot_kind::sidx)] = string_bound;
        bound_of[static_cast<std::size_t>(slot_kind::nidx)] = name_bound;
        bound_of[static_cast<std::size_t>(slot_kind::fidx)] = function_count;
        // Wide operands have their own bound table because they are checked
        // against fewer kinds than narrow ones (no `reg` check).
        std::uint32_t wide_bound_of[static_cast<std::size_t>(slot_kind::unused) + 1];
        for (std::uint32_t & b : wide_bound_of) { b = unchecked; }
        wide_bound_of[static_cast<std::size_t>(slot_kind::kidx)] = constant_bound;
        wide_bound_of[static_cast<std::size_t>(slot_kind::sidx)] = string_bound;
        wide_bound_of[static_cast<std::size_t>(slot_kind::nidx)] = name_bound;
        wide_bound_of[static_cast<std::size_t>(slot_kind::fidx)] = function_count;
        const std::size_t code_size = fn.code.size();
        for (std::size_t ip = 0; ip < code_size; ++ip) {
            const instruction & one = fn.code[ip];
            const opcode_shape & shape = shapes[static_cast<std::size_t>(one.code)];
            // Built on failure only, like `where` above.
            const auto at = [fi, ip] {
                return "function " + std::to_string(fi) + ", instruction " + std::to_string(ip) +
                       ": ";
            };

            const auto check_slot = [&](slot_kind kind, std::uint16_t operand, const char * which) {
                if (operand < bound_of[static_cast<std::size_t>(kind)] || in.bad) { return; }
                switch (kind) {
                case slot_kind::reg:
                    in.fail(at() + which + " names register " + std::to_string(operand) +
                            " in a frame of " + std::to_string(fn.frame_size));
                    return;
                case slot_kind::kidx:
                    in.fail(at() + which + " points outside the constant pool");
                    return;
                case slot_kind::sidx:
                    in.fail(at() + which + " points outside the string table");
                    return;
                case slot_kind::nidx:
                    in.fail(at() + which + " points outside the name table");
                    return;
                case slot_kind::fidx: in.fail(at() + which + " is not a function"); return;
                default: return; // Unreachable: their bound is UINT32_MAX.
                }
            };

            // A wide operand is one field, so b and c are checked together
            // when c is the high half.
            if (shape.c == slot_kind::bx_hi) {
                const std::uint32_t wide = one.bx();
                if (wide >= wide_bound_of[static_cast<std::size_t>(shape.b)]) {
                    switch (shape.b) {
                    case slot_kind::kidx: in.fail(at() + "constant index out of range"); break;
                    case slot_kind::sidx: in.fail(at() + "string index out of range"); break;
                    case slot_kind::nidx: in.fail(at() + "name index out of range"); break;
                    case slot_kind::fidx: in.fail(at() + "function index out of range"); break;
                    default: break; // Unreachable: their bound is UINT32_MAX.
                    }
                }
                check_slot(shape.a, one.a, "operand a");
            } else {
                check_slot(shape.a, one.a, "operand a");
                check_slot(shape.b, one.b, "operand b");
                check_slot(shape.c, one.c, "operand c");
            }

            if (shape.b == slot_kind::jump) {
                // The target is relative to the already-incremented ip, which
                // is what the compiler's patch arithmetic compensates for.
                const std::int64_t target = static_cast<std::int64_t>(ip) + 1 + one.sbx();
                if (target < 0 || target > static_cast<std::int64_t>(fn.code.size())) {
                    in.fail(at() + "jumps outside the function");
                }
            }
            // A closure's upvalues marked from_parent_local are read out of the
            // enclosing frame's registers without a bounds check (see
            // context::make_closure, reached from the `closure` opcode in
            // run_loop.cpp). An image naming register 65,535 of a
            // four-register frame would read far past the register window.
            //
            // The bound is the enclosing function's frame size: the compiler
            // emits `upvalue_desc{true, l->reg}` where l->reg is a register of
            // the function doing the closing, so the invariant belongs to the
            // (closer, closed-over) pair, and this is the only place both are
            // in hand.
            if (shape.b == slot_kind::fidx && shape.c == slot_kind::bx_hi) {
                const std::uint32_t target = one.bx();
                if (target < function_count) {
                    const function_proto & closed = result.functions[target];
                    for (std::size_t u = 0; u < closed.upvalues.size(); ++u) {
                        const upvalue_desc & up = closed.upvalues[u];
                        if (up.from_parent_local && up.index >= fn.frame_size) {
                            in.fail(at() + "closes over function " + std::to_string(target) +
                                    ", whose upvalue " + std::to_string(u) + " captures register " +
                                    std::to_string(up.index) + " of a frame that has " +
                                    std::to_string(fn.frame_size));
                            break;
                        }
                    }
                }
            }
            if (is_call_shape(one.code)) {
                // The callee's frame is placed at the caller's argument window,
                // and the arguments are gathered straight out of it.
                const std::size_t window = static_cast<std::size_t>(one.a) + 1u + one.b;
                if (window > fn.frame_size) {
                    in.fail(at() + "an argument window of " + std::to_string(window) +
                            " does not fit a frame of " + std::to_string(fn.frame_size));
                }
            }
            if (in.bad) { break; }
        }
        if (in.bad) { break; }
    }
    if (in.bad) {
        out.error = in.why;
        return out;
    }

    result.ok = true;
    out.value = std::move(result);
    out.ok = true;
    return out;
}

} // namespace ctbrowser::script
