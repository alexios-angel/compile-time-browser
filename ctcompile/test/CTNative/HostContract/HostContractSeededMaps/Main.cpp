#include "Tests.h"

using namespace ctcompile::test::host_contract_seeded_maps;

int main() {
    mlir::MLIRContext context;
    context.getOrLoadDialect<ctjs::CTJSDialect>();
    context.getOrLoadDialect<mlir::arith::ArithDialect>();
    context.getOrLoadDialect<mlir::scf::SCFDialect>();
    const auto shared = sharedMapWithPutCall(sharedMapSource(capturedGetterSource()));
    checkEntryNumericResults(context, shared);
    checkLeafObjectPayloads(context, shared);
    checkNestedMapResults(context, shared);
    checkSeededMapResults(context, shared);
    if (failures == 0) { std::puts("host contract seeded Map result proofs passed"); }
    return failures == 0 ? 0 : 1;
}
