// dom_bindings - the live, read-only CSSStyleDeclaration getComputedStyle hands
// back, and the global itself.
//
// One of three files carved out of a 1,326-line bindings/computed_style.cpp
// on 2026-09-08. The member functions belong to the one class declared in
// include/ctbrowser/shell/bindings.hpp; the serialisation helpers more than
// one file needs are declared in internal.hpp beside this, with external
// linkage in ctbrowser::shell::detail, and internal.hpp carries the note on
// where a computed value comes from. Nothing about the public header changed.

#include "internal.hpp"

namespace ctbrowser::shell {

using namespace detail;

namespace {

// ONE ELEMENT'S ANSWERS, CACHED FOR AS LONG AS THE DOCUMENT DOES NOT MOVE.
//
// The object below is live, so every property read has to be able to re-derive -
// and re-deriving means the probe's three tree walks plus 148 values, which a
// page reading one property per element after another would pay 148 times over.
// `stamp` is the document version the answers were computed at: a script cannot
// change what the cascade says without changing the document, and
// `document::version()` counts exactly that. A read at the same version is the
// same answer.
//
// WHAT THE STAMP DOES NOT SEE: a stylesheet edited through the CSSOM
// (insertRule, replaceSync) changes which rules match without touching the DOM.
// The browser is told about those through `set_author_styles_hook` and nothing
// reaches this file, so a page that edits a sheet and then reads a computed
// style it is already holding reads the previous answer. Naming it here because
// the fix is the same shared flush hook `refresh` below wants.
struct computed_cache {
    std::vector<std::pair<std::string, std::string>> entries;
    std::uint64_t stamp = 0;
};

// DOES `getComputedStyle`'s SECOND ARGUMENT NAME A PSEUDO-ELEMENT?
//
// CSSOM §5.1 asks the question in exactly this order, and the order is the
// whole rule: an argument that is null, absent, empty, or does NOT begin with a
// colon is IGNORED - `getComputedStyle(div, "before")` and
// `getComputedStyle(div, "totallynotapseudo")` both answer about the element
// itself, which is why the first assertion of getComputedStyle-pseudo,
// -pseudo-checkmark and -pseudo-picker-icon is "the argument is ignored (due to
// no colon)". Anything that DOES begin with a colon is a pseudo-element
// request, and it gets one of two answers: the pseudo-element's style, or - when
// it does not parse, or names a pseudo-element the engine has no styles for - an
// EMPTY CSSStyleDeclaration.
//
// THIS ENGINE HAS NO PSEUDO-ELEMENT STYLING AT ALL. `style/selector.hpp` models
// pseudo-CLASSES and nothing else, so `::before` matches no rule, generates no
// box and has no style to report. That makes the second answer the right one for
// every colon-prefixed argument, and it is a very different answer from the one
// this used to give: ignoring the argument reported the ORIGINATING ELEMENT's
// style as the pseudo-element's, so `getComputedStyle(div, "::before").width`
// came back as the div's `100px` where every engine says `""`. CSSOM is explicit
// that a pseudo-element that does not exist reports an empty declaration -
// `length === 0`, every property the empty string - and that is what
// getComputedStyle-pseudo's "Unknown pseudo-elements",
// -pseudo-with-argument's seventeen "should not parse" cases and -pseudo-picker's
// six "invalid pseudo-element" cases all assert.
//
// It costs one subtest to say it this bluntly: `::picker(select)` is a real
// pseudo-element that Chrome resolves, and we answer empty for it too. That is
// the honest report of an engine that does not implement it, and the moment a
// pseudo-element grows a cascade this becomes a lookup rather than a `true`.
[[nodiscard]] bool names_a_pseudo_element(context & c, value given) {
    if (given.is_nullish()) { return false; }
    const std::string text = c.to_string(given);
    return !text.empty() && text.front() == ':';
}

} // namespace

value dom_bindings::computed_style_object(context & cx, node_id id) {
    auto * held = static_cast<script::object_object *>(cx.make_object().as_heap());

    const auto cached = std::make_shared<computed_cache>();
    cached->entries = computed_style_entries(id);
    cached->stamp = doc_->version();

    // THE LIVE READ, and the flush it needs.
    //
    // Every property below re-derives, so a page holding the object across a
    // write sees the write - which is the whole of CSSOM's "live" and of
    // getComputedStyle-display-none-001/-002. Re-deriving is only correct if the
    // pipeline has caught up first, though: `el.style.color = 'green'` marks the
    // cascade stale and nothing resolves it until the next frame, so a read
    // that skipped the flush would answer from before the page's own write -
    // exactly the defect docs/css-conformance.md §6 measured for the call
    // itself.
    //
    // REACHED THROUGH THE GLOBAL, which is the only channel a binding has.
    // browser::run_scripts wraps `getComputedStyle` with a native that runs
    // precisely the stages `dirty_` says are stale; the bindings deliberately do
    // not know that layout exists ("a native that changes the document calls
    // on_mutation, and the browser decides what that invalidates", at the top of
    // shell/bindings.hpp), so calling that wrapper with no arguments is how a
    // property read asks for the same flush. With no argument the inner native
    // makes one empty object and returns, so the call costs the flush and
    // nothing else. A page that has REPLACED the global gets no flush rather
    // than a call into its own function: the kind check is what makes that safe.
    //
    // It is a stand-in for the shared flush hook on dom_bindings that
    // getBoundingClientRect, offsetWidth and clientHeight all want too, and it
    // is written to be deleted the moment that exists.
    const auto refresh = [this, id, cached](context & c) {
        const std::uint64_t now = doc_->version();
        if (now == cached->stamp) { return; }
        const value flush = c.global("getComputedStyle");
        if (flush.is_kind(script::heap_kind::native)) {
            (void)c.call(flush, std::span<const value>{});
        }
        cached->entries = computed_style_entries(id);
        cached->stamp = doc_->version();
    };
    const auto answer = [cached](std::string_view name) -> std::string {
        for (const auto & [key, text] : cached->entries) {
            if (std::string_view{key} == name) { return text; }
        }
        return {};
    };

    // A COMPUTED STYLE IS READ-ONLY, and CSSOM §6.7.2 says how: every mutating
    // member throws NoModificationAllowedError. Doing nothing instead let a page
    // write to it and believe the write had landed.
    //
    // ONE SETTER OBJECT FOR ALL OF THEM. The property-assignment half of this
    // rule used to be left out on the grounds that it would cost "a setter
    // accessor on each of the 125 published names, per call" - which was true
    // while the names were data properties. They are accessors now, for
    // liveness, so each already has a descriptor to hang this on and the whole
    // rule costs one extra allocation. computed-style-001 and
    // computed-style-set-property assert it three ways: `style.color = 'blue'`,
    // `style.cssText = '...'` and `style.setProperty(...)`.
    const value refuse = value::object(
        cx.allocate<script::native_object>("set", [this](context & c, std::span<value>) {
            throw_dom_exception(c, "NoModificationAllowedError",
                                "a computed style declaration is read-only");
            return value::undefined();
        }));
    const auto reader = [&cx, refresh, answer](const std::string & name) {
        return value::object(cx.allocate<script::native_object>(
            name, [refresh, answer, name](context & c, std::span<value>) {
                refresh(c);
                return c.string(answer(name));
            }));
    };

    // EVERY NAME THE OBJECT ANSWERS TO: the whole table - longhands and
    // shorthands alike, because CSSOM gives a computed style an attribute for
    // every SUPPORTED property - and then whatever this element declared that
    // the table has never heard of.
    //
    // BOTH SPELLINGS SHARE ONE GETTER. `background-color` and `backgroundColor`
    // are one property read two ways, and a second native per name would double
    // the allocations a getComputedStyle call costs on a page that asks for one
    // per element.
    const auto publish = [&](const std::string & css_name) {
        const value get = reader(css_name);
        held->define_accessor(css_name, get, refuse);
        const std::string idl = style::css::idl_name_of(css_name);
        if (idl != css_name) { held->define_accessor(idl, get, refuse); }
    };
    for (const style::css::property_syntax & p : style::css::known_properties()) {
        publish(std::string{p.name});
    }
    for (const auto & [name, text] : cached->entries) {
        if (style::css::find_property(name) == nullptr) { publish(name); }
    }

    // THE INDEXED GETTER AND `length`, CSSOM §6.7 - as DATA properties, which is
    // also what makes the object ITERABLE. `[...style]` and `for (const p of
    // style)` go through context::iterable_values, and that recognises anything
    // carrying a numeric `length` beside indexed properties; there is no
    // Symbol.iterator dispatch in this VM to hook instead. serialize-all-longhands
    // and getComputedStyle-property-order both spread one.
    //
    // THE SUPPORTED LONGHANDS, in the lexicographic order computed_style_entries
    // put them in - and none of the shorthands or custom properties that follow
    // them there. Empty for an element that is not being rendered, which is
    // `length === 0` in getComputedStyle-detached-subtree.
    //
    // FIXED WHEN THE OBJECT IS MADE, unlike every value on it. The set of
    // supported longhands cannot change under a page; what can is whether the
    // element is in the document at all, so a declaration taken for a detached
    // element keeps `length === 0` after the element is appended. Making the
    // index list live too would mean an accessor per index and a `length` that
    // is not a data property, and `context::iterable_values` finds the object
    // iterable by reading exactly that data property.
    std::vector<std::string> indexed;
    for (const auto & [name, text] : cached->entries) {
        const style::css::property_syntax * known = style::css::find_property(name);
        if (known != nullptr && !known->shorthand) { indexed.push_back(name); }
    }
    for (std::size_t i = 0; i < indexed.size(); ++i) {
        held->set(std::to_string(i), cx.string(indexed[i]));
    }
    held->set("length", value::number(static_cast<double>(indexed.size())));

    const auto method = [&](std::string name, script::native_fn fn) {
        held->set(name, value::object(cx.allocate<script::native_object>(name, std::move(fn))));
    };
    // THE SPELLING THE DUMP USES, and the only one both engines agree on. It
    // takes a CSS name and accepts the IDL one too, because a page holding
    // `backgroundColor` should not have to hyphenate it itself. Live, like the
    // accessors: css-style-declaration-modifications edits a stylesheet rule and
    // reads the computed value back through this.
    method("getPropertyValue", [refresh, answer](context & c, std::span<value> args) {
        if (args.empty()) { return c.string(std::string{}); }
        const std::string asked = style::css::css_name_of(c.to_string(args[0]));
        refresh(c);
        return c.string(answer(asked));
    });
    // Always empty. Importance is a cascade INPUT, and by the time a value is
    // computed the question has been settled; this engine does not keep which
    // declaration won past resolve().
    method("getPropertyPriority", [](context & c, std::span<value>) { return c.string(""); });
    method("item", [indexed](context & c, std::span<value> args) {
        if (args.empty()) { return c.string(std::string{}); }
        const double i = context::to_number(args[0]);
        if (!(i >= 0) || static_cast<std::size_t>(i) >= indexed.size()) { return c.string(""); }
        return c.string(indexed[static_cast<std::size_t>(i)]);
    });
    const auto refuse_method = [this](context & c, std::span<value>) {
        throw_dom_exception(c, "NoModificationAllowedError",
                            "a computed style declaration is read-only");
        return value::undefined();
    };
    method("setProperty", refuse_method);
    method("removeProperty", refuse_method);
    // Empty rather than reconstructed - which is also what the specification
    // says: a computed style's cssText getter returns the empty string, because
    // serialising one means deciding how to rebuild every shorthand and engines,
    // including Chrome across its own versions, disagree there. Which is exactly
    // why the compared property set is longhands only. An ACCESSOR so that the
    // setter can refuse: `cs.cssText = "color: blue"` is the first of
    // computed-style-001's three read-only assertions.
    held->define_accessor(
        "cssText",
        value::object(cx.allocate<script::native_object>(
            "cssText", [](context & c, std::span<value>) { return c.string(std::string{}); })),
        refuse);
    // A computed style belongs to no rule.
    held->set("parentRule", value::null());
    return value::object(held);
}

void dom_bindings::install_computed_style(context & cx) {
    // A BARE GLOBAL IS ENOUGH for `window.getComputedStyle` as well: `window` is
    // a proxy whose handler falls back to the globals (install_window), so it
    // does not need its own copy. p5.js reads the bare form
    // (`getComputedStyle(el)` in vendor/p5/p5.js) and pages write both.
    //
    // browser::run_scripts WRAPS THIS GLOBAL to flush a pending restyle before
    // it runs - the bindings deliberately do not know that layout exists, and
    // without the flush every answer below is the one from before the script's
    // own write. See the note there, and the one on `refresh` above: a LIVE read
    // needs the same flush and reaches it back through this same global.
    cx.define_native("getComputedStyle", [this](context & c, std::span<value> args) {
        const node_id id = args.empty() ? node_id{} : handle_of(args[0]);
        if (!id) { return c.make_object(); }
        // A SECOND ARGUMENT NAMING A PSEUDO-ELEMENT gets an EMPTY declaration,
        // for the reason `names_a_pseudo_element` sets out - and it is spelled
        // as an empty node handle rather than as a flag, because
        // `computed_style_entries` ALREADY answers nothing for an element that
        // is not in the document. A pseudo-element this engine does not style is
        // the same case, gets the same answer, and gets it through the same
        // object: every property reads back the empty string, `length` is zero,
        // and every write still throws NoModificationAllowedError.
        if (args.size() > 1 && names_a_pseudo_element(c, args[1])) {
            return computed_style_object(c, node_id{});
        }
        return computed_style_object(c, id);
    });
}

} // namespace ctbrowser::shell
