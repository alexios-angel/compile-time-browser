//===- type_record.cpp - Type and escape recording --------------*- C++ -*-===//
//
// The type oracle's recording side: per-register type observations and
// per-allocation-site escape observations, written to a text recording that
// tools/check/type-oracle.py and escape-oracle.py check ctcompile against.
// See include/ctbrowser/script/type_record.hpp for what it is for.
//
// With no recorder installed this costs nothing: run_loop.cpp is a template
// on a bool, and the shipped instantiation has no recording hook. (A plain
// `if (recorder_ != nullptr)` at the loop head measured +0.5% on
// bench_script.) Everything below runs only when a recorder is installed.
//
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <format>
#include <fstream>
#include <iterator>
#include <numeric>
#include <string>
#include <vector>

#include <ctbrowser/script/type_record.hpp>
#include <ctbrowser/script/vm.hpp>

#include <boost/hash2/fnv1a.hpp>

namespace ctbrowser::script {

namespace {

// One recorder per process, not thread_local, so one recording covers the
// whole page. Recording is a single-threaded developer mode: a page with
// workers must be recorded one agent at a time, or the merge is racy.
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

// Byte-wise FNV-1a 64 from the standard basis, which is what a default
// fnv1a_64 is. Checked against the published test vector for "a", since the
// value is written into recordings.
static_assert([] {
    boost::hash2::fnv1a_64 h;
    const unsigned char a[] = {'a'};
    h.update(a, 1);
    return h.result();
}() == 0xaf63dc4c8601ec8cull);

std::uint64_t program_source_hash(const program & prog) noexcept {
    boost::hash2::fnv1a_64 h;
    h.update(prog.source.data(), prog.source.size());
    // The kind is part of the identity for the same reason program_image
    // records it: the same text compiled as a module and as a classic script
    // are different programs with different top-level scoping.
    const auto kind = static_cast<std::uint8_t>(prog.kind);
    h.update(&kind, 1);
    return h.result();
}

void register_observation::observe(value v) noexcept {
    ++defs;
    // Test in value.hpp's order. is_number() means "does not match the boxed
    // pattern", so it must be asked first: a heap pointer has the sign bit and
    // the quiet-NaN bits, and a negative NaN double has both too. Asking
    // is_heap() first would read a NaN as a pointer and crash.
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
            // -0 is integral and in int32 range but cannot be lowered to an
            // integer: `1/-0` is -Infinity while `1/0` is Infinity, so a
            // register that ever held -0 must be flagged.
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
    // Every function, not only the ones that ran. Materialising the empty
    // ones up front lets the checker tell "never executed" from "any type" and
    // know how many functions there were; otherwise every precision number
    // computed from the recording would have the wrong denominator.
    for (std::size_t i = 0; i < owner->functions.size(); ++i) {
        const function_proto & fp = owner->functions[i];
        function_observation f;
        f.program = id;
        f.index = static_cast<std::uint32_t>(i);
        f.name = fp.name;
        f.param_count = fp.param_count;
        f.frame_size = fp.frame_size;
        f.regs.resize(fp.frame_size);
        // The static allocation sites too, for the same reason: the checker
        // can only say "unobserved" about a site it knows exists. They are
        // read out of the bytecode by opcode, so the inventory is the
        // interpreter's own.
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
        // Counted rather than assumed impossible: `new Function(body)` makes
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

    // Flush the previous def in this frame. A def is recorded one step late,
    // once its instruction has written the register, and only if four things
    // match:
    //
    //   the depth      indexes this vector, so it matches by construction
    //   the proto      a frame at this depth returned and another was pushed
    //   the base       the same body called recursively, one window along
    //   expect_pc      the important one. An instruction that threw did not
    //                  write its destination, so the register still holds a
    //                  stale value. Requiring that the next instruction
    //                  executed in this frame is the one after the def drops
    //                  every unwind, because a handler's address is not pc+1.
    //                  Recording stale values would widen the observed set
    //                  and make the checker report false soundness violations.
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

    // Frame entry: record the parameters, which no instruction defines.
    // Arguments are written into r0.. by the caller before the first
    // instruction runs, so a def-based recorder never sees them. Without this,
    // every parameter would read as "never executed", and parameters are the
    // registers a static inference is least sure about.
    if (pc == 0) {
        if (function_observation * f = resolve(owner, proto); f != nullptr) {
            ++f->entries;
            // Only the declared parameters are observed, not every argument
            // passed. context::call copies all arguments into the callee's
            // window, so a `function (x)` called by Array.prototype.map also
            // has index and array in slots 1 and 2 at entry. Those slots are
            // not variables the program can read (the compiler initialises
            // every `var` before its first read); they are reachable only
            // through make_arguments_object and gather_rest_values, which a
            // bytecode analysis does not model by argc.
            const std::size_t n = std::min<std::size_t>(proto->param_count, f->regs.size());
            (void)argc;
            for (std::size_t r = 0; r < n; ++r) {
                if (base + r < registers.size()) { f->regs[r].observe(registers[base + r]); }
            }
            recorded_ += n;
        }
    }

    // Arm this instruction's def, to be flushed on the next step.
    if (opcode_writes_a(in.code)) {
        p.proto = proto;
        p.base = base;
        p.expect_pc = pc + 1;
        p.reg = in.a;
        p.live = true;
    }
}

namespace {
template <typename... Args>
void put(std::string & text, std::format_string<Args...> fmt, Args &&... args) {
    std::format_to(std::back_inserter(text), fmt, std::forward<Args>(args)...);
}
} // namespace

bool type_recorder::write(const std::string & path) const {
    // Programs in source-hash order, so two runs that happen to load the same
    // scripts in a different order still produce byte-identical recordings.
    std::vector<std::size_t> order(programs_.size());
    std::iota(order.begin(), order.end(), std::size_t{0});
    std::ranges::sort(order, [this](std::size_t a, std::size_t b) {
        return programs_[a].source_hash < programs_[b].source_hash;
    });

    std::string text;
    // Recording format version 2: version 1 plus the escape half (one
    // `escape` header line, an `alloc` line per static site and a `site` line
    // per adjudicated one). tools/check/type-oracle.py reads both versions and
    // skips the new lines; tools/check/escape-oracle.py reads only 2.
    put(text, "ctbrowser-type-recording 2\n");
    put(text, "opcodes {} writers {}\n", opcode_count, opcode_writer_count);
    put(text, "defs recorded {} dropped {} orphan-frames {}\n", recorded_, dropped_, orphans_);
    if (budget_ == 0) {
        put(text, "escape budget unlimited");
    } else {
        put(text, "escape budget {}", budget_);
    }
    put(text, " pops {} unwinds {} checks {} unframed {} unresolved {}\n", pops_, unwinds_, checks_,
        unframed_, unresolved_);
    put(text, "programs {}\n", programs_.size());
    const std::vector<std::vector<site_observation>> sites = all_sites();
    for (const std::size_t id : order) {
        const program_observation & prog = programs_[id];
        put(text, "program {:016x} size {} functions {} label {}\n", prog.source_hash,
            prog.source_size, prog.count, sanitised(prog.label));
        for (std::size_t i = 0; i < prog.count; ++i) {
            const function_observation & fn = functions_[prog.first + i];
            put(text, "fn {} entries {} params {} frame {} name {}\n", fn.index, fn.entries,
                fn.param_count, fn.frame_size, sanitised(fn.name));
            // Only observed registers get a line. `frame` above is the
            // denominator, so the missing ones are exactly those nothing
            // reached.
            for (std::size_t r = 0; r < fn.regs.size(); ++r) {
                const register_observation & obs = fn.regs[r];
                if (!obs.observed()) { continue; }
                put(text, "r {} defs {} kinds {:08x} num {:08x}\n", r, obs.defs, obs.kinds,
                    obs.numbers);
            }
            // Every static site, then only the adjudicated ones: the same
            // denominator discipline for the escape half. A site that made
            // nothing has no `site` line.
            for (const static_site & a : fn.allocs) {
                put(text, "alloc {} kind {}\n", a.pc, site_kind_name(a.kind));
            }
            for (const site_observation & s : sites[prog.first + i]) {
                if (s.pc == prologue_pc) {
                    put(text, "site prologue");
                } else {
                    put(text, "site {}", s.pc);
                }
                put(text,
                    " kind {} made {} confined {} escaped {} unresolved {} unchecked {} routes",
                    site_kind_name(s.kind), s.made, s.confined, s.escaped, s.unresolved,
                    s.unchecked);
                bool first = true;
                for (std::size_t l = 0; l < root_label_count; ++l) {
                    if (s.routes[l] == 0) { continue; }
                    put(text, "{}{}:{}", first ? ' ' : ',', root_label_names[l], s.routes[l]);
                    first = false;
                }
                text += first ? " -\n" : "\n";
            }
        }
    }
    std::ofstream out{path, std::ios::binary};
    if (!out) { return false; }
    out << text;
    return static_cast<bool>(out);
}

//===----------------------------------------------------------------------===//
// Escape recording
//===----------------------------------------------------------------------===//
//
// Records, per allocation site, whether objects made there outlive the frame
// that made them (escaped) or not (confined). ctcompile's escape analysis is
// checked against these observations (tools/check/escape-oracle.py).
//
// The recorder's side is bookkeeping: a record per tracked allocation, filed
// under the frame that made it, resolved when that frame ends. The context's
// side, further down, needs the VM: the bounded root walk that decides
// whether an object is still reachable.

void type_recorder::allocated(heap_object * p, const program * owner, const function_proto * proto,
                              std::uint32_t pc, std::uint64_t serial) {
    function_observation * f = resolve(owner, proto);
    if (f == nullptr) { return; } // An orphan frame, counted by resolve.
    const auto function = static_cast<std::size_t>(f - functions_.data());
    // An address seen twice without a free in between should not happen (the
    // only two `delete`s of a heap object are in sweep() and sweep_all(), both
    // hooked), but it is handled rather than assumed: the older record is
    // flagged dead, which counts as unresolved, never confined.
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

void type_recorder::begin_check(std::uint64_t serial, std::vector<escape_record> & out,
                                bool compiled_return) {
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
    // A compiled (AOT) frame is checked only when it ends through the
    // explicit return hook, which supplies the in-flight return value; other
    // compiled exits are folded as unchecked. Compiled records keep the
    // `compiled_pc` sentinel, so they never merge with a bytecode site.
    const bool compiled = records.front().pc == compiled_pc;
    std::uint64_t & checks = compiled ? f.compiled_checks : f.checks;
    if ((compiled && !compiled_return) || (budget_ != 0 && checks >= budget_)) {
        for (const escape_record & r : records) { fold(r, root_label::globals, false, true); }
        return;
    }
    ++checks;
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
    // Every record still pending is unchecked: its frame ended through a path
    // FrameEnds.def lists as hooked=0, or has not ended. Folded into a copy,
    // so asking twice gives the same answer and the stored tallies stay what
    // the hooks made them.
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

// The context's side: the hooks and the bounded mark.
//
// Every hook is defined in every build, like record_step: `context` is a
// public type and its shape cannot depend on a flag (see record_step).

void context::note_allocation(heap_object * p) {
    if (!escape_tracked(p->kind)) { return; }
    if (frames_.empty()) {
        // install_builtins, a microtask's own machinery, the DOM building a
        // wrapper: allocations with no JavaScript activation to belong to.
        recorder_->unframed_allocation();
        return;
    }
    call_frame & frame = frames_.back();
    // The site, resolved exactly as record_step resolves a step: the
    // closure's program, or the context's for a top level; `ip - 1` is the
    // instruction being executed because `ip` was post-incremented at fetch.
    // Whatever a native called from this instruction allocates lands here
    // too, on the call's own pc: an unclaimed site, by design.
    const program * owner = frame.closure != nullptr && frame.closure->owner != nullptr
                                ? frame.closure->owner
                                : program_;
    if (owner == nullptr) { return; }
    if (frame.serial == 0) { frame.serial = recorder_->fresh_serial(); }
    const std::uint32_t pc = frame.proto != nullptr && frame.proto->aot_entry != nullptr
                                 ? compiled_pc
                             : frame.ip == 0 ? prologue_pc
                                             : static_cast<std::uint32_t>(frame.ip - 1);
    recorder_->allocated(p, owner, frame.proto, pc, frame.serial);
}

void context::note_freed(heap_object * o) {
    recorder_->freed(o);
}

void context::record_frame_pop(const call_frame & popped, value carried, bool compiled_return) {
    if (popped.serial == 0) { return; } // It allocated nothing.
    recorder_->note_pop();
    std::vector<type_recorder::escape_record> records;
    recorder_->begin_check(popped.serial, records, compiled_return);
    if (records.empty()) { return; }
    // The return value is in flight: it lives in a C++ local of the dispatch
    // loop and reaches the caller's register only after this hook returns. So
    // it is rooted here, as `construct` roots its instance, and `return {}`
    // reads as escaped via temporaries.
    const rooted keep{*this, carried};
    adjudicate(popped.base, frames_.size(), records);
}

void context::record_frames_unwound(std::size_t first) {
    std::vector<type_recorder::escape_record> records;
    for (std::size_t k = first; k < frames_.size(); ++k) {
        if (frames_[k].serial == 0) { continue; }
        recorder_->note_unwind();
        recorder_->begin_check(frames_[k].serial, records);
    }
    if (records.empty()) { return; }
    // Every frame from `first` up is ending; the lowest of their bases is the
    // start of the dead window, and their per-frame roots die with them.
    // `thrown_` is still set (the unwinder clears it after this), so the
    // value in flight is a root, as it should be.
    adjudicate(frames_[first].base, first, records);
}

/// The bounded mark: the collector's own root walk with the dead window
/// (registers from \p register_limit, frames from \p frame_limit) excluded,
/// mark only, then every mark cleared. Nothing is freed.
///
/// The exclusion turns "reachable in the VM" into "reachable in the
/// JavaScript program". An inline callee is placed at `base + a + 1`, inside
/// its caller's register extent, and `ret` never shrinks the register file;
/// a C++-entered callee sits at the end of the file and is resized away only
/// after its call returns. So when a frame ends, every object it held in a
/// register is still in a register, and an unbounded walk would report every
/// local as escaped via registers, a witness that can never say "confined".
///
/// The bound is sound because the compiler's register allocator is a stack
/// (compile/frames.cpp alloc_reg / release_to): a call's arguments are the
/// topmost allocations at the call, so nothing the caller still needs lies at
/// or above the callee's base, and the call's result slot is below it.
/// `--unbounded` runs the same walk without the bound and must report a
/// superset of escapes; the self-test asserts that, so it fails if the
/// allocator ever changes shape.
void context::adjudicate(std::size_t register_limit, std::size_t frame_limit,
                         std::vector<type_recorder::escape_record> & records) {
    if (recorder_->unbounded()) {
        register_limit = registers_.size();
        frame_limit = frames_.size();
    }
    // The route is the first root category that reached the object. The walk
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
    // No sweep: the oracle observes and does not collect. The only thing it
    // touched is the mark bit, and it clears that again.
    unmark_all();
}

/// Records one executed instruction. Defined unconditionally, like the hooks
/// above.
///
/// `context` is declared in a public header, so changing its shape with a
/// build flag would compile this library against one layout and every
/// consumer against another: an ODR violation that links cleanly. That is why
/// `recorder_` is a member of `context` in every build (the same choice
/// CTBROWSER_SCRIPT_DEBUG_NAMES makes for the debug side tables); whether the
/// dispatch loop calls these is decided at run time by whether a recorder is
/// installed.
void context::record_step(instruction in) {
    const call_frame & frame = frames_.back();
    // Which program's function table this body lives in. The closure knows;
    // the top-level frame has no closure, so it falls back to the context's
    // program, as op::closure does, because a function index only means
    // something in the program it was compiled against.
    const program * owner = frame.closure != nullptr && frame.closure->owner != nullptr
                                ? frame.closure->owner
                                : program_;
    if (owner == nullptr) { return; }
    recorder_->step(frames_.size(), owner, frame.proto, frame.base, frame.ip - 1, frame.argc, in,
                    std::span<const value>{registers_});
}

} // namespace ctbrowser::script
