// The type oracle's recording side. See include/ctbrowser/script/type_record.hpp
// for what it is for; this is how it is done.
//
// THE COST WHEN IT IS OFF IS NOTHING, and getting there took a measurement.
// The hook started as `if (recorder_ != nullptr)` at the loop head, which cost
// +0.53% on bench_script and +0.48% on phaser_invaders - a perfectly predicted
// not-taken branch is still a branch when the loop runs it a hundred million
// times. run_loop.cpp is now a template on one bool and the shipped
// instantiation contains no trace of this file. Everything below - the deferred
// flush, the interning, the parameter sweep - lives out of the loop's
// translation unit and runs only when a recorder is installed.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include <ctbrowser/script/type_record.hpp>
#include <ctbrowser/script/vm.hpp>

// ON UNLESS THE BUILD SAYS OTHERWISE, and the default has to be spelled out in
// every translation unit that reads it: the option is a PRIVATE definition and
// is only passed when it is OFF, so an `#if` on an undefined macro would read
// as 0 and `type_recording_enabled()` would answer false in the build where it
// is on. That is exactly how this file's first run reported "built with
// CTBROWSER_SCRIPT_RECORD_TYPES=0" from a build that had it ON.
#ifndef CTBROWSER_SCRIPT_RECORD_TYPES
#define CTBROWSER_SCRIPT_RECORD_TYPES 1
#endif

