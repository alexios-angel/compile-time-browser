// Compiled frames are outside the escape oracle's source-site proof. Exercise
// real AOT entries so neither a catch-pad id nor the VM's shared unwinder can
// turn their allocations into bytecode observations that look adjudicated.
#include <ctbrowser/aot/aot.hpp>
#include <ctbrowser/script/builtins.hpp>
#include <ctbrowser/script/compile.hpp>
#include <ctbrowser/script/type_record.hpp>
#include <ctbrowser/script/vm.hpp>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string_view>

namespace {
namespace aot = ctbrowser::aot;
using namespace ctbrowser::script;

int failures = 0;
std::uint32_t landing_pad = 0;
std::size_t returned_calls = 0;
std::size_t thrown_calls = 0;
std::size_t landed_calls = 0;

void check(bool ok, const char * message) {
    if (!ok) {
        std::printf("FAIL %s\n", message);
        ++failures;
    }
}

std::int32_t finish(aot::ct_aot_frame * frame, aot::ct_aot_status status) {
    if (status != aot::ct_aot_status::unwound) { aot::ct_aot_leave(frame); }
    return static_cast<std::int32_t>(status);
}

extern "C" std::int32_t compiled_return(aot::ct_aot_ctx * ctx, const aot::ct_aot_site * site,
                                        const std::uint64_t *, std::uint32_t,
                                        std::uint64_t receiver, std::uint32_t constructing,
                                        std::uint64_t * out) {
    ++returned_calls;
    alignas(std::max_align_t) unsigned char storage[CT_AOT_FRAME_BYTES];
    auto * frame = aot::ct_aot_enter(ctx, site, 4, receiver, storage);
    if (frame == nullptr) { return static_cast<std::int32_t>(aot::ct_aot_status::failed); }
    const std::uint64_t object = aot::ct_aot_new_object(frame);
    aot::ct_aot_slots(frame)[0] = object;
    *out = aot::ct_aot_return_value(object, receiver, constructing);
    return finish(frame, aot::ct_aot_status::ok);
}

extern "C" std::int32_t compiled_throw(aot::ct_aot_ctx * ctx, const aot::ct_aot_site * site,
                                       const std::uint64_t *, std::uint32_t, std::uint64_t receiver,
                                       std::uint32_t, std::uint64_t *) {
    ++thrown_calls;
    alignas(std::max_align_t) unsigned char storage[CT_AOT_FRAME_BYTES];
    auto * frame = aot::ct_aot_enter(ctx, site, 4, receiver, storage);
    if (frame == nullptr) { return static_cast<std::int32_t>(aot::ct_aot_status::failed); }
    aot::ct_aot_slots(frame)[0] = aot::ct_aot_new_object(frame);
    const std::uint64_t thrown = aot::ct_aot_new_object(frame);
    aot::ct_aot_slots(frame)[1] = thrown;
    const auto status = static_cast<aot::ct_aot_status>(aot::ct_aot_throw(frame, thrown));
    check(status == aot::ct_aot_status::unwound, "compiled throw reached its VM caller's catch");
    return finish(frame, status);
}

extern "C" std::int32_t compiled_landing(aot::ct_aot_ctx * ctx, const aot::ct_aot_site * site,
                                         const std::uint64_t * argv, std::uint32_t argc,
                                         std::uint64_t receiver, std::uint32_t, std::uint64_t *) {
    // ct_aot_enter may move the arriving window.
    const std::uint64_t callee = argc == 0 ? value::undefined().bits() : argv[0];
    alignas(std::max_align_t) unsigned char storage[CT_AOT_FRAME_BYTES];
    auto * frame = aot::ct_aot_enter(ctx, site, 4, receiver, storage);
    if (frame == nullptr) { return static_cast<std::int32_t>(aot::ct_aot_status::failed); }
    aot::ct_aot_slots(frame)[0] = callee;
    aot::ct_aot_handler_push(frame, landing_pad, 3);
    const auto caught =
        static_cast<aot::ct_aot_status>(aot::ct_aot_throw(frame, value::number(1).bits()));
    check(caught == aot::ct_aot_status::caught, "compiled frame caught its own first throw");
    if (caught != aot::ct_aot_status::caught) { return finish(frame, caught); }

    // Deliberately allocate before consuming the pending pad. It is a legal
    // runtime allocation, but the low 32 bits of ip - 1 now collide with this
    // function's real new_object PC. Neither value is a source coordinate.
    aot::ct_aot_slots(frame)[1] = aot::ct_aot_new_object(frame);
    std::uint64_t ignored = value::undefined().bits();
    check(aot::ct_aot_catch_land(frame, &ignored) == landing_pad, "the colliding pad was taken");
    ++landed_calls;
    // An allocation after consuming the pad used to be called `prologue`,
    // even though the frame has already executed a catch.
    aot::ct_aot_slots(frame)[2] = aot::ct_aot_new_array(frame, 0);

    // The VM child and this compiled parent unwind together. The child's
    // thrown object remains an ordinary, checked bytecode observation.
    const auto status = static_cast<aot::ct_aot_status>(
        aot::ct_aot_call(frame, aot::ct_aot_slots(frame)[0], value::undefined().bits(), nullptr, 0,
                         0, site, &ignored));
    check(status == aot::ct_aot_status::unwound, "VM throw crossed the compiled frame");
    return finish(frame, status);
}

constexpr std::string_view fixture = R"JS(
function nativeReturn() { return {}; }
function nativeThrow() { var local = {}; throw {}; }
function nativeLanding(f) { try { throw 1; } catch (e) { var local = {}; f(); } }
function vmThrower() { throw {}; }
function vmLocal() { var local = {}; return 42; }
var first = nativeReturn();
var second = nativeReturn();
try { nativeThrow(); } catch (e) { var caught = e; }
try { nativeLanding(vmThrower); } catch (e) { var mixed = e; }
var answer = vmLocal();
)JS";

