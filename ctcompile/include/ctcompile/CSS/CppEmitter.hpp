//===- CppEmitter.hpp - Compiled CSS as C++ data ----------------*- C++ -*-===//
//
// Writes a compiled stylesheet (ctbrowser/style/css/compiled.hpp) as C++
// constants: one `inline constexpr` table per kind of record, each record a
// designated initializer naming only the fields that differ from the default,
// and one `compiled_sheet` constant pointing at the tables. Long text (the
// pool, the names) is a table of chunks, each a string literal of at most
// 32 KiB, which compilers accept without -Woverlength-strings. A program that
// includes the file materializes the sheet without parsing CSS. The output
// starts with a comment naming the source file and its SHA-256, so a stale
// file can be told from a current one.
//
// The emitter prints what css::compile produced and decides nothing: which
// records exist and what they hold is ctbrowser's parser's answer.
//
//===----------------------------------------------------------------------===//

#pragma once

#include <ostream>
#include <string_view>

#include <ctbrowser/style/css/compiled.hpp>

namespace ctcompile::css {

/// Writes the comment that opens a generated file, its include, and the
/// namespace aliases the tables use, inside `namespace ns {`. close_file()
/// ends the namespace.
void open_file(std::ostream & out, std::string_view source, std::string_view sha256,
               std::string_view ns);
void close_file(std::ostream & out, std::string_view ns);

/// Writes `sheet` as tables named `<name>_<kind>` and the constant `<name>`, a
/// ctbrowser::style::css::compiled_sheet. `name` must be a C++ identifier.
void emit_sheet(std::ostream & out, const ctbrowser::style::css::compiled_sheet & sheet,
                std::string_view name);

/// Writes `bytes` as `inline constexpr std::string_view <name>[]`: chunks of
/// literals of at most 32 KiB, a ctbrowser::style::css::text_chunks. Writes
/// nothing for empty text, and returns whether a table was written.
bool emit_text(std::ostream & out, std::string_view name, std::string_view bytes);

/// Writes `tokens` as tables named `<name>_pool` and `<name>_tokens` and the
/// constant `<name>`, a ctbrowser::style::css::compiled_tokens.
void emit_tokens(std::ostream & out, const ctbrowser::style::css::compiled_tokens & tokens,
                 std::string_view name);

} // namespace ctcompile::css
