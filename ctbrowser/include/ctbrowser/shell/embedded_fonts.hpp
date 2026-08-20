#pragma once
#include <string_view>

#include <ctbrowser/shell/page/assets.hpp>

// The vendored OFL faces, baked into the binary where the toolchain can.
//
// `use_real_fonts` loads them through the asset registry, and the registry is
// consulted BEFORE the filesystem - which was always the design ("a binary that
// baked them in never touches the disk"), with nothing actually baking them in.
// This is that. An application that has them compiled in runs from any
// directory with no `fonts/` beside it.
//
// OPPORTUNISTIC. `#embed` is C23 and C++26; clang has had it since 19 and the
// llvm-mingw cross toolchain has it, but GCC 13 does not, so this compiles to a
// function that registers nothing and says so. The caller falls back to reading
// the directory, which is what every build did until now.

namespace ctbrowser::shell {

// Register every vendored face under `<directory>/<Stem>-<Style>.ttf`, the
// names use_real_fonts asks for. Returns how many were registered - 0 when this
// build has no embedded fonts, which is not an error.
std::size_t register_embedded_fonts(asset_registry & into, std::string_view directory);

// Whether this build baked them in at all. For a diagnostic that would
// otherwise have to infer it from a count of zero.
[[nodiscard]] bool have_embedded_fonts() noexcept;

} // namespace ctbrowser::shell
