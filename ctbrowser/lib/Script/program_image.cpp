#include <ctbrowser/script/program_image.hpp>

#include <boost/hash2/xxhash.hpp>

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

// FNV-1a's constants, used by `image_fingerprint()` and by nothing else in
// this file - `image_source_hash` was the other consumer until it became
// XXH64, which is why they are named here rather than written inline.
//
// The basis had a digit missing - 1469598103934665603 rather than
// 14695981039346656037. It hashed perfectly well, any odd starting value does,
// but it named a constant the code did not contain. Correcting it changes what
// `image_fingerprint()` returns, which is the refusal an image written before
// today was going to get regardless.
constexpr std::uint64_t fnv_basis = 14695981039346656037ull;
constexpr std::uint64_t fnv_prime = 1099511628211ull;

// WHICH ALGORITHM `image_source_hash` IS, mixed into the fingerprint. See
// image_fingerprint() for why it lives there and not in `format_version`.
// 1 was byte-at-a-time FNV-1a; 2 is XXH64.
constexpr std::uint32_t source_hash_algorithm = 2;

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
    std::uint64_t h = fnv_basis;
    const auto mix = [&h](std::uint64_t v) {
        h ^= v;
        h *= fnv_prime;
    };
    mix(opcode_count);
    mix(sizeof(instruction));
    mix(sizeof(value));
    mix(sizeof(upvalue_desc));
    mix(format_version);
    // THE SOURCE HASH ALGORITHM, because an image records what its source
    // hashed to and that number means nothing across a change in how it is
    // computed. It belongs here rather than in `format_version` for a precise
    // reason: the byte layout did not change, the MEANING of one field did -
    // and the version check runs first, so bumping the version would refuse an
    // old image with a message about a format that is in fact identical. With
    // the tag here the refusal says what is true.
    //
    // Without it the old image is still refused, by the source-hash comparison
    // one field later, and told "the image was built from different source"
    // about source that never changed. This is a message, not a safety net.
    //
    // NOT COVERED BY A TEST, and it cannot be from inside one build: producing
    // an image carrying the old hash needs the old build. ProgramImage.cpp
    // pins the precondition instead - that the algorithm really did change -
    // by checking a corpus against the byte-at-a-time hash this replaced.
    mix(source_hash_algorithm);
    return h;
}

