//===- program_image/write.cpp - Program image writer -----------*- C++ -*-===//
//
// The program image writer: the engine fingerprint an image is stamped with,
// the source hash, and write_image itself. The format's constants and the
// check both reader and writer make are in internal.hpp beside this, so the
// two cannot drift.
//
//===----------------------------------------------------------------------===//

#include "internal.hpp"

namespace ctbrowser::script {

using detail::format_version;
using detail::magic;
using detail::why_not_a_constant;

namespace {

// The 64-bit FNV-1a offset basis and prime, used by image_fingerprint() and
// codegen_identity().
constexpr std::uint64_t fnv_basis = 14695981039346656037ull;
constexpr std::uint64_t fnv_prime = 1099511628211ull;

// Which algorithm `image_source_hash` uses, mixed into the fingerprint (see
// image_fingerprint() for why it is not part of `format_version`).
// 1 was byte-at-a-time FNV-1a; 2 is XXH64.
constexpr std::uint32_t source_hash_algorithm = 2;

thread_local std::string last_write_error;

/// The output buffer. Every integer is written little-endian, byte by byte,
/// so the image does not depend on the host's byte order.
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

} // namespace

/// Hash of the bytecode this build emits for a fixed canary program.
///
/// `opcode_set_identity` covers what the opcodes are, not what the compiler
/// does with them: a codegen change (such as how `finally` is lowered) changes
/// the bytecode without changing any opcode, and an old image would load
/// silently. Compiling a canary catches that without anyone remembering to
/// bump a version. Codegen changes that only affect constructs absent from the
/// canary are not detected, so the canary is deliberately broad; adding to it
/// is free.
///
/// image_fingerprint() is `noexcept` and this allocates, so the compile is
/// wrapped in a try block. A failure yields its own value rather than zero, so
/// an engine that cannot compile its own canary refuses every image.
[[nodiscard]] std::uint64_t codegen_identity() noexcept {
    // Compiled once: this is on the path of every image load and write.
    static const std::uint64_t identity = []() noexcept -> std::uint64_t {
        try {
            constexpr std::string_view canary =
                "var g = 0;\n"
                "function plain(a, b) { return a + b * 2; }\n"
                "const arrow = (x) => x === 0 ? 'z' : `t${x}`;\n"
                "function loops(n) {\n"
                "  let total = 0;\n"
                "  outer: for (let i = 0; i < n; i++) {\n"
                "    for (const c of [1, 2, 3]) { if (c === 2) { continue outer; } total += c; }\n"
                "    while (total > 100) { break outer; }\n"
                "    do { total += 1; } while (false);\n"
                "  }\n"
                "  return total;\n"
                "}\n"
                "function guards(x) {\n"
                "  try { if (x) { return 'r'; } throw new Error('e'); }\n"
                "  catch (e) { return e.message; }\n"
                "  finally { g += 1; }\n"
                "}\n"
                "function bare() { try { return 1; } finally { g += 2; } }\n"
                "function looped() { for (;;) { try { break; } finally { g += 3; } } return g; }\n"
                "class Shape { constructor(n) { this.n = n; } get twice() { return this.n * 2; }\n"
                "  set twice(v) { this.n = v / 2; } static make(n) { return new Shape(n); }\n"
                "  #secret = 4; reveal() { return this.#secret; } }\n"
                "class Round extends Shape { constructor() { super(7); } }\n"
                "function* counter(n) { for (let i = 0; i < n; i++) { yield i; } }\n"
                "async function later(p) { const v = await p; return v; }\n"
                "function shapes({ a, b: [c] = [1] }, ...rest) { return a + c + rest.length; }\n"
                "const chained = (o) => o?.a?.b ?? 'none';\n"
                "const big = 9007199254740993n;\n"
                "switch (g) { case 0: g = 1; break; default: g = 2; }\n"
                "const { x = 1, ...others } = { y: 2 };\n"
                "label: { if (g) { break label; } g = 3; }\n"
                "for (const k in { a: 1 }) { g += k.length; }\n"
                "g = plain(1, 2) + loops(3) + guards(0).length + bare() + looped();\n";

            const program built = compiler::compile(canary);
            std::uint64_t h = fnv_basis;
            const auto mix = [&h](std::uint64_t v) {
                h ^= v;
                h *= fnv_prime;
            };
            // A compile failure is an identity of its own rather than a crash or
            // a silent zero: if this source ever stops compiling, every image
            // this build reads is refused until somebody looks.
            mix(built.ok ? 1u : 0u);
            mix(built.functions.size());
            for (const function_proto & fn : built.functions) {
                // The shape of the frame as well as the code: a change that moves a
                // value between registers without changing an opcode is still a
                // change to what runs.
                mix(fn.param_count);
                mix(fn.frame_size);
                mix(fn.code.size());
                mix(fn.constants.size());
                mix(fn.strings.size());
                mix(fn.names.size());
                mix(fn.upvalues.size());
                for (const instruction & one : fn.code) {
                    mix(static_cast<std::uint64_t>(one.code));
                    mix(one.a);
                    mix(one.b);
                    mix(one.c);
                }
                for (const upvalue_desc & up : fn.upvalues) {
                    mix(up.from_parent_local ? 1u : 0u);
                    mix(up.index);
                }
            }
            return h;
        } catch (...) { return fnv_basis ^ 0xC0DE'0000'0000'DEADull; }
    }();
    return identity;
}

std::uint64_t image_fingerprint() noexcept {
    // Mixes in everything whose meaning an image depends on. This is not a
    // version number: a version says what someone remembered to bump, and
    // this says what is actually true of the build reading the file.
    //
    // `opcode_set_identity` (bytecode.hpp) folds every opcode's spelling in
    // order. An opcode count alone cannot see a renumbering of `enum class op`
    // that adds or removes nothing, and an old image would then run different
    // instructions from the same bytes.
    std::uint64_t h = fnv_basis;
    const auto mix = [&h](std::uint64_t v) {
        h ^= v;
        h *= fnv_prime;
    };
    mix(opcode_count);
    // And what the opcodes are, not only how many: renumbering `enum class op`
    // keeps the count and changes what every stored byte means.
    mix(opcode_set_identity);
    mix(sizeof(instruction));
    mix(sizeof(value));
    mix(sizeof(upvalue_desc));
    mix(format_version);
    // The source hash algorithm, because an image records what its source
    // hashed to, and that number means nothing across a change in how it is
    // computed. It belongs here rather than in `format_version` because the
    // byte layout is unchanged, only the meaning of one field; the version
    // check runs first and would blame the format. (Without it, an old image
    // would still be refused, by the source-hash comparison, but with a
    // misleading "different source" message.)
    //
    // Not testable from inside one build, since producing an image with the
    // old hash needs the old build. ProgramImage.cpp checks the precondition
    // instead: that the algorithm differs from byte-at-a-time FNV-1a.
    mix(source_hash_algorithm);
    // And what the compiler emits, which nothing above can see (see
    // codegen_identity): the opcodes can stay identical while the bytecode
    // built from a given source changes completely.
    mix(codegen_identity());
    return h;
}

std::uint64_t image_source_hash(std::string_view source) noexcept {
    // XXH64 via Boost.Hash2 (header-only, already linked): this runs on every
    // page load, and byte-at-a-time FNV-1a cost 4.16 ms on p5.js against
    // 0.18 ms for XXH64. FNV-1a widened to 64-bit words was tried and collides
    // badly (a multiply only carries differences upward, so about one in five
    // single-byte edits of p5.js collided), so a published, SMHasher-tested
    // hash is used rather than a hand-rolled one. Boost.Hash2 is
    // endian-independent, so the value is the same on any host.
    //
    // Not a security property (see the header's note on trust): XXH64 is
    // unkeyed.
    boost::hash2::xxhash_64 hash;
    // A default-constructed string_view has a null data(), which `update` may
    // not be handed. Skipping it is also correct: the hash of nothing is what
    // an untouched xxhash_64 already holds.
    if (!source.empty()) { hash.update(source.data(), source.size()); }
    return hash.result();
}

std::uint32_t image_format_version() noexcept {
    return format_version;
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
    // The image records the function count as a u32.
    if (from.functions.size() > 0xFFFFFFFFull) {
        last_write_error = "refusing to write a program with more than 4,294,967,295 functions - "
                           "the image records the count as a 32-bit number";
        return {};
    }
    // The reader's constant rule (`why_not_a_constant`) is enforced here too,
    // so an image this build cannot load is caught when it is written, not
    // when it is loaded. It also checks that the compiler still never emits a
    // NaN constant, which the reader's refusal relies on.
    for (std::uint32_t fi = 0; fi < from.functions.size(); ++fi) {
        const function_proto & fn = from.functions[fi];
        for (std::size_t ci = 0; ci < fn.constants.size(); ++ci) {
            if (const char * why = why_not_a_constant(fn.constants[ci].bits()); why != nullptr) {
                last_write_error = "refusing to write function " + std::to_string(fi) +
                                   ": constant " + std::to_string(ci) + " " + why;
                return {};
            }
        }
    }

    // One pool for names and one for strings, each entry stored once. Names
    // repeat heavily (Babylon.js: 166,396 entries, 24,172 unique, 2686 KB to
    // 495 KB); string literals much less, but pooling them costs nothing.
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
        // A local's name goes in the same pool. `n`, `i`, `this` and every
        // method name a bundle uses are already there as property names, so a
        // separate pool would store most of them twice.
        for (const local_desc & d : fn.locals) { (void)intern(names, name_at, d.name); }
    }

    sink out;
    out.u32(magic);
    out.u32(format_version);
    out.u64(image_fingerprint());
    out.u32(static_cast<std::uint32_t>(option));
    // Whether this was compiled as a classic script or a module. The same text
    // compiles differently either way and the source hash covers only the
    // text, so without this a module image would load for a classic script.
    out.u8(static_cast<std::uint8_t>(from.kind));
    // Hashed from the program's own source, before any decision about keeping
    // it: an image that drops the text must still say what it was built from.
    out.u64(image_source_hash(from.source));

    out.u32(static_cast<std::uint32_t>(names.size()));
    for (const std::string & s : names) { out.text(s); }
    out.u32(static_cast<std::uint32_t>(strings.size()));
    for (const std::string & s : strings) { out.text(s); }

    // Order is preserved for all three lists, and for two of them it is
    // observable: imports fix depth-first instantiation and therefore the order
    // of modules' top-level side effects, and the first claim on a re-exported
    // name wins, so swapping two re-exports changes which cell a name resolves
    // to.
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
    out.u32(static_cast<std::uint32_t>(from.hoisted_vars.size()));
    for (const std::string & s : from.hoisted_vars) { out.text(s); }
    out.u32(static_cast<std::uint32_t>(from.lexical_names.size()));
    for (const std::string & s : from.lexical_names) { out.text(s); }
    out.u32(static_cast<std::uint32_t>(from.function_names.size()));
    for (const std::string & s : from.function_names) { out.text(s); }
    out.u32(static_cast<std::uint32_t>(from.annex_b_names.size()));
    for (const std::string & s : from.annex_b_names) { out.text(s); }

    out.text(option == image_option::keep_source ? std::string_view{from.source}
                                                 : std::string_view{});

    out.u32(static_cast<std::uint32_t>(from.functions.size()));
    for (const function_proto & fn : from.functions) {
        out.text(fn.module);
        out.text(fn.name);
        out.text(fn.inferred_name);
        out.u16(fn.param_count);
        out.u16(fn.length);
        out.u16(fn.frame_size);
        out.u8((fn.is_arrow ? 1u : 0u) | (fn.is_strict ? 2u : 0u));
        // Bits 0-1: 0 not a generator, 1 generator, 2 async generator (the one
        // case where the VM reads is_async). Bit 2 is eager_prologue, which
        // only a generator has.
        out.u8((fn.is_generator ? (fn.is_async ? 2u : 1u) : 0u) |
               (fn.is_generator && fn.eager_prologue ? 4u : 0u));
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
        // The debug side tables. Both may be empty (a compiler built without
        // CTBROWSER_SCRIPT_DEBUG_NAMES produces none); the counts are written
        // unconditionally so the format has one shape.
        out.u32(static_cast<std::uint32_t>(fn.locals.size()));
        for (const local_desc & d : fn.locals) {
            out.u32(intern(names, name_at, d.name));
            out.u16(d.reg);
            out.u32(d.first_pc);
            out.u32(d.last_pc);
            out.u8(d.boxed ? 1u : 0u);
        }
        // Source offsets are kept only with the source (see
        // image_option::drop_source): an offset into absent text is useless.
        const bool keep_offsets = option == image_option::keep_source;
        out.u32(keep_offsets ? static_cast<std::uint32_t>(fn.code_offsets.size()) : 0u);
        if (keep_offsets) {
            for (const std::uint32_t offset : fn.code_offsets) { out.u32(offset); }
        }
        // The rethrow table is meaningful only with the offsets.
        out.u32(keep_offsets ? static_cast<std::uint32_t>(fn.rethrows.size()) : 0u);
        if (keep_offsets) {
            for (const std::uint32_t at : fn.rethrows) { out.u32(at); }
        }
    }
    return std::move(out.bytes);
}

} // namespace ctbrowser::script
