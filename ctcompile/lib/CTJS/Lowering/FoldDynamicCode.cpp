//===- FoldDynamicCode.cpp - Compile constant code-creation strings -------===//
//
// Constant Function, indirect eval and string-timer source is imported before
// global resolution. The resolver checks the proposed module on a throwaway
// clone, so its existing global-object flow proof also guards these splices.
//
//===----------------------------------------------------------------------===//
#include "Globals/Effects.h"
#include "Globals/RegisterFlow.h"

#include "ctcompile/CTJS/IR/CTJSAttrs.h"
#include "ctcompile/CTJS/IR/CTJSDialect.h"
#include "ctcompile/CTJS/IR/CTJSOps.h"
#include "ctcompile/CTJS/Import/BytecodeImport.hpp"
#include "ctcompile/CTJS/Transforms/Passes.h"

#include <ctbrowser/script/compile.hpp>

#include "mlir/IR/Builders.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/Pass/PassManager.h"
#include "llvm/ADT/SmallVector.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <optional>
#include <string>

namespace ctcompile::ctjs {

#define GEN_PASS_DEF_CTJSFOLDDYNAMICCODE
#include "ctcompile/CTJS/Transforms/Passes.h.inc"

namespace {
constexpr llvm::StringLiteral compilerNames[] = {"Function", "eval", "setTimeout", "setInterval"};
constexpr llvm::StringLiteral protectedNames[] = {"Function", "eval", "setTimeout", "setInterval",
                                                  "SyntaxError"};

/// The global a value comes from, including the importer's register joins.
LoadGlobalOp loadedGlobal(mlir::Value value) {
    value = globals_detail::registerFlowOrigin(value);
    return value ? value.getDefiningOp<LoadGlobalOp>() : LoadGlobalOp{};
}

/// The constant string a value carries, including through register joins.
std::optional<std::string> constantString(mlir::Value value) {
    value = globals_detail::registerFlowOrigin(value);
    auto text = value ? constantAttr<StringAttr>(value) : StringAttr{};
    return text ? std::optional<std::string>{text.getValue().str()} : std::nullopt;
}

/// One past every index already in this module, including refused functions.
unsigned nextIndex(mlir::ModuleOp module) {
    unsigned next = nextFunctionIndex(module);
    if (auto skipped = module->getAttrOfType<mlir::ArrayAttr>("ctjs.skipped")) {
        for (mlir::Attribute row : skipped) {
            auto fields = mlir::dyn_cast<mlir::DictionaryAttr>(row);
            auto index = fields ? fields.getAs<mlir::IntegerAttr>("function") : mlir::IntegerAttr{};
            if (index) { next = std::max(next, static_cast<unsigned>(index.getInt()) + 1); }
        }
    }
    return next;
}

/// A compiled snippet owns its imported operations until the splice takes them.
struct compiled {
    mlir::OwningOpRef<mlir::ModuleOp> module;
    FuncOp entry;
    std::string error;
};

/// Whether the source needs declaration instantiation at the time it executes.
bool hasDeclarations(const ctbrowser::script::program & program) {
    return !program.hoisted_vars.empty() || !program.lexical_names.empty() ||
           !program.function_names.empty() || !program.imports.empty();
}

/// Compile and import all of a snippet, or leave its original site untouched.
/// Function wrappers return their entry closure; eval and timers use function 0.
compiled compileSource(mlir::ModuleOp module, const std::string & source, bool functionWrapper,
                       bool forEval, llvm::StringRef programId) {
    auto program = forEval ? ctbrowser::script::compiler::compile_for_eval(source)
                           : ctbrowser::script::compiler::compile(source);
    if (!program.ok) { return {{}, {}, std::move(program.error)}; }
    if (!functionWrapper) {
        // Eval keeps lexical declarations out of program.lexical_names. Compile
        // once in script mode to see those too, without moving their creation
        // into the enclosing program's startup.
        if (hasDeclarations(program) ||
            (forEval && hasDeclarations(ctbrowser::script::compiler::compile(source)))) {
            return {};
        }
    }
    auto imported = js::import_program(program, programId, module.getContext());
    if (!imported.module || !imported.skipped.empty()) { return {}; }
    FuncOp top;
    for (FuncOp function : imported.module->getOps<FuncOp>()) {
        if (functionIndex(function) == 0) { top = function; }
    }
    if (!top) { return {}; }
    unsigned entryIndex = 0;
    if (functionWrapper) {
        // Parameter/body text must not inject statements into the wrapper;
        // extracting only its returned closure would skip those statements.
        bool simpleWrapper = true;
        top.walk([&](mlir::Operation * op) {
            if (!mlir::isa<FuncOp, FrameEnterOp, FrameExitOp, ConstantOp, CreateClosureOp,
                           ReturnOp>(op)) {
                simpleWrapper = false;
            }
        });
        if (!simpleWrapper) { return {}; }
        auto returned = mlir::dyn_cast<ReturnOp>(top.getBody().front().getTerminator());
        if (!returned) { return {}; }
        mlir::Value value = globals_detail::registerFlowOrigin(returned.getOperand());
        auto made = value ? value.getDefiningOp<CreateClosureOp>() : CreateClosureOp{};
        if (!made) { return {}; }
        entryIndex = made.getFunction();
    }
    const unsigned base = nextIndex(module);
    const auto limit = static_cast<unsigned>(std::numeric_limits<std::int32_t>::max());
    if (base > limit || program.functions.size() > limit - base) { return {}; }
    mlir::Builder builder(module.getContext());
    FuncOp entry;
    for (FuncOp function : imported.module->getOps<FuncOp>()) {
        const auto index = functionIndex(function);
        if (!index) { return {}; }
        if (*index == entryIndex) { entry = function; }
        const llvm::StringRef name = function.getSymName();
        function.setSymName(builder.getStringAttr(name.take_front(name.rfind('$')).str() + "$" +
                                                  std::to_string(base + *index)));
        function.walk([&](CreateClosureOp made) {
            made.setFunctionAttr(
                builder.getI32IntegerAttr(static_cast<std::int32_t>(base + made.getFunction())));
        });
    }
    if (!entry || entry.getUpvalueCount() != 0) { return {}; }
    if (functionWrapper) { top.erase(); }
    return {std::move(imported.module), entry, {}};
}

/// Escape a diagnostic as a JavaScript string without changing its bytes.
std::string jsQuoted(llvm::StringRef text) {
    std::string out = "\"";
    for (const unsigned char c : text.bytes()) {
        if (c == '\\' || c == '"') {
            out += '\\';
            out += static_cast<char>(c);
        } else if (c < 0x20 || c == 0x7f) {
            char escape[5];
            std::snprintf(escape, sizeof escape, "\\x%02x", c);
            out += escape;
        } else {
            out += static_cast<char>(c);
        }
    }
    return out + '"';
}

/// Drop just the compiler loads made dead by the replaced sites. Register-file
/// successor operands are not real uses; replace those dead slots by undefined.
void dropUnusedCompilerLoads(mlir::ModuleOp module) {
    llvm::SmallVector<LoadGlobalOp> dead;
    module.walk([&](LoadGlobalOp load) {
        if (!llvm::is_contained(compilerNames, load.getName())) { return; }
        auto uses = globals_detail::registerFlowUses(load.getResult());
        if (uses && uses->empty()) { dead.push_back(load); }
    });
    for (LoadGlobalOp load : dead) {
        mlir::OpBuilder builder(load);
        auto undefined = ConstantOp::create(builder, load.getLoc(), load.getType(),
                                            UndefinedAttr::get(module.getContext()));
        load.getResult().replaceAllUsesWith(undefined.getResult());
        load.erase();
    }
}

/// Build candidate splices in private IR; nothing here changes the real module.
unsigned splice(mlir::ModuleOp module) {
    llvm::SmallVector<mlir::Operation *> sites;
    module.walk([&](mlir::Operation * op) {
        if (mlir::isa<CallOp, ConstructOp>(op)) { sites.push_back(op); }
    });
    unsigned count = 0;
    unsigned siteId = 0;
    for (mlir::Operation * site : sites) {
        ++siteId;
        auto call = mlir::dyn_cast<CallOp>(site);
        auto construct = mlir::dyn_cast<ConstructOp>(site);
        mlir::Value callee = call ? call.getCallee() : construct.getCallee();
        auto load = loadedGlobal(callee);
        if (!load || !llvm::is_contained(compilerNames, load.getName())) { continue; }
        const std::string name = load.getName().str();
        const bool function = name == "Function";
        const bool eval = name == "eval";
        const bool timer = !function && !eval;
        if (construct && (!function || construct.getNewTarget() != callee)) { continue; }
        auto parent = site->getParentOfType<FuncOp>();
        mlir::Block * entry = implicitEntry(parent);
        if (!entry) { continue; }
        const auto args = call ? call.getArgs() : construct.getArgs();
        std::string source;
        if (function) {
            llvm::SmallVector<std::string> texts;
            for (mlir::Value arg : args) {
                auto text = constantString(arg);
                if (!text) { break; }
                texts.push_back(std::move(*text));
            }
            if (texts.size() != args.size()) { continue; }
            source = "return (function anonymous(";
            for (std::size_t i = 0; i + 1 < texts.size(); ++i) {
                if (i) { source += ','; }
                source += texts[i];
            }
            source += "\n) {\n";
            if (!texts.empty()) { source += texts.back(); }
            source += "\n});";
        } else {
            if (args.empty()) { continue; }
            auto text = constantString(args.front());
            if (!text) { continue; }
            source = std::move(*text);
        }
        const std::string programId = "<dynamic:" + std::to_string(siteId) + ">";
        auto made = compileSource(module, source, function, eval, programId);
        bool invoke = eval;
        if (!made.error.empty()) {
            source = "return (function anonymous() { throw new SyntaxError(" +
                     jsQuoted(made.error) + "); });";
            made = compileSource(module, source, true, false, programId);
            invoke = !timer;
        }
        if (!made.module) { continue; }
        const unsigned index = *functionIndex(made.entry);
        module.getBody()->getOperations().splice(module.getBody()->end(),
                                                 made.module->getBody()->getOperations());
        mlir::OpBuilder builder(site);
        const auto type = ValueType::get(module.getContext());
        auto undefined = ConstantOp::create(builder, site->getLoc(), type,
                                            UndefinedAttr::get(module.getContext()));
        auto closure = CreateClosureOp::create(
            builder, site->getLoc(), type, entry->getArgument(arg_callee), undefined.getResult(),
            builder.getI32IntegerAttr(static_cast<std::int32_t>(index)), mlir::ValueRange{},
            mlir::DenseI32ArrayAttr{});
        mlir::Value replacement = closure.getResult();
        if (timer) {
            llvm::SmallVector<mlir::Value> forwarded{closure.getResult()};
            // A string task receives no arguments, even when setTimeout had
            // callback arguments. Their evaluations stay, but not their values.
            if (args.size() > 1) { forwarded.push_back(args[1]); }
            replacement =
                CallOp::create(builder, site->getLoc(), type, callee, call.getReceiver(), forwarded)
                    .getResult();
        } else if (invoke) {
            replacement = CallOp::create(builder, site->getLoc(), type, closure.getResult(),
                                         undefined.getResult(), mlir::ValueRange{})
                              .getResult();
        }
        site->getResult(0).replaceAllUsesWith(replacement);
        site->erase();
        ++count;
    }
    dropUnusedCompilerLoads(module);
    llvm::SmallVector<ConstantOp> unused;
    module.walk([&](ConstantOp constant) {
        if (constant->use_empty()) { unused.push_back(constant); }
    });
    for (ConstantOp constant : unused) { constant.erase(); }
    return count;
}

struct CTJSFoldDynamicCodePass : impl::CTJSFoldDynamicCodeBase<CTJSFoldDynamicCodePass> {
    using CTJSFoldDynamicCodeBase::CTJSFoldDynamicCodeBase;