struct run_counts {
    std::size_t collections = 0;
    std::size_t live = 0;
};

run_counts run(type_recorder * recorder) {
    program prog = compiler::compile(fixture);
    check(prog.ok, "the AOT oracle fixture compiles");
    if (!prog.ok) { return {}; }
    landing_pad = 0;
    for (function_proto & fn : prog.functions) {
        if (fn.name == "nativeReturn") { fn.aot_entry = compiled_return; }
        if (fn.name == "nativeThrow") { fn.aot_entry = compiled_throw; }
        if (fn.name != "nativeLanding") { continue; }
        fn.aot_entry = compiled_landing;
        for (std::size_t pc = 0; pc < fn.code.size(); ++pc) {
            if (fn.code[pc].code == op::new_object) {
                landing_pad = static_cast<std::uint32_t>(pc + 1);
                break;
            }
        }
    }
    check(landing_pad != 0, "the catch-pad regression names a real static object allocation");
    returned_calls = thrown_calls = landed_calls = 0;
    set_active_type_recorder(recorder);
    run_counts counts;
    {
        context cx;
        install_builtins(cx);
        const run_result result = cx.run(prog);
        check(result.ok, "the mixed AOT/VM fixture runs");
        if (!result.ok) { std::printf("%s\n", result.error.c_str()); }
        check(returned_calls == 2 && thrown_calls == 1 && landed_calls == 1,
              "all four compiled entries actually ran");
        check(cx.global("first").is_object() && cx.global("second").is_object() &&
                  cx.global("first").bits() != cx.global("second").bits(),
              "two compiled return values survive as distinct objects");
        check(cx.global("caught").is_object() && cx.global("mixed").is_object(),
              "compiled and interpreted thrown objects reach the VM caller");
        check(cx.global("answer").is_number() && cx.global("answer").as_number() == 42,
              "the following interpreted call still runs");
        counts = {cx.collections(), cx.live_objects()};
    }
    set_active_type_recorder(nullptr);
    return counts;
}

