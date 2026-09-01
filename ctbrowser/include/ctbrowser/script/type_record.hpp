#pragma once
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <ctbrowser/script/bytecode.hpp>
#include <ctbrowser/script/value.hpp>

// THE TYPE ORACLE - what the interpreter knows and a static inference has to
// be checked against. Phase 54B of ctcompile's native backend.
//
// A static type inference is either right or it is a miscompiler, and arguing
// about which is not a method. The interpreter, meanwhile, knows every value's
// REAL type at run time and has been running the three corpora for months. So:
// record what it sees, per (function, register), and any claim an analysis
// makes can be checked against reality.
//
// TWO NUMBERS COME OUT OF THAT AND THEY MUST NEVER BE CONFLATED:
//
//   SOUNDNESS   did the inference ever claim a type NARROWER than what was
//               observed? `i32` where the interpreter saw 0.5 is a DEFECT, and
//               the checker names the function and the register.
//   PRECISION   how often did it beat "boxed"? Low precision is a backlog
//               item. It is not a bug.
//
// AND A THIRD THING THAT IS NEITHER: a register no execution ever reached has
// NO observation, and "unobserved" is NOT "any type". Counting an unobserved
// register as sound makes the soundness number a lie; counting it as imprecise
// makes the precision number one. They are reported separately.
//
// WHAT A RECORDING IS NOT. It is a witness, not a proof: it says the analysis
// is wrong when it disagrees, and says nothing at all when it agrees on paths
// no run took. That asymmetry is the whole value - a counterexample generator
// costs one run and settles an argument that otherwise costs a phase.
//
// OFF unless a recorder is installed (see `set_active_type_recorder`), and
// compiled out entirely with -DCTBROWSER_SCRIPT_RECORD_TYPES=0.

