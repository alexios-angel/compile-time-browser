//===- embedded_fonts.hpp - Embedded fonts ----------------------*- C++ -*-===//
//
// The vendored OFL faces, compiled into the binary where the toolchain can.
//
// `use_real_fonts` loads faces through the asset registry, which is consulted
// before the filesystem, so a binary with the fonts compiled in runs from any
// directory without a `fonts/` folder.
//
// Optional: `#embed` (C23/C++26) exists in clang 19+ and llvm-mingw but not
// GCC 13; without it register_embedded_fonts registers nothing and callers
// read the font directory instead.
//
//===----------------------------------------------------------------------===//
#pragma once
#include <string_view>

#include <ctbrowser/shell/page/assets.hpp>

namespace ctbrowser::shell {

/// Registers every vendored face under `<directory>/<Stem>-<Style>.ttf`, the
/// names use_real_fonts asks for. Returns how many were registered: 0 when this
/// build has no embedded fonts, which is not an error.
std::size_t register_embedded_fonts(asset_registry & into, std::string_view directory);

/// Whether this build compiled the fonts in at all, for a diagnostic that
/// would otherwise have to infer it from a count of zero.
[[nodiscard]] bool have_embedded_fonts() noexcept;

} // namespace ctbrowser::shell
