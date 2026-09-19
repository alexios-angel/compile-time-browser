#pragma once

namespace mlir {
class MLIRContext;
}

namespace ctcompile::test::exception_recovery {
void testDOMURITransaction(mlir::MLIRContext & context);
}
