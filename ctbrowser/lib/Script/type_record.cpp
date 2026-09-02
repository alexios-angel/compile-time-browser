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
    for (const char c : s) { out += (static_cast<unsigned char>(c) <= ' ' || c == 0x7F) ? '_' : c; }
    return out;
}

} // namespace

void set_active_type_recorder(type_recorder * recorder) noexcept {
    g_active = recorder;
}
type_recorder * active_type_recorder() noexcept {
    return g_active;
}

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
        // AND THE STATIC ALLOCATION SITES, for the same reason: a site
        // nothing reached has no observation, and the checker can only say
        // "unobserved" about a site it knows exists. Read out of the bytecode
        // by opcode, so the inventory is the interpreter's and not a claim.
        for (std::size_t pc = 0; pc < fp.code.size(); ++pc) {
            if (const std::optional<heap_kind> kind = opcode_site_kind(fp.code[pc].code)) {
                f.allocs.push_back(static_site{static_cast<std::uint32_t>(pc), *kind});
            }
        }
        functions_.push_back(std::move(f));
    }
    programs_.push_back(std::move(rec));
    by_pointer_.emplace_back(owner, id);
    return id;
}

function_observation * type_recorder::resolve(const program * owner, const function_proto * proto) {
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
            // THE DECLARED PARAMETERS, AND NOT THE ARRIVED ONES. This used to
            // observe max(declared, argc) registers, and that was the
            // recorder's own soundness bug in the direction its comment above
            // warns about: context::call copies EVERY argument into the
            // callee's window, so a callback declared `function (x)` that
            // Array.prototype.map invokes with (value, index, array) has the
            // index sitting in slot 1 and the array in slot 2 at entry - on
            // top of its locals. Six one-parameter callbacks in p5 and phaser
            // then read as "claimed {i32}, observed {arr,i32}".
            //
            // Those slots are not the register AS THE PROGRAM SEES IT. The
            // compiler initialises every `var` explicitly before a read - so
            // `function f(x) { var y; return y; }` called as f(1, 2) returns
            // undefined, measured - and the surplus is reachable only through
            // the raw window that make_arguments_object and
            // gather_rest_values are handed. A dataflow analysis over the
            // bytecode cannot know argc, and should not have to: the extra
            // values are the frame layout's business, not the variable's.
            const std::size_t n = std::min<std::size_t>(proto->param_count, f->regs.size());
            (void)argc;
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

    // VERSION 2: version 1 plus the escape half - one `escape` header line,
    // an `alloc` line per static site and a `site` line per adjudicated one.
    // tools/check/type-oracle.py reads both versions and skips the new lines;
    // tools/check/escape-oracle.py reads only 2.
    std::fprintf(out, "ctbrowser-type-recording 2\n");
    std::fprintf(out, "opcodes %zu writers %zu\n", opcode_count, opcode_writer_count);
    std::fprintf(out, "defs recorded %llu dropped %llu orphan-frames %llu\n",
                 static_cast<unsigned long long>(recorded_),
                 static_cast<unsigned long long>(dropped_),
                 static_cast<unsigned long long>(orphans_));
    if (budget_ == 0) {
        std::fprintf(out, "escape budget unlimited");
    } else {
        std::fprintf(out, "escape budget %llu", static_cast<unsigned long long>(budget_));
    }
    std::fprintf(out, " pops %llu unwinds %llu checks %llu unframed %llu unresolved %llu\n",
                 static_cast<unsigned long long>(pops_), static_cast<unsigned long long>(unwinds_),
                 static_cast<unsigned long long>(checks_),
                 static_cast<unsigned long long>(unframed_),
                 static_cast<unsigned long long>(unresolved_));
    std::fprintf(out, "programs %zu\n", programs_.size());
    const std::vector<std::vector<site_observation>> sites = all_sites();
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
            // EVERY STATIC SITE, then ONLY THE ADJUDICATED ONES - the same
            // denominator discipline for the escape half. A site that made
            // nothing has no `site` line.
            for (const static_site & a : fn.allocs) {
                std::fprintf(out, "alloc %u kind %s\n", a.pc,
                             std::string{site_kind_name(a.kind)}.c_str());
            }
            for (const site_observation & s : sites[prog.first + i]) {
                if (s.pc == prologue_pc) {
                    std::fprintf(out, "site prologue");
                } else {
                    std::fprintf(out, "site %u", s.pc);
                }
                std::fprintf(out,
                             " kind %s made %llu confined %llu escaped %llu unresolved %llu "
                             "unchecked %llu routes",
                             std::string{site_kind_name(s.kind)}.c_str(),
                             static_cast<unsigned long long>(s.made),
                             static_cast<unsigned long long>(s.confined),
                             static_cast<unsigned long long>(s.escaped),
                             static_cast<unsigned long long>(s.unresolved),
                             static_cast<unsigned long long>(s.unchecked));
                bool first = true;
                for (std::size_t l = 0; l < root_label_count; ++l) {
                    if (s.routes[l] == 0) { continue; }
                    std::fprintf(out, "%c%s:%llu", first ? ' ' : ',',
                                 std::string{root_label_names[l]}.c_str(),
                                 static_cast<unsigned long long>(s.routes[l]));
                    first = false;
                }
                std::fprintf(out, first ? " -\n" : "\n");
            }
        }
    }
    const bool ok = std::fclose(out) == 0;
    return ok;
}

