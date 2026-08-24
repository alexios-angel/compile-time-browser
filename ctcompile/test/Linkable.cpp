// DOES THE GENERATED CODE LINK?
//
// Nothing else in the suite asks. Every EmitC lit test compiles what the
// backend emits with -fsyntax-only, which cannot tell a helper that exists from
// one that is only declared - and aot.hpp declares all 69 while aot_bridge.cpp
// defines 32. A call to one of the other 37 compiles perfectly and fails at
// link, which is how ct_aot_global_get and ct_aot_negate were emitted for two
// commits with a green suite.
//
// THE LINK IS THE ASSERTION. This translation unit is built from linkable.js
// through the real pipeline and linked against the runtime; if the backend
// names a symbol that does not exist, the BUILD fails with the symbol's name in
// the message. main() only has to make sure the entry is not discarded.
//
// It deliberately does not CALL it. Running generated code against the runtime
// is GCRoots.cpp's job and needs a context, a program and a frame; this needs
// none of that, and keeping the two apart means a link failure and a wrong
// answer never look like each other.
#include <cstdint>
#include <cstdio>

extern "C" std::int32_t ctcompile_test_entry(void *, const void *, const std::uint64_t *,
                                             std::uint32_t, std::uint64_t, std::uint32_t,
                                             std::uint64_t *);

int main() {
    // TAKING THE ADDRESS IS ENOUGH, and it has to be done at run time rather
    // than in a fold the optimiser can discard - otherwise a sufficiently
    // clever build could drop the reference and with it the link check.
    void * const entry = reinterpret_cast<void *>(&ctcompile_test_entry);
    if (entry == nullptr) {
        std::printf("the compiled entry is null, which cannot happen\n");
        return 1;
    }
    std::printf("the generated translation unit links\n");
    return 0;
}
