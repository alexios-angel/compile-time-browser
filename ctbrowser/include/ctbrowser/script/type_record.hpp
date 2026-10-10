//===- type_record.hpp - Interpreter type and escape oracle -----*- C++ -*-===//
//
// The type oracle: records the types the interpreter actually observes, per
// (function, register), and whether allocations escape their frame, so that
// ctcompile's static type inference and escape analysis can be checked
// against real runs (the Bootstrap, p5 and Phaser bundles among them). A
// static type inference is either right or it miscompiles; the interpreter
// knows every value's real type at run time.
//
// Two numbers come out of that and they must never be conflated:
//
//   soundness   did the inference ever claim a type narrower than what was
//               observed? `i32` where the interpreter saw 0.5 is a defect,
//               and the checker names the function and the register.
//   precision   how often did it beat "boxed"? Low precision is a backlog
//               item, not a bug.
//
// A third thing is neither: a register no execution ever reached has no
// observation, and "unobserved" is not "any type". Counting an unobserved
// register as sound would make the soundness number a lie; counting it as
// imprecise would do the same to the precision number. They are reported
// separately.
//
// A recording is a witness, not a proof: it shows the analysis is wrong when
// it disagrees, and says nothing when it agrees on paths no run took. That
// asymmetry is the value: one run produces counterexamples.
//
// Recording is off unless a recorder is installed (see
// `set_active_type_recorder`).
//
//===----------------------------------------------------------------------===//

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

namespace ctbrowser::script {

// --- Which operations define a register ----------------------------===//
//
// Read out of bytecode_opcodes.def's `writes_a` column rather than written
// down here, so this table cannot drift from the interpreter: the dispatch
// loop builds its jump table from the same file.
namespace detail {
#define CT_OPCODE(name_, a_kind_, b_kind_, c_kind_, writes_a_, ...) (writes_a_) != 0,
inline constexpr bool opcode_writes_a_table[] = {
#include <ctbrowser/script/bytecode_opcodes.def>
};
#undef CT_OPCODE
static_assert(std::size(opcode_writes_a_table) == opcode_count,
              "bytecode_opcodes.def must list every opcode exactly once");

// Operand `a` must be a register wherever it is written. The recorder reads
// registers[base + in.a] after the handler ran; for an opcode encoding `a` as
// a count or an index that address is not a register at all and the
// observation would be of some unrelated slot. `set_upvalue` takes a count in
// `a`, and its row says writes_a=0, so this holds.
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

/// Whether `code` writes a value to the register named by operand `a`.
[[nodiscard]] constexpr bool opcode_writes_a(op code) noexcept {
    return detail::opcode_writes_a_table[static_cast<std::size_t>(code)];
}
/// The number of opcodes that write operand `a`. ctcompile's oracle test
/// asserts the exact value, so adding a writer opcode must update that test.
inline constexpr std::size_t opcode_writer_count = [] {
    std::size_t n = 0;
    for (const bool w : detail::opcode_writes_a_table) {
        if (w) { ++n; }
    }
    return n;
}();

// --- The observed type, in the interpreter's own vocabulary ---------===//
//
// `heap_kind` and a number shape, and deliberately nothing else. Mapping these
// onto the compiler's `!ctnative.*` types is the checker's job, so a recording
// does not have to be redone whenever the compiler's type system changes, and
// does not presuppose the answer the oracle exists to check.
inline constexpr std::uint32_t obs_undefined = 1u << 0;
inline constexpr std::uint32_t obs_null = 1u << 1;
inline constexpr std::uint32_t obs_boolean = 1u << 2;
inline constexpr std::uint32_t obs_number = 1u << 3;
inline constexpr unsigned obs_heap_shift = 4;
/// One bit per heap_kind, derived from the enum so a new kind cannot silently
/// share a bit with an old one.
[[nodiscard]] constexpr std::uint32_t obs_heap(heap_kind k) noexcept {
    return 1u << (obs_heap_shift + static_cast<unsigned>(k));
}
static_assert(obs_heap_shift + static_cast<unsigned>(heap_kind::coroutine) < 32,
              "heap_kind has outgrown the observation word");

// The number shape. Every JS number is a double; the question a native
// backend asks is whether this register's doubles were ever anything an
// `int32_t` or a `double` cannot hold, and each of these flags records one
// way the answer is no. num_seen is set for every number observed.
inline constexpr std::uint32_t num_seen = 1u << 0;
inline constexpr std::uint32_t num_fractional = 1u << 1;    // finite, not integral
inline constexpr std::uint32_t num_wide = 1u << 2;          // integral, outside int32
inline constexpr std::uint32_t num_negative_zero = 1u << 3; // -0, which int32 cannot represent
inline constexpr std::uint32_t num_nan = 1u << 4;
inline constexpr std::uint32_t num_infinite = 1u << 5;

/// Every way a number can fail to be an i32. Named once, because the checker
/// re-derives it and the two must agree.
inline constexpr std::uint32_t num_not_i32 =
    num_fractional | num_wide | num_negative_zero | num_nan | num_infinite;

/// Everything observed about one register of one function.
struct register_observation {
    std::uint64_t defs = 0;    // how many times this register was defined
    std::uint32_t kinds = 0;   // obs_* bits
    std::uint32_t numbers = 0; // num_* bits, meaningful only with obs_number

