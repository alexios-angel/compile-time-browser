#pragma once

#include "../HostContractFixtures.h"
#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/SCF/IR/SCF.h"
#include "llvm/ADT/APFloat.h"

#include <tuple>

namespace ctcompile::test::host_contract_seeded_maps {

using namespace ctcompile::test::host_contract;

void checkEntryNumericResults(mlir::MLIRContext & context, const std::string & shared);
void checkCapturedMapClear(mlir::MLIRContext & context, const std::string & source, bool prepared);
void checkDefiniteMapAbsence(mlir::MLIRContext & context, const std::string & source,
                             bool prepared);
void checkLeafReadbacks(mlir::MLIRContext & context, const std::string & source, bool prepared);
void checkLeafObjectPayloads(mlir::MLIRContext & context, const std::string & shared);
void checkNestedMapResults(mlir::MLIRContext & context, const std::string & shared);
void checkNullablePayloadResults(mlir::MLIRContext & context, const std::string & nullable,
                                 bool prepared);
void checkNullableMapResults(mlir::MLIRContext & context, const std::string & scalar,
                             bool prepared);
void checkConditionalMapResults(mlir::MLIRContext & context, std::string source, bool prepared);
void checkSeededMapResults(mlir::MLIRContext & context, const std::string & shared);

} // namespace ctcompile::test::host_contract_seeded_maps
