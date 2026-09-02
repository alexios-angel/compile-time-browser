// THE NATIVE GATE'S REFERENCE SIDE - ctcompile Phase 62½-D.
//
// "When a CTJS operation and the ctbrowser VM disagree, the VM is correct by
// definition." So nothing about a native binary's answers is written down
// anywhere: this runs the SAME JavaScript under the interpreter and prints
// what it left in the globals, in the exact shape the native binary's `main`
// prints its own (native-fixture.emitc.mlir defines that shape; this file
// mirrors it), and check-native-unit.cmake compares the two texts.
//
// THE SHAPE, restated: one line per Number-valued global the program created
// or changed, `<name>=<value>`, ascending bytewise by name, the value as
// printf("%.17g") of the double - which round-trips it exactly, so the
// comparison is on the double and not on JavaScript's shortest-representation
// string.
//
// WHAT IS SKIPPED IS COUNTED, on stderr, because the gate asserts it. The
// fixture is numbers and booleans only, and a boolean cannot survive as a
// global under Phase 62½-C, so any non-Number global that is not a function
// is a fixture error - and a reference that silently skipped it would let the
// native side agree on a subset. Functions are expected: every top-level
// `function` is a global too.
//
// "CREATED OR CHANGED" rather than "every global", because install_builtins
// fills the table first - Math, Object, the lot - and none of that is the
// program's. A snapshot before the run says which entries were already there
// with which value; a program that reassigns a builtin's name still counts,
// since its value changed.
#include <ctbrowser/script/builtins.hpp>
#include <ctbrowser/script/compile.hpp>
#include <ctbrowser/script/vm.hpp>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

int main(int argc, char ** argv) {
    if (argc != 2) {
        std::fprintf(stderr, "usage: ctcompile-test-native-reference PROGRAM.js\n");
        return 2;
    }
    std::ifstream in{argv[1], std::ios::binary};
    if (!in) {
        std::fprintf(stderr, "native reference: cannot read %s\n", argv[1]);
        return 1;
    }
    const std::string source{std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};

    // THE PROGRAM OUTLIVES THE CONTEXT - vm.hpp's rule, since closures hold
    // function_protos that point into it - so it is compiled first and declared
    // first, and the context below is destroyed before it.
    const ctbrowser::script::program prog = ctbrowser::script::compiler::compile(source);
    if (!prog.ok) {
        std::fprintf(stderr, "native reference: %s does not compile: %s\n", argv[1],
                     prog.error.c_str());
        return 1;
    }

    ctbrowser::script::context cx;
    ctbrowser::script::install_builtins(cx);
    std::unordered_map<std::string, std::uint64_t> before;
    for (const auto & [name, v] : cx.globals()) { before.emplace(name, v.bits()); }

    const ctbrowser::script::run_result result = cx.run(prog);
    if (!result.ok) {
        std::fprintf(stderr, "native reference: %s threw: %s\n", argv[1], result.error.c_str());
        return 1;
    }

    std::vector<std::pair<std::string, double>> numbers;
    std::size_t functions = 0;
    std::size_t others = 0;
    for (const auto & [name, v] : cx.globals()) {
        const auto was = before.find(name);
        if (was != before.end() && was->second == v.bits()) { continue; }
        if (v.is_number()) {
            numbers.emplace_back(name, v.as_number());
        } else if (v.is_kind(ctbrowser::script::heap_kind::function)) {
            ++functions;
        } else {
            std::fprintf(stderr, "native reference: %s is not a Number (%s)\n", name.c_str(),
                         std::string{ctbrowser::script::context::type_of(v)}.c_str());
            ++others;
        }
    }
    std::ranges::sort(numbers, {}, &std::pair<std::string, double>::first);
    for (const auto & [name, d] : numbers) { std::printf("%s=%.17g\n", name.c_str(), d); }
    std::fprintf(stderr,
                 "native reference: %zu number globals printed, %zu function globals skipped, "
                 "%zu other globals skipped\n",
                 numbers.size(), functions, others);
    return 0;
}
