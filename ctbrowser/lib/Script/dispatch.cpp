//===- dispatch.cpp - Mixed-mode dispatch and transition counts -*- C++ -*-===//
//
// The out-of-line half of dispatch.hpp: the transition counters between C++,
// the interpreter and compiled (AOT) bodies, and enter_compiled_body, which
// calls a function's compiled entry point and records the crossing.
//
//===----------------------------------------------------------------------===//

#include <ctbrowser/script/dispatch.hpp>

#include <ctbrowser/aot/aot.hpp>
#include <ctbrowser/script/vm.hpp>

#include <array>
#include <cstddef>
#include <new>

namespace ctbrowser::script {

namespace {

// One counter per transition. They are ordinary statics rather than
// thread_local because a `context` is not shared between threads, and two
// contexts on two threads counting into the same six numbers is a reporting
// question, not a correctness one. If script ever runs on more than one
// thread, make these thread_local and have the test sum them.
std::array<std::uint64_t, static_cast<std::size_t>(transition::count)> counts{};

[[nodiscard]] transition into_vm_from(executing_kind from) noexcept {
    return from == executing_kind::aot ? transition::aot_to_vm : transition::cxx_to_vm;
}

} // namespace

std::uint64_t transitions(transition which) noexcept {
    const auto index = static_cast<std::size_t>(which);
    return index < counts.size() ? counts[index] : 0;
}

void reset_transitions() noexcept {
    counts.fill(0);
}

const char * transition_name(transition which) noexcept {
    switch (which) {
    case transition::cxx_to_vm: return "C++ -> VM";
    case transition::cxx_to_aot: return "C++ -> AOT";
    case transition::vm_to_aot: return "VM -> AOT";
    case transition::aot_to_vm: return "AOT -> VM";
    case transition::aot_to_aot: return "AOT -> AOT";
    case transition::aot_to_cxx: return "AOT -> C++";
    case transition::count: break;
    }
    return "unknown";
}

executing_as::executing_as(context & ctx, executing_kind now) noexcept
    : ctx_(&ctx), saved_(ctx.executing_) {
    ctx.executing_ = now;
}

executing_as::~executing_as() noexcept {
    ctx_->executing_ = saved_;
}

void note_transition_into_vm(const context & ctx) noexcept {
    ++counts[static_cast<std::size_t>(into_vm_from(ctx.executing_))];
}

void note_transition_into_cxx(const context & ctx) noexcept {
    // Count only calls made from a compiled body. C++ calling C++ is not a
    // mixed-mode transition, and VM -> C++ is not counted because a native
    // built-in called from bytecode never had a choice of backend.
    if (ctx.executing_ == executing_kind::aot) {
        ++counts[static_cast<std::size_t>(transition::aot_to_cxx)];
    }
}

// Reached only when there is a compiled body: the header's inline half has
// already checked. Everything here happens once per compiled call, so none of
// it is on the interpreter's ordinary path.
bool enter_compiled_body(context & ctx, const function_proto & target, value closure,
                         const value * argv, std::size_t argv_base, std::uint32_t argc,
                         value receiver, bool constructing, value & out) {
    // Where the arguments are, handed on as an index. ct_aot_enter consumes
    // and clears both, as it does pending_new_target_ and pending_closure_;
    // they are set here because this is the last place that knows them.
    ctx.pending_argv_base_ = argv_base;
    ctx.pending_argc_ = argc;
    const transition crossing = ctx.executing_ == executing_kind::aot  ? transition::aot_to_aot
                                : ctx.executing_ == executing_kind::vm ? transition::vm_to_aot
                                                                       : transition::cxx_to_aot;
    ++counts[static_cast<std::size_t>(crossing)];

    std::uint64_t produced = 0;
    aot::ct_aot_status status = aot::ct_aot_status::failed;
    // The closure travels through the context, not the entry signature:
    // aot_entry.h explains that the signature is what two code generators are
    // written against, so adding a parameter breaks them. new.target already
    // travels this way, and ct_aot_enter consumes both in the same place.
    ctx.pending_closure_ = closure;
    {
        const executing_as running{ctx, executing_kind::aot};
        status = static_cast<aot::ct_aot_status>(
            target.aot_entry(reinterpret_cast<aot::ct_aot_ctx *>(&ctx),
                             reinterpret_cast<const aot::ct_aot_site *>(&target),
                             reinterpret_cast<const std::uint64_t *>(argv), argc, receiver.bits(),
                             constructing ? 1u : 0u, &produced));
    }

    // Nothing is written on any other status. An unwound frame is gone and a
    // failed one is the run loop's business; the interpreter handles both
    // exactly as after `op::throw_value`, and a C++ caller sees the failure
    // flag it would have seen from an interpreted throw.
    if (status == aot::ct_aot_status::ok) { out = value::from_bits(produced); }
    return true;
}

} // namespace ctbrowser::script
