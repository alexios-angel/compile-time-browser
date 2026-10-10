//===- TranslateRegistration.cpp - Register the forked translation --------===//
//
// Derived from mlir/lib/Target/Cpp/TranslateRegistration.cpp, llvm/llvm-project
// at tag llvmorg-22.1.8 (sha256
// 235037917181929225272a7396d1ebb5a3e1874041cae46711e1f7dc65148e6e), which is
// Apache-2.0 WITH LLVM-exception. See NOTICE.
//
//===----------------------------------------------------------------------===//
//
// Registered under upstream's flag name, `-mlir-to-cpp`, but pointed at our
// forked emitter, so the vendored upstream EmitC lit tests in
// ctcompile/test/Target/Cpp/upstream/ run unchanged: their RUN lines say
// `mlir-translate -mlir-to-cpp`, and their lit.local.cfg only redirects which
// binary `mlir-translate` names. TranslateToCpp.cpp's header says which LLVM
// tag each test comes from.
//
// This is not linked beside MLIRTargetCpp in any binary today, but if it ever
// is, nothing collides: the function is `ctcompile::cpp::registerToCppTranslation`
// and upstream's is `mlir::registerToCppTranslation`. Two translations of the
// same name registered in one process would be a genuine conflict, and it is
// deliberately left as one rather than papered over: a driver offering
// `-mlir-to-cpp` should not have to guess which emitter it got.
//
//===----------------------------------------------------------------------===//

#include "ctcompile/CTNative/IR/CTNativeDialect.h"
#include "ctcompile/Target/Cpp/CppEmitter.h"

#include "mlir/Dialect/ControlFlow/IR/ControlFlow.h"
#include "mlir/Dialect/EmitC/IR/EmitC.h"
#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "mlir/Tools/mlir-translate/Translation.h"
#include "llvm/Support/CommandLine.h"

namespace ctcompile::cpp {

/// Registers the `-mlir-to-cpp` translation and its two options,
/// -declare-variables-at-top and -file-id.
void registerToCppTranslation() {
    static llvm::cl::opt<bool> declareVariablesAtTop(
        "declare-variables-at-top", llvm::cl::desc("Declare variables at top when emitting C/C++"),
        llvm::cl::init(false));

    static llvm::cl::opt<std::string> fileId(
        "file-id", llvm::cl::desc("Emit emitc.file ops with matching id"), llvm::cl::init(""));

    // A function-local static, registered when main calls this function rather
    // than by a library initialiser (ctjs-translate's registrations do the same
    // for the same reason). A
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
                            mlir::func::FuncDialect, ctnative::CTNativeDialect>();
        });
}

} // namespace ctcompile::cpp
