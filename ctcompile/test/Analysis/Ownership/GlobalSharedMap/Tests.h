#pragma once

#include "../GlobalMethodsFixtures.h"
#include "mlir/Dialect/SCF/IR/SCF.h"
#include "llvm/ADT/STLExtras.h"

#include <algorithm>

namespace ctcompile::test::owned_global_shared_map {

using namespace ctcompile::test::owned_global_methods;

template <typename Query> bool scalarReadsEmpty(mlir::ModuleOp module, const Query & query) {
    bool result = query.scalarReads().empty();
    module.walk([&](ctjs::LoadGlobalOp load) { result &= query.scalarRead(load) == nullptr; });
    return result;
}

void checkSavedScalarReads(mlir::MLIRContext & context, const std::string & source, bool prepared);
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
void checkLeafReadbackOwner(mlir::MLIRContext & context, const std::string & source, bool lifted);
void checkLeafOwner(mlir::MLIRContext & context, const std::string & source, bool lifted);
void checkNestedOwner(mlir::MLIRContext & context, const std::string & source, bool lifted);
void checkObjectKeyArguments(mlir::MLIRContext & context, const std::string & source,
                             bool prepared);
void checkRetainedObjectKeyFamily(mlir::MLIRContext & context, const std::string & source,
                                  bool prepared);
void checkDOMKeyInputs(mlir::MLIRContext & context, const std::string & source,
                       const std::string & family, bool prepared);
void checkSharedMap(mlir::MLIRContext & context);

} // namespace ctcompile::test::owned_global_shared_map
