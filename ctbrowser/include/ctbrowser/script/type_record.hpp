#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
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
inline constexpr std::uint32_t num_fractional = 1u << 1;    // finite, not integral
inline constexpr std::uint32_t num_wide = 1u << 2;          // integral, outside int32
inline constexpr std::uint32_t num_negative_zero = 1u << 3; // -0, which int32 cannot represent
inline constexpr std::uint32_t num_nan = 1u << 4;
inline constexpr std::uint32_t num_infinite = 1u << 5;

// EVERY WAY A NUMBER CAN FAIL TO BE AN i32. Named once, because the checker
// re-derives it and the two must agree.
inline constexpr std::uint32_t num_not_i32 =
    num_fractional | num_wide | num_negative_zero | num_nan | num_infinite;

struct register_observation {
    std::uint64_t defs = 0;    // how many times this register was defined
    std::uint32_t kinds = 0;   // obs_* bits
    std::uint32_t numbers = 0; // num_* bits, meaningful only with obs_number

    void observe(value v) noexcept;
    [[nodiscard]] bool observed() const noexcept { return defs != 0; }
};

// --- THE ESCAPE HALF - ctcompile Phase 55O -----------------------------------
//
// The second question the native backend has to have answered before it can
// give an allocation an RAII lifetime: is every object born at this site
// unreachable from every GC root once the activation that made it has
// returned or unwound? `confined` means a stack object, a `unique_ptr` or a
// `shared_ptr` is on the table; `escapes` means the site is outside what the
// native subset can own and is diagnosed. A static escape analysis answers
// that per site, and this is the witness it is checked against.
//
// THE COLLECTOR IS THE REFERENCE SEMANTICS HERE, NOT A RUNTIME. The interpreter
// knows, at the moment a frame ends, exactly which objects are still reachable
// from which root - that is what a precise mark phase computes - so the oracle
// borrows the collector's own root walk, bounded at the popped frame's base,
// marks, classifies, and unmarks. It never sweeps: it observes and does not
// collect, and the only VM state it touches is the `marked` bit that collect()
// itself treats as transient.
//
// WHAT IT OBSERVES IS RETENTION AT FRAME EXIT, NOT TRANSIT. An object handed
// to a callee that drops it before the caller returns reads `confined` here,
// and that transit is still a real escape for a by-value lowering. So the
// oracle can MISS an escape and can never invent one; the call-argument rows
// of the analysis are justified by the VM source and by negative unit rows,
// and the self-test pins the blind spot as a row (`transit`) so nobody reads
// retention-at-exit as coverage of the call sink.

// THE ROOT INVENTORY'S LABELS. One per row of ctcompile's GCRoots.def, in the
// order `context::each_root` visits them - which is the order collect() has
// always marked them in. An escaped object is reported with the FIRST label
// that reached it, which turns a soundness violation into a diagnosis: `via
// globals` and `via temporaries` (the in-flight return value) are different
// bugs in different places.
//
// Duplicated by name in tools/check/escape-oracle.py, on purpose: the checker
// reads its own definitions rather than the recording's, so a drift shows up
// as an unknown label rather than as a quietly different answer.
#define CTBROWSER_ROOT_LABELS(X)                                                                   \
    X(globals)                                                                                     \
    X(registers)                                                                                   \
    X(current_this)                                                                                \
    X(pending_new_target)                                                                          \
    X(pending_closure)                                                                             \
    X(frame_closure)                                                                               \
    X(frame_receiver)                                                                              \
    X(frame_arguments)                                                                             \
    X(frame_async_promise)                                                                         \
    X(frame_new_target)                                                                            \
    X(microtasks)                                                                                  \
    X(module_exports)                                                                              \
    X(module_namespace)                                                                            \
    X(thrown)                                                                                      \
    X(temporaries)                                                                                 \
    X(prototypes)                                                                                  \
    X(string_cache)                                                                                \
    X(bigint_cache)                                                                                \
    X(external)

enum class root_label : std::uint8_t {
#define CT_ROOT_LABEL(name_) name_,
    CTBROWSER_ROOT_LABELS(CT_ROOT_LABEL)
#undef CT_ROOT_LABEL
};
inline constexpr std::string_view root_label_names[] = {
#define CT_ROOT_LABEL(name_) #name_,
    CTBROWSER_ROOT_LABELS(CT_ROOT_LABEL)
#undef CT_ROOT_LABEL
};
inline constexpr std::size_t root_label_count = std::size(root_label_names);
[[nodiscard]] constexpr std::string_view root_label_name(root_label l) noexcept {
    return root_label_names[static_cast<std::size_t>(l)];
}