std::size_t find_function(const type_recorder & recorder, std::string_view name) {
    const auto & functions = recorder.functions();
    for (std::size_t i = 0; i < functions.size(); ++i) {
        if (functions[i].name == name) { return i; }
    }
    check(false, "the recorder inventoried every fixture function");
    return functions.size();
}

void check_compiled(const type_recorder & recorder, std::string_view name, heap_kind kind,
                    std::uint64_t made) {
    const std::size_t index = find_function(recorder, name);
    const auto all = recorder.all_sites();
    if (index >= all.size()) { return; }
    check(recorder.functions()[index].checks == 0, "compiled frames never spend the check budget");
    std::size_t found = 0;
    for (const site_observation & site : all[index]) {
        if (site.kind != kind) { continue; }
        ++found;
        check(site.pc == compiled_pc, "a compiled site is neither a prologue nor a bytecode PC");
        check(site.made == made && site.unchecked == made,
              "every compiled allocation stays UNCHECKED on return and unwind");
        check(site.confined == 0 && site.escaped == 0 && site.unresolved == 0,
              "a compiled frame has no adjudicated escape verdict");
        for (const static_site & claim : recorder.functions()[index].allocs) {
            check(claim.pc != site.pc, "a compiled observation cannot join a bytecode claim");
        }
    }
    check(found == 1, "one non-bytecode site groups each compiled allocation kind");
}

void check_interpreted(const type_recorder & recorder, std::string_view name, bool escapes) {
    const std::size_t index = find_function(recorder, name);
    const auto all = recorder.all_sites();
    if (index >= all.size()) { return; }
    check(all[index].size() == 1, "the interpreted function has one observed object site");
    if (all[index].size() != 1) { return; }
    const site_observation & site = all[index].front();
    check(site.kind == heap_kind::object && site.pc != compiled_pc && site.pc != prologue_pc,
          "the interpreted site retains its bytecode coordinate");
    check(site.made == 1 && site.unchecked == 0 && site.unresolved == 0,
          "the interpreted allocation remains checked");
    check(site.escaped == (escapes ? 1u : 0u) && site.confined == (escapes ? 0u : 1u),
          "the interpreted escape verdict is unchanged across a mixed stack");
    if (escapes) {
        check(site.routes[static_cast<std::size_t>(root_label::thrown)] == 1,
              "the VM child's thrown object is rooted during the mixed unwind");
    }
}
} // namespace

int main(int argc, char ** argv) {
    if (!type_recording_enabled()) {
        std::printf("ok escape_oracle_aot (SKIPPED - recording disabled)\n");
        return 0;
    }
    if (argc != 1 && (argc != 3 || std::strcmp(argv[1], "--out") != 0)) { return 2; }
    const run_counts baseline = run(nullptr);
    type_recorder recorder;
    const run_counts recorded = run(&recorder);
    check(baseline.collections == recorded.collections && baseline.live == recorded.live,
          "recording neither collects nor changes the live heap");
    check_compiled(recorder, "nativeReturn", heap_kind::object, 2);
    check_compiled(recorder, "nativeThrow", heap_kind::object, 2);
    check_compiled(recorder, "nativeLanding", heap_kind::object, 1);
    check_compiled(recorder, "nativeLanding", heap_kind::array, 1);
    check_interpreted(recorder, "vmThrower", true);
    check_interpreted(recorder, "vmLocal", false);
    check(recorder.pending_records() == 2, "only the two unhooked AOT returns remain pending");
    check(recorder.unwinds() > 0 && recorder.checks() > 0,
          "the test exercised the shared unwinder and real interpreter checks");
    if (argc == 3) { check(recorder.write(argv[2]), "the AOT recording was written"); }
    if (failures == 0) { std::printf("ok escape_oracle_aot\n"); }
    return failures == 0 ? 0 : 1;
}
