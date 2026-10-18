//===- WholePage.cpp - The page a module is compiled for ------------------===//
//
// Implements WholePage.h: reading the page driver's kWholePage record, and the
// census of code the module may create at run time.
//
// The census errs toward "may create code": a tag, an attribute name or
// markup that is not a constant all count, as does a timer whose first
// argument may be a String (a constant one, or a value ClosureValues cannot
// prove is a function). Each method is matched by its name on any receiver,
// since which object a value is (the document, an element, the window) is not
// known here; a same-named method of another object is refused with them.
// Reading one of these functions as a value counts as well, because a call
// through the value cannot be checked.
//
//===----------------------------------------------------------------------===//
#include "ctcompile/CTJS/Lowering/WholePage.h"

#include "ClosureValues.h"
#include "Effects.h"
#include "RegisterFlow.h"
#include "ctcompile/CTJS/IR/CTJSOps.h"

#include "mlir/IR/Builders.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringSwitch.h"

#include <optional>

namespace ctcompile::ctjs {
namespace {

using globals_detail::describe;
using globals_detail::registerFlowOrigin;
using globals_detail::registerFlowUses;

/// The constant String `value` always is, through block arguments.
std::optional<llvm::StringRef> constantString(mlir::Value value) {
    const mlir::Value origin = registerFlowOrigin(value);
    auto text = origin ? constantAttr<StringAttr>(origin) : StringAttr{};
    return text ? std::optional<llvm::StringRef>(text.getValue()) : std::nullopt;
}

/// Whether `text` starts with `prefix`, ignoring ASCII case.
bool startsWithLower(llvm::StringRef text, llvm::StringRef prefix) {
    return text.size() >= prefix.size() &&
           text.take_front(prefix.size()).equals_insensitive(prefix);
}

/// Why setting a URL through `api` to `value` may run code, or empty: a
/// constant `javascript:` URL (the URL parser strips leading C0 controls and
/// spaces before it reads the scheme), or a URL not known at build time.
std::string urlTarget(mlir::Value value, llvm::StringRef api) {
    const auto text = constantString(value);
    if (!text) {
        return "`" + api.str() +
               "` set to a URL not known at build time, which may be a "
               "javascript: URL";
    }
    const llvm::StringRef url =
        text->drop_while([](char c) { return static_cast<unsigned char>(c) <= 0x20; });
    return startsWithLower(url, "javascript:")
               ? "`" + api.str() + "` set to the javascript: URL `" + text->str() +
                     "`, which runs code when followed"
               : std::string{};
}

/// Whether markup `text` may hold an `on*` attribute: any `on` that starts a
/// word and is followed by name characters and `=`. Text that only looks like
/// one counts too.
bool markupMayAddHandler(llvm::StringRef text) {
    for (std::size_t at = text.find_insensitive("on"); at != llvm::StringRef::npos;
         at = text.find_insensitive("on", at + 1)) {
        if (at > 0 && (llvm::isAlnum(text[at - 1]) || text[at - 1] == '-' || text[at - 1] == '_')) {
            continue;
        }
        std::size_t end = at + 2;
        while (end < text.size() &&
               (llvm::isAlnum(text[end]) || text[end] == '-' || text[end] == '_')) {
            ++end;
        }
        while (end < text.size() && llvm::isSpace(text[end])) { ++end; }
        if (end > at + 2 && end < text.size() && text[end] == '=') { return true; }
    }
    return false;
}

/// The leftmost constant first part of a concatenation `value` lowers to, or
/// null: a String the name always starts with. HTML lowercases attribute
/// names, so the comparison is case-insensitive.
std::optional<llvm::StringRef> leftmostPart(mlir::Value value) {
    mlir::Value origin = registerFlowOrigin(value);
    while (auto sum = origin ? origin.getDefiningOp<BinaryOp>() : BinaryOp{}) {
        if (sum.getKind() != BinaryKind::Add && sum.getKind() != BinaryKind::Concat) { break; }
        origin = registerFlowOrigin(sum.getLhs());
        if (!origin) { break; }
    }
    auto text = origin ? constantAttr<StringAttr>(origin) : StringAttr{};
    return text ? std::optional<llvm::StringRef>(text.getValue()) : std::nullopt;
}

/// Why an attribute named by `name` may be an `on*` handler, or empty. A name
/// is known not to start a handler when every value it may hold (`closures`
/// answers which, followed through calls) is one of:
///  - a constant String whose lowercase does not start with "on"; or
///  - a concatenation whose first part is a constant String `p` that rules
///    "on" out: two or more characters and not starting with "on", or one
///    character that is not "o".
/// Anything else (a name the query cannot follow, a 1-character "o" prefix,
/// an "on..." constant) may add a handler and is refused with the reason.
std::string handlerName(mlir::Value name, llvm::StringRef api,
                        globals_detail::ClosureValues & closures) {
    const auto refused = [&]() {
        return "`" + api.str() +
               "` with a name not known at build time may add an on* event "
               "handler, whose code a native program cannot compile";
    };
    if (const auto text = constantString(name)) {
        return startsWithLower(*text, "on")
                   ? "`" + api.str() + "` with the name `" + text->str() +
                         "` adds an on* event handler, whose code a native program cannot compile"
                   : std::string{};
    }
    globals_detail::ClosureValues::Leaves leaves = closures.leafValuesOf(name);
    if (!leaves.unknown.empty() || leaves.values.empty()) { return refused(); }
    for (mlir::Value leaf : leaves.values) {
        if (const auto text = constantString(leaf)) {
            if (startsWithLower(*text, "on")) { return refused(); }
            continue;
        }
        const auto prefix = leftmostPart(leaf);
        const bool rules_out =
            prefix && ((prefix->size() == 1 && (*prefix)[0] != 'o' && (*prefix)[0] != 'O') ||
                       (prefix->size() >= 2 && !startsWithLower(*prefix, "on")));
        if (!rules_out) { return refused(); }
    }
    return {};
}

/// Why markup `value` written through `api` may add a handler, or empty.
std::string handlerMarkup(mlir::Value value, llvm::StringRef api) {
    if (const auto text = constantString(value)) {
        return markupMayAddHandler(*text)
                   ? "`" + api.str() +
                         "` with markup holding an on* attribute adds an event "
                         "handler, whose code a native program cannot compile"
                   : std::string{};
    }
    return "`" + api.str() +
           "` with markup not known at build time may add an on* event "
           "handler, whose code a native program cannot compile";
}

/// Why calling `method` with `call`'s arguments may create code, or empty.
/// `closures` answers which closures a value can hold (a timer's first
/// argument that is one of them is a function, never a String).
std::string methodCall(llvm::StringRef method, CallOp call,
                       globals_detail::ClosureValues & closures) {
    const auto args = call.getArgs();
    const auto arg = [&](std::size_t index) {
        return index < args.size() ? args[index] : mlir::Value{};
    };
    if (method == "setTimeout" || method == "setInterval") {
        const mlir::Value first = arg(0);
        if (!first) { return {}; }
        const mlir::Value origin = registerFlowOrigin(first);
        if (origin && origin.getDefiningOp<CreateClosureOp>()) { return {}; }
        // A String is compiled, so a value that may be one is refused: a
        // constant String, or a value the query cannot prove is a function.
        // A value that is one of the closures it names is a function.
        if (!constantString(first)) {
            globals_detail::ClosureValues::Answer held = closures.closuresOf(first);
            if (held.unknown.empty() && !held.closures.empty()) { return {}; }
        }
        return "`" + method.str() +
               "` compiles a String first argument as code at run time, "
               "and this one may be a String, so it is not known at build time";
    }
    if (method == "createElement" || method == "createElementNS") {
        const mlir::Value tag = arg(method == "createElement" ? 0 : 1);
        const auto text = tag ? constantString(tag) : std::nullopt;
        if (!text) {
            return "`" + method.str() +
                   "` with a tag not known at build time may make a script "
                   "or frame element, whose code a native program cannot run";
        }
        const llvm::StringRef local = text->contains(':') ? text->split(':').second : *text;
        const bool runs = llvm::StringSwitch<bool>(local.lower())
                              .Cases({"script", "iframe", "frame", "object", "embed"}, true)
                              .Default(false);
        return runs ? "`" + method.str() + "` makes a `" + local.lower() +
                          "` element, whose code a native program cannot run"
                    : std::string{};
    }
    if (method == "write" || method == "writeln" || method == "createContextualFragment") {
        return "`" + method.str() +
               "` can add a script element, whose code a native program "
               "cannot run";
    }
    if (method == "insertAdjacentHTML" || method == "setHTMLUnsafe") {
        const mlir::Value markup = arg(method == "insertAdjacentHTML" ? 1 : 0);
        return markup ? handlerMarkup(markup, method) : std::string{};
    }
    if (method == "parseFromString") {
        const mlir::Value markup = arg(0);
        return markup ? handlerMarkup(markup, method) : std::string{};
    }
    if (method == "setAttribute" || method == "setAttributeNS" || method == "createAttribute" ||
        method == "createAttributeNS") {
        const bool namespaced = method.ends_with("NS");
        const mlir::Value name = arg(namespaced ? 1 : 0);
        if (!name) { return {}; }
        if (std::string why = handlerName(name, method, closures); !why.empty()) { return why; }
        // A link or form target set to a URL.
        const auto text = constantString(name);
        const bool target = text && llvm::StringSwitch<bool>(text->lower())
                                        .Cases({"href", "action", "formaction", "xlink:href"}, true)
                                        .Default(false);
        const mlir::Value value = arg(namespaced ? 2 : 1);
        return method.starts_with("setAttribute") && target && value
                   ? urlTarget(value, method.str() + "(\"" + text->str() + "\")")
                   : std::string{};
    }
    // Navigating to a URL: `location.assign(u)`, `location.replace(u)` and
    // `window.open(u)`, when the receiver is that object.
    if ((method == "assign" || method == "replace" || method == "open") && arg(0)) {
        const mlir::Value receiver = registerFlowOrigin(call.getReceiver());
        auto global = receiver ? receiver.getDefiningOp<LoadGlobalOp>() : LoadGlobalOp{};
        auto read = receiver ? receiver.getDefiningOp<GetPropertyOp>() : GetPropertyOp{};
        const bool location = (global && global.getName() == "location") ||
                              (read && constantKey(read.getKey()) == "location");
        const bool window = global && globals_detail::names_global_object(global.getName());
        if ((method == "open" && window) || (method != "open" && location)) {
            return urlTarget(arg(0), method);
        }
    }
    return {};
}

/// Whether reading `key` off an object yields a function that creates code
/// when called.
bool createsCode(llvm::StringRef key) {
    return llvm::StringSwitch<bool>(key)
        .Cases({"setTimeout", "setInterval", "createElement", "createElementNS"}, true)
        .Cases({"write", "writeln", "createContextualFragment", "insertAdjacentHTML"}, true)
        .Cases({"setHTMLUnsafe", "parseFromString", "setAttribute", "setAttributeNS"}, true)
        .Cases({"createAttribute", "createAttributeNS", "assign", "replace", "open"}, true)
        .Default(false);
}

/// Why the value of `read` (a constant-key read off any object) or of `load` (a
/// global) may create code, or empty: each use must call it, and that call
/// must be one methodCall() admits.
std::string usedFunction(mlir::Value function, llvm::StringRef name,
                         globals_detail::ClosureValues & closures,
                         llvm::SmallVectorImpl<std::pair<mlir::Operation *, std::string>> & sites) {
    auto uses = registerFlowUses(function);
    if (!uses) { return "`" + name.str() + "` moves through too many registers to follow"; }
    for (mlir::OpOperand * use : *uses) {
        auto call = mlir::dyn_cast<CallOp>(use->getOwner());
        if (!call || use->getOperandNumber() != 0) {
            // Testing or comparing the function does not call it. A navigation
            // method is matched only at its calls (`assign`, `replace` and
            // `open` name other objects' methods far more often).
            const bool navigation = name == "assign" || name == "replace" || name == "open";
            if (!navigation && !mlir::isa<RootOp>(use->getOwner()) &&
                !nonCoercing(use->getOwner())) {
                return "`" + name.str() +
                       "` is used as a value, and a call through it may create "
                       "code at run time";
            }
            continue;
        }
        if (std::string why = methodCall(name, call, closures); !why.empty()) {
            sites.push_back({call, std::move(why)});
        }
    }
    return {};
}

} // namespace

bool wholePage(mlir::ModuleOp module) {
    return module->hasAttr(kWholePage);
}

llvm::StringSet<> pageNamedElements(mlir::ModuleOp module) {
    llvm::StringSet<> named;
    auto record = module->getAttrOfType<mlir::DictionaryAttr>(kWholePage);
    auto list = record ? record.getAs<mlir::ArrayAttr>("named") : mlir::ArrayAttr{};
    for (mlir::Attribute entry : list ? list.getValue() : llvm::ArrayRef<mlir::Attribute>{}) {
        if (auto name = mlir::dyn_cast<mlir::StringAttr>(entry)) { named.insert(name.getValue()); }
    }
    return named;
}

llvm::SmallVector<RuntimeCodeSite> markRuntimeCode(mlir::ModuleOp module) {
    llvm::SmallVector<RuntimeCodeSite> sites;
    // Only a module that is its whole page keeps the model's promise that no
    // code is added later; the closure query the timer check makes assumes it.
    if (!wholePage(module)) { return sites; }
    globals_detail::ClosureValues closures(module);
    llvm::SmallVector<std::pair<mlir::Operation *, std::string>> found;
    module.walk([&](mlir::Operation * op) {
        if (auto load = mlir::dyn_cast<LoadGlobalOp>(op)) {
            const llvm::StringRef name = load.getName();
            if (load.getTypeofLookup()) { return; }
            if (name == "eval" || name == "Function") {
                found.push_back({op, "`" + name.str() + "` compiles a String as code at run time"});
            } else if (name == "setTimeout" || name == "setInterval") {
                if (std::string why = usedFunction(load.getResult(), name, closures, found);
                    !why.empty()) {
                    found.push_back({op, std::move(why)});
                }
            }
            return;
        }
        if (mlir::isa<DynamicImportOp>(op)) {
            found.push_back({op, "`import()` loads code at run time"});
            return;
        }
        if (auto read = mlir::dyn_cast<GetPropertyOp>(op)) {
            const llvm::StringRef key = constantKey(read.getKey());
            if (createsCode(key)) {
                if (std::string why = usedFunction(read.getResult(), key, closures, found);
                    !why.empty()) {
                    found.push_back({op, std::move(why)});
                }
            }
            return;
        }
        if (auto write = mlir::dyn_cast<SetPropertyOp>(op)) {
            const llvm::StringRef key = constantKey(write.getKey());
            if (key == "innerHTML" || key == "outerHTML") {
                if (std::string why = handlerMarkup(write.getValue(), key); !why.empty()) {
                    found.push_back({op, std::move(why)});
                }
            } else if (llvm::StringSwitch<bool>(key)
                           .Cases({"href", "action", "formAction", "location"}, true)
                           .Default(false)) {
                if (std::string why = urlTarget(write.getValue(), key); !why.empty()) {
                    found.push_back({op, std::move(why)});
                }
            }
            return;
        }
        if (auto store = mlir::dyn_cast<StoreGlobalOp>(op);
            store && store.getName() == "location") {
            if (std::string why = urlTarget(store.getValue(), "location"); !why.empty()) {
                found.push_back({op, std::move(why)});
            }
        }
    });
    mlir::Builder builder(module.getContext());
    for (auto & [op, why] : found) {
        why += " (" + describe(op) + ")";
        op->setAttr(kRuntimeCode, builder.getStringAttr(why));
        sites.push_back({op, std::move(why)});
    }
    return sites;
}

} // namespace ctcompile::ctjs
