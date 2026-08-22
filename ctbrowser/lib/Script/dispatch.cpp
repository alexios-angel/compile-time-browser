#include <ctbrowser/script/dispatch.hpp>

#include <ctbrowser/aot/aot.hpp>
#include <ctbrowser/script/vm.hpp>

#include <array>
#include <cstddef>
#include <new>

namespace ctbrowser::script {

namespace {

// ONE COUNTER PER TRANSITION, and they are ordinary statics rather than
// thread_local because a `context` is not shared between threads and two
// contexts on two threads counting into the same six numbers is a reporting
// question, not a correctness one. If script ever runs on more than one thread
// this becomes thread_local and the test sums them.
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
    // ONLY FROM A COMPILED BODY IS COUNTED. C++ calling C++ is not a mixed-mode
    // transition and the plan does not list it; VM -> C++ is not listed either,
    // because a native built-in called from bytecode never had a choice of
    // backend to make.
    if (ctx.executing_ == executing_kind::aot) {
        ++counts[static_cast<std::size_t>(transition::aot_to_cxx)];
    }
}

// Reached only when there IS a compiled body - the header's inline half has
// already asked. Everything here happens once per compiled call, so none of it
// is on the interpreter's ordinary path.
bool enter_compiled_body(context & ctx, const function_proto & target, const value * argv,
                         std::uint32_t argc, value receiver, bool constructing, value & out) {
    const transition crossing = ctx.executing_ == executing_kind::aot  ? transition::aot_to_aot
                                : ctx.executing_ == executing_kind::vm ? transition::vm_to_aot
                                                                       : transition::cxx_to_aot;
    ++counts[static_cast<std::size_t>(crossing)];

    // A body sizes its own frame storage with the number the ABI publishes, and
    // this is the caller's half of that contract.
    alignas(std::max_align_t) unsigned char storage[CT_AOT_FRAME_BYTES];
    (void)storage;
    std::uint64_t produced = 0;
    aot::ct_aot_status status = aot::ct_aot_status::failed;
    {
        const executing_as running{ctx, executing_kind::aot};
        status = static_cast<aot::ct_aot_status>(
            target.aot_entry(reinterpret_cast<aot::ct_aot_ctx *>(&ctx),
                             reinterpret_cast<const aot::ct_aot_site *>(&target),
                             reinterpret_cast<const std::uint64_t *>(argv), argc, receiver.bits(),
                             constructing ? 1u : 0u, &produced));
    }

    // NOTHING IS WRITTEN ON ANY OTHER STATUS. An unwound frame is gone and a
    // failed one is the run loop's business; both are re-derived by the
    // interpreter exactly as they are after `op::throw_value`, and a C++ caller
    // sees the failure flag it would have seen from an interpreted throw.
    if (status == aot::ct_aot_status::ok) { out = value::from_bits(produced); }
    return true;
}

} // namespace ctbrowser::script
