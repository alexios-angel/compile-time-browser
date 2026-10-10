//===- Tests.h - Shared-Map ownership tests ---------------------*- C++ -*-===//
//
// Declarations shared by the ctcompile-test-owned-global-shared-map test
// binary. Each check* function exercises one family of OwnedGlobalRoots (and
// HostContractAnalysis) proofs over a Map captured by published methods. A
// `prepared` or `lifted` flag selects the closure-converted form of the fixture,
// as produced by the `prepare` lambda in SharedMap.cpp.
//
//===----------------------------------------------------------------------===//

#pragma once

#include "../GlobalMethodsFixtures.h"
#include "mlir/Dialect/SCF/IR/SCF.h"
#include "llvm/ADT/STLExtras.h"

#include <algorithm>

namespace ctcompile::test::owned_global_shared_map {

using namespace ctcompile::test::owned_global_methods;

/// Returns true when `query` records no saved scalar read, neither in its list
/// nor for any ctjs.load_global in `module`.
template <typename Query> bool scalarReadsEmpty(mlir::ModuleOp module, const Query & query) {
    bool result = query.scalarReads().empty();
    module.walk([&](ctjs::LoadGlobalOp load) { result &= query.scalarRead(load) == nullptr; });
    return result;
}

/// Checks globals written once (from published-call results or a Number,
/// Boolean or String literal) and read later as scalars.
void checkSavedScalarReads(mlir::MLIRContext & context, const std::string & source, bool prepared);
/// Checks a Number-argument setter whose results feed entry-script arithmetic.
void checkEntryNumericOwner(mlir::MLIRContext & context, const std::string & shared);
void checkCapturedMapClearOwner(mlir::MLIRContext & context, const std::string & source,
                                bool prepared);
void checkCapturedMapZeroSizeOwner(mlir::MLIRContext & context, const std::string & source,
                                   bool prepared);
void checkCapturedMapExactSizeOwner(mlir::MLIRContext & context, const std::string & source,
                                    bool prepared);
void checkCapturedMapDeleteSizeOwner(mlir::MLIRContext & context, const std::string & source,
                                     bool prepared);
void checkDefiniteMapAbsenceOwner(mlir::MLIRContext & context, const std::string & source,
                                  bool prepared);
/// Checks a setter that saves an object and reads it back from the Map.
void checkLeafReadbackOwner(mlir::MLIRContext & context, const std::string & source, bool lifted);
void checkLeafOwner(mlir::MLIRContext & context, const std::string & source, bool lifted);
/// Checks nested calls whose results feed further family calls.
void checkNestedOwner(mlir::MLIRContext & context, const std::string & source, bool lifted);
/// Checks fresh objects passed as Map keys or payloads to a published method.
void checkObjectKeyArguments(mlir::MLIRContext & context, const std::string & source,
                             bool prepared);
void checkRetainedObjectKeyFamily(mlir::MLIRContext & context, const std::string & source,
                                  bool prepared);
void checkDOMKeyInputs(mlir::MLIRContext & context, const std::string & source,
                       const std::string & family, bool prepared);
/// Runs the whole shared-Map suite; Main.cpp calls this.
void checkSharedMap(mlir::MLIRContext & context);

} // namespace ctcompile::test::owned_global_shared_map
