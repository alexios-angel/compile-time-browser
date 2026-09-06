// EmitC/Module.cpp - native lowering implementation.
#include "../Admission/Admission.h"
#include "../ObjectValues/RuntimeHelpers.h"
#include "Emitter.h"
#include "MethodTableHelpers.h"
#include "NativeMapHelpers.h"
#include "NullableHelpers.h"
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
        for (const std::string & builder : callableBuilders) {
            ec::VerbatimOp::create(b, module.getLoc(), b.getStringAttr(builder));
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
    needsNullable |= needsObjectValue;
    needsObjectIdentity |= needsObjectValue;
    mlir::OpBuilder b(context);
    b.setInsertionPointToStart(module.getBody());
    // INCLUDES FIRST: the builder advances past each op it creates, so
    // creation order is file order, and a global initialised to NAN
    // needs <cmath> above it.
    ec::IncludeOp::create(b, module.getLoc(), b.getStringAttr("cmath"), b.getUnitAttr());
    ec::IncludeOp::create(b, module.getLoc(), b.getStringAttr("cstdio"), b.getUnitAttr());
    if (needsString || needsNullable || needsMap || needsVector || !globals.empty()) {
        ec::IncludeOp::create(b, module.getLoc(), b.getStringAttr("string"), b.getUnitAttr());
    }
    if (needsNullable || needsMap || needsVector || !globals.empty()) {
        ec::IncludeOp::create(b, module.getLoc(), b.getStringAttr("exception"), b.getUnitAttr());
        ec::VerbatimOp::create(b, module.getLoc(), b.getStringAttr(kNullableHelpers));
    }
    if (needsObjectIdentity) {
        ec::IncludeOp::create(b, module.getLoc(), b.getStringAttr("memory"), b.getUnitAttr());
        ec::VerbatimOp::create(b, module.getLoc(), b.getStringAttr(identityDefinition()));
    }
    if (needsObjectValue) {
        ec::IncludeOp::create(b, module.getLoc(), b.getStringAttr("utility"), b.getUnitAttr());
        ec::VerbatimOp::create(b, module.getLoc(), b.getStringAttr(kObjectValueHelpers));
    }
    if (!identityFields.empty()) {
        ec::VerbatimOp::create(b, module.getLoc(), b.getStringAttr(identityFieldHelpers()));
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
        for (llvm::StringRef header : {"exception", "memory", "utility", "vector"}) {
            ec::IncludeOp::create(b, module.getLoc(), b.getStringAttr(header), b.getUnitAttr());
        }
        ec::VerbatimOp::create(b, module.getLoc(), b.getStringAttr(kNativeMapHelpers));
        if (needsObjectValue) {
            ec::VerbatimOp::create(b, module.getLoc(), b.getStringAttr(kObjectMapHelpers));
        }
    }
    if (!methodTables.empty()) {
        for (llvm::StringRef header : {"functional", "memory", "utility"}) {
            ec::IncludeOp::create(b, module.getLoc(), b.getStringAttr(header), b.getUnitAttr());
        }
        ec::VerbatimOp::create(b, module.getLoc(), b.getStringAttr(kMethodTableHelpers));
    }
    if (!environments.empty()) {
        ec::IncludeOp::create(b, module.getLoc(), b.getStringAttr("tuple"), b.getUnitAttr());
        for (const std::string & definition : environments) {
            ec::VerbatimOp::create(b, module.getLoc(), b.getStringAttr(definition));
        }
    }
    for (const std::string & definition : methodTables) {
        ec::VerbatimOp::create(b, module.getLoc(), b.getStringAttr(definition));
    }
    llvm::SmallVector<llvm::StringRef> names(globals.keys().begin(), globals.keys().end());
    llvm::sort(names);
    for (llvm::StringRef name : names) {
        // Default tagged storage is undefined until the first generated
        // store. global_number checks the tag at the output boundary, so a
        // missing store cannot imitate a computed NaN and hide a compiler bug.
        auto global =
            ec::GlobalOp::create(b, module.getLoc(), ("g_" + name).str(),
                                 carrierType(context, carrier::nullable), mlir::Attribute{},
                                 /*extern_specifier=*/false, /*static_specifier=*/true,
                                 /*const_specifier=*/false);
        global->setAttr("ctnative.provenance", b.getStringAttr("global " + name.str()));
    }
}

} // namespace ctcompile::ctnative::lowering_detail
