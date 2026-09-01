//===- TranslateRegistration.cpp - Register the forked translation --------===//
//
// Derived from mlir/lib/Target/Cpp/TranslateRegistration.cpp, llvm/llvm-project
// at tag llvmorg-22.1.8 (sha256
// 235037917181929225272a7396d1ebb5a3e1874041cae46711e1f7dc65148e6e), which is
// Apache-2.0 WITH LLVM-exception. See NOTICE.
//
//===----------------------------------------------------------------------===//
//
// UNDER UPSTREAM'S OWN FLAG NAME, `-mlir-to-cpp`, and pointed at OUR emitter.
// That is the whole trick behind Stage 47A's gate: upstream's EmitC lit tests
// are vendored verbatim in ctcompile/test/Target/Cpp/upstream/ and their RUN
// lines say `mlir-translate -mlir-to-cpp`, so the only thing that has to move
// is which binary lit calls `mlir-translate` - which is one lit.local.cfg,
// rather than 35 edited test files.
//
// THIS IS NOT LINKED BESIDE MLIRTargetCpp in any binary today, but if it ever
// is, nothing collides: the function is `ctcompile::cpp::registerToCppTranslation`
// and upstream's is `mlir::registerToCppTranslation`. Two translations of the
// same NAME registered in one process would be a genuine conflict, and it is
// deliberately left as one rather than papered over - a driver offering
// `-mlir-to-cpp` should not have to guess which emitter it got.
//
//===----------------------------------------------------------------------===//

#include "ctcompile/Target/Cpp/CppEmitter.h"

#include "mlir/Dialect/ControlFlow/IR/ControlFlow.h"
#include "mlir/Dialect/EmitC/IR/EmitC.h"
#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "mlir/Tools/mlir-translate/Translation.h"
#include "llvm/Support/CommandLine.h"

namespace ctcompile::cpp {

void registerToCppTranslation() {
    static llvm::cl::opt<bool> declareVariablesAtTop(
        "declare-variables-at-top", llvm::cl::desc("Declare variables at top when emitting C/C++"),
        llvm::cl::init(false));

    static llvm::cl::opt<std::string> fileId(
        "file-id", llvm::cl::desc("Emit emitc.file ops with matching id"), llvm::cl::init(""));

    // STATIC, AND CALLED FROM main RATHER THAN RUN AS A LIBRARY INITIALISER -
    // the same reason ctjs-translate's own registrations say so out loud. A
    // registration object living at namespace scope in a static archive is
    // dropped by the linker unless something references it, and the symptom is
    // a driver that builds and simply does not have the flag.
    static mlir::TranslateFromMLIRRegistration reg(
        "mlir-to-cpp", "translate from mlir to cpp",
        [](mlir::Operation * op, mlir::raw_ostream & output) {
            return translateToCpp(op, output,
                                  /*declareVariablesAtTop=*/declareVariablesAtTop,
                                  /*fileId=*/fileId);
        },
        [](mlir::DialectRegistry & registry) {
            registry.insert<mlir::cf::ControlFlowDialect, mlir::emitc::EmitCDialect,
                            mlir::func::FuncDialect>();
        });
}

} // namespace ctcompile::cpp
