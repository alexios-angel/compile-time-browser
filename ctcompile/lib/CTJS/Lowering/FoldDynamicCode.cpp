//===- FoldDynamicCode.cpp - Compile constant code-creation strings -------===//
//
// A `new Function('a', 'b', 'return a + b')`, a `Function(...)` call, an
// indirect `eval('1 + 2')` and a `setTimeout('tick()', 100)` whose string
// arguments are ctjs.constants carry their program at build time. This pass
// compiles that program with the script front end, imports it into the
// module and splices the site, as Passes.td describes. It runs before
// --ctjs-resolve-globals, whose clause 5 resolves nothing while a load of
// `Function` or `eval` remains.
//
// A name is spliced only when the module never stores it and never writes
// or deletes it as a constant property key, so no site the pass rewrites
// can observe a rebind. Anything else (a non-constant string, a rebound
// name, a method form such as `window.eval(...)`) is left for clause 5.
//
// Written in C++ rather than DRR for the same reason as
// --ctjs-resolve-globals: splicing needs a module-wide check (no store of
// the name), a fresh function index over the whole module and a front-end
// compile, none of which a Pat<> match can express.
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
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringSet.h"

#include <cstdint>
#include <optional>
#include <string>

namespace ctcompile::ctjs {

#define GEN_PASS_DEF_CTJSFOLDDYNAMICCODE
#include "ctcompile/CTJS/Transforms/Passes.h.inc"

namespace {

/// The names this pass can splice: each compiles source text at run time.
constexpr llvm::StringLiteral splicableNames[] = {"Function", "eval", "setTimeout",
                                                  "setInterval"};

/// The global a value was loaded from, through block arguments (the importer
/// threads every register through them, so the read can be blocks away).
LoadGlobalOp loadedGlobal(mlir::Value value) {
    const mlir::Value origin = globals_detail::registerFlowOrigin(value);
    return origin ? origin.getDefiningOp<LoadGlobalOp>() : LoadGlobalOp{};
}

/// The constant string a value carries, or nullopt.
std::optional<std::string> constantString(mlir::Value value) {
    const mlir::Value origin = globals_detail::registerFlowOrigin(value);
    auto text = origin ? ctjs::constantAttr<ctjs::StringAttr>(origin) : ctjs::StringAttr{};
    if (!text) { return std::nullopt; }
    return text.getValue().str();
}

/// Whether \p value is a load of the global object itself (globalThis,
/// window or self): what a `globalThis[k] = v` writes onto.
bool isGlobalObject(mlir::Value value) {
    LoadGlobalOp load = loadedGlobal(value);
    return load && globals_detail::names_global_object(load.getName());
}

/// Whether the callee of \p call is `Object.defineProperty` or `Object.assign`
/// (a get_property of a load of "Object" with that constant key) and its
/// first argument is the global object, so the call can define or copy any
/// name onto it. `Reflect.defineProperty`/`Reflect.set` are the same shape;
/// the four names below are the only ones the realm's own object methods
/// accept as a target for rebinding a global.
bool definesOntoGlobalObject(mlir::Operation * op) {
    auto call = mlir::dyn_cast<CallOp>(op);
    if (!call || call.getArgs().empty()) { return false; }
    auto get = call.getCallee().getDefiningOp<GetPropertyOp>();
    if (!get) {
        if (const mlir::Value origin = globals_detail::registerFlowOrigin(call.getCallee())) {
            get = origin.getDefiningOp<GetPropertyOp>();
        }
    }
    if (!get) { return false; }
    const llvm::StringRef key = constantKey(get.getKey());
    if (key != "defineProperty" && key != "assign" && key != "set" &&
        key != "defineProperties") {
        return false;
    }
    LoadGlobalOp holder = loadedGlobal(get.getObject());
    if (!holder) { return false; }
    const llvm::StringRef holderName = holder.getName();
    if (holderName != "Object" && holderName != "Reflect") { return false; }
    return isGlobalObject(call.getArgs().front());
}

/// Whether the module never rebinds `name`: no ctjs.store_global of it, and
/// no property write or delete under `name` as a constant key anywhere (a
/// write through the global object rebinds it, and proving the receiver is
/// not the global object is clause 5's walk, which this pass does not redo).
///
/// The global-object clauses are per-module rather than per-name, because a
/// computed key or an Object method can name ANY of the four: a
/// `globalThis[k] = v` whose key is not a constant string could write
/// `Function`, an `Object.defineProperty(globalThis, k, d)` could define
/// `eval`, and an `Object.assign(globalThis, o)` could copy all four. Any of
/// those in the module refuses every name the pass splices, so the check is
/// made once by neverReboundAny below rather than per name.
bool neverRebound(mlir::ModuleOp module, llvm::StringRef name) {
    bool rebound = false;
    module.walk([&](mlir::Operation * op) {
        if (auto store = mlir::dyn_cast<StoreGlobalOp>(op)) {
            rebound = rebound || store.getName() == name;
        } else if (auto set = mlir::dyn_cast<SetPropertyOp>(op)) {
            rebound = rebound || constantKey(set.getKey()) == name;
        } else if (auto erase = mlir::dyn_cast<DeleteNamedOp>(op)) {
            rebound = rebound || erase.getName() == name;
        } else if (auto accessor = mlir::dyn_cast<DefineAccessorOp>(op)) {
            rebound = rebound || accessor.getName() == name;
        }
        return rebound ? mlir::WalkResult::interrupt() : mlir::WalkResult::advance();
    });
    return !rebound;
}

/// Whether the module has no store that could rebind ANY of the splicable
/// names by going through the global object: no computed-key write onto it
/// (a `globalThis[k] = v` whose key is not a constant string), and no
/// `Object.defineProperty` / `Object.assign` / `Reflect.set` / similar call
/// that names it as the target. The constant-key forms are per name and are
/// already covered by neverRebound; this catches the forms whose key is only
/// known at run time. A single walk: the pass splices nothing while any of
/// these is present, because which name they hit is unknowable at build time.
bool neverReboundAny(mlir::ModuleOp module) {
    bool rebound = false;
    module.walk([&](mlir::Operation * op) {
        if (auto set = mlir::dyn_cast<SetPropertyOp>(op)) {
            if (constantKey(set.getKey()).empty() && isGlobalObject(set.getObject())) {
                rebound = true;
            }
        } else if (definesOntoGlobalObject(op)) {
            rebound = true;
        }
        return rebound ? mlir::WalkResult::interrupt() : mlir::WalkResult::advance();
    });
    return !rebound;
}

/// One past the highest function index in the module: the index the first
/// spliced function takes. Function indices are the `$N` suffix of the
/// symbol (CTJSOps.h, functionIndex).
unsigned nextFunctionIndex(mlir::ModuleOp module) {
    unsigned highest = 0;
    module.walk([&](FuncOp function) {
        if (const std::optional<unsigned> index = functionIndex(function)) {
            highest = std::max(highest, *index + 1);
        }
    });
    return highest;
}

/// The result of compiling one site's string: the functions to splice in
/// (already renamed and renumbered), the index of the one the site uses,
/// and the program's own refusal rows and hoisted vars to merge into the
/// module. Empty when the string could not be used, in which case `throwing`
/// says the source failed to parse and the site must throw a SyntaxError
/// when it runs.
struct compiled {
    std::string error; ///< Non-empty when the source did not compile.
    llvm::SmallVector<FuncOp> functions;   ///< Cloned into the module, renamed.
    unsigned entry = 0;                    ///< functions[] index the site uses.
    llvm::SmallVector<mlir::Attribute> skipped;   ///< ctjs.skipped rows to merge.
    llvm::SmallVector<std::string> hoisted;       ///< ctjs.hoisted_vars to merge.
};

/// Spells `text` as a JavaScript string literal's contents, for the
/// SyntaxError message the throw-only body names. Only the characters a
/// message can contain are escaped; the source is otherwise ASCII.
std::string jsEscaped(llvm::StringRef text) {
    std::string out;
    out.reserve(text.size());
    for (const char c : text) {
        switch (c) {
        case '\\': out += "\\\\"; break;
        case '"': out += "\\\""; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (static_cast<unsigned char>(c) < 0x20 || c == 0x7f) {
                char buf[8];
                snprintf(buf, sizeof buf, "\\x%02x", static_cast<unsigned char>(c));
                out += buf;
            } else {
                out += c;
            }
        }
    }
    return out;
}

/// Compiles `source` and imports it, cloning the functions the site needs
/// into `module` with fresh indices. `entryIndex` names the program
/// function the site uses: 0 for an eval or timer program (its top level),
/// 1 for the anonymous function a `new Function` wrap returns.
///
/// A function the importer refused in the spliced program merges its
/// ctjs.skipped row into the module, so --ctjs-resolve-globals still
/// refuses exactly the globals that body stores. A refusal of the entry
/// function itself abandons the splice: the site then compiles to the
/// throw-only body only when `error` is set, and otherwise stays as it is.
std::optional<compiled> compileSource(mlir::ModuleOp module, const std::string & source,
                                      unsigned entryIndex, bool forEval,
                                      llvm::StringRef programId) {
    const ctbrowser::script::program program =
        forEval ? ctbrowser::script::compiler::compile_for_eval(source)
                : ctbrowser::script::compiler::compile(source);
    compiled out;
    if (!program.ok) {
        out.error = program.error;
        return out;
    }
    ctcompile::js::import_result imported =
        ctcompile::js::import_program(program, programId, module.getContext());
    if (!imported.module) { return std::nullopt; }
    // The spliced program's entry must exist and have been imported: the
    // anonymous function of a `new Function` wrap is index 1, and an eval or
    // timer program's top level is index 0.
    llvm::DenseMap<unsigned, FuncOp> byIndex;
    imported.module->getBody()->walk([&](FuncOp function) {
        if (const std::optional<unsigned> index = functionIndex(function)) {
            byIndex[*index] = function;
        }
    });
    if (!byIndex.contains(entryIndex)) { return std::nullopt; }
    // Renaming and renumbering: old index k becomes base + k, which leaves
    // `base` itself a hole no ctjs.create_closure names (index 0, the wrap,
    // never moves). The symbol's `$` suffix carries the new index; the
    // `function` attribute of every create_closure inside a moved function
    // shifts by the same base.
    const unsigned base = nextFunctionIndex(module);
    mlir::OpBuilder builder(module.getContext());
    for (auto & [oldIndex, function] : byIndex) {
        if (oldIndex == 0 && entryIndex != 0) { continue; }
        FuncOp clone = function.clone();
        const llvm::StringRef name = clone.getSymName();
        const std::size_t dollar = name.rfind('$');
        clone.setSymName(builder.getStringAttr(
            (name.substr(0, dollar).str() + "$" + std::to_string(base + oldIndex))));
        clone.walk([&](CreateClosureOp made) {
            const std::uint32_t shifted = base + made.getFunction();
            made.setFunctionAttr(builder.getI32IntegerAttr(
                static_cast<std::int32_t>(shifted)));
        });
        out.functions.push_back(clone);
        if (oldIndex == entryIndex) {
            out.entry = static_cast<unsigned>(out.functions.size() - 1);
        }
    }
    // The program's refusal rows and hoisted vars merge into the module's
    // attributes. The rows' `program` id keeps the spliced source's name, so
    // a diagnostic names the site that made it.
    if (auto rows = (*imported.module)->getAttrOfType<mlir::ArrayAttr>("ctjs.skipped")) {
        for (const mlir::Attribute row : rows) { out.skipped.push_back(row); }
    }
    out.hoisted.assign(program.hoisted_vars.begin(), program.hoisted_vars.end());
    return out;
}

/// Builds the body that throws `new SyntaxError(message)` when called, as a
/// spliced function: a site whose string does not parse throws when it runs
/// (new Function and eval), or when the task runs (a string timer), never
/// at build time. `message` is escaped into the source, which the front end
/// then compiles like any other.
std::optional<compiled> compileThrowing(mlir::ModuleOp module, llvm::StringRef message,
                                        llvm::StringRef programId) {
    const std::string source = "return (function anonymous() {\nthrow new SyntaxError(\"" +
                               jsEscaped(message) + "\");\n});";
    return compileSource(module, source, 1, false, programId);
}

/// Splices `functions` into the module before `site`'s block, and returns a
/// ctjs.create_closure of the entry function, inserted before `site`. The
/// closure captures nothing: a body compiled from a string sees the global
/// scope and its own locals, never the enclosing frame (the specification's
/// CreateDynamicFunction and PerformEval both use the running realm's global
/// environment, not the caller's variable environment).
CreateClosureOp spliceEntry(mlir::ModuleOp module, compiled & into, mlir::Operation * site,
                            mlir::Value enclosingClosure) {
    mlir::OpBuilder builder(module.getContext());
    for (FuncOp function : into.functions) {
        module.getBody()->push_back(function);
    }
    builder.setInsertionPoint(site);
    const auto valueType = ValueType::get(module.getContext());
    mlir::Value undefined = ConstantOp::create(builder, site->getLoc(), valueType,
                                               UndefinedAttr::get(module.getContext()));
    return CreateClosureOp::create(builder, site->getLoc(), valueType, enclosingClosure,
                                   undefined,
                                   builder.getI32IntegerAttr(static_cast<std::int32_t>(
                                       *functionIndex(into.functions[into.entry]))),
                                   mlir::ValueRange{}, mlir::DenseI32ArrayAttr{});
}

/// Merges a spliced program's refusal rows and hoisted vars into the
/// module's attributes.
void mergeModuleAttrs(mlir::ModuleOp module, const compiled & from) {
    mlir::OpBuilder builder(module.getContext());
    if (!from.skipped.empty()) {
        llvm::SmallVector<mlir::Attribute> rows;
        if (auto existing = module->getAttrOfType<mlir::ArrayAttr>("ctjs.skipped")) {
            rows.assign(existing.begin(), existing.end());
        }
        rows.append(from.skipped.begin(), from.skipped.end());
        module->setAttr("ctjs.skipped", builder.getArrayAttr(rows));
    }
    if (!from.hoisted.empty()) {
        llvm::SmallVector<mlir::Attribute> names;
        llvm::StringSet<> seen;
        if (auto existing = module->getAttrOfType<mlir::ArrayAttr>("ctjs.hoisted_vars")) {
            for (const mlir::Attribute name : existing) {
                names.push_back(name);
                seen.insert(mlir::cast<mlir::StringAttr>(name).getValue());
            }
        }
        for (const std::string & name : from.hoisted) {
            if (seen.insert(name).second) {
                names.push_back(builder.getStringAttr(name));
            }
        }
        module->setAttr("ctjs.hoisted_vars", builder.getArrayAttr(names));
    }
}

/// The `%arg2` of the function `site` sits in: the enclosing closure a
/// spliced closure names, as the importer passes it (Instructions.cpp,
/// op::closure).
mlir::Value ownClosure(mlir::Operation * site) {
    FuncOp parent = site->getParentOfType<FuncOp>();
    mlir::Block * entry = implicitEntry(parent);
    return entry ? entry->getArgument(arg_callee) : mlir::Value{};
}

struct CTJSFoldDynamicCodePass
    : impl::CTJSFoldDynamicCodeBase<CTJSFoldDynamicCodePass> {
    using CTJSFoldDynamicCodeBase::CTJSFoldDynamicCodeBase;

    void runOnOperation() override {
        mlir::ModuleOp module = getOperation();
        std::size_t splicedHere = 0;

        // The names the module never rebinds, computed once. A name that
        // fails this stays for clause 5 even at a constant site, because the
        // page could have replaced what the load reads. neverReboundAny's
        // check is global: a computed-key write onto the global object, or an
        // Object.defineProperty / Object.assign call onto it, could rebind
        // any of the four, so its failure marks them all unusable.
        llvm::StringMap<bool> usable;
        const bool globalObjectSafe = neverReboundAny(module);
        for (const llvm::StringLiteral name : splicableNames) {
            usable[name] = globalObjectSafe && neverRebound(module, name);
        }

        // Sites are collected first and spliced after the walk: the splice
        // adds operations (the closure and the spliced functions) a walk
        // would visit.
        llvm::SmallVector<mlir::Operation *> sites;
        module.walk([&](mlir::Operation * op) {
            if (mlir::isa<ConstructOp, CallOp>(op)) { sites.push_back(op); }
            return mlir::WalkResult::advance();
        });

        unsigned siteId = 0;
        for (mlir::Operation * site : sites) {
            ++siteId;
            auto call = mlir::dyn_cast<CallOp>(site);
            auto construct = mlir::dyn_cast<ConstructOp>(site);
            const mlir::Value callee = call ? call.getCallee() : construct.getCallee();
            const mlir::OperandRange args =
                call ? call.getArgs() : construct.getArgs();
            LoadGlobalOp load = loadedGlobal(callee);
            if (!load) { continue; }
            const llvm::StringRef name = load.getName();
            if (!usable.lookup(name)) { continue; }
            // A construct splices only `new Function(...)` itself: its callee
            // and new.target are the one load. A subclass's
            // `class F extends Function {}` passes F as new.target, and the
            // closure it builds gets F's prototype, which a plain spliced
            // closure does not model.
            if (construct && construct.getNewTarget() != callee) { continue; }
            mlir::Value enclosing = ownClosure(site);
            if (!enclosing) { continue; }
            const std::string programId = "<dynamic:" + std::to_string(siteId) + ">";

            std::optional<compiled> made;
            bool asCall = false;  ///< Replace the site with a call of the entry closure.
            bool asValue = false; ///< Replace the site's first argument with the entry closure.
            if (name == "Function") {
                // Every argument but the last names a parameter; the last is
                // the body. `new Function()` is a function that does nothing.
                llvm::SmallVector<std::string> texts;
                bool constant = true;
                for (mlir::Value arg : args) {
                    std::optional<std::string> text = constantString(arg);
                    if (!text) {
                        constant = false;
                        break;
                    }
                    texts.push_back(std::move(*text));
                }
                if (!constant) { continue; }
                llvm::SmallVector<std::string> params;
                std::string body;
                if (!texts.empty()) {
                    body = texts.back();
                    params.assign(texts.begin(), texts.end() - 1);
                }
                // The spec's own wrap (CreateDynamicFunction,
                // builtins/objects/function.cpp's dynamic_function): the
                // newlines keep a trailing `//` comment in the body from
                // swallowing the brace.
                std::string joined;
                for (std::size_t i = 0; i < params.size(); ++i) {
                    if (i) { joined += ","; }
                    joined += params[i];
                }
                const std::string source =
                    "return (function anonymous(" + joined + "\n) {\n" + body + "\n});";
                made = compileSource(module, source, 1, false, programId);
                asCall = false;
            } else if (name == "eval") {
                // An indirect eval: the compiler routes direct eval through
                // __ctbrowser_direct_eval, so a call through the `eval`
                // global is indirect. A non-string first argument comes back
                // unchanged, so only a constant string splices; the result is
                // the program's completion value.
                if (args.empty()) { continue; }
                std::optional<std::string> source = constantString(args.front());
                if (!source) { continue; }
                made = compileSource(module, *source, 0, true, programId);
                asCall = true;
            } else {
                // setTimeout / setInterval with a string handler: the string
                // is the task, compiled as a classic script and run with the
                // global object as its receiver and no argument (the host's
                // timer task does exactly that). The first argument becomes
                // the entry closure; the rest (delay, callback arguments)
                // stay. A function first argument is the ordinary form and
                // is not this pass's business.
                if (args.empty()) { continue; }
                std::optional<std::string> source = constantString(args.front());
                if (!source) { continue; }
                made = compileSource(module, *source, 0, false, programId);
                asValue = true;
            }
            if (!made) { continue; }

            if (!made->error.empty()) {
                // A SyntaxError in the string: the site throws when it runs.
                // The throwing body is itself spliced; a timer hands it to
                // the host, which reports the task's error.
                std::optional<compiled> throwing =
                    compileThrowing(module, made->error, programId + ":syntax");
                if (!throwing || throwing->functions.empty()) { continue; }
                made = std::move(throwing);
                asCall = name != "setTimeout" && name != "setInterval";
                asValue = !asCall;
            }
            if (made->functions.empty()) { continue; }

            CreateClosureOp closure = spliceEntry(module, *made, site, enclosing);
            mergeModuleAttrs(module, *made);

            mlir::OpBuilder builder(module.getContext());
            builder.setInsertionPoint(site);
            const auto valueType = ValueType::get(module.getContext());
            if (asValue) {
                // The timer's first argument becomes the closure.
                llvm::SmallVector<mlir::Value> newArgs;
                newArgs.push_back(closure.getResult());
                for (mlir::Value arg : llvm::drop_begin(args)) { newArgs.push_back(arg); }
                mlir::Value receiver = call.getReceiver();
                auto replacement = CallOp::create(builder, site->getLoc(), valueType, callee,
                                                  receiver, newArgs);
                site->replaceAllUsesWith(llvm::SmallVector<mlir::Value>{
                    mlir::Value(replacement.getResult())});
                site->erase();
            } else if (asCall) {
                // The site becomes a call of the entry closure, with an
                // undefined receiver and no argument: the eval'd program runs
                // at global scope (run_nested's call), and its result is the
                // completion value.
                mlir::Value receiver = ConstantOp::create(
                    builder, site->getLoc(), valueType, UndefinedAttr::get(module.getContext()));
                auto replacement = CallOp::create(builder, site->getLoc(), valueType,
                                                  closure.getResult(), receiver,
                                                  mlir::ValueRange{});
                site->replaceAllUsesWith(llvm::SmallVector<mlir::Value>{
                    mlir::Value(replacement.getResult())});
                site->erase();
            } else {
                // `new Function(...)` and `Function(...)`: the site's value
                // is the closure itself.
                site->replaceAllUsesWith(llvm::SmallVector<mlir::Value>{
                    mlir::Value(closure.getResult())});
                site->erase();
            }
            ++splicedHere;
        }

        spliced = static_cast<unsigned>(splicedHere);
        if (report && splicedHere) {
            module.emitRemark() << "dynamic code: " << splicedHere << " site(s) spliced";
        }
    }
};

} // namespace
} // namespace ctcompile::ctjs