// ===================== THE ESCAPE HALF - ctcompile Phase 55O ==================
//
// The recorder's side of the protocol is bookkeeping: a record per tracked
// allocation, filed under the frame that made it, resolved when that frame
// ends. The context's side, further down, is the part that needs the VM - the
// bounded root walk that decides whether an object is still reachable.

void type_recorder::allocated(heap_object * p, const program * owner, const function_proto * proto,
                              std::uint32_t pc, std::uint64_t serial) {
    function_observation * f = resolve(owner, proto);
    if (f == nullptr) { return; } // an orphan frame, counted by resolve
    const auto function = static_cast<std::size_t>(f - functions_.data());
    // AN ADDRESS SEEN TWICE WITHOUT A FREE BETWEEN cannot happen - the only two
    // `delete`s of a heap object are in sweep() and sweep_all(), both hooked -
    // and is handled rather than assumed: the older record is flagged dead,
    // which is UNRESOLVED, never confined.
    if (const auto stale = alloc_.find(p); stale != alloc_.end()) {
        if (const auto list = by_serial_.find(stale->second.serial);
            list != by_serial_.end() && stale->second.slot < list->second.size()) {
            list->second[stale->second.slot].dead = true;
        }
        alloc_.erase(stale);
    }
    std::vector<escape_record> & list = by_serial_[serial];
    alloc_[p] = alloc_slot{serial, list.size()};
    escape_record record;
    record.object = p;
    record.function = function;
    record.pc = pc;
    record.kind = p->kind;
    list.push_back(record);
    ++pending_;
}

void type_recorder::freed(heap_object * p) {
    const auto found = alloc_.find(p);
    if (found == alloc_.end()) { return; }
    if (const auto list = by_serial_.find(found->second.serial);
        list != by_serial_.end() && found->second.slot < list->second.size()) {
        list->second[found->second.slot].dead = true;
    }
    alloc_.erase(found);
}

site_observation & type_recorder::site_for(std::size_t function, std::uint32_t pc, heap_kind kind) {
    std::vector<site_observation> & sites = functions_[function].sites;
    for (site_observation & s : sites) {
        if (s.pc == pc && s.kind == kind) { return s; }
    }
    site_observation fresh;
    fresh.pc = pc;
    fresh.kind = kind;
    sites.push_back(fresh);
    return sites.back();
}

void type_recorder::fold(const escape_record & r, root_label route, bool escaped, bool unchecked) {
    site_observation & s = site_for(r.function, r.pc, r.kind);
    ++s.made;
    if (unchecked) {
        ++s.unchecked;
    } else if (r.dead) {
        // Swept before its frame ended. A witness must not guess: a transient
        // store-then-overwrite would otherwise read as confined.
        ++s.unresolved;
        ++unresolved_;
    } else if (escaped) {
        ++s.escaped;
        ++s.routes[static_cast<std::size_t>(route)];
    } else {
        ++s.confined;
    }
}

