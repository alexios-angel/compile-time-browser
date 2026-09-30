#pragma once
// Private to lib/Shell/bindings/window/: the operating system's CSPRNG, which
// `crypto.getRandomValues` and `crypto.randomUUID` read (crypto.cpp). The
// platform call - getrandom(2), arc4random_buf, BCryptGenRandom - is in
// os_random.cpp alone, so <windows.h> reaches one translation unit.
//
// NEVER SEEDED: Math.random is deterministic so goldens can byte-compare, and
// this is the one source of randomness a page gets that is not. No CTBROWSER_*
// variable, clock or test harness reaches it. std::random_device is not used
// because the standard does not promise it is cryptographic, or even random.

#include <cstdint>
#include <span>

namespace ctbrowser::shell {

// Fills `out` from the OS CSPRNG. False (with `out` unspecified) only if the
// platform refused, which a caller must surface rather than use `out`.
[[nodiscard]] bool os_random(std::span<std::uint8_t> out) noexcept;

} // namespace ctbrowser::shell
