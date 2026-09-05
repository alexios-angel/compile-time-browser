// EmitC/Module.cpp - native lowering implementation.
#include "../Admission/Admission.h"
#include "Emitter.h"
#include "NativeMapHelpers.h"
#include "RuntimeHelpers.h"

namespace ctcompile::ctnative::lowering_detail {

void lowering::finish() {
    // PROTOTYPES FIRST. main is the importer's function 0 and is emitted
    // first, and C++ needs a declaration before a use; one
    // emitc.declare_func per lowered function, at the top of the module
    // after the includes and carrier definitions, is what the emitter
    // prints as a prototype. Map parameters and returns name the helper's
    // number_map template, so its definition must precede the prototypes.
    {
        mlir::OpBuilder b(context);
        b.setInsertionPointToStart(module.getBody());
        llvm::SmallVector<ec::FuncOp> lowered;
        module.walk([&](ec::FuncOp f) {
            if (f.getSymName() != "main") { lowered.push_back(f); }
        });
        // After the includes and helpers, which declareGlobals put first.
        for (mlir::Operation & op : module.getBody()->getOperations()) {
            if (!llvm::isa<ec::IncludeOp, ec::VerbatimOp>(op)) {
                b.setInsertionPoint(&op);
                break;
            }
        }
        // THE CLASSES FIRST, one per SHAPE and no longer one per site
        // (Phase 56C), public fields only: emitc.class prints exactly that.
        // A family that disagrees on a field type carries its parameter
        // list as an attribute and its varying fields as the parameters -
        // the fork's emitter is the one consumer, like every other
        // ctcompile divergence in it.
        for (const family & f : families) {
            auto cls = ec::ClassOp::create(b, module.getLoc(), f.name);
            cls->setAttr("ctnative.provenance", b.getStringAttr(provenanceOf(f)));
            if (f.varies.any()) {
                llvm::SmallVector<mlir::Attribute> parameters;
                for (unsigned i = 0; i < f.fields.size(); ++i) {
                    if (f.varies[i]) { parameters.push_back(b.getStringAttr(f.parameters[i])); }
                }
                cls->setAttr("ctnative.template_params", b.getArrayAttr(parameters));
            }
            mlir::Block & body = cls.getBody().emplaceBlock();
            mlir::OpBuilder inside = mlir::OpBuilder::atBlockEnd(&body);
            for (unsigned i = 0; i < f.fields.size(); ++i) {
                const mlir::Type type =
                    f.varies[i] ? ec::OpaqueType::get(context, f.parameters[i]) : f.types[i];
                ec::FieldOp::create(inside, module.getLoc(), f.fields[i], type, mlir::Attribute{});
            }
        }
        for (ec::FuncOp f : lowered) {
            ec::DeclareFuncOp::create(b, f.getLoc(),
                                      mlir::FlatSymbolRefAttr::get(context, f.getSymName()));
        }
    }
    for (ctjs::FuncOp fn : shells) {
        if (!mlir::SymbolTable::symbolKnownUseEmpty(fn.getOperation(), module)) {
            llvm::report_fatal_error(llvm::Twine("ctnative lowering: `") + fn.getSymName() +
                                     "` still has symbol uses after every accepted function "
                                     "was lowered - the call-graph closure should have "
                                     "refused its caller");
        }
        fn.erase();
    }
    shells.clear();
}

void lowering::declareGlobals() {
    mlir::OpBuilder b(context);
    b.setInsertionPointToStart(module.getBody());
    // INCLUDES FIRST: the builder advances past each op it creates, so
    // creation order is file order, and a global initialised to NAN
    // needs <cmath> above it.
    ec::IncludeOp::create(b, module.getLoc(), b.getStringAttr("cmath"), b.getUnitAttr());
    ec::IncludeOp::create(b, module.getLoc(), b.getStringAttr("cstdio"), b.getUnitAttr());
    if (needsString) {
        ec::IncludeOp::create(b, module.getLoc(), b.getStringAttr("string"), b.getUnitAttr());
    }
    // ONLY WHEN A VECTOR SITE EXISTS. An include and a preamble emitted
    // unconditionally would move every byte count the printing gate
    // reports and every line the other native lits pin, for programs that
    // have no array in them.
    if (needsVector) {
        ec::IncludeOp::create(b, module.getLoc(), b.getStringAttr("vector"), b.getUnitAttr());
        ec::VerbatimOp::create(b, module.getLoc(), b.getStringAttr(kVectorHelpers));
    }
    if (needsMap) {
        for (llvm::StringRef header : {"memory", "utility", "vector"}) {
            ec::IncludeOp::create(b, module.getLoc(), b.getStringAttr(header), b.getUnitAttr());
        }
        ec::VerbatimOp::create(b, module.getLoc(), b.getStringAttr(kNativeMapHelpers));
    }
    llvm::SmallVector<llvm::StringRef> names(globals.keys().begin(), globals.keys().end());
    llvm::sort(names);
    for (llvm::StringRef name : names) {
        // NO INITIALISER, so static zero-initialisation gives 0. Not NaN,
        // which is what this used to emit: every global then started at
        // the same bytes the gate prints for a NaN a program computed, so
        // "never written" and "computed NaN" compared EQUAL and any global
        // whose right answer is NaN was un-failable. Deleting the whole
        // body of the fixture function that exists to prove undefined-field
        // semantics kept the gate green. A global that is never stored is
        // refused outright (see the census in runOnOperation), so 0 is not
        // a value any correct program can observe here.
        auto global = ec::GlobalOp::create(b, module.getLoc(), ("g_" + name).str(),
                                           mlir::Float64Type::get(context), mlir::Attribute{},
                                           /*extern_specifier=*/false, /*static_specifier=*/true,
                                           /*const_specifier=*/false);
        global->setAttr("ctnative.provenance", b.getStringAttr("global " + name.str()));
    }
}

} // namespace ctcompile::ctnative::lowering_detail
