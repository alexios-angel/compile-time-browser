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
#include <ctcompile/Target/Cpp/CppEmitter.h>

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
            // AND WHAT THE DROPPED BODY CAN STILL DO TO THE GLOBALS TABLE.
            // `stores` is the exact set of names its `op::set_global`s name -
            // recoverable from the bytecode without lowering a line of it -
            // and `opaque` is non-empty when that set does not bound the body.
            // --ctjs-resolve-globals refuses those NAMES rather than every
            // name in the program, which is the difference between 0 and 72
            // resolved globals on phaser.
            //
            // BOTH ARE ALWAYS WRITTEN, empty or not. A row whose summary is
            // missing is a row from a translator that did not compute one, and
            // the pass treats an absent key as opaque - so writing the empty
            // string here rather than nothing is what says "computed, and it
            // found nothing".
            builder.getNamedAttr("stores", builder.getStrArrayAttr(std::vector<llvm::StringRef>(
                                               one.stores.begin(), one.stores.end()))),
            builder.getNamedAttr("opaque", builder.getStringAttr(one.opaque)),
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

// WHICH KIND OF SCRIPT THE TEXT IS, and it is not a detail. A classic script's
// top-level `var` is a GLOBAL and a module's is a local of a scope of its own -
// but the difference that matters here is that compile_program emits
// op::load_import, op::bind_export and op::load_namespace from its module arm
// and from NOWHERE ELSE. Until this parameter existed, no fixture in
// ctcompile/test/ could contain one of those three at all, so no lowering for
// them could be shown to compute anything.
mlir::OwningOpRef<mlir::Operation *> import_source(llvm::StringRef text, llvm::StringRef name,
                                                   mlir::MLIRContext * context,
                                                   ctbrowser::script::script_kind kind) {
    const ctbrowser::script::program compiled =
        ctbrowser::script::compiler::compile(std::string{text}, kind);
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
    // THE SOURCE-MANAGER FORM, for the input's name: it becomes the program id
    // and the file slot of every location, so a diagnostic or a Stage 53F pin
    // reads `fixture.js:12:5` rather than a placeholder.
    static mlir::TranslateToMLIRRegistration from_source(
        "ctbrowser-js-to-ctjs", "compile JavaScript source and import it as CTJS MLIR",
        [](const std::shared_ptr<llvm::SourceMgr> & sources, mlir::MLIRContext * context) {
            const llvm::MemoryBuffer * buffer = sources->getMemoryBuffer(sources->getMainFileID());
            return import_source(buffer->getBuffer(), buffer->getBufferIdentifier(), context,
                                 ctbrowser::script::script_kind::classic);
        },
        dialects);
    // A SEPARATE FLAG RATHER THAN AN OPTION ON THE ONE ABOVE. mlir-translate's
    // registrations are selected BY FLAG, and a translation whose behaviour
    // depends on a second unrelated option is one a pipeline can get wrong
    // silently - the module would compile as a classic script, every `import`
    // would be a syntax error, and the failure would read as a bad fixture.
    static mlir::TranslateToMLIRRegistration from_module(
        "ctbrowser-module-to-ctjs",
        "compile JavaScript source AS AN ES MODULE and import it as CTJS MLIR",
        [](const std::shared_ptr<llvm::SourceMgr> & sources, mlir::MLIRContext * context) {
            const llvm::MemoryBuffer * buffer = sources->getMemoryBuffer(sources->getMainFileID());
            return import_source(buffer->getBuffer(), buffer->getBufferIdentifier(), context,
                                 ctbrowser::script::script_kind::module_);
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
    // AND THE FORKED C++ EMITTER, under upstream's own `-mlir-to-cpp` name.
    //
    // HERE RATHER THAN IN A DRIVER OF ITS OWN. An mlir-translate binary hosts
    // every translation it is given - that is what the tool IS - and a second
    // executable would have to be added to the lit suite's DEPENDS to be built
    // before the tests that call it. This one already is.
    //
    // It is what makes ctcompile/test/Target/Cpp/upstream/ work: those 35 files
    // are upstream's, verbatim, and their RUN lines say `mlir-translate
    // -mlir-to-cpp`. A lit.local.cfg beside them points `mlir-translate` at
    // this binary, and nothing in a vendored test is edited.
    ctcompile::cpp::registerToCppTranslation();
    return mlir::failed(mlir::mlirTranslateMain(argc, argv, "CTJS translation driver\n")) ? 1 : 0;
}