std::uint64_t image_source_hash(std::string_view source) noexcept {
    // XXH64, FROM Boost.Hash2, because this runs on the page-load path and a
    // byte at a time was 4.16 ms of it for p5 alone - measured, and bigger than
    // anything left inside the loader it guards. 4.16 ms becomes 0.181 for p5,
    // 8.17 becomes 0.346 for phaser, 10.31 becomes 0.454 for babylon.
    //
    // WHAT WAS WRITTEN FIRST WAS FNV-1a WIDENED TO 64-BIT WORDS - four lanes of
    // `h = (h ^ w) * prime` - and it was wrong in a way no round-trip test can
    // see. A multiply only ever carries a difference UPWARD, so a change in a
    // word's most significant bits leaves the accumulator differing in its top
    // three bits and nowhere else, and it is the SAME three bits wherever in the
    // lane the change happened. Two different edits reach the same accumulator.
    // Over 262,145 single-byte edits of real p5.js, 50,678 of them - ONE IN
    // FIVE - hashed identically to another edit. FNV-1a a byte at a time does
    // not have that problem, because a byte enters at the bottom where the
    // multiply can spread it; widening it to words is what broke it.
    //
    // Rotating inside the round removes the collisions and replaces them with a
    // magic number: swept over all 63 rotate amounts, 51 collide with FNV's
    // prime and 49 with a dense one, and the amounts that work do so by dodging
    // a property of ASCII - bit 7 of a byte is never set. A constant that is
    // good because of the corpus is not a fix, it is a coincidence with a test
    // suite in front of it.
    //
    // SO THE ALGORITHM IS SOMEBODY ELSE'S AND SO IS THE CODE. XXH64 leaves no
    // constant to choose and was tested against SMHasher rather than against
    // p5.js. It was first written out here by hand, forty lines of it, and that
    // is the version this comment replaced: a hand transcription of a published
    // algorithm is exactly the code most likely to be quietly wrong, and today
    // is the day that argument stopped being hypothetical. Boost.Hash2 is
    // header-only, `ctbrowser-script` ALREADY links `Boost::headers`, the
    // header costs 0.02 s in this translation unit, and it measured 0.181 ms
    // against the hand-written 0.183. Three implementations agree on all three
    // corpora - this one, the hand-written one, and Yann Collet's own `xxhsum`.
    //
    // The endianness guarantee comes with it: Boost.Hash2 is endian-independent
    // by contract, so the number is the same on a host of either byte order,
    // which is what the rest of this file spells out by hand for its integers.
    //
    // Still not a security property; see the header's note on trust. XXH64 is
    // unkeyed and this is not the defence an image from elsewhere would need.
    boost::hash2::xxhash_64 hash;
    // A default-constructed string_view has a NULL data(), and `update` may not
    // be handed one. Skipping it is also the right answer: the hash of nothing
    // is what an untouched xxhash_64 already holds.
    if (!source.empty()) { hash.update(source.data(), source.size()); }
    return hash.result();
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
        if (!in.need(static_cast<std::size_t>(n) * 4u)) { // see read_pool: a length prefix each
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
        // TWELVE BYTES EACH: a re-export is three length-prefixed strings, and
        // the struct is 96 bytes, so this is the site where a corrupt count
        // reserves gigabytes.
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
        // ONE BOUND PER KIND, IN A TABLE, so that checking an operand is a load
        // and a compare rather than a jump table. The switch this replaces ran
        // three times per instruction on an index the branch predictor cannot
        // learn - opcode kinds arrive in whatever order the program is written
        // - and p5 is half a million instructions.
        //
        // NOTHING ABOUT WHAT IS CHECKED CHANGES. The kinds the switch did not
        // check - count, jump, bx_hi, unused - get a bound of UINT32_MAX, and
        // an operand is a uint16, so their compare is false by construction
        // rather than by omission. The kind-specific message is still built by
        // a switch, on the failure path, where a branch costs nothing.
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
        // A WIDE OPERAND IS CHECKED AGAINST FEWER KINDS than a narrow one -
        // the switch it replaces had no `reg` case and fell through its
        // `default` - so it gets its own table rather than sharing one and
        // quietly becoming stricter than the code it replaced.
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
            // Built on failure only - see the note above. p5 has half a million
            // instructions and this ran for every one of them.
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
                default: return; // unreachable: their bound is UINT32_MAX
                }
            };

            // A WIDE OPERAND IS ONE FIELD, so b and c are checked together
            // rather than separately when c is the high half.
            if (shape.c == slot_kind::bx_hi) {
                const std::uint32_t wide = one.bx();
                if (wide >= wide_bound_of[static_cast<std::size_t>(shape.b)]) {
                    switch (shape.b) {
                    case slot_kind::kidx: in.fail(at() + "constant index out of range"); break;
                    case slot_kind::sidx: in.fail(at() + "string index out of range"); break;
                    case slot_kind::nidx: in.fail(at() + "name index out of range"); break;
                    case slot_kind::fidx: in.fail(at() + "function index out of range"); break;
                    default: break; // unreachable: their bound is UINT32_MAX
                    }
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
            // A CLOSURE'S UPVALUES ARE READ OUT OF THE ENCLOSING FRAME, and
            // nothing bounded them. run_loop.cpp:904 does
            // `made->upvalues.push_back(reg(up.index))` where `reg` is
            // `registers_[base + r]` and is unchecked - the OTHER branch, three
            // lines below it, does bound its index against the closure's
            // upvalue count, which makes the gap easy to miss. An image could
            // therefore name register 65,535 of a frame that has four, read
            // half a megabyte past the register window, and hand whatever was
            // there to a cell test that dereferences it.
            //
            // The bound is the ENCLOSING function's frame size, which is only
            // knowable here: the compiler emits `upvalue_desc{true, l->reg}`
            // where l->reg is a register of the function doing the closing, so
            // the invariant belongs to the (closer, closed-over) PAIR rather
            // than to either function alone. This is the only place both are in
            // hand at once.
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
