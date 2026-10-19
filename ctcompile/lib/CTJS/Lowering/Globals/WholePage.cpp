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
#include "ctcompile/CTJS/Lowering/NativeRealm.h"

#include <ctbrowser/dom/tokenizer.hpp>

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
/// spaces, then TAB/LF/CR anywhere), or a URL not known at build time.
std::string urlTarget(mlir::Value value, llvm::StringRef api) {
    const auto text = constantString(value);
    if (!text) {
        return "`" + api.str() +
               "` set to a URL not known at build time, which may be a "
               "javascript: URL";
    }
    std::string url =
        text->drop_while([](char c) { return static_cast<unsigned char>(c) <= 0x20; }).str();
    std::erase_if(url, [](char c) { return c == '\t' || c == '\n' || c == '\r'; });
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

/// Whether a markup fragment can introduce a script, a browsing context or
/// an executable URL. Attribute values have already had entities decoded.
bool markupMayRunCode(llvm::StringRef text, unsigned depth = 0) {
    if (depth == 16) { return true; }
    ctbrowser::html::tokenizer tokenizer(text);
    for (auto token = tokenizer.next(); token.kind != ctbrowser::html::token_kind::end_of_file;
         token = tokenizer.next()) {
        if (token.kind != ctbrowser::html::token_kind::start_tag) { continue; }
        if (llvm::StringSwitch<bool>(token.name)
                .Cases({"script", "iframe", "frame", "object", "embed"}, true)
                .Default(false)) {
            return true;
        }
        for (const auto & attr : token.attributes) {
            if (llvm::StringRef(attr.name).starts_with_insensitive("on")) { return true; }
            if (attr.name == "srcdoc" && markupMayRunCode(attr.value, depth + 1)) { return true; }
            if (llvm::StringSwitch<bool>(attr.name)
                    .Cases({"href", "src", "data", "action", "formaction", "xlink:href"}, true)
                    .Default(false)) {
                std::string url =
                    llvm::StringRef(attr.value)
                        .drop_while([](char c) { return static_cast<unsigned char>(c) <= 0x20; })
                        .str();
                std::erase_if(url, [](char c) { return c == '\t' || c == '\n' || c == '\r'; });
                if (llvm::StringRef(url).starts_with_insensitive("javascript:")) { return true; }
            }
        }
    }
    return false;
}

/// Why markup `value` written through `api` may introduce executable content.
std::string codeMarkup(mlir::Value value, llvm::StringRef api) {
    const auto text = value ? constantString(value) : std::nullopt;
    if (text && !markupMayRunCode(*text)) { return {}; }
    return "`" + api.str() + "` with " +
           (text ? "executable markup" : "markup not known at build time") +
           " may add a script or browsing context, whose code a native program cannot compile";
}

/// The tag of a node returned by an unchanged realm document method, or none.
std::optional<llvm::StringRef> elementTag(mlir::Value value,
                                          globals_detail::ClosureValues & closures) {
    const mlir::Value origin = registerFlowOrigin(value);
    if (auto call = origin ? origin.getDefiningOp<CallOp>() : CallOp{}) {
        const mlir::Value callee = registerFlowOrigin(call.getCallee());
        auto read = callee ? callee.getDefiningOp<GetPropertyOp>() : GetPropertyOp{};
        const llvm::StringRef method = read ? constantKey(read.getKey()) : llvm::StringRef{};
        auto module = call->getParentOfType<mlir::ModuleOp>();
        const mlir::Value owner = read ? registerFlowOrigin(read.getObject()) : mlir::Value{};
        auto document = owner ? owner.getDefiningOp<LoadGlobalOp>() : LoadGlobalOp{};
        const auto writes = globals_detail::read_prototype_writes(module);
        bool pristine = document && document.getName() == "document" &&
                        realmBinding(module, "document") && writes.prototypes.empty() &&
                        writes.any_key.empty() && !writes.written.contains(method);
        // The shared census does not put computed foreign writes in any_key.
        // Until it does, each such receiver must be proved unable to be document.
        module.walk([&](SetPropertyOp write) {
            if (constantString(write.getKey())) { return; }
            const auto leaves = closures.leafValuesOf(write.getObject());
            pristine &= leaves.unknown.empty() && !leaves.values.empty() &&
                        llvm::all_of(leaves.values, [](mlir::Value leaf) {
                            return mlir::isa_and_present<ConstantOp, CreateObjectOp, CreateArrayOp,
                                                         CreateClosureOp>(leaf.getDefiningOp());
                        });
        });
        if (!pristine) { return std::nullopt; }
        const bool create = method == "createElement" || method == "createElementNS";
        const bool query = method == "querySelector";
        const std::size_t index = method == "createElementNS" ? 1 : 0;
        if ((create || query) && index < call.getArgs().size()) {
            if (const auto tag = constantString(call.getArgs()[index]); tag && !tag->empty()) {
                const llvm::StringRef local =
                    create && tag->contains(':') ? tag->split(':').second : *tag;
                if (llvm::all_of(local, [](char c) { return llvm::isAlnum(c) || c == '-'; })) {
                    return local;
                }
            }
        }
    }
    return std::nullopt;
}

/// Opaque sources stay possible; primitives and owned literals cannot be scripts.
bool possiblyScript(mlir::Value value, globals_detail::ClosureValues & closures) {
    if (const auto tag = elementTag(value, closures)) { return tag->equals_insensitive("script"); }
    const auto leaves = closures.leafValuesOf(value);
    return !leaves.unknown.empty() || leaves.values.empty() ||
           llvm::any_of(leaves.values, [](mlir::Value leaf) {
               return !mlir::isa_and_present<ConstantOp, CreateObjectOp, CreateArrayOp,
                                             CreateClosureOp>(leaf.getDefiningOp());
           });
}

std::string scriptWrite(mlir::Value object, llvm::StringRef key,
                        globals_detail::ClosureValues & closures) {
    if (!possiblyScript(object, closures)) { return {}; }
    return "`" + key.str() +
           "` writes a possibly-script element, whose new code a native program cannot compile";
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
/// `closures` follows every possible leaf, not just the closures among them.
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
        // A closure MAY-set says nothing about the non-callable alternatives.
        // Every leaf must be a function, including scalar leaves of returns.
        const auto held = closures.leafValuesOf(first);
        if (held.unknown.empty() && !held.values.empty() &&
            llvm::all_of(held.values, [](mlir::Value leaf) {
                return static_cast<bool>(leaf.getDefiningOp<CreateClosureOp>());
            })) {
            return {};
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
    if (method == "setHTMLUnsafe" || method == "parseHTMLUnsafe") {
        return codeMarkup(arg(0), method);
    }
    if (method == "insertAdjacentHTML") {
        const mlir::Value markup = arg(1);
        if (!markup) { return {}; }
        if (std::string why = handlerMarkup(markup, method); !why.empty()) { return why; }
        return codeMarkup(markup, method);
    }
    if (method == "appendChild" || method == "insertBefore" || method == "replaceChild" ||
        method == "append" || method == "prepend" || method == "before" || method == "after" ||
        method == "replaceWith" || method == "replaceChildren" ||
        method == "insertAdjacentElement") {
        const auto first = method == "insertAdjacentElement" ? 1u : 0u;
        const auto count = method == "appendChild" || method == "insertBefore" ||
                                   method == "replaceChild" || method == "insertAdjacentElement"
                               ? std::min<std::size_t>(args.size(), first + 1)
                               : args.size();
        for (std::size_t index = first; index < count; ++index) {
            if (possiblyScript(args[index], closures)) {
                return "`" + method.str() +
                       "` inserts a possibly-script node, whose code a native program cannot "
                       "compile";
            }
        }
        return {};
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
        const mlir::Value value = arg(namespaced ? 2 : 1);
        if (!method.starts_with("setAttribute") || !value) { return {}; }
        const auto check = [&](llvm::StringRef key) -> std::string {
            const std::string api = method.str() + "(\"" + key.str() + "\")";
            if (key.equals_insensitive("srcdoc")) { return codeMarkup(value, api); }
            const bool target =
                llvm::StringSwitch<bool>(key.lower())
                    .Cases({"href", "src", "data", "action", "formaction", "xlink:href"}, true)
                    .Default(false);
            const auto tag = elementTag(call.getReceiver(), closures);
            const bool image =
                tag && tag->equals_insensitive("img") && key.equals_insensitive("src");
            if (target && !image) {
                if (std::string why = urlTarget(value, api); !why.empty()) { return why; }
            }
            return key.equals_insensitive("src") ? scriptWrite(call.getReceiver(), api, closures)
                                                 : std::string{};
        };
        if (const auto text = constantString(name)) { return check(*text); }
        const auto leaves = closures.leafValuesOf(name);
        if (!leaves.unknown.empty() || leaves.values.empty()) {
            return "`" + method.str() + "` has an unknown attribute name that may create code";
        }
        for (mlir::Value leaf : leaves.values) {
            if (const auto text = constantString(leaf)) {
                if (std::string why = check(*text); !why.empty()) { return why; }
                continue;
            }
            const auto prefix = leftmostPart(leaf);
            for (llvm::StringRef key :
                 {"srcdoc", "href", "src", "data", "action", "formaction", "xlink:href"}) {
                if (!prefix || key.take_front(prefix->size()).equals_insensitive(*prefix)) {
                    if (std::string why = check(key); !why.empty()) { return why; }
                }
            }
        }
        return {};
    }
    // Navigating to a URL: `location.assign(u)`, `location.replace(u)` and
    // `window.open(u)`, when the receiver is that object.
    if ((method == "assign" || method == "replace" || method == "open") && arg(0)) {
        const mlir::Value receiver = registerFlowOrigin(call.getReceiver());
        auto global = receiver ? receiver.getDefiningOp<LoadGlobalOp>() : LoadGlobalOp{};
        auto read = receiver ? receiver.getDefiningOp<GetPropertyOp>() : GetPropertyOp{};
        const bool location = (global && global.getName() == "location") ||
                              (read && constantKey(read.getKey()) == "location");
        const bool window = (global && globals_detail::names_global_object(global.getName())) ||
                            (!call.getReceiver() || isUndefinedConstant(call.getReceiver()));
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
        .Cases({"setHTMLUnsafe", "parseHTMLUnsafe", "parseFromString", "setAttribute",
                "setAttributeNS"},
               true)
        .Cases({"createAttribute", "createAttributeNS", "assign", "replace", "open"}, true)
        .Cases({"appendChild", "insertBefore", "replaceChild", "append", "prepend"}, true)
        .Cases({"before", "after", "replaceWith", "replaceChildren", "insertAdjacentElement"}, true)
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

bool PageNamedElements::contains(llvm::StringRef name) const {
    return names.contains(name) || llvm::any_of(prefixes, [&](const std::string & prefix) {
               return name.starts_with(prefix);
           });
}

PageNamedElements pageNamedElements(mlir::ModuleOp module) {
    PageNamedElements named;
    if (!wholePage(module)) {
        named.complete = false;
        return named;
    }
    auto record = module->getAttrOfType<mlir::DictionaryAttr>(kWholePage);
    auto list = record ? record.getAs<mlir::ArrayAttr>("named") : mlir::ArrayAttr{};
    for (mlir::Attribute entry : list ? list.getValue() : llvm::ArrayRef<mlir::Attribute>{}) {
        if (auto name = mlir::dyn_cast<mlir::StringAttr>(entry)) {
            named.names.insert(name.getValue());
        }
    }
    globals_detail::ClosureValues closures(module);
    mlir::Builder builder(module.getContext());
    llvm::SetVector<mlir::Attribute> refusals;
    if (auto prior = module->getAttrOfType<mlir::ArrayAttr>(kUnboundedPageNaming)) {
        refusals.insert(prior.begin(), prior.end());
        named.complete &= prior.empty();
    }
    const auto unbounded = [&](mlir::Operation * op, llvm::StringRef why) {
        named.complete = false;
        auto reason = builder.getStringAttr("DOM naming mutation is unbounded: " + why.str() +
                                            " (" + describe(op) + ")");
        refusals.insert(reason);
        op->setAttr(kUnboundedPageNaming, reason);
    };
    const auto notElement = [&](mlir::Value value) {
        const auto leaves = closures.leafValuesOf(value);
        return leaves.unknown.empty() && !leaves.values.empty() &&
               llvm::all_of(leaves.values, [](mlir::Value leaf) {
                   if (auto constant = leaf.getDefiningOp<ConstantOp>()) {
                       return isPrimitiveAttr(constant.getValue());
                   }
                   return mlir::isa_and_present<CreateArrayOp, CreateObjectOp>(
                       leaf.getDefiningOp());
               });
    };
    const auto leavesOf = [&](mlir::Value value) {
        globals_detail::ClosureValues::Leaves leaves;
        if (value && constantString(value)) {
            leaves.values.push_back(value);
        } else if (value) {
            leaves = closures.leafValuesOf(value);
        } else {
            leaves.unknown = "missing value";
        }
        return leaves;
    };
    const auto add = [&](mlir::Value value, mlir::Operation * op) {
        const auto leaves = leavesOf(value);
        if (!leaves.unknown.empty() || leaves.values.empty()) {
            unbounded(op, "id/name value is unknown");
        }
        for (mlir::Value leaf : leaves.values) {
            if (const auto text = constantString(leaf)) {
                named.names.insert(*text);
            } else if (const auto prefix = leftmostPart(leaf); prefix && !prefix->empty()) {
                named.prefixes.push_back(prefix->str());
            } else {
                unbounded(op, "id/name value has no constant prefix");
            }
        }
    };
    const auto markup = [&](mlir::Value value, mlir::Operation * op) {
        const auto leaves = leavesOf(value);
        if (!leaves.unknown.empty() || leaves.values.empty()) {
            unbounded(op, "markup is unknown");
        }
        for (mlir::Value leaf : leaves.values) {
            const auto text = constantString(leaf);
            if (!text) {
                unbounded(op, "markup is not a constant string");
                continue;
            }
            ctbrowser::html::tokenizer tokenizer(*text);
            for (auto token = tokenizer.next();
                 token.kind != ctbrowser::html::token_kind::end_of_file; token = tokenizer.next()) {
                for (const auto & attr : token.attributes) {
                    if (attr.name == "id" || attr.name == "name") {
                        named.names.insert(attr.value);
                    }
                }
            }
        }
    };
    const auto mayName = [&](mlir::Value key, bool lowercase) {
        const auto leaves = leavesOf(key);
        if (!leaves.unknown.empty() || leaves.values.empty()) { return true; }
        return llvm::any_of(leaves.values, [&](mlir::Value leaf) {
            if (const auto text = constantString(leaf)) {
                return lowercase
                           ? text->equals_insensitive("id") || text->equals_insensitive("name")
                           : *text == "id" || *text == "name";
            }
            const auto prefix = leftmostPart(leaf);
            if (!prefix) { return true; }
            const auto starts = [&](llvm::StringRef word) {
                return lowercase ? word.take_front(prefix->size()).equals_insensitive(*prefix)
                                 : word.starts_with(*prefix);
            };
            return starts("id") || starts("name");
        });
    };
    module.walk([&](mlir::Operation * op) {
        mlir::Value callee;
        mlir::Value receiver;
        mlir::Value newTarget;
        mlir::Value spread;
        llvm::SmallVector<mlir::Value> arguments;
        if (auto call = mlir::dyn_cast<CallOp>(op)) {
            callee = call.getCallee();
            receiver = call.getReceiver();
            llvm::append_range(arguments, call.getArgs());
        } else if (auto construct = mlir::dyn_cast<ConstructOp>(op)) {
            callee = construct.getCallee();
            newTarget = construct.getNewTarget();
            llvm::append_range(arguments, construct.getArgs());
        } else if (auto call = mlir::dyn_cast<CallSpreadOp>(op)) {
            callee = call.getCallee();
            receiver = call.getReceiver();
            spread = call.getArgs();
        } else if (auto construct = mlir::dyn_cast<ConstructSpreadOp>(op)) {
            callee = construct.getCallee();
            spread = construct.getArgs();
        } else if (auto direct = mlir::dyn_cast<CallDirectOp>(op); direct && !direct.getTarget()) {
            callee = direct.getCalleeValue();
            receiver = direct.getReceiver();
            newTarget = direct.getNewTarget();
            llvm::append_range(arguments, direct.getArgs());
        }
        if (callee) {
            const auto targets = closures.closuresOf(callee);
            const bool opaque = !targets.unknown.empty() || targets.closures.empty();
            // Spread containers are copied, so a fresh array does not prove
            // that its elements exclude Nodes. Fail closed without unrolling it.
            if (opaque &&
                (spread || (receiver && !notElement(receiver)) ||
                 (newTarget && !notElement(newTarget)) ||
                 llvm::any_of(arguments, [&](mlir::Value value) { return !notElement(value); }))) {
                named.vmComplete = false;
            }
        }
        if (auto load = mlir::dyn_cast<LoadGlobalOp>(op);
            load && (load.getName() == "Object" || load.getName() == "Reflect")) {
            const auto uses = registerFlowUses(load.getResult());
            if (!uses) {
                unbounded(op, "reflection builtin moves through unknown registers");
                return;
            }
            for (mlir::OpOperand * use : *uses) {
                if (mlir::isa<RootOp>(use->getOwner()) || nonCoercing(use->getOwner()) ||
                    mlir::isa<ConstructOp>(use->getOwner())) {
                    continue;
                }
                auto read = mlir::dyn_cast<GetPropertyOp>(use->getOwner());
                auto call = mlir::dyn_cast<CallOp>(use->getOwner());
                if ((read && use->getOperandNumber() == 0 && !constantKey(read.getKey()).empty()) ||
                    (call && use->getOperandNumber() == 1)) {
                    continue;
                }
                unbounded(op, "reflection builtin escapes or is aliased");
            }
            return;
        }
        if (auto write = mlir::dyn_cast<SetPropertyOp>(op)) {
            if (notElement(write.getObject())) { return; }
            if (mayName(write.getKey(), false)) { add(write.getValue(), op); }
            const auto keys = leavesOf(write.getKey());
            if (!keys.unknown.empty() || keys.values.empty()) {
                unbounded(op, "property key may be an Attr or markup setter");
            }
            for (mlir::Value leaf : keys.values) {
                const auto key = constantString(leaf);
                if (!key) {
                    unbounded(op, "property key may be an Attr or markup setter");
                } else if (*key == "innerHTML" || *key == "outerHTML") {
                    markup(write.getValue(), op);
                } else if (*key == "value" || *key == "nodeValue" || *key == "textContent") {
                    add(write.getValue(), op);
                }
            }
            return;
        }
        auto read = mlir::dyn_cast<GetPropertyOp>(op);
        if (!read || notElement(read.getObject())) { return; }
        const llvm::StringRef key = constantKey(read.getKey());
        const mlir::Value owner = registerFlowOrigin(read.getObject());
        auto builtin = owner ? owner.getDefiningOp<LoadGlobalOp>() : LoadGlobalOp{};
        const bool global = builtin && globals_detail::names_global_object(builtin.getName());
        if (key == "Object" || key == "Reflect" || (global && key.empty())) {
            unbounded(op, "property read may yield an aliased reflection builtin");
        }
        const bool attribute = key == "setAttribute" || key == "setAttributeNS";
        const bool reflection =
            builtin &&
            ((builtin.getName() == "Reflect" && (key == "set" || key == "defineProperty")) ||
             (builtin.getName() == "Object" &&
              (key == "assign" || key == "defineProperty" || key == "defineProperties")));
        const bool html = llvm::StringSwitch<bool>(key)
                              .Cases({"write", "writeln", "createContextualFragment",
                                      "parseFromString", "insertAdjacentHTML", "setHTMLUnsafe"},
                                     true)
                              .Default(false);
        const bool other = llvm::StringSwitch<bool>(key)
                               .Cases({"setAttributeNode", "setAttributeNodeNS", "setNamedItem",
                                       "setNamedItemNS", "toggleAttribute"},
                                      true)
                               .Default(false);
        if (!attribute && !html && !other && !reflection) { return; }
        const auto uses = registerFlowUses(read.getResult());
        if (!uses) {
            unbounded(op, "naming method moves through unknown registers");
            return;
        }
        for (mlir::OpOperand * use : *uses) {
            if (mlir::isa<RootOp>(use->getOwner()) || nonCoercing(use->getOwner())) { continue; }
            auto call = mlir::dyn_cast<CallOp>(use->getOwner());
            if (!call || use->getOperandNumber() != 0 || other) {
                unbounded(op, "naming method is indirect or has unknown attribute contents");
                continue;
            }
            const auto args = call.getArgs();
            if (reflection) {
                if (!args.empty() && notElement(args.front())) { continue; }
                unbounded(call, "reflection target or property contents may name an element");
                continue;
            }
            if (html) {
                if (key == "write" || key == "writeln") {
                    // Separate writes can complete one tag across calls.
                    unbounded(call, "document.write may complete markup from another write");
                } else {
                    const std::size_t index = key == "insertAdjacentHTML" ? 1 : 0;
                    markup(index < args.size() ? args[index] : mlir::Value{}, call);
                }
                continue;
            }
            const std::size_t index = key.ends_with("NS") ? 1 : 0;
            if (index >= args.size() || mayName(args[index], true)) {
                add(index + 1 < args.size() ? args[index + 1] : mlir::Value{}, call);
            }
        }
    });
    if (!refusals.empty()) {
        module->setAttr(kUnboundedPageNaming, builder.getArrayAttr(refusals.getArrayRef()));
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
            } else if (name == "setTimeout" || name == "setInterval" || name == "open") {
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
                } else if (std::string why = codeMarkup(write.getValue(), key); !why.empty()) {
                    found.push_back({op, std::move(why)});
                }
            } else if (key == "srcdoc") {
                if (std::string why = codeMarkup(write.getValue(), key); !why.empty()) {
                    found.push_back({op, std::move(why)});
                }
            } else if (llvm::StringSwitch<bool>(key)
                           .Cases({"href", "src", "data", "action", "formAction", "location"}, true)
                           .Default(false)) {
                const auto tag =
                    key == "src" ? elementTag(write.getObject(), closures) : std::nullopt;
                const bool image = tag && tag->equals_insensitive("img");
                if (std::string why = image ? std::string{} : urlTarget(write.getValue(), key);
                    !why.empty()) {
                    found.push_back({op, std::move(why)});
                } else if (key == "src") {
                    if (std::string why = scriptWrite(write.getObject(), key, closures);
                        !why.empty()) {
                        found.push_back({op, std::move(why)});
                    }
                }
            } else if (key == "text" || key == "textContent" || key == "innerText") {
                if (std::string why = scriptWrite(write.getObject(), key, closures); !why.empty()) {
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
