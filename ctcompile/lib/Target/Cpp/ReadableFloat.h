#pragma once

//===- ReadableFloat.h - Readable float literals ----------------*- C++ -*-===//
//
// Prints float and double constants in their shortest exact decimal form, for
// modules that ask for readable literals.
//
//===----------------------------------------------------------------------===//

namespace llvm {
class APFloat;
class raw_ostream;
} // namespace llvm

namespace ctcompile::cpp {

/// Emits a finite IEEE float or double as the shortest C++ literal that
/// round-trips. Returns false and leaves the stream untouched for other
/// semantics and non-finite values, which keep the upstream emitter's spelling.
bool emitReadableFloat(const llvm::APFloat & value, llvm::raw_ostream & output);

} // namespace ctcompile::cpp
