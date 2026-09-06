#include <ctbrowser/aot/aot.hpp>
#include <ctbrowser/aot/aot_entry.h>
#include <ctbrowser/script/builtins.hpp>
#include <ctbrowser/script/compile.hpp>
#include <ctbrowser/script/dispatch.hpp>
#include <ctbrowser/script/vm.hpp>

#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

extern "C" std::int32_t ctcompile_host_prefix_wrapper(ctbrowser::aot::ct_aot_ctx *,
                                                      const ctbrowser::aot::ct_aot_site *,
                                                      const std::uint64_t *, std::uint32_t,
                                                      std::uint64_t, std::uint32_t,
                                                      std::uint64_t *);

namespace {

constexpr std::pair<std::string_view, double> expected[] = {
#include "expected.inc"
};

std::vector<double> run(const std::string & source, bool compiled, bool stress) {
    using namespace ctbrowser::script;
    auto program = compiler::compile(source, script_kind::classic);
    if (!program.ok || program.functions.size() != CTCOMPILE_HOST_PREFIX_FUNCTIONS) {
        std::fprintf(stderr, "host prefix: expected source function count did not compile\n");
        return {};
    }
    if (compiled) {
        program.functions[CTCOMPILE_HOST_PREFIX_ENTRY].aot_entry = &ctcompile_host_prefix_wrapper;
    }
    context runtime;
    install_builtins(runtime);
#ifdef CTCOMPILE_HOST_PREFIX_REALM_SLOT
    // This is the embedding's own-data publication slot, separate from the
    // source's writable globalThis alias and separate self object.
    runtime.define_global("bootstrap", value::undefined());
#endif
    runtime.set_gc_stress(stress);
    reset_transitions();
    const auto result = runtime.run(program);
    if (!result.ok) {
        std::fprintf(stderr, "host prefix: %s\n", result.error.c_str());
        return {};
    }
    if (compiled && transitions(transition::vm_to_aot) != CTCOMPILE_HOST_PREFIX_INVOCATIONS) {
        std::fprintf(stderr, "host prefix: compiled body invocation count changed\n");
        return {};
    }
    std::vector<double> observed;
    for (const auto & [name, value] : expected) {
        const auto found = runtime.globals().find(std::string(name));
        if (found == runtime.globals().end() || !found->second.is_number() ||
            found->second.as_number() != value) {
            std::fprintf(stderr, "host prefix: wrong exact observation %s\n",
                         std::string(name).c_str());
            return {};
        }
        observed.push_back(found->second.as_number());
    }
    return observed;
}

} // namespace

int main(int argc, char ** argv) {
    if (argc != 2) { return 2; }
    std::ifstream input(argv[1], std::ios::binary);
    const std::string source{std::istreambuf_iterator<char>(input),
                             std::istreambuf_iterator<char>()};
    if (source.empty()) { return 1; }
    const auto reference = run(source, false, false);
    if (reference.size() != std::size(expected)) { return 1; }
    for (bool stress : {false, true}) {
        const auto observed = run(source, true, stress);
        if (observed != reference) { return 1; }
    }
    std::printf(
        "host prefix: %zu exact observations agree with specialized wrapper, including GC stress\n",
        reference.size());
    return 0;
}
