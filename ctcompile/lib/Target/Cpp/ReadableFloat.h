#pragma once

namespace llvm {
class APFloat;
class raw_ostream;
} // namespace llvm

namespace ctcompile::cpp {

// Emit a finite IEEE float/double as a shortest round-trip C++ literal.
// False leaves the stream untouched so unsupported semantics and non-finite
// values retain the upstream emitter's spelling.
bool emitReadableFloat(const llvm::APFloat & value, llvm::raw_ostream & output);

} // namespace ctcompile::cpp
