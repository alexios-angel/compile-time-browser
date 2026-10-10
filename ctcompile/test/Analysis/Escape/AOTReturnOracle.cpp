//===- AOTReturnOracle.cpp - Compiled returns reach the oracle --*- C++ -*-===//
//
// Checks that compiled (EmitC) function bodies built from aot/return.js report
// their return value to the escape-analysis recorder, so objects they return
// are observed as escaping. Their allocations are recorded at compiled_pc and
// are never attributed to a bytecode allocation site.
//
//===----------------------------------------------------------------------===//
#include <ctbrowser/aot/aot.hpp>
#include <ctbrowser/script/builtins.hpp>
#include <ctbrowser/script/compile.hpp>
#include <ctbrowser/script/type_record.hpp>
#include <ctbrowser/script/vm.hpp>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>

#include "check.hpp"
#include "options.hpp"

namespace aot = ctbrowser::aot;
using namespace ctbrowser::script;
extern "C" std::remove_pointer_t<aot::ct_aot_entry_fn> ctc_oracleReturn;
extern "C" std::remove_pointer_t<aot::ct_aot_entry_fn> ctc_oracleLocal;
extern "C" std::remove_pointer_t<aot::ct_aot_entry_fn> ctc_oracleCtor;

namespace {
constexpr std::string_view fixture =
#include "escape-oracle-aot-return.js.inc"
    ;

/// A hand-written entry standing in for a compiled body that does not report
/// its return value. For oracleFailure it throws first, to check that a failed
/// exit records no verdict.
extern "C" std::int32_t legacy_or_failed(aot::ct_aot_ctx * ctx, const aot::ct_aot_site * site,
                                         const std::uint64_t *, std::uint32_t,
                                         std::uint64_t receiver, std::uint32_t constructing,
                                         std::uint64_t * out) {
    alignas(std::max_align_t) unsigned char storage[CT_AOT_FRAME_BYTES];
    auto * frame = aot::ct_aot_enter(ctx, site, 2, receiver, storage);
    if (frame == nullptr) { return static_cast<std::int32_t>(aot::ct_aot_status::failed); }
    const std::uint64_t object = aot::ct_aot_new_object(frame);
    aot::ct_aot_slots(frame)[0] = object;
    *out = aot::ct_aot_return_value(object, receiver, constructing);
    const auto * proto = reinterpret_cast<const function_proto *>(site);
    if (proto->name == "oracleFailure") {
        const auto status =
            static_cast<aot::ct_aot_status>(aot::ct_aot_throw(frame, value::number(1).bits()));
        check(status == aot::ct_aot_status::failed, "the failure control raises through the ABI");
        // A failed exit must not gain a verdict by supplying a nominal result.
        aot::ct_aot_leave_return(frame, *out);
        return static_cast<std::int32_t>(aot::ct_aot_status::failed);
    }
    aot::ct_aot_leave(frame);
    return static_cast<std::int32_t>(aot::ct_aot_status::ok);
}

struct counts {
    std::size_t collections = 0;
    std::size_t live = 0;
};

/// Runs the fixture with the generated entries (and legacy_or_failed)
/// installed, recording into \p recorder when non-null. With \p also_interpret,
/// oracleReturn is called once more with its AOT entry removed. Returns heap
/// counts so a recorded run can be compared with an unrecorded one.
counts run(type_recorder * recorder, bool also_interpret = false) {
    program prog = compiler::compile(fixture);
    check(prog.ok, "the exact generated fixture compiles");
    if (!prog.ok) { return {}; }
    function_proto * returning = nullptr;
    function_proto * constructor = nullptr;
    for (function_proto & fn : prog.functions) {
        if (fn.name == "oracleReturn") {
            fn.aot_entry = ctc_oracleReturn;
            returning = &fn;
        }
        if (fn.name == "oracleLocal") { fn.aot_entry = ctc_oracleLocal; }
        if (fn.name == "oracleCtor") {
            fn.aot_entry = ctc_oracleCtor;
            constructor = &fn;
        }
        if (fn.name == "oracleLegacy" || fn.name == "oracleFailure") {
            fn.aot_entry = legacy_or_failed;
        }
    }
    check(returning != nullptr, "the generated return body is installed");
    check(constructor != nullptr, "the generated constructor body is installed");
    set_active_type_recorder(recorder);
    counts result;
    {
        context cx;
        install_builtins(cx);
        check(cx.run(prog).ok, "the fixture declarations run");
        cx.define_global("first", cx.call(cx.global("oracleReturn"), {}));
        cx.define_global("second", cx.call(cx.global("oracleReturn"), {}));
        check(cx.global("first").is_object() && cx.global("second").is_object() &&
                  cx.global("first").bits() != cx.global("second").bits(),
              "generated return bodies retain distinct owning object graphs");
        check(cx.lookup_property(cx.global("first"), "child").is_object(),
              "the returned child remains live");
        const value number = cx.call(cx.global("oracleLocal"), {});
        check(number.is_number() && number.as_number() == 42, "the local-only body returns 42");
        // Invoke the real generated ABI without construct()'s temporary root.
        // oracleCtor returns 7, a primitive, so the constructor result must be
        // normalized to the receiver; only that normalized handoff keeps the
        // receiver's child reachable after the frame is removed.
        const value receiver = cx.make_object();
        std::uint64_t constructed = value::undefined().bits();
        if (constructor != nullptr) {
            const auto status = static_cast<aot::ct_aot_status>(
                ctc_oracleCtor(reinterpret_cast<aot::ct_aot_ctx *>(&cx),
                               reinterpret_cast<const aot::ct_aot_site *>(constructor), nullptr, 0,
                               receiver.bits(), 1, &constructed));
            check(status == aot::ct_aot_status::ok && constructed == receiver.bits(),
                  "the generated constructor returns its normalized receiver");
        }
        cx.define_global("constructed", value::from_bits(constructed));
        check(cx.lookup_property(cx.global("constructed"), "child").is_object(),
              "constructor normalization retains its receiver's child");
        cx.define_global("legacy", cx.call(cx.global("oracleLegacy"), {}));
        check(cx.global("legacy").is_object(), "the legacy exit still returns its object");
        if (also_interpret && returning != nullptr) {
            returning->aot_entry = nullptr;
            cx.define_global("interpreted", cx.call(cx.global("oracleReturn"), {}));
            check(cx.global("interpreted").is_object(), "the same proto also runs interpreted");
        }
        check(!cx.failed(), "all ordinary calls finish before the failure control");
        (void)cx.call(cx.global("oracleFailure"), {});
        check(cx.failed(), "the failure control reached the uncatchable failure tier");
        result = {cx.collections(), cx.live_objects()};
    }
    set_active_type_recorder(nullptr);
    return result;
}

std::size_t function_index(const type_recorder & recorder, std::string_view name) {
    for (std::size_t i = 0; i < recorder.functions().size(); ++i) {
        if (recorder.functions()[i].name == name) { return i; }
    }
    check(false, "the recorder inventoried every fixture function");
    return recorder.functions().size();
}

/// Checks function \p name's single compiled_pc site against the expected
/// made/escaped/confined/unchecked counts and its compiled return-check count.
void expect_compiled(const type_recorder & recorder, std::string_view name, std::uint64_t made,
                     std::uint64_t escaped, std::uint64_t confined, std::uint64_t unchecked,
                     std::uint64_t checks) {
    const auto index = function_index(recorder, name);
    if (index >= recorder.functions().size()) { return; }
    check(recorder.functions()[index].compiled_checks == checks,
          "the compiled return check counter matches executed handoffs");
    const auto sites = recorder.all_sites();
    std::size_t found = 0;
    for (const site_observation & site : sites[index]) {
        if (site.pc != compiled_pc) { continue; }
        ++found;
        check(site.kind == heap_kind::object && site.made == made && site.escaped == escaped &&
                  site.confined == confined && site.unchecked == unchecked && site.unresolved == 0,
              "compiled allocations have the expected returned/local/unchecked partition");
        check(site.routes[static_cast<std::size_t>(root_label::temporaries)] == escaped,
              "only the actual in-flight return graph is rooted through temporaries");
        for (const static_site & source : recorder.functions()[index].allocs) {
            check(source.pc != site.pc, "checked compiled observations have no source-site claim");
        }
    }
    check(found == 1, "the compiled object site retains its unique sentinel coordinate");
}
} // namespace

