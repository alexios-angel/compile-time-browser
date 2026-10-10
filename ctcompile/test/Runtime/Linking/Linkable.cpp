//===- ctcompile/test/Runtime/Linking/Linkable.cpp - generated code links -===//
//
// Does the generated code link?
//
// Every EmitC lit test compiles emitted code with -fsyntax-only, which cannot
// tell a helper aot.hpp only declares (from ctbrowser/include/ctbrowser/aot/
// helpers/*.def) from one lib/Script/aot_bridge/ defines; such a call compiles
// and fails only at link. This test links.
//
// The link is the assertion. This translation unit is built from linkable.js
// through the real pipeline and linked against the runtime; if the backend
// names a symbol that does not exist, the build fails with the symbol's name in
// the message. main() only has to make sure the entry is not discarded.
//
// It deliberately does not call it. Running generated code against the runtime
// is GC/Roots.cpp's job and needs a context, a program and a frame; this needs
// none of that, and keeping the two apart means a link failure and a wrong
// answer never look like each other.
//
//===----------------------------------------------------------------------===//
#include <cstdint>
#include <cstdio>

extern "C" std::int32_t ctc_everything(void *, const void *, const std::uint64_t *, std::uint32_t,
                                       std::uint64_t, std::uint32_t, std::uint64_t *);

int main() {
    // Taking the address is enough, and it has to be done at run time rather
    // than in a fold the optimiser can discard - otherwise a sufficiently
    // clever build could drop the reference and with it the link check.
    void * const entry = reinterpret_cast<void *>(&ctc_everything);
    if (entry == nullptr) {
        std::printf("the compiled entry is null, which cannot happen\n");
        return 1;
    }
    std::printf("the generated translation unit links\n");
    return 0;
}