    /// Reuse the resolver's alias/call/parameter proof without keeping its
    /// rewrites. Probe stores ensure even a completely removed builtin has a row.
    bool cannotRebind(mlir::ModuleOp candidate) {
        mlir::OwningOpRef<mlir::ModuleOp> proof = candidate.clone();
        auto skipped = globals_detail::read_skipped(*proof);
        if (skipped.opaque) { return false; }
        for (auto name : protectedNames) {
            if (skipped.stores.contains(mlir::StringAttr::get(candidate.getContext(), name))) {
                return false;
            }
        }
        bool runtimeCompiler = false;
        proof->walk([&](LoadGlobalOp load) {
            if (load.getName() == "__ctbrowser_direct_eval") { runtimeCompiler = true; }
            if (load.getName() != "setTimeout" && load.getName() != "setInterval") { return; }
            auto uses = globals_detail::registerFlowUses(load.getResult());
            if (!uses) {
                runtimeCompiler = true;
                return;
            }
            for (mlir::OpOperand * use : *uses) {
                auto call = mlir::dyn_cast<CallOp>(use->getOwner());
                if (!call || use->getOperandNumber() != 0 || call.getArgs().empty()) {
                    runtimeCompiler = true;
                    continue;
                }
                // A closure may-set does not exclude String alternatives.
                // Require one exact closure definition; helper parameters stay
                // refused until a complete all-values proof is available.
                mlir::Value handler = globals_detail::registerFlowOrigin(call.getArgs().front());
                if (!handler || !handler.getDefiningOp<CreateClosureOp>()) {
                    runtimeCompiler = true;
                }
            }
        });
        if (runtimeCompiler || proof->getBody()->empty()) { return false; }
        auto function = mlir::dyn_cast<FuncOp>(proof->getBody()->front());
        if (!function || function.getBody().empty()) { return false; }
        mlir::OpBuilder builder(&function.getBody().front(), function.getBody().front().begin());
        auto undefined =
            ConstantOp::create(builder, function.getLoc(), ValueType::get(candidate.getContext()),
                               UndefinedAttr::get(candidate.getContext()));
        for (auto name : protectedNames) {
            // Loads of Function/eval would themselves signal a compiler escape.
            // Dummy stores give every name a census row without such a load;
            // only the original candidate's stores are tested below.
            StoreGlobalOp::create(builder, function.getLoc(), builder.getStringAttr(name),
                                  undefined.getResult());
        }
        mlir::PassManager passes(candidate.getContext());
        passes.addPass(createCTJSResolveGlobals());
        if (mlir::failed(passes.run(*proof))) { return false; }
        if ((*proof)->hasAttr("ctjs.global_escapes")) { return false; }
        bool rebound = false;
        candidate.walk([&](StoreGlobalOp store) {
            if (llvm::is_contained(protectedNames, store.getName())) { rebound = true; }
        });
        if (auto rows = (*proof)->getAttrOfType<mlir::ArrayAttr>("ctjs.globals")) {
            for (mlir::Attribute row : rows) {
                auto fields = mlir::cast<mlir::DictionaryAttr>(row);
                auto name = fields.getAs<mlir::StringAttr>("name");
                auto reason = fields.getAs<mlir::StringAttr>("reason");
                if (name && reason && llvm::is_contained(protectedNames, name.getValue()) &&
                    reason.getValue().starts_with("written through ")) {
                    rebound = true;
                }
            }
        }
        return !rebound;
    }

    void runOnOperation() override {
        mlir::ModuleOp module = getOperation();
        mlir::OwningOpRef<mlir::ModuleOp> candidate = module.clone();
        const unsigned count = splice(*candidate);
        if (!count || !cannotRebind(*candidate)) { return; }
        module.getBodyRegion().takeBody(candidate->getBodyRegion());
        spliced = count;
        if (report) { module.emitRemark() << "dynamic code: " << count << " site(s) spliced"; }
    }
};
} // namespace
} // namespace ctcompile::ctjs