int main(int argc, char ** argv) {
    namespace po = boost::program_options;
    std::optional<std::string> out;
    po::options_description options{
        "usage: ctcompile-test-escape-oracle-aot-return [--out RECORDING]"};
    options.add_options()("out", po::value(&out), "also write the recording to RECORDING");
    if (!ctbrowser_cli::parse_options(argc, argv, options)) { return 2; }
    const counts baseline = run(nullptr);
    type_recorder recorder;
    const counts recorded = run(&recorder);
    check(baseline.collections == recorded.collections && baseline.live == recorded.live,
          "observing generated returns neither collects nor changes the live heap");
    // Arguments: allocations made, escaped, confined, unchecked, then return
    // checks run, summed over every call in the recorded run (see aot/return.js;
    // oracleReturn is called twice).
    expect_compiled(recorder, "oracleReturn", 6, 4, 2, 0, 2);
    expect_compiled(recorder, "oracleLocal", 1, 0, 1, 0, 1);
    expect_compiled(recorder, "oracleCtor", 1, 1, 0, 0, 1);
    expect_compiled(recorder, "oracleLegacy", 1, 0, 0, 1, 0);
    expect_compiled(recorder, "oracleFailure", 1, 0, 0, 1, 0);
    check(recorder.pending_records() == 2, "only legacy and failed exits remain pending");

    // With an escape budget of 1, only the first compiled return is checked;
    // the rest stay unchecked. The interpreted call made afterwards (run's
    // also_interpret) is recorded at ordinary bytecode sites.
    type_recorder budgeted;
    budgeted.set_escape_budget(1);
    (void)run(&budgeted, true);
    expect_compiled(budgeted, "oracleReturn", 6, 2, 1, 3, 1);
    const auto index = function_index(budgeted, "oracleReturn");
    if (index < budgeted.functions().size()) {
        check(budgeted.functions()[index].checks == 1,
              "compiled return observations do not spend the interpreted check budget");
        const auto sites = budgeted.all_sites();
        std::uint64_t made = 0, escaped = 0, confined = 0, unchecked = 0;
        for (const site_observation & site : sites[index]) {
            if (site.pc == compiled_pc) { continue; }
            made += site.made;
            escaped += site.escaped;
            confined += site.confined;
            unchecked += site.unchecked;
        }
        check(made == 3 && escaped == 2 && confined == 1 && unchecked == 0,
              "the later interpreted call retains ordinary source-site observations");
    }
    if (out) { check(recorder.write(*out), "the compiled return recording was written"); }
    if (ctbrowser_test_failures == 0) { std::printf("ok escape_oracle_aot_return\n"); }
    return ctbrowser_test_failures == 0 ? 0 : 1;
}
