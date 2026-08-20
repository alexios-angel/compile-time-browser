#pragma once
#include <string>

// What this build of ctcompile is, and what engine it was built against.
//
// The second half is the point. ctcompile's whole contract is that it produces
// applications for one specific ctbrowser runtime - the bytecode it reads, the
// value representation it emits against and the helpers it calls are all that
// engine's, not a specification's. A compiler that cannot say which engine it
// was built against cannot be trusted about anything else.
namespace ctcompile {

// `major.minor.patch`, from the project() version.
[[nodiscard]] std::string version_string();

// One line naming the engine this binary reads: its version, and the number of
// bytecode operations it knows about.
//
// The opcode count is not decoration. It is the size of the AOT coverage
// problem, and Phase 0's bytecode inventory has to account for every one of
// them; reporting it here means the number is never guessed at.
[[nodiscard]] std::string engine_summary();

} // namespace ctcompile
