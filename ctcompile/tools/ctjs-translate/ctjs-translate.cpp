// ctjs-translate - the CTJS translation driver.
//
// TWO TRANSLATIONS, and the second one is the reason the first is testable.
//
//   --ctbrowser-js-to-ctjs        JavaScript source -> CTJS MLIR
//   --ctbrowser-bytecode-to-ctjs  a serialized program image -> CTJS MLIR
//
// The plan's deliverable names only the image form. The SOURCE form is added
// because a lit test then reads as one file - the JavaScript in, the expected
// IR beside it - where the image form would need a binary fixture built by
// another tool and regenerated whenever the image format moves. Both go through
// exactly the same importer; the only difference is where the program comes
// from.
//
// The other two the deliverable names - CTJS to EmitC, EmitC to C++ - are Phase
// 10A and 10B and are still absent rather than stubbed.
#include <ctcompile/CTJS/Import/BytecodeImport.hpp>
#include <ctcompile/Support/MLIRContextSetup.hpp>

#include <ctbrowser/script/compile.hpp>
#include <ctbrowser/script/program_image.hpp>

#include "mlir/IR/Builders.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/DialectRegistry.h"
#include "mlir/IR/MLIRContext.h"
#include "mlir/IR/Verifier.h"
#include "mlir/Tools/mlir-translate/MlirTranslateMain.h"
#include "mlir/Tools/mlir-translate/Translation.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/SourceMgr.h"

#include <string>
#include <vector>

namespace {

// WHAT WAS SKIPPED, ON THE MODULE. Three surfaces, one set of facts: a
// diagnostic per skipped function so a person sees it, this attribute so a lit
// test can assert it and Phase 15's manifest can consume it, and the
// import_result vector for a library caller.
//
// A SKIPPED FUNCTION EMITS NO ctjs.func AT ALL. "Never emit partially correct
// AOT code" is the invariant, and a module that silently contains fewer
// functions than the program had is exactly the failure this attribute exists
// to make loud.
void record_skipped(mlir::ModuleOp module,
                    const std::vector<ctcompile::js::unsupported_opcode> & skipped) {
    if (skipped.empty()) { return; }
    mlir::MLIRContext * context = module.getContext();
    mlir::Builder builder(context);
    std::vector<mlir::Attribute> rows;
    rows.reserve(skipped.size());
    for (const ctcompile::js::unsupported_opcode & one : skipped) {
        rows.push_back(builder.getDictionaryAttr({
            builder.getNamedAttr("program", builder.getStringAttr(one.program_id)),
            builder.getNamedAttr("function", builder.getI32IntegerAttr(
                                                 static_cast<std::int32_t>(one.function_index))),
            builder.getNamedAttr(
                "offset", builder.getI32IntegerAttr(static_cast<std::int32_t>(one.bc_offset))),
            builder.getNamedAttr("opcode", builder.getStringAttr(one.opcode)),
            builder.getNamedAttr("reason", builder.getStringAttr(one.reason)),
        }));
        // ON A LOCATION, NOT ON THE MODULE. `module.emitWarning()` attaches the
        // module as the diagnostic's current operation, so every warning prints
        // the entire module underneath it - which for a real program is
        // thousands of lines per skipped function.
        mlir::emitWarning(mlir::UnknownLoc::get(context))
            << "function " << one.function_index << " is not compiled: " << one.reason << " ("
            << one.opcode << " at " << one.bc_offset << ")";
    }
    module->setAttr("ctjs.skipped", builder.getArrayAttr(rows));
}

mlir::OwningOpRef<mlir::Operation *> import_source(llvm::StringRef text, llvm::StringRef name,
                                                   mlir::MLIRContext * context) {
    const ctbrowser::script::program compiled =
        ctbrowser::script::compiler::compile(std::string{text});
    if (!compiled.ok) {
        mlir::emitError(mlir::UnknownLoc::get(context))
            << "the JavaScript did not compile: " << compiled.error;
        return {};
    }
    ctcompile::js::import_result imported = ctcompile::js::import_program(compiled, name, context);
    if (!imported.module) { return {}; }
    record_skipped(*imported.module, imported.skipped);
    return mlir::OwningOpRef<mlir::Operation *>(imported.module.release().getOperation());
}

mlir::OwningOpRef<mlir::Operation *> import_image(llvm::StringRef bytes, llvm::StringRef name,
                                                  mlir::MLIRContext * context) {
    // A SPAN OVER THE BUFFER'S OWN BYTES, copied nowhere. StringRef is NUL-safe,
    // which matters because an image is arbitrary binary.
    const std::span<const std::byte> raw{reinterpret_cast<const std::byte *>(bytes.data()),
                                         bytes.size()};
    auto loaded = ctbrowser::script::load_image(raw);
    if (!loaded.ok) {
        mlir::emitError(mlir::UnknownLoc::get(context))
            << "not a ctbrowser program image: " << loaded.error;
        return {};
    }
    ctcompile::js::import_result imported =
        ctcompile::js::import_program(loaded.value, name, context);
    if (!imported.module) { return {}; }
    record_skipped(*imported.module, imported.skipped);
    return mlir::OwningOpRef<mlir::Operation *>(imported.module.release().getOperation());
}

// REGISTERED FROM main, NOT BY A STATIC IN A LIBRARY. A static
// TranslateToMLIRRegistration living in a static archive is dropped by the
// linker unless somebody references it, and the symptom is a driver that builds
// and simply does not have the flag.
void register_translations() {
    // THE DIALECT REGISTRY IS THE THIRD ARGUMENT AND IT IS NOT OPTIONAL IN
    // PRACTICE. Without it the context the callback receives has no ctjs
    // dialect loaded, and the first ValueType::get(context) crashes inside the
    // importer - a stack trace that names the importer and says nothing about
    // registration.
    const auto dialects = [](mlir::DialectRegistry & registry) {
        ctcompile::registerCTCompileDialects(registry);
    };
    static mlir::TranslateToMLIRRegistration from_source(
        "ctbrowser-js-to-ctjs", "compile JavaScript source and import it as CTJS MLIR",
        [](llvm::StringRef text, mlir::MLIRContext * context) {
            return import_source(text, "<source>", context);
        },
        dialects);
    static mlir::TranslateToMLIRRegistration from_image(
        "ctbrowser-bytecode-to-ctjs", "import a serialized ctbrowser program image as CTJS MLIR",
        [](llvm::StringRef bytes, mlir::MLIRContext * context) {
            return import_image(bytes, "<image>", context);
        },
        dialects);
}

} // namespace

int main(int argc, char ** argv) {
    register_translations();
    return mlir::failed(mlir::mlirTranslateMain(argc, argv, "CTJS translation driver\n")) ? 1 : 0;
}
