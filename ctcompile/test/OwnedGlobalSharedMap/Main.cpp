#include "Tests.h"

using namespace ctcompile::test::owned_global_shared_map;

int main() {
    mlir::MLIRContext context;
    context.getOrLoadDialect<ctjs::CTJSDialect>();
    context.getOrLoadDialect<mlir::scf::SCFDialect>();
    checkSharedMap(context);
    if (failures == 0) { std::puts("owned global shared Map proofs passed"); }
    return failures == 0 ? 0 : 1;
}