void type_recorder::begin_check(std::uint64_t serial, std::vector<escape_record> & out) {
    const auto found = by_serial_.find(serial);
    if (found == by_serial_.end()) { return; }
    std::vector<escape_record> records = std::move(found->second);
    by_serial_.erase(found);
    if (records.empty()) { return; }
    pending_ -= records.size();
    // These records are leaving the side table either way; nothing frees
    // between here and finish_check, so a live pointer stays live.
    for (const escape_record & r : records) {
        if (!r.dead) { alloc_.erase(r.object); }
    }
    function_observation & f = functions_[records.front().function];
    if (budget_ != 0 && f.checks >= budget_) {
        for (const escape_record & r : records) { fold(r, root_label::globals, false, true); }
        return;
    }
    ++f.checks;
    ++checks_;
    out.insert(out.end(), records.begin(), records.end());
}

void type_recorder::finish_check(const std::vector<escape_record> & records) {
    for (const escape_record & r : records) {
        const bool escaped = !r.dead && r.object->marked;
        fold(r, r.route, escaped, false);
    }
}

std::vector<std::vector<site_observation>> type_recorder::all_sites() const {
    std::vector<std::vector<site_observation>> out(functions_.size());
    for (std::size_t i = 0; i < functions_.size(); ++i) { out[i] = functions_[i].sites; }
    // EVERY RECORD STILL PENDING IS UNCHECKED: its frame ended through a path
    // FrameEnds.def lists as hooked=0, or has not ended. Folded into a copy,
    // so that asking twice answers the same and the tallies stay what the
    // hooks made them.
    for (const auto & [serial, list] : by_serial_) {
        for (const escape_record & r : list) {
            std::vector<site_observation> & sites = out[r.function];
            site_observation * s = nullptr;
            for (site_observation & each : sites) {
                if (each.pc == r.pc && each.kind == r.kind) { s = &each; }
            }
            if (s == nullptr) {
                site_observation fresh;
                fresh.pc = r.pc;
                fresh.kind = r.kind;
                sites.push_back(fresh);
                s = &sites.back();
            }
            ++s->made;
            ++s->unchecked;
        }
    }
    for (std::vector<site_observation> & sites : out) {
        std::sort(sites.begin(), sites.end(),
                  [](const site_observation & a, const site_observation & b) {
                      return a.pc != b.pc ? a.pc < b.pc : a.kind < b.kind;
                  });
    }
    return out;
}

// --- the context's side: the hooks and the bounded mark ------------------------
//
// UNCONDITIONAL DEFINITIONS, like record_step, for the reason its comment
// gives; the #if here empties the bodies in a build that turned recording off,
// so that build records nothing on either half, as type_recording_enabled()
// says.

void context::note_allocation(heap_object * p) {
#if CTBROWSER_SCRIPT_RECORD_TYPES
    if (!escape_tracked(p->kind)) { return; }
    if (frames_.empty()) {
        // install_builtins, a microtask's own machinery, the DOM building a
        // wrapper: allocations with no JavaScript activation to belong to.
        recorder_->unframed_allocation();
        return;
    }
    call_frame & frame = frames_.back();
    // THE SITE, resolved exactly as record_step resolves a step: the closure's
    // program, or the context's for a top level; `ip - 1` is the instruction
    // being executed because `ip` was post-incremented at fetch. Whatever a
    // native called from this instruction allocates lands here too, on the
    // call's own pc - an UNCLAIMED site, by design.
    const program * owner = frame.closure != nullptr && frame.closure->owner != nullptr
                                ? frame.closure->owner
                                : program_;
    if (owner == nullptr) { return; }
    if (frame.serial == 0) { frame.serial = recorder_->fresh_serial(); }
    const std::uint32_t pc = frame.ip == 0 ? prologue_pc : static_cast<std::uint32_t>(frame.ip - 1);
    recorder_->allocated(p, owner, frame.proto, pc, frame.serial);
#else
    (void)p;
#endif
}