// THE TRACKED KINDS: the four heap kinds a native lowering could give an RAII
// lifetime to. Strings, symbols, bigints, natives, proxies and coroutines are
// never claimed and never recorded.
[[nodiscard]] constexpr bool escape_tracked(heap_kind k) noexcept {
    return k == heap_kind::object || k == heap_kind::array || k == heap_kind::function ||
           k == heap_kind::cell;
}
[[nodiscard]] constexpr std::string_view site_kind_name(heap_kind k) noexcept {
    switch (k) {
    case heap_kind::object: return "obj";
    case heap_kind::array: return "arr";
    case heap_kind::function: return "fn";
    case heap_kind::cell: return "cell";
    default: return "?";
    }
}

// WHICH OPCODES ARE SITES: the ones that allocate a tracked kind AT THEIR OWN
// PC, every time they run (or, for `iterable`, every time the source is not
// already an array). This is the recording's DENOMINATOR for the escape half,
// the way `frame` is for registers: every function's static inventory is
// written whether or not it ran, so a checker can tell "this site was never
// reached" from "there is no such site" - and a stub that claims every site
// confined has something to be UNOBSERVED on.
//
// Each of these has allocates=1 in bytecode_opcodes.def, and the static_assert
// below holds it to that. The list is NOT every allocates=1 row - `call`,
// `construct`, `add` and the property ops can all allocate on some path, at a
// pc the analysis never claims - and what they make lands on the calling
// instruction's pc as an UNCLAIMED site, by design.
[[nodiscard]] constexpr std::optional<heap_kind> opcode_site_kind(op code) noexcept {
    switch (code) {
    case op::new_object: return heap_kind::object;
    case op::new_array:
    case op::make_arguments:
    case op::gather_rest:
    case op::own_keys:
    case op::iterable: return heap_kind::array;
    case op::closure: return heap_kind::function;
    case op::new_cell: return heap_kind::cell;
    default: return std::nullopt;
    }
}
namespace detail {
#define CT_OPCODE(name_, a_kind_, b_kind_, c_kind_, writes_a_, allocates_, ...) (allocates_) != 0,
inline constexpr bool opcode_allocates_table[] = {
#include <ctbrowser/script/bytecode_opcodes.def>
};
#undef CT_OPCODE
inline constexpr bool every_site_opcode_allocates = [] {
    for (std::size_t i = 0; i < opcode_count; ++i) {
        if (opcode_site_kind(static_cast<op>(i)).has_value() && !opcode_allocates_table[i]) {
            return false;
        }
    }
    return true;
}();
static_assert(every_site_opcode_allocates,
              "opcode_site_kind names an opcode whose bytecode_opcodes.def row says it does not "
              "allocate - one of the two tables is wrong");
} // namespace detail

// THE PC OF AN ALLOCATION MADE BEFORE A FRAME'S FIRST INSTRUCTION - `ip == 0`,
// so `ip - 1` has no meaning. Written as `prologue` in the file.
inline constexpr std::uint32_t prologue_pc = 0xFFFF'FFFFu;

struct static_site {
    std::uint32_t pc = 0;
    heap_kind kind = heap_kind::object;
};

// ONE SITE'S TALLY. `made = confined + escaped + unresolved + unchecked`, always:
//
//   confined    unreachable from every root when its frame ended
//   escaped     reachable; `routes` says through which root category first
//   unresolved  swept before its frame ended - a witness must not guess, so a
//               store-then-overwrite is never read as confined
//   unchecked   its frame ended through a path with no hook (FrameEnds.def),
//               or the per-function budget was spent - never confined
struct site_observation {
    std::uint32_t pc = 0;
    heap_kind kind = heap_kind::object;
    std::uint64_t made = 0;
    std::uint64_t confined = 0;
    std::uint64_t escaped = 0;
    std::uint64_t unresolved = 0;
    std::uint64_t unchecked = 0;
    std::array<std::uint64_t, root_label_count> routes{};
};

struct function_observation {
    std::uint32_t program = 0;
    std::uint32_t index = 0; // into program::functions
    std::string name;
    std::uint16_t param_count = 0;
    std::uint16_t frame_size = 0;
    std::uint64_t entries = 0; // times a frame for this body was entered
    std::vector<register_observation> regs;
    // The escape half: the static inventory (every function, whether it ran or
    // not), the sites that were actually adjudicated, and how many frame ends
    // of this body have been checked against the budget.
    std::vector<static_site> allocs;
    std::vector<site_observation> sites;
    std::uint64_t checks = 0;
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

