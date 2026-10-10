//===- Module.cpp - Module-level EmitC output ----------------- -*- C++ -*-===//
//
// Module-level EmitC output: the runtime include and defines, globals, shape
// classes, function prototypes and the DOM session class, plus the final
// erasure of the emptied ctjs.funcs.
//
//===----------------------------------------------------------------------===//
#include "../Admission/Admission.h"
#include "Emitter.h"
#include "Runtime.h"

namespace ctcompile::ctnative::lowering_detail {

void lowering::verbatim(mlir::OpBuilder & b, const llvm::Twine & text) {
    ec::VerbatimOp::create(b, module.getLoc(), b.getStringAttr(text));
}

void lowering::finish() {
    // Declare every lowered function except main before any definition, since
    // main (the importer's function 0) is emitted first and C++ needs a
    // declaration before a use. Each emitc.declare_func prints as a prototype.
    // The order at the top of the module is: includes and helpers, shape
    // classes, owned-global storage, then the prototypes (skipped in a DOM
    // data session, whose functions are class members).
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
        // One emitc.class per object shape (not per allocation site), with
        // public fields only. A shape family whose members disagree on a field
        // type becomes a class template: the varying fields' types are template
        // parameters listed in `ctnative.template_params`, an attribute only
        // ctcompile's patched C++ emitter reads.
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
        for (llvm::StringRef name : sortedKeys(ownedGlobals)) {
            const auto & storage = ownedGlobalStoragePlans[ownedGlobals.lookup(name)];
            if (!domDataSession.empty() && llvm::isa<ec::PointerType>(storage.type)) {
                verbatim(b, llvm::Twine(storage.className) + " g_" + name + "{};\n" +
                                storage.className + " * reset_g_" + name + "() { g_" + name +
                                " = {}; return &g_" + name + "; }\n");
                continue;
            }
            auto global =
                ec::GlobalOp::create(b, module.getLoc(), ("g_" + name).str(), storage.type,
                                     mlir::Attribute{}, false, domDataSession.empty(), false);
            global->setAttr("ctnative.provenance", b.getStringAttr("owning global " + name.str()));
        }
        for (ec::FuncOp f : lowered) {
            if (!domDataSession.empty()) { continue; }
            ec::DeclareFuncOp::create(b, f.getLoc(),
                                      mlir::FlatSymbolRefAttr::get(context, f.getSymName()));
        }
        if (!domSessionDefinition.empty()) { verbatim(b, domSessionDefinition); }
        for (const std::string & builder : callableBuilders) { verbatim(b, builder); }
    }
    if (!domDataSession.empty()) {
        std::string text;
        llvm::raw_string_ostream out(text);
        out << "public:\n"
            << nonmovable(domDataSession, "    ")
            << "    ctbrowser::document & document() { return document_; }\n";
        sessionInvoke(out, domDataEntry, domParameters.size(), "");
        for (auto name : sortedKeys(observations)) {
            out << "    auto observe_" << cIdentifier(name) << "() const { return g_" << name
                << "; }\n";
        }
        out << "};\n";
        mlir::OpBuilder b = mlir::OpBuilder::atBlockEnd(module.getBody());
        verbatim(b, text);
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
    // Presentation policies for the C++ printer only: keep typed constants
    // through every optimization and let the printer choose their literal
    // spelling, declare bindings const where the operands allow it, and spell
    // double as js_num.
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
    // The runtime is one include (Runtime/ctnative.hpp), and the two things a
    // program decides that a header cannot are defines in front of it. The
    // builder advances past each op it creates, so creation order is file
    // order. Everything after the include is text the program parameterises:
    // its identity struct and field accessors, its environments, its method
    // tables, its DOM session.
    if (needsMapOrder) { verbatim(b, kOrderedMapsDefine); }
    if (needsDOM) { verbatim(b, kDOMDefine); }
    ec::IncludeOp::create(b, module.getLoc(), b.getStringAttr(kRuntimeHeader), mlir::UnitAttr{});
    // Browser views include the live selector engine only for entries that
    // already borrow Style. Primitive-only programs keep the smaller header.
    if (!domStyleParameters.empty()) {
        ec::IncludeOp::create(b, module.getLoc(),
                              b.getStringAttr("ctcompile/CTNative/Runtime/Browser.hpp"),
                              b.getUnitAttr());
    }
    if (needsObjectIdentity) { verbatim(b, identityDefinition()); }
    if (!identityFields.empty()) { verbatim(b, identityFieldHelpers()); }
    for (const std::string & definition : environments) { verbatim(b, definition); }
    for (const std::string & definition : methodTables) { verbatim(b, definition); }
    if (!domDataSession.empty()) { verbatim(b, domDataDefinition()); }
    for (llvm::StringRef name : sortedKeys(globals)) {
        // Default tagged storage is undefined until the first generated
        // store. Each observation checks its exact tag, so a missing store
        // cannot imitate a computed NaN, false or an empty String.
        auto global = ec::GlobalOp::create(
            b, module.getLoc(), ("g_" + name).str(), globalStorageType(name), mlir::Attribute{},
            /*extern_specifier=*/false, /*static_specifier=*/domDataSession.empty(),
            /*const_specifier=*/false);
        global->setAttr("ctnative.provenance", b.getStringAttr("global " + name.str()));
    }
    // Boxed bindings of run-once functions. Like the globals above they start
    // as their type's default and are assigned where the JavaScript frame
    // made the box, before any closure can read them; unlike the globals,
    // they are not observations and are never printed.
    for (const auto & [name, type] : statics) {
        auto variable = ec::GlobalOp::create(b, module.getLoc(), name, type, mlir::Attribute{},
                                             /*extern_specifier=*/false,
                                             /*static_specifier=*/true,
                                             /*const_specifier=*/false);
        variable->setAttr("ctnative.provenance",
                          b.getStringAttr("binding of a function that runs once"));
    }
}

} // namespace ctcompile::ctnative::lowering_detail