void context::note_freed(heap_object * o) {
#if CTBROWSER_SCRIPT_RECORD_TYPES
    recorder_->freed(o);
#else
    (void)o;
#endif
}

void context::record_frame_pop(const call_frame & popped, value carried) {
#if CTBROWSER_SCRIPT_RECORD_TYPES
    if (popped.serial == 0) { return; } // it allocated nothing
    recorder_->note_pop();
    std::vector<type_recorder::escape_record> records;
    recorder_->begin_check(popped.serial, records);
    if (records.empty()) { return; }
    // THE RETURN VALUE IS IN FLIGHT. At this moment it lives in a C++ local of
    // the dispatch loop and is written to the caller's register only after
    // this hook returns - so it is rooted here, through the same mechanism
    // `construct` uses, and `return {}` reads `escaped via temporaries`.
    const rooted keep{*this, carried};
    adjudicate(popped.base, frames_.size(), records);
#else
    (void)popped;
    (void)carried;
#endif
}

void context::record_frames_unwound(std::size_t first) {
#if CTBROWSER_SCRIPT_RECORD_TYPES
    std::vector<type_recorder::escape_record> records;
    for (std::size_t k = first; k < frames_.size(); ++k) {
        if (frames_[k].serial == 0) { continue; }
        recorder_->note_unwind();
        recorder_->begin_check(frames_[k].serial, records);
    }
    if (records.empty()) { return; }
    // Every frame from `first` up is ending; the lowest of their bases is the
    // start of the dead window, and their per-frame roots die with them.
    // `thrown_` is still set - the unwinder clears it after this - so the
    // value in flight is a root, as it should be.
    adjudicate(frames_[first].base, first, records);
#else
    (void)first;
#endif
}

// THE BOUNDED MARK. The collector's own root walk with the dead window
// excluded, mark only, then every mark cleared - nothing freed.
//
// WHY THE EXCLUSION IS LOAD-BEARING. It is what turns "reachable in the VM"
// into "reachable in the JavaScript program". An inline callee is placed at
// `base + a + 1`, INSIDE its caller's register extent, and `ret` never
// shrinks the register file; a C++-entered callee sits at the end of the
// file and is resized away only after its call returns. So at the moment a
// frame ends, every object it held in a register is still in a register - and
// an unbounded walk would report every local of every function as escaped
// `via registers`, which is the shape of a witness that can never say
// "confined" and therefore checks nothing. What makes the bound SOUND is the
// compiler's register allocator: it is a stack (compile/frames.cpp alloc_reg /
// release_to), a call's arguments are the topmost allocations at the call, so
// nothing the caller still needs lies at or above the callee's base, and the
// call's result slot is below it. `--unbounded` runs the same walk without the
// bound and must report a superset of escapes; the self-test asserts that,
// and it is the number that goes red if the allocator ever changes shape.
void context::adjudicate(std::size_t register_limit, std::size_t frame_limit,
                         std::vector<type_recorder::escape_record> & records) {
#if CTBROWSER_SCRIPT_RECORD_TYPES
    if (recorder_->unbounded()) {
        register_limit = registers_.size();
        frame_limit = frames_.size();
    }
    // THE ROUTE is the first root category that reached the object. The walk
    // visits labels contiguously, so a scan of the (small) record list at
    // every change of label attributes each newly marked object to the label
    // whose marks just finished.
    root_label current = root_label::globals;
    bool any = false;
    const auto attribute = [&](root_label label) {
        for (type_recorder::escape_record & r : records) {
            if (!r.dead && !r.routed && r.object->marked) {
                r.routed = true;
                r.route = label;
            }
        }
    };
    each_root(register_limit, frame_limit, [&](root_label label, value v) {
        if (!any || label != current) {
            if (any) { attribute(current); }
            current = label;
            any = true;
        }
        mark(v);
    });
    if (any) { attribute(current); }
    recorder_->finish_check(records);
    // NO SWEEP. The oracle observes and does not collect; the one thing it
    // touched is the mark bit, and it puts that back.
    unmark_all();
#else
    (void)register_limit;
    (void)frame_limit;
    (void)records;
#endif
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