    /// Records one definition of the register with value `v`.
    void observe(value v) noexcept;
    [[nodiscard]] bool observed() const noexcept { return defs != 0; }
};

// --- Escape observation ---------------------------------------------===//
//
// Records, per allocation site, whether objects made there were still
// reachable from a GC root when the frame that made them ended.
//
// The native backend needs this answered before it can give an allocation an
// RAII lifetime: is every object born at this site unreachable from every GC
// root once the activation that made it has returned or unwound? `confined`
// means a stack object, a `unique_ptr` or a `shared_ptr` is possible;
// `escapes` means the site is outside what the native subset can own and is
// diagnosed. A static escape analysis answers that per site, and this is the
// witness it is checked against.
//
// The collector is the reference semantics here, not a runtime mechanism. The
// interpreter knows, at the moment a frame ends, exactly which objects are
// still reachable from which root (that is what a precise mark phase
// computes), so the oracle borrows the collector's own root walk, bounded at
// the popped frame's base, marks, classifies, and unmarks. It never sweeps: it
// observes and does not collect, and the only VM state it touches is the
// `marked` bit that collect() itself treats as transient.
//
// What it observes is retention at frame exit, not transit. An object handed
// to a callee that drops it before the caller returns reads `confined` here,
// yet that transit is still a real escape for a by-value lowering. So the
// oracle can miss an escape and can never invent one; the call-argument rows
// of the analysis are justified by the VM source and by negative unit rows,
// and the self-test pins the blind spot as a row (`transit`) so nobody reads
// retention-at-exit as coverage of the call sink.

/// The root inventory's labels, one per root, in the order
/// `context::each_root` visits them, which is the order collect() marks them in.
/// An escaped object is reported with the first label that reached it, which
/// turns a soundness violation into a diagnosis: `via globals` and `via
/// temporaries` (the in-flight return value) are different bugs in different
/// places.
///
/// Duplicated by name in tools/check/escape-oracle.py, on purpose: the checker
/// reads its own definitions rather than the recording's, so a drift shows up
/// as an unknown label rather than as a quietly different answer.
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

/// The tracked kinds: the four heap kinds a native lowering could give an RAII
/// lifetime to. Strings, symbols, bigints, natives, proxies and coroutines are
/// never claimed and never recorded.
[[nodiscard]] constexpr bool escape_tracked(heap_kind k) noexcept {
    return k == heap_kind::object || k == heap_kind::array || k == heap_kind::function ||
           k == heap_kind::cell;
}
/// The short name of a tracked kind as the recording file spells it.
[[nodiscard]] constexpr std::string_view site_kind_name(heap_kind k) noexcept {
    switch (k) {
    case heap_kind::object: return "obj";
    case heap_kind::array: return "arr";
    case heap_kind::function: return "fn";
    case heap_kind::cell: return "cell";
    default: return "?";
    }
}

/// Which opcodes are allocation sites: the ones that allocate a tracked kind at
/// their own pc, every time they run (or, for `iterable`, every time the source
/// is not already an array). This is the recording's denominator for escape
/// observation, the way `frame` is for registers: every function's static
/// inventory is written whether or not it ran, so a checker can tell "this site
/// was never reached" from "there is no such site", and a stub that claims
/// every site confined has something to be unobserved on.
///
/// Each of these has allocates=1 in bytecode_opcodes.def, and the static_assert
/// below holds it to that. The list is not every allocates=1 row: `call`,
/// `construct`, `add` and the property ops can all allocate on some path, at a
/// pc the analysis never claims, and what they make lands on the calling
/// instruction's pc as an unclaimed site, by design.
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

/// The pc recorded for an allocation made before a frame's first instruction
/// (`ip == 0`, so `ip - 1` has no meaning). Written as `prologue` in the file.
inline constexpr std::uint32_t prologue_pc = 0xFFFF'FFFFu;

/// The pc recorded for an allocation in a compiled frame, which has no
/// bytecode pc: its `ip` is either zero or an encoded catch-pad id. Keeps its
/// allocations outside the bytecode inventory, even when that pad's low bits
/// happen to name a real allocation instruction. In the recording file (format
/// version 2) it is written as an ordinary numeric pc. It stays unclaimed even
/// when a compiled frame's normal-return handoff allows its escapes to be
/// observed.
inline constexpr std::uint32_t compiled_pc = 0xFFFF'FFFEu;

/// One allocation site from a function's static inventory.
struct static_site {
    std::uint32_t pc = 0;
    heap_kind kind = heap_kind::object;
};

/// One site's tally. `made = confined + escaped + unresolved + unchecked`,
/// always:
///
///   confined    unreachable from every root when its frame ended
///   escaped     reachable; `routes` says through which root category first
///   unresolved  swept before its frame ended; a witness must not guess, so a
///               store-then-overwrite is never read as confined
///   unchecked   its frame ended through a path with no hook
///               (ctcompile/include/ctcompile/JavaScript/FrameEnds.def), or
///               the per-function budget was spent; never confined
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

/// Everything observed about one function body.
struct function_observation {
    std::uint32_t program = 0;
    std::uint32_t index = 0; // into program::functions
    std::string name;
    std::uint16_t param_count = 0;
    std::uint16_t frame_size = 0;
    std::uint64_t entries = 0; // times a frame for this body was entered
    std::vector<register_observation> regs;
    /// Escape observation: the static inventory (every function, whether it ran
    /// or not), the sites that were actually adjudicated, and how many frame ends
    /// of this body have been checked against the budget.
    std::vector<static_site> allocs;
    std::vector<site_observation> sites;
    std::uint64_t checks = 0;
    /// Separate from interpreted checks: compiled observations have no source
    /// coordinates and must not exhaust the interpreted site's budget.
    std::uint64_t compiled_checks = 0;
};

/// One program the recorder has seen, and its slice of `functions()`.
struct program_observation {
    std::uint64_t source_hash = 0; // FNV-1a over program::source - the key
    std::size_t source_size = 0;
    std::string label; // functions[0].module, or empty for a classic script
    std::size_t first = 0;
    std::size_t count = 0; // functions [first, first + count) in the recorder
};

/// Records register types and allocation escapes as the interpreter runs.
/// One per process is the normal arrangement; it merges across contexts
/// because it is keyed by each program's source hash, not by any pointer that
/// only means something inside one VM.
class type_recorder {
public:
    /// Records one interpreter step, called from the dispatch loop after the
    /// instruction has been fetched and before its handler runs.
    ///
    /// The observation is therefore of the previous instruction's destination: a
    /// def is only readable once the handler that made it has finished, and
    /// `op::call` does not finish until the callee returns. Deferring by one step
    /// in the same frame is what makes a call's result observable at all.
    void step(std::size_t depth, const program * owner, const function_proto * proto,
              std::size_t base, std::size_t pc, std::uint16_t argc, instruction in,
              std::span<const value> registers);