namespace ctbrowser::script {

namespace {

// One recorder per process. Not thread_local: a worker is its own agent with
// its own context, and the whole point is that one recording covers the page.
// Recording is a single-threaded developer mode - a page with workers must be
// recorded one agent at a time or the merge is racy, and that is stated here
// rather than pretended away.
type_recorder * g_active = nullptr;

[[nodiscard]] std::string sanitised(std::string_view s) {
    if (s.empty()) { return "-"; }
    std::string out;
    out.reserve(s.size());
    for (const char c : s) {
        out += (static_cast<unsigned char>(c) <= ' ' || c == 0x7F) ? '_' : c;
    }
    return out;
}

} // namespace

void set_active_type_recorder(type_recorder * recorder) noexcept { g_active = recorder; }
type_recorder * active_type_recorder() noexcept { return g_active; }

bool type_recording_enabled() noexcept {
#if CTBROWSER_SCRIPT_RECORD_TYPES
    return true;
#else
    return false;
#endif
}

std::uint64_t program_source_hash(const program & prog) noexcept {
    std::uint64_t h = 14695981039346656037ull;
    for (const char c : prog.source) {
        h ^= static_cast<std::uint8_t>(c);
        h *= 1099511628211ull;
    }
    // The KIND is part of the identity for the same reason program_image
    // records it: the same text compiled as a module and as a classic script
    // are different programs with different top-level scoping.
    h ^= static_cast<std::uint8_t>(prog.kind);
    h *= 1099511628211ull;
    return h;
}

void register_observation::observe(value v) noexcept {
    ++defs;
    // THE ORDER IS value.hpp's OWN. is_number() is "does not match the boxed
    // pattern", so it must be asked first: a heap pointer has the sign bit and
    // the quiet-NaN bits, and a negative NaN double has both too. Asking
    // is_heap() first would read a NaN as a pointer, which is the one mistake
    // NaN-boxing punishes with a segfault rather than a wrong answer.
    if (v.is_number()) {
        kinds |= obs_number;
        const double d = v.as_number();
        std::uint32_t f = num_seen;
        if (std::isnan(d)) {
            f |= num_nan;
        } else if (std::isinf(d)) {
            f |= num_infinite;
        } else if (d != std::trunc(d)) {
            f |= num_fractional;
        } else {
            // -0 IS INTEGRAL AND FITS int32 AND IS STILL NOT AN i32. `1/-0` is
            // -Infinity and `1/0` is Infinity, so a register that ever held -0
            // cannot be lowered to an integer without changing an answer. It
            // is the first row of the plan's own counterexample table.
            if (d == 0.0 && std::signbit(d)) { f |= num_negative_zero; }
            if (d < -2147483648.0 || d > 2147483647.0) { f |= num_wide; }
        }
        numbers |= f;
        return;
    }
    if (v.is_heap()) {
        if (const heap_object * h = v.as_heap(); h != nullptr) { kinds |= obs_heap(h->kind); }
        return;
    }
    if (v.is_undefined()) {
        kinds |= obs_undefined;
    } else if (v.is_null()) {
        kinds |= obs_null;
    } else if (v.is_boolean()) {
        kinds |= obs_boolean;
    }
}

std::uint32_t type_recorder::intern(const program * owner) {
    for (const auto & [ptr, id] : by_pointer_) {
        if (ptr == owner) { return id; }
    }
    const auto id = static_cast<std::uint32_t>(programs_.size());
    program_observation rec;
    rec.source_hash = program_source_hash(*owner);
    rec.source_size = owner->source.size();
    rec.label = owner->functions.empty() ? std::string{} : owner->functions[0].module;
    rec.first = functions_.size();
    rec.count = owner->functions.size();
    // EVERY function, not only the ones that ran. A register nothing reached
    // has no observation, and the difference between "never executed" and "any
    // type" is the whole reason this materialises the empty ones up front: a
    // recording that simply omits them cannot tell the checker how many there
    // were, and every precision number computed from it would be a fraction
    // with the wrong denominator.
    for (std::size_t i = 0; i < owner->functions.size(); ++i) {
        const function_proto & fp = owner->functions[i];
        function_observation f;
        f.program = id;
        f.index = static_cast<std::uint32_t>(i);
        f.name = fp.name;
        f.param_count = fp.param_count;
        f.frame_size = fp.frame_size;
        f.regs.resize(fp.frame_size);
        functions_.push_back(std::move(f));
    }
    programs_.push_back(std::move(rec));
    by_pointer_.emplace_back(owner, id);
    return id;
}

function_observation * type_recorder::resolve(const program * owner,
                                              const function_proto * proto) {
    if (last_valid_ && proto == last_proto_) { return &functions_[last_index_]; }
    if (owner == nullptr || proto == nullptr || owner->functions.empty()) { return nullptr; }
    const function_proto * first = owner->functions.data();
    if (proto < first || proto >= first + owner->functions.size()) {
        // The running body is not in the program its closure claims to own.
        // Counted rather than assumed impossible - `new Function(body)` makes
        // a program the context owns, and a devtools eval runs a function from
        // a different one.
        ++orphans_;
        return nullptr;
    }
    const auto index = static_cast<std::size_t>(proto - first);
    const std::uint32_t id = intern(owner);
    last_index_ = programs_[id].first + index;
    last_proto_ = proto;
    last_valid_ = true;
    return &functions_[last_index_];
}

void type_recorder::step(std::size_t depth, const program * owner, const function_proto * proto,
                         std::size_t base, std::size_t pc, std::uint16_t argc, instruction in,
                         std::span<const value> registers) {
    if (pend_.size() <= depth) { pend_.resize(depth + 1); }
    pending & p = pend_[depth];

    // --- FLUSH the previous def in this frame --------------------------------
    //
    // FOUR THINGS MUST MATCH, and each of them is a real case rather than
    // defensive noise:
    //
    //   the depth      indexes this vector, so it matches by construction
    //   the proto      a frame at this depth returned and another was pushed
    //   the base       the same body called recursively, one window along
    //   expect_pc      THE ONE THAT MATTERS. An instruction that threw did not
    //                  write its destination, and the register still holds
    //                  whatever was there before. Requiring that the very next
    //                  instruction executed in this frame is the one after the
    //                  def drops every unwind, because a handler's address is
    //                  not pc+1. Without it the recorder observes stale values
    //                  and the observed set gets WIDER than the truth - which
    //                  would make the checker report soundness violations that
    //                  are its own fault.
    if (p.live) {
        if (p.proto == proto && p.base == base && p.expect_pc == pc) {
            const std::size_t at = base + p.reg;
            if (at < registers.size()) {
                if (function_observation * f = resolve(owner, proto);
                    f != nullptr && p.reg < f->regs.size()) {
                    f->regs[p.reg].observe(registers[at]);
                    ++recorded_;
                }
            }
        } else {
            ++dropped_;
        }
        p.live = false;
    }

    // --- FRAME ENTRY: the parameters, which no instruction defines -----------
    //
    // Arguments are written into r0.. by the CALLER, before the first
    // instruction runs, so a def-based recorder never sees them. Left out,
    // every parameter of every function would read as "never executed" - and
    // parameters are exactly the registers a static inference is least sure
    // about, so that is the half of the recording most worth having.
    if (pc == 0) {
        if (function_observation * f = resolve(owner, proto); f != nullptr) {
            ++f->entries;
            const std::size_t declared = proto->param_count;
            const std::size_t arrived = argc;
            const std::size_t n = std::min<std::size_t>(std::max(declared, arrived), f->regs.size());
            for (std::size_t r = 0; r < n; ++r) {
                if (base + r < registers.size()) { f->regs[r].observe(registers[base + r]); }
            }
            recorded_ += n;
        }
    }

    // --- ARM this instruction's def ------------------------------------------
    if (opcode_writes_a(in.code)) {
        p.proto = proto;
        p.base = base;
        p.expect_pc = pc + 1;
        p.reg = in.a;
        p.live = true;
    }
}

bool type_recorder::write(const std::string & path) const {
    std::FILE * out = std::fopen(path.c_str(), "wb");
    if (out == nullptr) { return false; }

    // Programs in source-hash order, so two runs that happen to load the same
    // scripts in a different order still produce byte-identical recordings.
    std::vector<std::size_t> order(programs_.size());
    for (std::size_t i = 0; i < order.size(); ++i) { order[i] = i; }
    std::sort(order.begin(), order.end(), [this](std::size_t a, std::size_t b) {
        return programs_[a].source_hash < programs_[b].source_hash;
    });

    std::fprintf(out, "ctbrowser-type-recording 1\n");
    std::fprintf(out, "opcodes %zu writers %zu\n", opcode_count, opcode_writer_count);
    std::fprintf(out, "defs recorded %llu dropped %llu orphan-frames %llu\n",
                 static_cast<unsigned long long>(recorded_),
                 static_cast<unsigned long long>(dropped_),
                 static_cast<unsigned long long>(orphans_));
    std::fprintf(out, "programs %zu\n", programs_.size());
    for (const std::size_t id : order) {
        const program_observation & prog = programs_[id];
        std::fprintf(out, "program %016llx size %zu functions %zu label %s\n",
                     static_cast<unsigned long long>(prog.source_hash), prog.source_size,
                     prog.count, sanitised(prog.label).c_str());
        for (std::size_t i = 0; i < prog.count; ++i) {
            const function_observation & fn = functions_[prog.first + i];
            std::fprintf(out, "fn %u entries %llu params %u frame %u name %s\n", fn.index,
                         static_cast<unsigned long long>(fn.entries),
                         static_cast<unsigned>(fn.param_count),
                         static_cast<unsigned>(fn.frame_size), sanitised(fn.name).c_str());
            // ONLY THE OBSERVED REGISTERS get a line. `frame` above is the
            // denominator, so the ones missing here are exactly the ones
            // nothing reached - which is the number the checker must not
            // confuse with anything else.
            for (std::size_t r = 0; r < fn.regs.size(); ++r) {
                const register_observation & obs = fn.regs[r];
                if (!obs.observed()) { continue; }
                std::fprintf(out, "r %zu defs %llu kinds %08x num %08x\n", r,
                             static_cast<unsigned long long>(obs.defs), obs.kinds, obs.numbers);
            }
        }
    }
    const bool ok = std::fclose(out) == 0;
    return ok;
}

// UNCONDITIONAL, and the #if is in run_loop.cpp alone.
//
// `context` is declared in a PUBLIC header, so anything that changes its shape
// with a build flag compiles this library against one layout and every consumer
// against another - one ODR violation per translation unit, and the kind that
// links cleanly. That is the bargain CTBROWSER_SCRIPT_DEBUG_NAMES already
// struck for the debug side tables, and the reason `recorder_` is a member of
// `context` in every build while only the CALL from the dispatch loop is
// conditional.
void context::record_step(instruction in) {
    const call_frame & frame = frames_.back();
    // WHICH PROGRAM'S FUNCTION TABLE THIS BODY LIVES IN. The closure knows,
    // and the top-level frame has no closure - the same fallback op::closure
    // itself makes, and for the same reason: an index only means something in
    // the program it was compiled against.
    const program * owner = frame.closure != nullptr && frame.closure->owner != nullptr
                                ? frame.closure->owner
                                : program_;
    if (owner == nullptr) { return; }
    recorder_->step(frames_.size(), owner, frame.proto, frame.base, frame.ip - 1, frame.argc, in,
                    std::span<const value>{registers_});
}

} // namespace ctbrowser::script
