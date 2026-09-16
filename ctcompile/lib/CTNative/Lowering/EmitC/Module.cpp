// EmitC/Module.cpp - native lowering implementation.
#include "../Admission/Admission.h"
#include "Emitter.h"
#include "Runtime.h"

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
            if (auto body = callableBodies.find(f.getSymName()); body != callableBodies.end()) {
                f->setAttr("ctnative.callable_body", body->second);
            }
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
        // The owner carrier names a concrete class, so its storage follows
        // the class definition and precedes every function using it.
        llvm::SmallVector<llvm::StringRef> ownerNames(ownedGlobals.keys().begin(),
                                                      ownedGlobals.keys().end());
        llvm::sort(ownerNames);
        for (llvm::StringRef name : ownerNames) {
            if (!domDataSession.empty() && llvm::isa<ec::PointerType>(ownedGlobals.lookup(name))) {
                const auto & storage =
                    *llvm::find_if(ownedGlobalStoragePlans,
                                   [&](const auto & plan) { return plan.binding == name; });
                ec::VerbatimOp::create(b, module.getLoc(),
                                       b.getStringAttr(storage.className + " g_" + name.str() +
                                                       "{};\n" + storage.className + " * reset_g_" +
                                                       name.str() + "() { g_" + name.str() +
                                                       " = {}; return &g_" + name.str() + "; }\n"));
                continue;
            }
            auto global = ec::GlobalOp::create(b, module.getLoc(), ("g_" + name).str(),
                                               ownedGlobals.lookup(name), mlir::Attribute{}, false,
                                               domDataSession.empty(), false);
            global->setAttr("ctnative.provenance", b.getStringAttr("owning global " + name.str()));
        }
        for (ec::FuncOp f : lowered) {
            if (!domDataSession.empty()) { continue; }
            ec::DeclareFuncOp::create(b, f.getLoc(),
                                      mlir::FlatSymbolRefAttr::get(context, f.getSymName()));
        }
        if (!domSessionDefinition.empty()) {
            ec::VerbatimOp::create(b, module.getLoc(), b.getStringAttr(domSessionDefinition));
        }
        for (const std::string & builder : callableBuilders) {
            ec::VerbatimOp::create(b, module.getLoc(), b.getStringAttr(builder));
        }
    }
    if (!domDataSession.empty()) {
        std::string text;
        llvm::raw_string_ostream out(text);
        out << "public:\n    " << domDataSession << "() = default;\n"
            << "    " << domDataSession << "(const " << domDataSession << " &) = delete;\n"
            << "    " << domDataSession << " & operator=(const " << domDataSession
            << " &) = delete;\n"
            << "    " << domDataSession << "(" << domDataSession << " &&) = delete;\n"
            << "    " << domDataSession << " & operator=(" << domDataSession << " &&) = delete;\n"
            << "    ctbrowser::document & document() { return document_; }\n"
            << "    auto invoke(";
        for (unsigned i = 0; i < domParameters.size(); ++i) {
            if (i) { out << ", "; }
            out << "ctbrowser::element_ref element" << i;
        }
        out << ") {\n        if (";
        for (unsigned i = 0; i < domParameters.size(); ++i) {
            if (i) { out << " || "; }
            out << "element" << i << ".owner != &document_";
        }
        out << ") { throw std::invalid_argument(\"DOM element belongs to another session\"); }\n"
            << "        return " << domDataEntry << "(";
        for (unsigned i = 0; i < domParameters.size(); ++i) {
            if (i) { out << ", "; }
            out << "element" << i;
        }
        out << ");\n    }\n";
        llvm::SmallVector<llvm::StringRef> observed(observations.keys().begin(),
                                                    observations.keys().end());
        llvm::sort(observed);
        for (auto name : observed) {
            out << "    auto observe_" << cIdentifier(name) << "() const { return g_" << name
                << "; }\n";
        }
        out << "};\n";
        mlir::OpBuilder b = mlir::OpBuilder::atBlockEnd(module.getBody());
        ec::VerbatimOp::create(b, module.getLoc(), b.getStringAttr(text));
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
    // A presentation policy only: retain typed constants through every
    // optimization and let the C++ printer choose their literal spelling.
    module->setAttr("ctnative.readable_literals", mlir::UnitAttr::get(context));
    module->setAttr("ctnative.const_bindings", mlir::UnitAttr::get(context));
    module->setAttr("ctnative.numeric_alias", mlir::UnitAttr::get(context));
    needsObjectIdentity |= needsObjectValue;
    // Snapshot projections remain ordered even after their producer has
    // been deforested. The choice only becomes more conservative here.
    module.walk([&](ec::CallOpaqueOp call) {
        needsMapOrder |= call.getCallee() == "ctnative::map_keys" ||
                         call.getCallee() == "ctnative::map_values" ||
                         call.getCallee().starts_with("ctnative::map_snapshot_at<");
    });
    mlir::OpBuilder b(context);
    b.setInsertionPointToStart(module.getBody());
    // THE RUNTIME IS ONE INCLUDE (Runtime/ctnative.hpp), and the two things a
    // program decides that a header cannot are defines in front of it. The
    // builder advances past each op it creates, so creation order is file
    // order. Everything after the include is text the program parameterises:
    // its identity struct and field accessors, its environments, its method
    // tables, its DOM session.
    if (needsMapOrder) {
        ec::VerbatimOp::create(b, module.getLoc(), b.getStringAttr(kOrderedMapsDefine));
    }
    if (needsDOM) { ec::VerbatimOp::create(b, module.getLoc(), b.getStringAttr(kDOMDefine)); }
    ec::IncludeOp::create(b, module.getLoc(), b.getStringAttr(kRuntimeHeader), mlir::UnitAttr{});
    if (needsObjectIdentity) {
        ec::VerbatimOp::create(b, module.getLoc(), b.getStringAttr(identityDefinition()));
    }
    if (!identityFields.empty()) {
        ec::VerbatimOp::create(b, module.getLoc(), b.getStringAttr(identityFieldHelpers()));
    }
    for (const std::string & definition : environments) {
        ec::VerbatimOp::create(b, module.getLoc(), b.getStringAttr(definition));
    }
    for (const std::string & definition : methodTables) {
        ec::VerbatimOp::create(b, module.getLoc(), b.getStringAttr(definition));
    }
    if (!domDataSession.empty()) {
        ec::VerbatimOp::create(b, module.getLoc(), b.getStringAttr(domDataDefinition()));
    }
    llvm::SmallVector<llvm::StringRef> names(globals.keys().begin(), globals.keys().end());
    llvm::sort(names);
    for (llvm::StringRef name : names) {
        // Default tagged storage is undefined until the first generated
        // store. Each observation checks its exact tag, so a missing store
        // cannot imitate a computed NaN, false or an empty String.
        auto global = ec::GlobalOp::create(
            b, module.getLoc(), ("g_" + name).str(), globalStorageType(name), mlir::Attribute{},
            /*extern_specifier=*/false, /*static_specifier=*/domDataSession.empty(),
            /*const_specifier=*/false);
        global->setAttr("ctnative.provenance", b.getStringAttr("global " + name.str()));
    }
}

} // namespace ctcompile::ctnative::lowering_detail
