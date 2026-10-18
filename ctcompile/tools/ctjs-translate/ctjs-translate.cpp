//===- ctjs-translate.cpp - CTJS translation driver -----------------------===//
//
// An mlir-translate driver that registers three imports and one export:
//
//   --ctbrowser-js-to-ctjs        JavaScript classic-script source -> CTJS MLIR
//   --ctbrowser-module-to-ctjs    JavaScript ES-module source -> CTJS MLIR
//   --ctbrowser-bytecode-to-ctjs  a serialized program image -> CTJS MLIR
//   --mlir-to-cpp                 EmitC MLIR -> C++ (the forked upstream emitter)
//
// All three imports go through the same importer and differ only in where the
// program comes from. The source forms exist so a lit test can keep the
// JavaScript input and the expected IR in one file instead of needing a binary
// image fixture that must be regenerated whenever the image format changes.
//
//===----------------------------------------------------------------------===//
#include <ctcompile/CTJS/Import/BytecodeImport.hpp>
#include <ctcompile/CTJS/Lowering/WholePage.h>
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
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/SourceMgr.h"

#include <string>
#include <vector>

namespace {

/// The page driver's promise (ctcompile/CTJS/Lowering/WholePage.h): the input
/// is every script its page will ever run, and the driver checked that the
/// page has no other <script> and no inline on* handler.
llvm::cl::opt<bool> whole_page(
    "whole-page",
    llvm::cl::desc("Record the JavaScript input as every script its page will ever run "
                   "(ctjs.whole_page); the caller checked the page's scripts and handlers"),
    llvm::cl::init(false));

/// The ids and names of the page's elements, for HTML's named access on the
/// Window object (ctjs.whole_page's `named` array): the page driver lists
/// every one it found, and a fold of a never-bound global skips these names.
llvm::cl::list<std::string> whole_page_named(
    "whole-page-named",
    llvm::cl::desc("With --whole-page: id or name of an element on the page "
                   "(repeatable, or comma-separated)"),
    llvm::cl::CommaSeparated);

/// Records the functions the importer could not compile, in three places: a
/// warning per function for a person, the module's `ctjs.skipped` attribute
/// for lit tests and later passes (such as --ctjs-resolve-globals), and
/// import_result::skipped for library callers.
///
/// A skipped function emits no ctjs.func at all. The invariant is "never emit
/// partially correct AOT code", and a module that silently holds fewer
/// functions than the program had is exactly what this attribute makes
/// visible.
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
            // What the dropped body can still do to the globals table.
            // `stores` is the exact set of names its `op::set_global`s write,
            // recoverable from the bytecode without lowering it, and `opaque`
            // is non-empty when that set does not bound the body.
            // --ctjs-resolve-globals then refuses only those names rather than
            // every name in the program (on phaser, the difference between 0
            // and 72 resolved globals).
            //
            // Both are always written, empty or not. The pass treats an absent
            // key as opaque (a translator that did not compute a summary), so
            // writing the empty value says "computed, and found nothing".
            builder.getNamedAttr("stores", builder.getStrArrayAttr(std::vector<llvm::StringRef>(
                                               one.stores.begin(), one.stores.end()))),
            builder.getNamedAttr("opaque", builder.getStringAttr(one.opaque)),
        }));
        // Attached to a location, not to the module: `module.emitWarning()`
        // makes the module the diagnostic's current operation, so every
        // warning would print the entire module beneath it, thousands of lines
        // per skipped function for a real program.
        mlir::emitWarning(mlir::UnknownLoc::get(context))
            << "function " << one.function_index << " is not compiled: " << one.reason << " ("
            << one.opcode << " at " << one.bc_offset << ")";
    }
    module->setAttr("ctjs.skipped", builder.getArrayAttr(rows));
}

/// Returns the imported module with its refusals recorded on it, or null.
mlir::OwningOpRef<mlir::Operation *> adopt(ctcompile::js::import_result imported) {
    if (!imported.module) { return {}; }
    record_skipped(*imported.module, imported.skipped);
    return mlir::OwningOpRef<mlir::Operation *>(imported.module.release().getOperation());
}

