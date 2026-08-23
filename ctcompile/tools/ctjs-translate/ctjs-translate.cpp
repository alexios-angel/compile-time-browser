// ctjs-translate - the CTJS translation driver.
//
// ZERO TRANSLATIONS REGISTERED, and that is the honest Phase 7 state rather
// than an omission. The deliverable list names four - bytecode to CTJS, CTJS to
// EmitC, EmitC to C++, CTJS to LLVM IR - and not one of them can exist yet: the
// importer is Phase 9, EmitC is 10A and 10B, LLVM IR is 12. The gate asks only
// that this "build and run".
//
// Stubbing the four names would be worse than leaving them out. A translation
// that is registered and produces nothing looks exactly like one that works on
// an input it does not understand, and the first thing built on it would be
// built on silence.
//
// A translation registers ITSELF, through a static TranslateFromMLIRRegistration
// that also names the dialects its input needs - so Phase 9 adds a file and
// this one does not change.
#include "mlir/Tools/mlir-translate/MlirTranslateMain.h"

int main(int argc, char ** argv) {
    return mlir::failed(mlir::mlirTranslateMain(argc, argv, "CTJS translation driver\n")) ? 1 : 0;
}