    /// Writes the recording. Returns false if the file could not be opened.
    [[nodiscard]] bool write(const std::string & path) const;

    // --- What it saw, for a caller that wants to assert on it -----------===//
    [[nodiscard]] const std::vector<function_observation> & functions() const noexcept {
        return functions_;
    }
    [[nodiscard]] const std::vector<program_observation> & programs() const noexcept {
        return programs_;
    }
    /// Observations that were armed and then thrown away because the next
    /// instruction in that frame was not the one after the def: an unwind, a
    /// suspension, or a frame that never ran another instruction. Reported rather
    /// than hidden: it is the recorder's own miss rate.
    [[nodiscard]] std::uint64_t dropped_defs() const noexcept { return dropped_; }
    [[nodiscard]] std::uint64_t recorded_defs() const noexcept { return recorded_; }
    /// Frames whose function_proto is not inside the program that owns the
    /// running closure. Should be zero; a non-zero count means the recording is
    /// incomplete and says by how much.
    [[nodiscard]] std::uint64_t orphan_frames() const noexcept { return orphans_; }

    // --- Escape observation ---------------------------------------------===//

    /// Sets the budget: how many frame ends of one function body are
    /// adjudicated before the rest are reported unchecked. Each check is a
    /// bounded mark of the reachable heap plus one heap walk to unmark, and
    /// under `--script` nothing ever collects, so the heap only grows; a body
    /// entered a hundred thousand times would cost a hundred thousand marks.
    /// Over budget is never confined. 0 means unlimited.
    void set_escape_budget(std::uint64_t per_function) noexcept { budget_ = per_function; }
    /// Includes the dead window, for the A/B comparison the self-test runs: an
    /// unbounded walk marks the popped frame's own registers too, so it must
    /// report a superset of the bounded walk's escapes. If a register-allocator
    /// change ever keeps a live value above a call base, that superset stops being
    /// one and this comparison fails.
    void set_unbounded(bool on) noexcept { unbounded_ = on; }
    [[nodiscard]] bool unbounded() const noexcept { return unbounded_; }

