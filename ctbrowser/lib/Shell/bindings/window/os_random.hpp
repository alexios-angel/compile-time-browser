//===- os_random.hpp - OS cryptographic random bytes ------------*- C++ -*-===//
//
// Private to lib/Shell/bindings/window/: the operating system's CSPRNG, which
// `crypto.getRandomValues` and `crypto.randomUUID` read (crypto.cpp). The
// platform call (getrandom(2), arc4random_buf, BCryptGenRandom) lives in
// os_random.cpp alone, so <windows.h> reaches one translation unit.
//
// This source is never seeded. Math.random is deterministic so goldens can
// byte-compare, and this is the one source of randomness a page gets that is
// not: no CTBROWSER_* variable, clock or test harness reaches it.
// std::random_device is not used because the standard does not promise it is
// cryptographic, or even random.
//
//===----------------------------------------------------------------------===//
#pragma once

#include <cstdint>
#include <span>

namespace ctbrowser::shell {

/// Fills `out` from the OS CSPRNG. Returns false (with `out` unspecified) only
/// if the platform refused; the caller must report that rather than use `out`.
[[nodiscard]] bool os_random(std::span<std::uint8_t> out) noexcept;

} // namespace ctbrowser::shell