namespace ctbrowser::script {

// --- WHICH OPERATIONS DEFINE A REGISTER ------------------------------------
//
// Read out of bytecode_opcodes.def's `writes_a` column rather than written
// down here. Phase 0's inventory exists precisely so that a table like this
// cannot drift from the interpreter: the dispatch loop builds its jump table
// from the same file.
namespace detail {
#define CT_OPCODE(name_, a_kind_, b_kind_, c_kind_, writes_a_, ...) (writes_a_) != 0,
inline constexpr bool opcode_writes_a_table[] = {
#include <ctbrowser/script/bytecode_opcodes.def>
};
#undef CT_OPCODE
static_assert(std::size(opcode_writes_a_table) == opcode_count,
              "bytecode_opcodes.def must list every opcode exactly once");

// AND `a` MUST BE A REGISTER WHEREVER IT IS WRITTEN. The recorder reads
// registers[base + in.a] after the handler ran; for an opcode encoding `a` as
// a count or an index that address is not a register at all and the
// observation would be of some unrelated slot. `set_upvalue` is the row that
// makes this a real question - it takes a count in `a` - and it answers
// writes_a=0, which is why this holds today rather than by luck.
#define CT_OPCODE(name_, a_kind_, b_kind_, c_kind_, writes_a_, ...)                                \
    (((writes_a_) == 0) || std::string_view{#a_kind_} == std::string_view{"reg"}) &&
inline constexpr bool every_writer_encodes_a_as_a_register =
#include <ctbrowser/script/bytecode_opcodes.def>
    true;
#undef CT_OPCODE
static_assert(every_writer_encodes_a_as_a_register,
              "an opcode with writes_a=1 must encode operand a as `reg` - the recorder reads "
              "registers[base + a] after the handler runs");
} // namespace detail

[[nodiscard]] constexpr bool opcode_writes_a(op code) noexcept {
    return detail::opcode_writes_a_table[static_cast<std::size_t>(code)];
}
// HOW MANY OF THEM THERE ARE, so a test can assert a number rather than
// believe one. 68 of 93 on 2026-09-01.
inline constexpr std::size_t opcode_writer_count = [] {
    std::size_t n = 0;
    for (const bool w : detail::opcode_writes_a_table) {
        if (w) { ++n; }
    }
    return n;
}();

// --- THE OBSERVED TYPE, IN THE INTERPRETER'S OWN VOCABULARY -----------------
//
// `heap_kind` and a number SHAPE, and deliberately nothing else. Mapping this
// onto whatever `!ctnative.*` type lattice Phase 53 settles on is a later,
// separate job: a recording that already spoke the compiler's dialect would
// have to be re-recorded every time the dialect moved, and would beg the
// question the oracle exists to answer.
inline constexpr std::uint32_t obs_undefined = 1u << 0;
inline constexpr std::uint32_t obs_null = 1u << 1;
inline constexpr std::uint32_t obs_boolean = 1u << 2;
inline constexpr std::uint32_t obs_number = 1u << 3;
inline constexpr unsigned obs_heap_shift = 4;
// One bit per heap_kind, DERIVED from the enum so a new kind cannot silently
// share a bit with an old one.
[[nodiscard]] constexpr std::uint32_t obs_heap(heap_kind k) noexcept {
    return 1u << (obs_heap_shift + static_cast<unsigned>(k));
}
static_assert(obs_heap_shift + static_cast<unsigned>(heap_kind::coroutine) < 32,
              "heap_kind has outgrown the observation word");

// THE NUMBER SHAPE. Every JS number is a double; the question a native backend
// asks is whether THIS register's doubles were ever anything an `int32_t` or a
// `double` cannot hold, and each of these flags is one way the answer is no.
inline constexpr std::uint32_t num_seen = 1u << 0;
inline constexpr std::uint32_t num_fractional = 1u << 1;   // finite, not integral
inline constexpr std::uint32_t num_wide = 1u << 2;         // integral, outside int32
inline constexpr std::uint32_t num_negative_zero = 1u << 3; // -0, which int32 cannot represent
inline constexpr std::uint32_t num_nan = 1u << 4;
inline constexpr std::uint32_t num_infinite = 1u << 5;

// EVERY WAY A NUMBER CAN FAIL TO BE AN i32. Named once, because the checker
// re-derives it and the two must agree.
inline constexpr std::uint32_t num_not_i32 =
    num_fractional | num_wide | num_negative_zero | num_nan | num_infinite;

struct register_observation {
    std::uint64_t defs = 0;      // how many times this register was defined
    std::uint32_t kinds = 0;     // obs_* bits
    std::uint32_t numbers = 0;   // num_* bits, meaningful only with obs_number

    void observe(value v) noexcept;
    [[nodiscard]] bool observed() const noexcept { return defs != 0; }
};

struct function_observation {
    std::uint32_t program = 0;
    std::uint32_t index = 0; // into program::functions
    std::string name;
    std::uint16_t param_count = 0;
    std::uint16_t frame_size = 0;
    std::uint64_t entries = 0; // times a frame for this body was entered
    std::vector<register_observation> regs;
};

struct program_observation {
    std::uint64_t source_hash = 0; // FNV-1a over program::source - the key
    std::size_t source_size = 0;
    std::string label; // functions[0].module, or empty for a classic script
    std::size_t first = 0;
    std::size_t count = 0; // functions [first, first + count) in the recorder
};

// THE RECORDER. One per process is the normal arrangement; it merges across
// contexts because it is keyed by the PROGRAM's source hash, not by any
// pointer that only means something inside one VM.
class type_recorder {
public:
    // ONE INTERPRETER STEP, called from the dispatch loop after the
    // instruction has been fetched and before its handler runs.
    //
    // The observation is therefore of the PREVIOUS instruction's destination -
    // a def is only readable once the handler that made it has finished, and
    // `op::call` does not finish until the callee returns. Deferring by one
    // step in the same frame is what makes a call's result observable at all.
    void step(std::size_t depth, const program * owner, const function_proto * proto,
              std::size_t base, std::size_t pc, std::uint16_t argc, instruction in,
              std::span<const value> registers);

    // Write the recording. Returns false if the file could not be opened.
    [[nodiscard]] bool write(const std::string & path) const;

    // --- what it saw, for a caller that wants to assert on it ---------------
    [[nodiscard]] const std::vector<function_observation> & functions() const noexcept {
        return functions_;
    }
    [[nodiscard]] const std::vector<program_observation> & programs() const noexcept {
        return programs_;
    }
    // Observations that were armed and then thrown away because the next
    // instruction in that frame was not the one after the def - an unwind, a
    // suspension, or a frame that never ran another instruction. Reported
    // rather than hidden: it is the recorder's own miss rate.
    [[nodiscard]] std::uint64_t dropped_defs() const noexcept { return dropped_; }
    [[nodiscard]] std::uint64_t recorded_defs() const noexcept { return recorded_; }
    // Frames whose function_proto is not inside the program that owns the
    // running closure. Should be zero; a non-zero count means the recording is
    // incomplete and says by how much.
    [[nodiscard]] std::uint64_t orphan_frames() const noexcept { return orphans_; }

private:
    struct pending {
        const function_proto * proto = nullptr;
        std::size_t base = 0;
        std::size_t expect_pc = 0;
        std::uint16_t reg = 0;
        bool live = false;
    };

    [[nodiscard]] function_observation * resolve(const program * owner,
                                                 const function_proto * proto);
    [[nodiscard]] std::uint32_t intern(const program * owner);

    std::vector<program_observation> programs_;
    std::vector<function_observation> functions_;
    // program pointer -> index in programs_. A pointer is only a key WITHIN one
    // process, which is all this is: the source hash is what leaves the file.
    std::vector<std::pair<const program *, std::uint32_t>> by_pointer_;
    std::vector<pending> pend_;
    // Single-entry memo: consecutive instructions are almost always in the same
    // body, so this removes the lookup from the common step. AN INDEX, NOT A
    // POINTER - interning a program appends to `functions_`, and a pointer
    // memoised before that reallocation is a dangling one.
    const function_proto * last_proto_ = nullptr;
    std::size_t last_index_ = 0;
    bool last_valid_ = false;
    std::uint64_t dropped_ = 0;
    std::uint64_t recorded_ = 0;
    std::uint64_t orphans_ = 0;
};

// THE ACTIVE RECORDER, read by `context` when it is constructed.
//
// Process-wide rather than an argument, because the thing that has to be
// instrumented is a whole PAGE - a `shell::browser` builds its own context,
// its workers build their own, and a module graph is several programs. Handing
// a recorder down through all of that would be a change to every layer in
// between for a mode that is off in every shipped build.
//
// Set it BEFORE the context that should record is constructed.
void set_active_type_recorder(type_recorder * recorder) noexcept;
[[nodiscard]] type_recorder * active_type_recorder() noexcept;

// Whether the recording hook was compiled in at all. A build with
// -DCTBROWSER_SCRIPT_RECORD_TYPES=0 answers false and every recording it
// produces is empty - which a caller must be able to tell from a program that
// genuinely ran nothing.
[[nodiscard]] bool type_recording_enabled() noexcept;

// The program key the recording is written under, and the checker re-derives.
// FNV-1a over the source text: two programs compiled from the same bytes are
// the same program, whoever ran them and in whatever order.
[[nodiscard]] std::uint64_t program_source_hash(const program & prog) noexcept;

} // namespace ctbrowser::script
