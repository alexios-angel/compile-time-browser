//===- Version.hpp - Compiler and engine version ----------------*- C++ -*-===//
//
// What this build of ctcompile is, and what engine it was built against.
//
// The engine half matters most. ctcompile produces applications for one
// specific ctbrowser runtime: the bytecode it reads, the value representation
// it emits against and the helpers it calls are all that engine's, not a
// specification's. A compiler that cannot say which engine it was built
// against cannot be trusted about anything else.
//
//===----------------------------------------------------------------------===//
#pragma once
#include <string>

namespace ctcompile {

/// `major.minor.patch`, from the project() version.
[[nodiscard]] std::string version_string();

/// One line naming the engine this binary reads: its version, and the number of
/// bytecode operations it knows about.
///
/// The opcode count is the size of the AOT coverage problem: every bytecode
/// operation needs a lowering or an explicit refusal, so the count is
/// reported rather than guessed.
[[nodiscard]] std::string engine_summary();

} // namespace ctcompile