    // --- the escape half ----------------------------------------------------
    //
    // THE BUDGET: how many frame ends of ONE function body are adjudicated
    // before the rest are reported UNCHECKED. Each check is a bounded mark of
    // the reachable heap plus one heap walk to unmark, and under `--script`
    // nothing ever collects, so the heap only grows; a body entered a hundred
    // thousand times would cost a hundred thousand marks. Over budget is never
    // confined. 0 means unlimited.
    void set_escape_budget(std::uint64_t per_function) noexcept { budget_ = per_function; }
    [[nodiscard]] std::uint64_t escape_budget() const noexcept { return budget_; }
    // THE DEAD WINDOW INCLUDED, for the A/B the self-test runs: an unbounded
    // walk marks the popped frame's own registers too, so it must report a
    // SUPERSET of the bounded walk's escapes. If a future register-allocator
    // change ever keeps a live value above a call base, that superset stops
    // being one and this is the number that goes red.
    void set_unbounded(bool on) noexcept { unbounded_ = on; }
    [[nodiscard]] bool unbounded() const noexcept { return unbounded_; }

    // ONE ALLOCATION AWAITING ITS FRAME'S END. `object` is a valid pointer
    // exactly until `dead` is set by `freed`, and is never dereferenced after.
    struct escape_record {
        heap_object * object = nullptr;
        std::size_t function = 0; // index into functions_
        std::uint32_t pc = 0;
        heap_kind kind = heap_kind::object;
        bool dead = false;
        bool routed = false;
        root_label route = root_label::globals;
    };

    // The context's side of the protocol - see context::note_allocation and
    // context::record_frame_pop in type_record.cpp. A frame's identity is a
    // serial the context asks for lazily at its first tracked allocation.
    [[nodiscard]] std::uint64_t fresh_serial() noexcept { return ++next_serial_; }
    void allocated(heap_object * p, const program * owner, const function_proto * proto,
                   std::uint32_t pc, std::uint64_t serial);
    void unframed_allocation() noexcept { ++unframed_; }
    void freed(heap_object * p);
    void note_pop() noexcept { ++pops_; }
    void note_unwind() noexcept { ++unwinds_; }
    // Hand over a frame's records for adjudication, appended to `out`. Applies
    // the budget: an over-budget frame's records are folded as UNCHECKED here
    // and nothing is appended.
    void begin_check(std::uint64_t serial, std::vector<escape_record> & out);
    // Fold adjudicated records into their sites. The caller has marked: a
    // record whose object is `marked` escaped by `route`, a dead one is
    // UNRESOLVED, anything else is confined.
    void finish_check(const std::vector<escape_record> & records);

    // Every function's sites as the file reports them, indexed like
    // functions(): the adjudicated tallies plus every record still pending
    // folded as UNCHECKED (its frame ended through a hook-less path, or never
    // ended), sorted by (pc, kind). The writer and the in-memory checker both
    // read this, so the two cannot disagree about what "pending" means.
    [[nodiscard]] std::vector<std::vector<site_observation>> all_sites() const;

    [[nodiscard]] std::uint64_t pops() const noexcept { return pops_; }
    [[nodiscard]] std::uint64_t unwinds() const noexcept { return unwinds_; }
    [[nodiscard]] std::uint64_t checks() const noexcept { return checks_; }
    [[nodiscard]] std::uint64_t unframed() const noexcept { return unframed_; }
    [[nodiscard]] std::uint64_t unresolved() const noexcept { return unresolved_; }
    [[nodiscard]] std::uint64_t pending_records() const noexcept { return pending_; }

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

    // --- the escape half's side tables ----------------------------------------
    //
    // A SIDE TABLE KEYED BY heap_object*, NOT A FIELD ON heap_object. A field
    // whose existence depends on a build flag in a public header is the ODR
    // trap type_record.cpp's record_step comment and vm.hpp's `recorder_`
    // comment both name; a field present in every build is eight bytes on
    // every string the engine ever makes. `alloc_` maps a live tracked object
    // to its record; `by_serial_` holds each frame's records until the frame
    // ends. `freed` erases the `alloc_` entry and flags the record dead, so a
    // later allocation at the same address gets a fresh entry and no record is
    // ever read through a stale pointer.
    struct alloc_slot {
        std::uint64_t serial = 0;
        std::size_t slot = 0;
    };
    void fold(const escape_record & r, root_label route, bool escaped, bool unchecked);
    [[nodiscard]] site_observation & site_for(std::size_t function, std::uint32_t pc,
                                              heap_kind kind);

    std::unordered_map<const heap_object *, alloc_slot> alloc_;
    std::unordered_map<std::uint64_t, std::vector<escape_record>> by_serial_;
    std::uint64_t next_serial_ = 0;
    std::uint64_t budget_ = 0;
    bool unbounded_ = false;
    std::uint64_t pops_ = 0;
    std::uint64_t unwinds_ = 0;
    std::uint64_t checks_ = 0;
    std::uint64_t unframed_ = 0;
    std::uint64_t unresolved_ = 0;
    std::uint64_t pending_ = 0; // records currently awaiting a frame end
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
