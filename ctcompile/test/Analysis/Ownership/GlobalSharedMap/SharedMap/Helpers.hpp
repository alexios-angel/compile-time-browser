//===- Helpers.hpp - Captured-Map test helpers ------------------*- C++ -*-===//
//
// Declarations for the captured-Map tests in SharedMap/Captured.cpp.
//
//===----------------------------------------------------------------------===//

#pragma once

#include "../Tests.h"

namespace ctcompile::test::owned_global_shared_map {

/// Checks that a template-string snapshot of a captured Map's keys is proved
/// only while every insertion into the Map uses a String key.
void checkCapturedStringSnapshots(mlir::MLIRContext & context, const std::string & source,
                                  bool prepared);
/// Checks `Array.from(map.keys())` snapshots of a captured Map and the
/// mutations and index kinds that must refuse them.
void checkCapturedSnapshots(mlir::MLIRContext & context, const std::string & source, bool prepared);
/// Checks a scalar callback stored as `console.error` that counts calls in
/// mutable Number globals.
void checkScalarCallbacks(mlir::MLIRContext & context, const std::string & source, bool prepared);

} // namespace ctcompile::test::owned_global_shared_map
