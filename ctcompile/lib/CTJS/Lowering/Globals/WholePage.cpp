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
    return llvm::StringRef(url).starts_with_insensitive("javascript:")
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
        return text->starts_with_insensitive("on")
                   ? "`" + api.str() + "` with the name `" + text->str() +
                         "` adds an on* event handler, whose code a native program cannot compile"
                   : std::string{};
    }
    globals_detail::ClosureValues::Leaves leaves = closures.leafValuesOf(name);
    if (!leaves.unknown.empty() || leaves.values.empty()) { return refused(); }
    for (mlir::Value leaf : leaves.values) {
        if (const auto text = constantString(leaf)) {
            if (text->starts_with_insensitive("on")) { return refused(); }
            continue;
        }
        const auto prefix = leftmostPart(leaf);
        const bool rules_out =
            prefix && ((prefix->size() == 1 && (*prefix)[0] != 'o' && (*prefix)[0] != 'O') ||
                       (prefix->size() >= 2 && !prefix->starts_with_insensitive("on")));
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
    auto call = origin ? origin.getDefiningOp<CallOp>() : CallOp{};
    const mlir::Value source = call ? registerFlowOrigin(call.getCallee()) : origin;
    if (auto read = source ? source.getDefiningOp<GetPropertyOp>() : GetPropertyOp{}) {
        const llvm::StringRef method = constantKey(read.getKey());
        auto module = read->getParentOfType<mlir::ModuleOp>();
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
        if (!call) {
            if (method == "body") { return "body"; }
            if (method == "head") { return "head"; }
            if (method == "documentElement") { return "html"; }
            return std::nullopt;
        }
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

/// Why writing text into `receiver` through `api` may change a script element's
/// code, or empty.
std::string textIntoScript(mlir::Value receiver, llvm::StringRef api,
                           globals_detail::ClosureValues & closures) {
    if (!possiblyScript(receiver, closures)) { return {}; }
    return "`" + api.str() +
           "` writes text into a possibly-script element, whose new code a native program cannot "
           "compile";
}

/// Why writing `api` on `object` to `value` may navigate a frame to a document
/// that runs scripts, or empty: only a constant `about:blank` is known not to,
/// and only a receiver proved not to be a frame element is out of reach.
std::string frameTarget(mlir::Value object, mlir::Value value, llvm::StringRef api,
                        globals_detail::ClosureValues & closures) {
    const auto text = constantString(value);
    if (text && text->equals_insensitive("about:blank")) { return {}; }
    if (const auto tag = elementTag(object, closures)) {
        if (!llvm::StringSwitch<bool>(tag->lower())
                 .Cases({"iframe", "frame", "object", "embed"}, true)
                 .Default(false)) { return {}; }
    }
    return "`" + api.str() +
           "` navigates a frame to a document that may run scripts, whose code a native program "
           "cannot compile";
}

/// A value every leaf of which is a primitive or an owned literal: it cannot
/// be a DOM node or hold a sink property.
bool ownedValue(mlir::Value value, globals_detail::ClosureValues & closures) {
    const auto leaves = closures.leafValuesOf(value);
    return leaves.unknown.empty() && !leaves.values.empty() &&
           llvm::all_of(leaves.values, [](mlir::Value leaf) {
               return mlir::isa_and_present<ConstantOp, CreateObjectOp, CreateArrayOp, CreateClosureOp>(
                   leaf.getDefiningOp());
           });
}

/// Why setting `key` to `value` on `receiver` may run code through a URL, a
/// frame navigation or a script element's source, or empty. `api` names the
/// operation in the reason.
std::string urlWrite(llvm::StringRef key, mlir::Value value, mlir::Value receiver,
                     llvm::StringRef api, globals_detail::ClosureValues & closures) {
    const bool source = key.equals_insensitive("src") || key.equals_insensitive("data");
    const auto tag = source ? elementTag(receiver, closures) : std::nullopt;
    const bool image = tag && tag->equals_insensitive("img") && key.equals_insensitive("src");
    std::string why;
    if (!image &&
        llvm::StringSwitch<bool>(key.lower())
            .Cases({"href", "src", "data", "action", "formaction", "xlink:href", "location"}, true)
            .Default(false)) {
        why = urlTarget(value, api);
    }
    if (why.empty() && key.equals_insensitive("src")) {
        why = scriptWrite(receiver, api, closures);
    }
    if (why.empty() && source && !image) {
        why = frameTarget(receiver, value, api, closures);
    }
    return why;
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

/// The attribute names `value` was made or fetched with (`createAttribute`,
/// `getNamedItem` and their NS forms), following a global's stores; a name not
/// known at build time is the empty StringRef. Empty when `value` is none of
/// those calls' results.
void attrNamesOf(mlir::Value value, mlir::ModuleOp module, unsigned depth,
                 llvm::SmallVectorImpl<llvm::StringRef> & names) {
    if (!value || depth == 8) { return; }
    const mlir::Value origin = registerFlowOrigin(value);
    auto call = origin ? origin.getDefiningOp<CallOp>() : CallOp{};
    if (call) {
        const mlir::Value source = registerFlowOrigin(call.getCallee());
        auto read = source ? source.getDefiningOp<GetPropertyOp>() : GetPropertyOp{};
        if (!read) { return; }
        const llvm::StringRef method = constantKey(read.getKey());
        const bool named = llvm::StringSwitch<bool>(method)
                               .Cases({"createAttribute", "createAttributeNS", "getNamedItem",
                                       "getNamedItemNS", "getAttributeNode", "getAttributeNodeNS"},
                                      true)
                               .Default(false);
        if (!named) { return; }
        const auto args = call.getArgs();
        const std::size_t index = method.ends_with("NS") ? 1 : 0;
        names.push_back(index < args.size()
                            ? constantString(args[index]).value_or(llvm::StringRef{})
                            : llvm::StringRef{});
        return;
    }
    if (auto load = origin ? origin.getDefiningOp<LoadGlobalOp>() : LoadGlobalOp{}) {
        module.walk([&](StoreGlobalOp store) {
            if (store.getName() == load.getName()) {
                attrNamesOf(store.getValue(), module, depth + 1, names);
            }
        });
    }
}

/// Why writing `value` to an attribute object's `value` property may run code,
/// or empty: the attribute's name names its sink.
std::string attrValueWrite(mlir::Value object, mlir::Value value, mlir::ModuleOp module,
                           globals_detail::ClosureValues & closures) {
    llvm::SmallVector<llvm::StringRef> names;
    attrNamesOf(object, module, 0, names);
    for (llvm::StringRef name : names) {
        if (name.empty()) {
            return "`value` writes an attribute whose name is not known at build time, which may "
                   "create code";
        }
        if (name.starts_with_insensitive("on")) {
            return "`value` with the name `" + name.str() +
                   "` adds an on* event handler, whose code a native program cannot compile";
        }
        if (name.equals_insensitive("srcdoc")) { return codeMarkup(value, "srcdoc"); }
        if (std::string why = urlWrite(name, value, object, name, closures); !why.empty()) {
            return why;
        }
    }
    return {};
}

/// Why setting `key` to `value` on `object` may create code, or empty: the
/// sink rules for a property write, shared with Reflect.set.
std::string writeSink(mlir::ModuleOp module, mlir::Value object, mlir::Value key, mlir::Value value,
                      globals_detail::ClosureValues & closures) {
    const auto check = [&](llvm::StringRef name) -> std::string {
        if (name == "innerHTML" || name == "outerHTML") {
            if (std::string why = handlerMarkup(value, name); !why.empty()) { return why; }
            if (std::string why = codeMarkup(value, name); !why.empty()) { return why; }
            // The content becomes a script element's text. Only a receiver
            // proved to be a script element counts here: a helper's markup
            // write on a value that merely might be one (safe.js) stays
            // unmarked, the way its safe controls require.
            const auto tag = elementTag(object, closures);
            return tag && tag->equals_insensitive("script") ? scriptWrite(object, name, closures)
                                                           : std::string{};
        }
        if (name == "srcdoc") { return codeMarkup(value, name); }
        if (llvm::StringSwitch<bool>(name)
                .Cases({"href", "src", "data", "action", "formAction", "location"}, true)
                .Default(false)) {
            return urlWrite(name, value, object, name, closures);
        }
        if (name == "text" || name == "textContent" || name == "innerText") {
            return scriptWrite(object, name, closures);
        }
        if (name == "value") { return attrValueWrite(object, value, module, closures); }
        return std::string{};
    };
    if (const auto text = constantString(key)) { return check(*text); }
    if (ownedValue(object, closures)) { return {}; }
    // A computed key may write a code sink: follow it the way the
    // attribute-name flow follows a setAttribute name.
    const auto leaves = closures.leafValuesOf(key);
    if (!leaves.unknown.empty() || leaves.values.empty()) {
        for (llvm::StringRef name :
             {"innerHTML", "outerHTML", "srcdoc", "text", "textContent", "innerText", "href",
              "src", "data", "action", "formAction", "location"}) {
            if (!check(name).empty()) {
                return "a property name not known at build time may write a code sink, whose code "
                       "a native program cannot compile";
            }
        }
        return {};
    }
    for (mlir::Value leaf : leaves.values) {
        if (const auto text = constantString(leaf)) {
            if (std::string why = check(*text); !why.empty()) { return why; }
            continue;
        }
        if (mlir::isa_and_present<ConstantOp>(leaf.getDefiningOp())) { continue; }
        // Every sink property name is ASCII letters only, so a concatenation
        // with a constant piece holding any other character (`.bs`, `::`) can
        // never name a sink, whatever the other pieces are.
        mlir::Value part = registerFlowOrigin(leaf);
        bool concat = false;
        bool ruledOut = false;
        while (auto sum = part ? part.getDefiningOp<BinaryOp>() : BinaryOp{}) {
            if (sum.getKind() != BinaryKind::Add && sum.getKind() != BinaryKind::Concat) { break; }
            concat = true;
            const auto text = constantString(sum.getRhs());
            if (text && !llvm::all_of(*text, [](char c) { return llvm::isAlpha(c); })) {
                ruledOut = true;
            }
            part = registerFlowOrigin(sum.getLhs());
        }
        if (const auto text = part ? constantString(part) : std::nullopt;
            text && !llvm::all_of(*text, [](char c) { return llvm::isAlpha(c); })) {
            ruledOut = true;
        }
        if (concat && ruledOut) { continue; }
        const auto prefix = leftmostPart(leaf);
        for (llvm::StringRef name :
             {"innerHTML", "outerHTML", "srcdoc", "text", "textContent", "innerText", "href",
              "src", "data", "action", "formAction", "location"}) {
            if (!prefix || name.starts_with(*prefix)) {
                if (std::string why = check(name); !why.empty()) { return why; }
            }
        }
    }
    return {};
}

/// Why calling `method` with `call`'s arguments may create code, or empty.
/// `closures` follows every possible leaf, not just the closures among them.
std::string methodCall(llvm::StringRef method, CallView call, mlir::Operation * site,
                       globals_detail::ClosureValues & closures) {
    const auto args = call.args;
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
        if (std::string why = codeMarkup(markup, method); !why.empty()) { return why; }
        return textIntoScript(call.receiver, method, closures);
    }
    if (method == "appendChild" || method == "insertBefore" || method == "replaceChild" ||
        method == "append" || method == "prepend" || method == "before" || method == "after" ||
        method == "replaceWith" || method == "replaceChildren" ||
        method == "insertAdjacentElement" || method == "insertAdjacentText") {
        const auto first =
            method == "insertAdjacentElement" || method == "insertAdjacentText" ? 1u : 0u;
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
        // Text into a possibly-script element changes the code it will run: an
        // argument that is not proved to be a node becomes a text node here
        // (the same values the node rule above follows), and insertAdjacentText
        // always inserts text.
        bool text = method == "insertAdjacentText";
        for (std::size_t index = first; !text && index < count; ++index) {
            text = args[index] && !elementTag(args[index], closures);
        }
        if (text) { return textIntoScript(call.receiver, method, closures); }
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
            return urlWrite(key, value, call.receiver, api, closures);
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
    if (method == "setAttributeNode" || method == "setAttributeNodeNS" ||
        method == "setNamedItem" || method == "setNamedItemNS") {
        // The attribute carries its own name and value into the element this
        // call attaches it to; only names that cannot be sinks are safe here.
        llvm::SmallVector<llvm::StringRef> names;
        attrNamesOf(arg(0), site->getParentOfType<mlir::ModuleOp>(), 0, names);
        const auto sink = [](llvm::StringRef name) {
            return name.empty() || name.starts_with_insensitive("on") ||
                   llvm::StringSwitch<bool>(name.lower())
                       .Cases({"srcdoc", "href", "src", "data", "action", "formaction",
                               "xlink:href"},
                              true)
                       .Default(false);
        };
        if (names.empty() || llvm::any_of(names, sink)) {
            return "`" + method.str() +
                   "` attaches an attribute that may create code, whose code a native program "
                   "cannot compile";
        }
        return {};
    }
    if (method == "defineProperty" || method == "getOwnPropertyDescriptor" ||
        method == "getOwnPropertyDescriptors") {
        // A sink property's descriptor carries a setter or value that runs
        // code; the setter read off one is called later, through a value this
        // census cannot see. The plural form returns every descriptor, so its
        // property name is never known at build time.
        const mlir::Value target = arg(0);
        const mlir::Value name = arg(1);
        if (target && ownedValue(target, closures)) { return {}; }
        if (method == "getOwnPropertyDescriptors") {
            return "`" + method.str() +
                   "` of a property not known at build time can reach code a native program "
                   "cannot compile";
        }
        if (!name) { return {}; }
        if (std::string why = handlerName(name, method, closures); !why.empty()) { return why; }
        const auto sink = [&](llvm::StringRef key) -> std::string {
            if (!llvm::StringSwitch<bool>(key.lower())
                     .Cases({"srcdoc", "innerHTML", "outerHTML", "text", "textContent", "innerText",
                             "href", "src", "data", "action", "formaction", "xlink:href",
                             "location"},
                            true)
                     .Default(false)) {
                return std::string{};
            }
            return "`" + method.str() + "` of the property `" + key.str() +
                   "` can reach code a native program cannot compile";
        };
        if (const auto text = constantString(name)) { return sink(*text); }
        const auto leaves = closures.leafValuesOf(name);
        if (!leaves.unknown.empty() || leaves.values.empty()) {
            return "`" + method.str() +
                   "` of a property not known at build time can reach code a native program "
                   "cannot compile";
        }
        for (mlir::Value leaf : leaves.values) {
            if (const auto text = constantString(leaf)) {
                if (std::string why = sink(*text); !why.empty()) { return why; }
                continue;
            }
            if (mlir::isa_and_present<ConstantOp>(leaf.getDefiningOp())) { continue; }
            const auto prefix = leftmostPart(leaf);
            for (llvm::StringRef key :
                 {"srcdoc", "innerHTML", "outerHTML", "text", "textContent", "innerText", "href",
                  "src", "data", "action", "formaction", "xlink:href", "location"}) {
                if (!prefix || key.take_front(prefix->size()).equals_insensitive(*prefix)) {
                    if (std::string why = sink(key); !why.empty()) { return why; }
                }
            }
        }
        return {};
    }
    // Navigating to a URL: `location.assign(u)`, `location.replace(u)` and
    // `window.open(u)`, when the receiver is that object.
    if ((method == "assign" || method == "replace" || method == "open") && arg(0)) {
        const mlir::Value receiver = registerFlowOrigin(call.receiver);
        auto global = receiver ? receiver.getDefiningOp<LoadGlobalOp>() : LoadGlobalOp{};
        auto read = receiver ? receiver.getDefiningOp<GetPropertyOp>() : GetPropertyOp{};
        const bool location = (global && global.getName() == "location") ||
                              (read && constantKey(read.getKey()) == "location");
        const bool window = (global && globals_detail::names_global_object(global.getName())) ||
                            (!call.receiver || isUndefinedConstant(call.receiver));
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
        .Cases({"insertAdjacentText", "setAttributeNode", "setAttributeNodeNS", "setNamedItem",
                "setNamedItemNS", "defineProperty", "getOwnPropertyDescriptor",
                "getOwnPropertyDescriptors", "set"},
               true)
        .Default(false);
}

/// The `get_property "set"` `value` reaches, through registers and the globals
/// stores write, or none: `Reflect.set` and a name holding it are the same
/// function here.
GetPropertyOp setReadOf(mlir::Value value, mlir::ModuleOp module, unsigned depth) {
    if (!value || depth == 8) { return {}; }
    const mlir::Value origin = registerFlowOrigin(value);
    if (auto read = origin ? origin.getDefiningOp<GetPropertyOp>() : GetPropertyOp{}) {
        return constantKey(read.getKey()) == "set" ? read : GetPropertyOp{};
    }
    if (auto load = origin ? origin.getDefiningOp<LoadGlobalOp>() : LoadGlobalOp{}) {
        GetPropertyOp found;
        module.walk([&](StoreGlobalOp store) {
            if (found || store.getName() != load.getName()) { return; }
            found = setReadOf(store.getValue(), module, depth + 1);
        });
        return found;
    }
    return {};
}

/// Whether `value` is the Reflect global, through registers and the globals
/// stores write (`var R = Reflect` makes R the same object).
bool isReflect(mlir::Value value, mlir::ModuleOp module, unsigned depth = 0) {
    if (!value || depth == 8) { return false; }
    const mlir::Value origin = registerFlowOrigin(value);
    auto load = origin ? origin.getDefiningOp<LoadGlobalOp>() : LoadGlobalOp{};
    if (!load) { return false; }
    if (load.getName() == "Reflect") { return true; }
    bool found = false;
    module.walk([&](StoreGlobalOp store) {
        if (found || store.getName() != load.getName()) { return; }
        found = isReflect(store.getValue(), module, depth + 1);
    });
    return found;
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
        // A resolved call (`ctjs.call_direct`) is a call here too: the value
        // is its callee operand, not a value that escapes to something else.
        const auto view = callView(use->getOwner());
        if (!view || view->callee != use->get()) {
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
        if (std::string why = methodCall(name, *view, use->getOwner(), closures); !why.empty()) {
            sites.push_back({use->getOwner(), std::move(why)});
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

void markRuntimeCode(mlir::ModuleOp module) {
    // Only a module that is its whole page keeps the model's promise that no
    // code is added later; the closure query the timer check makes assumes it.
    if (!wholePage(module)) { return; }
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
                return;
            }
            if (constantString(read.getKey()) ||
                ownedValue(read.getObject(), closures) /* not a sink element */) {
                return;
            }
            // A computed key may name a code-creating method: follow it the
            // way the attribute-name flow follows a setAttribute name. A call
            // through it is checked here; reading it as a value is not, which
            // would mark every indexed read of an array.
            const auto leaves = closures.leafValuesOf(read.getKey());
            const bool unbounded = !leaves.unknown.empty() || leaves.values.empty();
            llvm::SmallVector<std::string> names;
            if (!unbounded) {
                for (mlir::Value leaf : leaves.values) {
                    if (const auto text = constantString(leaf)) {
                        if (createsCode(*text)) { names.push_back(text->str()); }
                        continue;
                    }
                    if (mlir::isa_and_present<ConstantOp>(leaf.getDefiningOp())) { continue; }
                    const auto prefix = leftmostPart(leaf);
                    for (llvm::StringRef name :
                         {"setTimeout", "setInterval", "createElement", "createElementNS", "write",
                          "writeln", "createContextualFragment", "insertAdjacentHTML",
                          "setHTMLUnsafe", "parseHTMLUnsafe", "parseFromString", "setAttribute",
                          "setAttributeNS", "createAttribute", "createAttributeNS", "assign",
                          "replace", "open", "appendChild", "insertBefore", "replaceChild",
                          "append", "prepend", "before", "after", "replaceWith",
                          "replaceChildren", "insertAdjacentElement", "insertAdjacentText",
                          "setAttributeNode", "setAttributeNodeNS", "setNamedItem",
                          "setNamedItemNS", "defineProperty", "getOwnPropertyDescriptor",
                          "getOwnPropertyDescriptors", "set"}) {
                        if (!prefix || name.starts_with(*prefix)) {
                            names.push_back(name.str());
                        }
                    }
                }
            }
            if (!unbounded && names.empty()) { return; }
            auto uses = registerFlowUses(read.getResult());
            if (!uses) {
                found.push_back(
                    {op, "a method name not known at build time moves through too many registers "
                         "to follow"});
                return;
            }
            for (mlir::OpOperand * use : *uses) {
                const auto view = callView(use->getOwner());
                if (!view || view->callee != use->get()) { continue; }
                if (unbounded) {
                    found.push_back(
                        {use->getOwner(),
                         "a method name not known at build time may create code when "
                         "called, whose code a native program cannot compile"});
                    continue;
                }
                for (const std::string & name : names) {
                    if (std::string why = methodCall(name, *view, use->getOwner(), closures);
                        !why.empty()) {
                        found.push_back({use->getOwner(), std::move(why)});
                        break;
                    }
                }
            }
            return;
        }
        if (auto write = mlir::dyn_cast<SetPropertyOp>(op)) {
            if (std::string why = writeSink(module, write.getObject(), write.getKey(),
                                            write.getValue(), closures);
                !why.empty()) {
                found.push_back({op, std::move(why)});
            }
            return;
        }
        if (auto call = mlir::dyn_cast<CallOp>(op)) {
            // Reflect.set(target, key, value) writes `key` on `target` just
            // like a ctjs.set_property does, so the same sink rules run. The
            // builtin can be held in a name (`var R = Reflect; R.set`), so the
            // callee resolves to a `set` read whose object is the Reflect
            // global through registers and global stores.
            auto read = setReadOf(call.getCallee(), module, 0);
            const auto args = call.getArgs();
            if (read && isReflect(read.getObject(), module) && args.size() >= 3) {
                if (std::string why = writeSink(module, args[0], args[1], args[2], closures);
                    !why.empty()) {
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
    }
}

} // namespace ctcompile::ctjs
