#include <ctbrowser/aot/aot.hpp>
#include <ctbrowser/aot/aot_entry.h>
#include <ctbrowser/script/builtins.hpp>
#include <ctbrowser/script/compile.hpp>
#include <ctbrowser/script/vm.hpp>

#include <cstdint>
#include <cstdio>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#define ENTRY(name)                                                                                \
    extern "C" std::int32_t name(ctbrowser::aot::ct_aot_ctx *,                                     \
                                 const ctbrowser::aot::ct_aot_site *, const std::uint64_t *,       \
                                 std::uint32_t, std::uint64_t, std::uint32_t, std::uint64_t *);
ENTRY(ctc_formula)
ENTRY(ctc_drive)
#undef ENTRY

namespace {
constexpr std::string_view source =
#include "specialization-dispatch.js.inc"
    ;

std::optional<std::pair<double, double>> run(bool compiled) {
    auto program = ctbrowser::script::compiler::compile(std::string(source));
    if (!program.ok) { return {}; }
    unsigned installed = 0;
    if (compiled) {
        for (auto & function : program.functions) {
            if (function.name == "formula") {
                function.aot_entry = &ctc_formula;
                ++installed;
            } else if (function.name == "drive") {
                function.aot_entry = &ctc_drive;
                ++installed;
            }
        }
        if (installed != 2) { return {}; }
    }
    ctbrowser::script::context context;
    ctbrowser::script::install_builtins(context);
    if (!context.run(program).ok) { return {}; }
    const auto result = context.global("observation"), effect = context.global("effect");
    if (!result.is_number() || !effect.is_number()) { return {}; }
    return std::pair{result.as_number(), effect.as_number()};
}
} // namespace

int main() {
    const auto interpreted = run(false), compiled = run(true);
    if (!interpreted || !compiled || *interpreted != *compiled || interpreted->first != 1020.0 ||
        interpreted->second != 2.0) {
        std::fputs("specialized boxed dispatch disagrees with the original program\n", stderr);
        return 1;
    }
    std::puts("boxed specialization dispatch: observation=1020, effect=2");
}
