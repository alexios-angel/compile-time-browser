#include <ctbrowser/script/program_image.hpp>

#include <cstring>
#include <unordered_map>

// The format, in one file so the writer and the reader cannot drift.
//
// EVERYTHING IS LITTLE-ENDIAN AND EXPLICIT. Nothing is memcpy'd out of a struct,
// and that is not fastidiousness: `instruction` is {op at 0, PADDING at 1, a at
// 2, b at 4, c at 6} with alignof 2, and the padding byte measurably differs
// between -O0 and -O2 builds of the code that BUILT the instruction. A raw copy
// would produce different images for the same program depending on how the
// writer was compiled, while every correctness test passed. So op, a, b and c
// are written as four fields. ctcompile/include/ctcompile/JavaScript/
// EngineContract.hpp asserts the representation is not canonical, so this
// reasoning is enforced rather than remembered.

namespace ctbrowser::script {

namespace {

constexpr std::uint32_t magic = 0x43544243; // 'CTBC'
constexpr std::uint32_t format_version = 1;

thread_local std::string last_write_error;

// --- writing -------------------------------------------------------------

struct sink {
    std::vector<std::byte> bytes;

    void u8(std::uint8_t v) { bytes.push_back(static_cast<std::byte>(v)); }
    void u16(std::uint16_t v) {
        u8(static_cast<std::uint8_t>(v & 0xFF));
        u8(static_cast<std::uint8_t>(v >> 8));
    }
    void u32(std::uint32_t v) {
        u16(static_cast<std::uint16_t>(v & 0xFFFF));
        u16(static_cast<std::uint16_t>(v >> 16));
    }
    void u64(std::uint64_t v) {
        u32(static_cast<std::uint32_t>(v & 0xFFFFFFFFu));
        u32(static_cast<std::uint32_t>(v >> 32));
    }
    void text(std::string_view s) {
        u32(static_cast<std::uint32_t>(s.size()));
        for (const char c : s) { u8(static_cast<std::uint8_t>(c)); }
    }
};

// --- reading -------------------------------------------------------------

// Every read is bounds-checked and sets `bad` once. A reader that returned
// garbage past the end would turn a truncated file into a program.
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
    // ONE BOUNDS CHECK AND ONE LOOP PER VALUE, not one per byte. Reading a
    // 7 MB image a byte at a time through the check cost 13% of the load,
    // measured with callgrind - a bigger share than validating every operand.
    // Little-endian by construction rather than by cast, so the format does
    // not acquire an opinion about the host's byte order.
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

} // namespace

std::uint64_t image_fingerprint() noexcept {
    // The things whose MEANING an image depends on. Not a version number: a
    // version says what someone remembered to bump, and this says what is
    // actually true of the build reading the file.
    //
    // opcode_count is the load-bearing one. Phases 13 and 14 renumber
    // `enum class op`, and an image written before that describes different
    // instructions with the same bytes - which would run at full speed and be
    // wrong, the worst failure available here.
    std::uint64_t h = 1469598103934665603ull; // FNV-1a offset basis
    const auto mix = [&h](std::uint64_t v) {
        h ^= v;
        h *= 1099511628211ull;
    };
    mix(opcode_count);
    mix(sizeof(instruction));
    mix(sizeof(value));
    mix(sizeof(upvalue_desc));
    mix(format_version);
    return h;
}

std::uint64_t image_source_hash(std::string_view source) noexcept {
    // FNV-1a over the bytes. Not a security property - see the header's note on
    // trust - but enough that a changed script is a different number.
    std::uint64_t h = 1469598103934665603ull;
    for (const char c : source) {
        h ^= static_cast<std::uint8_t>(c);
        h *= 1099511628211ull;
    }
    return h;
}

std::string_view write_error() noexcept {
    return last_write_error;
}

std::vector<std::byte> write_image(const program & from, image_option option) {
    last_write_error = {};
    if (!from.ok) {
        last_write_error = "refusing to write a program that did not compile: " + from.error;
        return {};
    }
    if (from.functions.empty()) {
        last_write_error = "refusing to write a program with no functions - functions[0] is the "
                           "entry point and every caller assumes it exists";
        return {};
    }
    // The compiler's own ceiling, restated where an image could otherwise
    // exceed it: three of the four op::closure emitters narrow the function
    // index to uint16 before building the wide operand, so a program with more
    // than 65,535 functions is one the COMPILER already got wrong. Writing it
    // would freeze that into an image and round-trip it perfectly.
    if (from.functions.size() > 65535) {
        last_write_error = "refusing to write a program with more than 65,535 functions - "
                           "op::closure's operand is narrowed to 16 bits by the compiler, so "
                           "such a program is already miscompiled";
        return {};
    }

    // ONE POOL FOR NAMES, ONE FOR STRINGS. Measured on the corpora: babylon's
    // 166,396 name entries are 24,172 unique, so pooling turns 2686 KB into
    // 495 KB. String literals do not duplicate that way - 2050 KB to 1840 -
    // but they are pooled by the same mechanism because it costs nothing.
    std::vector<std::string> names;
    std::vector<std::string> strings;
    std::unordered_map<std::string, std::uint32_t> name_at;
    std::unordered_map<std::string, std::uint32_t> string_at;
    const auto intern = [](std::vector<std::string> & pool,
                           std::unordered_map<std::string, std::uint32_t> & index,
                           const std::string & text) {
        const auto found = index.find(text);
        if (found != index.end()) { return found->second; }
        const auto id = static_cast<std::uint32_t>(pool.size());
        pool.push_back(text);
        index.emplace(text, id);
        return id;
    };
    for (const function_proto & fn : from.functions) {
        for (const std::string & s : fn.names) { (void)intern(names, name_at, s); }
        for (const std::string & s : fn.strings) { (void)intern(strings, string_at, s); }
    }

    sink out;
    out.u32(magic);
    out.u32(format_version);
    out.u64(image_fingerprint());
    out.u32(static_cast<std::uint32_t>(option));
    // FROM THE PROGRAM'S OWN SOURCE, before any decision about keeping it: an
    // image that drops the text still has to be able to say what it was built
    // from, and that is precisely the build where nothing else can.
    out.u64(image_source_hash(from.source));

    out.u32(static_cast<std::uint32_t>(names.size()));
    for (const std::string & s : names) { out.text(s); }
    out.u32(static_cast<std::uint32_t>(strings.size()));
    for (const std::string & s : strings) { out.text(s); }

    // ORDER IS PRESERVED FOR ALL THREE, and for two of them it is observable:
    // imports fix depth-first instantiation and therefore the order of modules'
    // top-level side effects, and the FIRST claim on a re-exported name wins,
    // so swapping two reexports changes which cell a name resolves to.
    out.u32(static_cast<std::uint32_t>(from.imports.size()));
    for (const std::string & s : from.imports) { out.text(s); }
    out.u32(static_cast<std::uint32_t>(from.exports.size()));
    for (const std::string & s : from.exports) { out.text(s); }
    out.u32(static_cast<std::uint32_t>(from.reexports.size()));
    for (const program::reexport & r : from.reexports) {
        out.text(r.exported);
        out.text(r.source);
        out.text(r.from);
    }

    out.text(option == image_option::keep_source ? std::string_view{from.source}
                                                 : std::string_view{});

    out.u32(static_cast<std::uint32_t>(from.functions.size()));
    for (const function_proto & fn : from.functions) {
        out.text(fn.module);
        out.text(fn.name);
        out.u16(fn.param_count);
        out.u16(fn.frame_size);
        out.u8(fn.is_arrow ? 1u : 0u);
        out.u8(fn.is_generator ? 1u : 0u);
        out.u32(fn.source_begin);
        out.u32(fn.source_end);

        out.u32(static_cast<std::uint32_t>(fn.code.size()));
        for (const instruction & in : fn.code) {
            out.u8(static_cast<std::uint8_t>(in.code));
            out.u16(in.a);
            out.u16(in.b);
            out.u16(in.c);
        }
        out.u32(static_cast<std::uint32_t>(fn.constants.size()));
        for (const value & v : fn.constants) { out.u64(v.bits()); }
        out.u32(static_cast<std::uint32_t>(fn.strings.size()));
        for (const std::string & s : fn.strings) { out.u32(intern(strings, string_at, s)); }
        out.u32(static_cast<std::uint32_t>(fn.names.size()));
        for (const std::string & s : fn.names) { out.u32(intern(names, name_at, s)); }
        out.u32(static_cast<std::uint32_t>(fn.upvalues.size()));
        for (const upvalue_desc & up : fn.upvalues) {
            out.u8(up.from_parent_local ? 1u : 0u);
            out.u16(up.index);
        }
        // `nested` is written as a count so the day the compiler starts filling
        // it is a test failure rather than a silently unserialized field.
        // Nothing writes it today and its only reader is a ratchet check that
        // therefore cannot fire.
        out.u32(static_cast<std::uint32_t>(fn.nested.size()));
        for (const std::uint32_t n : fn.nested) { out.u32(n); }
    }
    return std::move(out.bytes);
}

namespace {

// THE OPERAND KINDS, STRAIGHT FROM THE TABLE, so validation cannot disagree
// with the inventory about which pool an operand indexes. This is what the
// kind columns are for, and it is why `sidx` had to be split from `nidx` and
// `fidx` from `kidx` before anything was generated from them: a check built on
// the old spelling would have bounds-checked a constant pool against a
// function index.
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

// The five that place a callee's frame AT the caller's argument window, where
// `b` is an argument COUNT rather than an index. `a + 1 + b` must fit the
// caller's frame or the argument gather reads past it.
[[nodiscard]] constexpr bool is_call_shape(op code) {
    return code == op::call || code == op::call_method || code == op::call_computed ||
           code == op::construct || code == op::call_receiver;
}

} // namespace

load_result load_image(std::span<const std::byte> bytes,
                       std::optional<std::uint64_t> expect_source_hash) {
    load_result out;
    source_bytes in{bytes, 0, false, {}};

    if (in.u32() != magic) {
        out.error = "not a ctbrowser program image";
        return out;
    }
    if (const std::uint32_t v = in.u32(); v != format_version) {
        out.error = "image format version " + std::to_string(v) + ", this build reads " +
                    std::to_string(format_version);
        return out;
    }
    if (const std::uint64_t got = in.u64(); got != image_fingerprint()) {
        // The most valuable refusal in the file. An image whose opcodes were
        // numbered differently would otherwise load and run at full speed,
        // executing different instructions than the ones that were compiled.
        out.error = "image was written by a different engine build - opcode numbering, value "
                    "layout or instruction layout has changed since it was written";
        return out;
    }
    const std::uint32_t option = in.u32();
    if (option > static_cast<std::uint32_t>(image_option::drop_source)) {
        out.error = "unknown image option " + std::to_string(option);
        return out;
    }
    out.source_hash = in.u64();
    if (expect_source_hash && *expect_source_hash != out.source_hash) {
        // THE REFUSAL THAT MAKES A CACHE SAFE. A stale image is not a slow
        // path - it is different JavaScript running at full speed, with the
        // page behaving as it did before an edit nobody can see.
        out.error = "the image was built from different source";
        return out;
    }

    const auto read_pool = [&in](const char * what) {
        std::vector<std::string> pool;
        const std::uint32_t n = in.u32();
        if (!in.need(n)) { // a count larger than the file cannot be honest
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
        if (!in.need(n)) {
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
        if (in.need(n)) {
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
    result.source = in.text();

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
    if (function_count > 65535) {
        out.error = "an image with more than 65,535 functions; op::closure's operand cannot "
                    "address them";
        return out;
    }

    result.functions.resize(function_count);
    for (std::uint32_t fi = 0; fi < function_count && !in.bad; ++fi) {
        function_proto & fn = result.functions[fi];
        // LAZY, because it is almost never used. Building "function N: " for
        // every function - and, in the operand pass below, a message for every
        // INSTRUCTION - cost 22% of the load in std::string mutation alone,
        // measured. An error message that is constructed whether or not there
        // is an error is a message that costs more than the check it explains.
        const auto where = [fi] { return "function " + std::to_string(fi) + ": "; };
        fn.module = in.text();
        fn.name = in.text();
        fn.param_count = in.u16();
        fn.frame_size = in.u16();
        const std::uint8_t arrow = in.u8();
        const std::uint8_t generator = in.u8();
        if (arrow > 1 || generator > 1) {
            in.fail(where() + "a boolean that is neither 0 nor 1");
            break;
        }
        fn.is_arrow = arrow != 0;
        fn.is_generator = generator != 0;
        fn.source_begin = in.u32();
        fn.source_end = in.u32();

        // frame_size 0 IS LEGAL AND THIS RULE USED TO REFUSE IT. An empty
        // body needs no registers: p5 has 23 such functions out of 4,754, one
        // instruction each and no pools at all. The rule was stricter than the
        // compiler, and it was measurement that said so rather than argument -
        // which is the right order for a validator, because a rule loosened
        // because it was inconvenient is how a validator stops validating.
        //
        // Nothing replaces it: a frame of zero registers is safe precisely
        // because the operand pass below rejects EVERY `reg` operand against
        // it, so such a function can only contain instructions that name no
        // register.
        // THE ONE THAT IS A WRITE. op::call fills [b, param_count) in the
        // callee's window without a bound of its own; the compiler cannot
        // produce param_count > frame_size because parameters are allocated
        // first, but an image can, and the result is a heap write past the
        // register vector.
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
            const value v = value::from_bits(in.u64());
            // A CONSTANT MAY NOT BE A HEAP POINTER. The pool holds immediates
            // only - a string literal lives in `strings` and is materialised by
            // the VM - so a boxed pointer here is a file handing the engine an
            // address to dereference.
            // is_heap(), NOT is_object(): is_object asks the pointed-to
            // object what KIND it is, which dereferences a pointer that came
            // out of a file. is_heap is a pure bit test and is the only one
            // that is safe to run on bytes nobody has validated yet.
            if (v.is_heap()) {
                in.fail(where() + "constant " + std::to_string(i) +
                        " carries a heap pointer; the pool holds immediates only");
                break;
            }
            fn.constants.push_back(v);
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

        const std::uint32_t nested_count = in.u32();
        if (!in.need(static_cast<std::size_t>(nested_count) * 4u)) {
            in.fail(where() + "the nested table claims more entries than the image holds");
            break;
        }
        fn.nested.reserve(nested_count);
        for (std::uint32_t i = 0; i < nested_count && !in.bad; ++i) {
            const std::uint32_t id = in.u32();
            if (id >= function_count) {
                in.fail(where() + "nested entry " + std::to_string(i) + " is not a function");
                break;
            }
            fn.nested.push_back(id);
        }
    }
    if (in.bad) {
        out.error = in.why;
        return out;
    }

    // OPERANDS LAST, because they are checked against tables that must all be
    // present first. Every one is validated against the kind the inventory
    // gives it - the VM reads these with unchecked operator[].
    for (std::uint32_t fi = 0; fi < function_count; ++fi) {
        const function_proto & fn = result.functions[fi];
        for (std::size_t ip = 0; ip < fn.code.size(); ++ip) {
            const instruction & one = fn.code[ip];
            const opcode_shape & shape = shapes[static_cast<std::size_t>(one.code)];
            // Built on failure only - see the note above. p5 has half a million
            // instructions and this ran for every one of them.
            const auto at = [fi, ip] {
                return "function " + std::to_string(fi) + ", instruction " + std::to_string(ip) +
                       ": ";
            };

            const auto check_slot = [&](slot_kind kind, std::uint16_t operand, const char * which) {
                if (in.bad) { return; }
                switch (kind) {
                case slot_kind::reg:
                    if (operand >= fn.frame_size) {
                        in.fail(at() + which + " names register " + std::to_string(operand) +
                                " in a frame of " + std::to_string(fn.frame_size));
                    }
                    return;
                case slot_kind::kidx:
                    if (operand >= fn.constants.size()) {
                        in.fail(at() + which + " points outside the constant pool");
                    }
                    return;
                case slot_kind::sidx:
                    if (operand >= fn.strings.size()) {
                        in.fail(at() + which + " points outside the string table");
                    }
                    return;
                case slot_kind::nidx:
                    if (operand >= fn.names.size()) {
                        in.fail(at() + which + " points outside the name table");
                    }
                    return;
                case slot_kind::fidx:
                    if (operand >= function_count) { in.fail(at() + which + " is not a function"); }
                    return;
                default: return; // count, jump, bx_hi and unused are checked below or not at all
                }
            };

            // A WIDE OPERAND IS ONE FIELD, so b and c are checked together
            // rather than separately when c is the high half.
            if (shape.c == slot_kind::bx_hi) {
                const std::uint32_t wide = one.bx();
                switch (shape.b) {
                case slot_kind::kidx:
                    if (wide >= fn.constants.size()) {
                        in.fail(at() + "constant index out of range");
                    }
                    break;
                case slot_kind::sidx:
                    if (wide >= fn.strings.size()) { in.fail(at() + "string index out of range"); }
                    break;
                case slot_kind::nidx:
                    if (wide >= fn.names.size()) { in.fail(at() + "name index out of range"); }
                    break;
                case slot_kind::fidx:
                    if (wide >= function_count) { in.fail(at() + "function index out of range"); }
                    break;
                case slot_kind::jump: break; // handled below
                default: break;
                }
                check_slot(shape.a, one.a, "operand a");
            } else {
                check_slot(shape.a, one.a, "operand a");
                check_slot(shape.b, one.b, "operand b");
                check_slot(shape.c, one.c, "operand c");
            }

            if (shape.b == slot_kind::jump) {
                // The target is relative to the ALREADY-INCREMENTED ip, which
                // is what the compiler's patch arithmetic compensates for.
                const std::int64_t target = static_cast<std::int64_t>(ip) + 1 + one.sbx();
                if (target < 0 || target > static_cast<std::int64_t>(fn.code.size())) {
                    in.fail(at() + "jumps outside the function");
                }
            }
            if (is_call_shape(one.code)) {
                // The callee's frame is placed AT the caller's argument window,
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