/// Compiles JavaScript source as a classic script or an ES module and imports
/// it. The kind matters beyond scoping (a classic script's top-level `var` is
/// a global, a module's is local): compile_program emits op::load_import,
/// op::bind_export and op::load_namespace only for modules, so a fixture needs
/// the module form to contain them.
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
    mlir::OwningOpRef<mlir::Operation *> module =
        adopt(ctcompile::js::import_program(compiled, name, context));
    if (module && whole_page) {
        mlir::Builder builder(context);
        llvm::SmallVector<mlir::Attribute> named;
        for (const std::string & element : whole_page_named) {
            named.push_back(builder.getStringAttr(element));
        }
        module.get()->setAttr(ctcompile::ctjs::kWholePage,
                              builder.getDictionaryAttr(
                                  {builder.getNamedAttr("scripts", builder.getStrArrayAttr({name})),
                                   builder.getNamedAttr("named", builder.getArrayAttr(named))}));
    }
    return module;
}

mlir::OwningOpRef<mlir::Operation *> import_image(llvm::StringRef bytes, llvm::StringRef name,
                                                  mlir::MLIRContext * context) {
    // A span over the buffer's own bytes, with no copy. StringRef is NUL-safe,
    // which matters because an image is arbitrary binary.
    const std::span<const std::byte> raw{reinterpret_cast<const std::byte *>(bytes.data()),
                                         bytes.size()};
    auto loaded = ctbrowser::script::load_image(raw);
    if (!loaded.ok) {
        mlir::emitError(mlir::UnknownLoc::get(context))
            << "not a ctbrowser program image: " << loaded.error;
        return {};
    }
    return adopt(ctcompile::js::import_program(loaded.value, name, context));
}

/// Registers the translations. Called from main rather than done by a static
/// in a library: the linker drops a static TranslateToMLIRRegistration in a
/// static archive unless something references it, and the driver then builds
/// but lacks the flag.
void register_translations() {
    // The dialect registry, the third argument, is required in practice.
    // Without it the context the callback receives has no ctjs dialect
    // loaded, and the first ValueType::get(context) crashes inside the
    // importer with a stack trace that says nothing about registration.
    const auto dialects = [](mlir::DialectRegistry & registry) {
        ctcompile::registerCTCompileDialects(registry);
    };
    // The source-manager form, to get the input's name: it becomes the
    // program id and the file of every location, so diagnostics and
    // location-checking tests read `fixture.js:12:5` rather than a placeholder.
    const auto from_text = [](ctbrowser::script::script_kind kind) {
        return [kind](const std::shared_ptr<llvm::SourceMgr> & sources,
                      mlir::MLIRContext * context) {
            const llvm::MemoryBuffer * buffer = sources->getMemoryBuffer(sources->getMainFileID());
            return import_source(buffer->getBuffer(), buffer->getBufferIdentifier(), context, kind);
        };
    };
    static mlir::TranslateToMLIRRegistration from_source(
        "ctbrowser-js-to-ctjs", "compile JavaScript source and import it as CTJS MLIR",
        from_text(ctbrowser::script::script_kind::classic), dialects);
    // A separate flag rather than an option on the one above. mlir-translate
    // selects registrations by flag, and a translation whose behaviour depends
    // on a second option is easy to get wrong silently: the module would be
    // compiled as a classic script, every `import` would be a syntax error,
    // and the failure would look like a bad fixture.
    static mlir::TranslateToMLIRRegistration from_module(
        "ctbrowser-module-to-ctjs",
        "compile JavaScript source AS AN ES MODULE and import it as CTJS MLIR",
        from_text(ctbrowser::script::script_kind::module_), dialects);
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
    // The forked C++ emitter, under upstream's own `-mlir-to-cpp` name.
    //
    // It is registered here rather than in a driver of its own: an
    // mlir-translate binary hosts every translation it is given, and a second
    // executable would also have to be added to the lit suite's DEPENDS.
    //
    // This makes ctcompile/test/Target/Cpp/upstream/ work: those 35 tests are
    // upstream's, verbatim, and their RUN lines say `mlir-translate
    // -mlir-to-cpp`. A lit.local.cfg beside them points `mlir-translate` at
    // this binary, so no vendored test is edited.
    ctcompile::cpp::registerToCppTranslation();
    return mlir::failed(mlir::mlirTranslateMain(argc, argv, "CTJS translation driver\n")) ? 1 : 0;
}