    /// One allocation awaiting its frame's end. `object` is a valid pointer
    /// exactly until `dead` is set by `freed`, and is never dereferenced after.
    struct escape_record {
        heap_object * object = nullptr;
        std::size_t function = 0; // index into functions_
        std::uint32_t pc = 0;
        heap_kind kind = heap_kind::object;
        bool dead = false;
        bool routed = false;
        root_label route = root_label::globals;
    };

    /// The context's side of the protocol; see context::note_allocation and
    /// context::record_frame_pop in type_record.cpp. A frame's identity is a
    /// serial the context asks for lazily at its first tracked allocation.
    [[nodiscard]] std::uint64_t fresh_serial() noexcept { return ++next_serial_; }
    void allocated(heap_object * p, const program * owner, const function_proto * proto,
                   std::uint32_t pc, std::uint64_t serial);
    void unframed_allocation() noexcept { ++unframed_; }
    void freed(heap_object * p);
    void note_pop() noexcept { ++pops_; }
    void note_unwind() noexcept { ++unwinds_; }
    /// Hands over a frame's records for adjudication, appended to `out`.
    /// A compiled (AOT) frame's records are adjudicated only on an explicit
    /// normal-return handoff (`compiled_return`). Otherwise, or once the
    /// function's budget for interpreted or for compiled frames is spent, they
    /// are recorded as unchecked.
    void begin_check(std::uint64_t serial, std::vector<escape_record> & out,
                     bool compiled_return = false);
    /// Folds adjudicated records into their sites. The caller has marked: a
    /// record whose object is `marked` escaped by `route`, a dead one is
    /// unresolved, anything else is confined.
    void finish_check(const std::vector<escape_record> & records);

    /// Every function's sites as the file reports them, indexed like
    /// functions(): the adjudicated tallies plus every record still pending
    /// folded as unchecked (its frame ended through a hook-less path, or never
    /// ended), sorted by (pc, kind). The writer and the in-memory checker both
    /// read this, so the two cannot disagree about what "pending" means.
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
    /// Program pointer to index in programs_. A pointer is only a key within one
    /// process, which is all this is: the source hash is what leaves the file.
    std::vector<std::pair<const program *, std::uint32_t>> by_pointer_;
    std::vector<pending> pend_;
    /// Single-entry memo: consecutive instructions are almost always in the same
    /// body, so this removes the lookup from the common step. An index, not a
    /// pointer: interning a program appends to `functions_`, and a pointer
    /// memoised before that reallocation would dangle.
    const function_proto * last_proto_ = nullptr;
    std::size_t last_index_ = 0;
    bool last_valid_ = false;
    std::uint64_t dropped_ = 0;
    std::uint64_t recorded_ = 0;
    std::uint64_t orphans_ = 0;

    // --- Escape observation side tables ---------------------------------===//
    //
    // A side table keyed by heap_object*, not a field on heap_object. A field
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

/// Sets the active recorder, which `context` reads when it is constructed.
///
/// Process-wide rather than an argument, because the thing that has to be
/// instrumented is a whole page: a `shell::browser` builds its own context,
/// its workers build their own, and a module graph is several programs. Handing
/// a recorder down through all of that would change every layer in between for
/// a mode that is off in every shipped build.
///
/// Set it before the context that should record is constructed.
void set_active_type_recorder(type_recorder * recorder) noexcept;
[[nodiscard]] type_recorder * active_type_recorder() noexcept;

/// The program key the recording is written under, and the checker re-derives:
/// FNV-1a over the source text. Two programs compiled from the same bytes are
/// the same program, whoever ran them and in whatever order.
[[nodiscard]] std::uint64_t program_source_hash(const program & prog) noexcept;

} // namespace ctbrowser::script
