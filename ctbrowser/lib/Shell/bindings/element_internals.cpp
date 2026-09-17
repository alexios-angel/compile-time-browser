// dom_bindings - ElementInternals, HTML 4.13.7 and 4.13.8: what
// `attachInternals()` hands a custom element's constructor.
//
// ONE OBJECT PER ELEMENT, hung off the element's wrapper under a symbol key
// (so it is a root for as long as the element is, and `for-in` does not see
// it) with the element under a symbol key of its own on the internals. Every
// member reads the element through that back-reference, so the natives live
// on ElementInternals.prototype and the object itself carries only state:
// the validity flags and message, the submission value, the ARIA defaults.
//
// WHAT IS FORM-ASSOCIATED HERE. `form`, `setFormValue`, `setValidity`,
// `willValidate`, `validity`, `validationMessage`, `checkValidity`,
// `reportValidity` and `labels` refuse (NotSupportedError) unless the
// definition said `static formAssociated = true` - and the rest of the
// form-association story is measured elsewhere: the submission value is
// stored and never submitted (the form's submission path is
// shell/page/forms, which does not know custom elements), `reportValidity`
// reports nothing, and formResetCallback never fires because form.reset()
// is not this file's. formAssociatedCallback and formDisabledCallback DO
// fire, from the scan in custom_elements.cpp, which diffs the form owner
// and the disabledness of a form-associated element like any other state.
//
// ponytail: `:enabled`/`:disabled`/`:valid`/`:invalid` on a form-associated
// custom element are the style engine's and are not answered.

#include <ctbrowser/core/algorithms.hpp>
#include <ctbrowser/shell/bindings.hpp>

#include "events/internal.hpp"

#include <cstddef>
#include <iterator>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace ctbrowser::shell {
namespace {

// Private-name keys (the `@#` the VM gives `#x`): no page can spell one, and
// neither getOwnPropertyNames nor getOwnPropertySymbols reports them.
constexpr std::string_view internals_key = "@#ctbrowser:internals";
constexpr std::string_view target_key = "@#ctbrowser:internals-target";
constexpr std::string_view validity_key = "@#ctbrowser:internals-validity";
constexpr std::string_view message_key = "@#ctbrowser:internals-message";
constexpr std::string_view form_value_key = "@#ctbrowser:internals-form-value";
constexpr std::string_view form_state_key = "@#ctbrowser:internals-form-state";
constexpr std::string_view states_key = "@#ctbrowser:internals-states";
constexpr std::string_view aria_prefix = "@#ctbrowser:internals-aria:";
constexpr std::string_view flag_prefix = "@#ctbrowser:internals-flag:";

// ValidityState's ten flags, HTML 4.10.20.3, in interface order.
constexpr std::string_view validity_flags[] = {
    "valueMissing",   "typeMismatch",  "patternMismatch", "tooLong",  "tooShort",
    "rangeUnderflow", "rangeOverflow", "stepMismatch",    "badInput", "customError"};

// The ARIA mixin (ARIA 1.3 "ARIAMixin"), as ElementInternals carries it.
// Three shapes: a nullable string, a nullable Element, a nullable
// FrozenArray<Element>; the setter keeps what it was given for the last two.
constexpr std::string_view aria_strings[] = {"role",
                                             "ariaAtomic",
                                             "ariaAutoComplete",
                                             "ariaBrailleLabel",
                                             "ariaBrailleRoleDescription",
                                             "ariaBusy",
                                             "ariaChecked",
                                             "ariaColCount",
                                             "ariaColIndex",
                                             "ariaColIndexText",
                                             "ariaColSpan",
                                             "ariaCurrent",
                                             "ariaDescription",
                                             "ariaDisabled",
                                             "ariaExpanded",
                                             "ariaHasPopup",
                                             "ariaHidden",
                                             "ariaInvalid",
                                             "ariaKeyShortcuts",
                                             "ariaLabel",
                                             "ariaLevel",
                                             "ariaLive",
                                             "ariaModal",
                                             "ariaMultiLine",
                                             "ariaMultiSelectable",
                                             "ariaOrientation",
                                             "ariaPlaceholder",
                                             "ariaPosInSet",
                                             "ariaPressed",
                                             "ariaReadOnly",
                                             "ariaRelevant",
                                             "ariaRequired",
                                             "ariaRoleDescription",
                                             "ariaRowCount",
                                             "ariaRowIndex",
                                             "ariaRowIndexText",
                                             "ariaRowSpan",
                                             "ariaSelected",
                                             "ariaSetSize",
                                             "ariaSort",
                                             "ariaValueMax",
                                             "ariaValueMin",
                                             "ariaValueNow",
                                             "ariaValueText"};
constexpr std::string_view aria_elements[] = {"ariaActiveDescendantElement", "ariaControlsElements",
                                              "ariaDescribedByElements",     "ariaDetailsElements",
                                              "ariaErrorMessageElements",    "ariaFlowToElements",
                                              "ariaLabelledByElements",      "ariaOwnsElements"};

[[nodiscard]] script::object_object * object_of(value v) {
    return v.is_object() ? static_cast<script::object_object *>(v.as_heap()) : nullptr;
}

[[nodiscard]] value slot(script::object_object & obj, std::string_view key) {
    const value * held = obj.find(std::string{key});
    return held == nullptr ? value::undefined() : *held;
}

} // namespace

// --- the form-association facts ------------------------------------------------

node_id dom_bindings::form_owner_of(const read_txn & txn, node_id id) const {
    const auto is_form = [&](node_id at) {
        return txn.kind(at).value_or(node_kind::text) == node_kind::element &&
               txn.element_ns(at) == node_ns::html && txn.local_name(at) == "form";
    };
    // A `form` attribute names a form by id, in the element's own tree - and
    // names nothing when no such form is there.
    const std::string_view named = txn.attribute_value(id, atoms_->intern("form"));
    if (txn.has_attribute(id, atoms_->intern("form"))) {
        const atom id_name = atoms_->intern("id");
        std::vector<node_id> pending{txn.root_of_tree(id, false)};
        while (!pending.empty()) {
            const node_id at = pending.back();
            pending.pop_back();
            if (is_form(at) && txn.attribute_value(at, id_name) == named) { return at; }
            const std::span<const node_id> children = txn.children(at);
            for (std::size_t i = children.size(); i-- > 0;) { pending.push_back(children[i]); }
        }
        return node_id{};
    }
    for (node_id at = txn.parent(id); at; at = txn.parent(at)) {
        if (is_form(at)) { return at; }
    }
    return node_id{};
}

bool dom_bindings::form_control_disabled(const read_txn & txn, node_id id) const {
    const atom disabled = atoms_->intern("disabled");
    if (txn.has_attribute(id, disabled)) { return true; }
    // A disabled <fieldset> ancestor disables everything in it except what
    // sits in its first <legend> child.
    node_id child = id;
    for (node_id at = txn.parent(id); at; child = at, at = txn.parent(at)) {
        if (txn.kind(at).value_or(node_kind::text) != node_kind::element ||
            txn.element_ns(at) != node_ns::html || txn.local_name(at) != "fieldset" ||
            !txn.has_attribute(at, disabled)) {
            continue;
        }
        node_id first_legend;
        for (const node_id kid : txn.children(at)) {
            if (txn.kind(kid).value_or(node_kind::text) == node_kind::element &&
                txn.element_ns(kid) == node_ns::html && txn.local_name(kid) == "legend") {
                first_legend = kid;
                break;
            }
        }
        if (child != first_legend) { return true; }
    }
    return false;
}

// --- the interface ----------------------------------------------------------------

void dom_bindings::install_element_internals(context & cx,
                                             script::object_object & html_element_proto) {
    auto * proto = cx.allocate<script::object_object>();
    element_internals_prototype_ = value::object(proto);

    // THE ELEMENT BEHIND A RECEIVER, with the bindings that own it - or a
    // TypeError, which is what a call on the wrong receiver is.
    struct target {
        dom_bindings * owner = nullptr;
        node_id id;
        script::object_object * internals = nullptr;
        value element;
    };
    const auto target_of = [this](context & c, const char * method) -> target {
        target out;
        out.internals = object_of(c.current_this());
        if (out.internals != nullptr) {
            out.element = slot(*out.internals, target_key);
            out.owner = owner_of(out.element);
            if (out.owner != nullptr) { out.id = out.owner->handle_of(out.element); }
        }
        if (!out.id) {
            c.throw_error("TypeError", std::string{"Failed to execute '"} + method +
                                           "' on 'ElementInternals': Illegal invocation");
        }
        return out;
    };
    // ...and whether its definition is form-associated (HTML 4.13.7.2: every
    // form-related member is a NotSupportedError otherwise).
    const auto form_associated_target = [this, target_of](context & c,
                                                          const char * method) -> target {
        target out = target_of(c, method);
        if (!out.id) { return out; }
        const auto state = out.owner->custom_elements_.find(out.id.key());
        const bool associated =
            state != out.owner->custom_elements_.end() &&
            primary().custom_definitions_[state->second.definition].form_associated;
        if (!associated) {
            throw_dom_exception(c, "NotSupportedError",
                                std::string{"Failed to execute '"} + method +
                                    "' on 'ElementInternals': the target element is not a "
                                    "form-associated custom element");
            out.id = node_id{};
        }
        return out;
    };
    // "Barred from constraint validation", HTML 4.10.20.1, for a
    // form-associated custom element: disabled, readonly, or inside a
    // <datalist>.
    const auto barred = [](const dom_bindings & b, const read_txn & txn, node_id id) {
        if (b.form_control_disabled(txn, id) ||
            txn.has_attribute(id, b.atoms_->intern("readonly"))) {
            return true;
        }
        for (node_id at = txn.parent(id); at; at = txn.parent(at)) {
            if (txn.kind(at).value_or(node_kind::text) == node_kind::element &&
                txn.element_ns(at) == node_ns::html && txn.local_name(at) == "datalist") {
                return true;
            }
        }
        return false;
    };
    const auto flag_key = [](std::string_view name) {
        return std::string{flag_prefix} + std::string{name};
    };
    const auto is_invalid = [flag_key](script::object_object & internals) {
        for (const std::string_view name : validity_flags) {
            if (context::truthy(slot(internals, flag_key(name)))) { return true; }
        }
        return false;
    };
    // checkValidity() and reportValidity(): the `invalid` event, which does
    // not bubble and can be cancelled, at an invalid element that validates.
    const auto check = [form_associated_target, barred, is_invalid](context & c,
                                                                    const char * method) {
        const target at = form_associated_target(c, method);
        if (!at.id) { return value::undefined(); }
        bool validates = true;
        {
            const auto txn = at.owner->doc_->read();
            validates = !barred(*at.owner, txn, at.id);
        }
        if (!validates || !is_invalid(*at.internals)) { return value::boolean(true); }
        const value event = at.owner->make_event_object(c, "invalid", false, true);
        // An engine event: trusted, and initialised - dispatch refuses one
        // that is not, as it refuses a createEvent() nobody initEvent()ed.
        auto * object = static_cast<script::object_object *>(event.as_heap());
        object->set(std::string{detail::trusted_property}, value::boolean(true));
        object->set(std::string{detail::initialised_property}, value::boolean(true));
        (void)at.owner->dispatch_event("invalid", at.id, event);
        return value::boolean(false);
    };

    define_getter(cx, *proto, "shadowRoot", [target_of](context & c, std::span<value>) {
        const target at = target_of(c, "shadowRoot");
        if (!at.id) { return value::undefined(); }
        const node_id root = at.owner->shadow_root_of(at.id);
        return root ? at.owner->wrap(c, root) : value::null();
    });

    define_getter(cx, *proto, "form", [form_associated_target](context & c, std::span<value>) {
        const target at = form_associated_target(c, "form");
        if (!at.id) { return value::undefined(); }
        node_id form;
        {
            const auto txn = at.owner->doc_->read();
            form = at.owner->form_owner_of(txn, at.id);
        }
        return form ? at.owner->wrap(c, form) : value::null();
    });

    set_method(cx, *proto, "setFormValue",
               [form_associated_target](context & c, std::span<value> args) {
                   const target at = form_associated_target(c, "setFormValue");
                   if (!at.id) { return value::undefined(); }
                   // The submission value and the state, kept as given: a
                   // File, a FormData, a string, or null (undefined is null).
                   const value given = arg(args, 0);
                   at.internals->set(std::string{form_value_key},
                                     given.is_nullish() ? value::null() : given);
                   const value state = arg(args, 1);
                   at.internals->set(std::string{form_state_key},
                                     args.size() < 2 || state.is_nullish() ? value::null() : state);
                   return value::undefined();
               });

    set_method(
        cx, *proto, "setValidity",
        [this, form_associated_target, flag_key](context & c, std::span<value> args) {
            const target at = form_associated_target(c, "setValidity");
            if (!at.id) { return value::undefined(); }
            // 1-2: the flags dictionary, every member defaulting to
            // false; a message is required when any of them is true.
            const value flags = arg(args, 0);
            bool any = false;
            bool held[std::size(validity_flags)] = {};
            for (std::size_t i = 0; i < std::size(validity_flags); ++i) {
                held[i] = dict_flag(c, flags, std::string{validity_flags[i]});
                if (c.throw_pending()) { return value::undefined(); }
                any = any || held[i];
            }
            const value message = arg(args, 1);
            if (any && (message.is_undefined() || c.to_string(message).empty())) {
                c.throw_error("TypeError", "Failed to execute 'setValidity' on 'ElementInternals': "
                                           "The second argument should not be empty if one or more "
                                           "flags in the first argument are true.");
                return value::undefined();
            }
            // 3-4: the anchor is an HTMLElement that is a
            // shadow-including inclusive descendant of the target.
            const value anchor = arg(args, 2);
            if (!anchor.is_undefined()) {
                dom_bindings * owner = owner_of(anchor);
                const node_id where = owner == nullptr ? node_id{} : owner->handle_of(anchor);
                bool html = false;
                bool inside = false;
                if (where) {
                    const auto txn = owner->doc_->read();
                    html = txn.kind(where).value_or(node_kind::text) == node_kind::element &&
                           txn.element_ns(where) == node_ns::html;
                    if (owner == at.owner) {
                        for (node_id up = where; up;) {
                            if (up == at.id) {
                                inside = true;
                                break;
                            }
                            const node_id parent = txn.parent(up);
                            if (parent) {
                                up = parent;
                                continue;
                            }
                            const document::shadow_tree * tree = owner->shadow_tree_of(up);
                            up = tree == nullptr ? node_id{} : tree->host;
                        }
                    }
                }
                if (!html) {
                    c.throw_error("TypeError",
                                  "Failed to execute 'setValidity' on 'ElementInternals': "
                                  "parameter 3 is not of type 'HTMLElement'.");
                    return value::undefined();
                }
                if (!inside) {
                    throw_dom_exception(c, "NotFoundError",
                                        "Failed to execute 'setValidity' on "
                                        "'ElementInternals': The second argument should be "
                                        "a shadow-including descendant of the target element");
                    return value::undefined();
                }
            }
            for (std::size_t i = 0; i < std::size(validity_flags); ++i) {
                at.internals->set(flag_key(validity_flags[i]), value::boolean(held[i]));
            }
            at.internals->set(std::string{message_key},
                              any ? c.string(c.to_string(message)) : c.string(""));
            return value::undefined();
        });

    define_getter(cx, *proto, "willValidate",
                  [form_associated_target, barred](context & c, std::span<value>) {
                      const target at = form_associated_target(c, "willValidate");
                      if (!at.id) { return value::undefined(); }
                      const auto txn = at.owner->doc_->read();
                      return value::boolean(!barred(*at.owner, txn, at.id));
                  });

    define_getter(
        cx, *proto, "validity",
        [this, form_associated_target, flag_key, is_invalid](context & c, std::span<value>) {
            const target at = form_associated_target(c, "validity");
            if (!at.id) { return value::undefined(); }
            // ONE ValidityState PER INTERNALS, live: a page holds it across
            // setValidity() calls and reads the flags off it afterwards.
            value held = slot(*at.internals, validity_key);
            if (held.is_object()) { return held; }
            auto * state = c.allocate<script::object_object>();
            state->prototype = interface_prototype("ValidityState");
            const value internals = value::object(at.internals);
            for (const std::string_view name : validity_flags) {
                define_getter(
                    c, *state, std::string{name},
                    [internals, key = flag_key(name)](context &, std::span<value>) {
                        return value::boolean(context::truthy(
                            slot(*static_cast<script::object_object *>(internals.as_heap()), key)));
                    });
            }
            define_getter(c, *state, "valid", [internals, is_invalid](context &, std::span<value>) {
                return value::boolean(
                    !is_invalid(*static_cast<script::object_object *>(internals.as_heap())));
            });
            held = value::object(state);
            at.internals->set(std::string{validity_key}, held);
            return held;
        });

    define_getter(cx, *proto, "validationMessage",
                  [form_associated_target, barred, is_invalid](context & c, std::span<value>) {
                      const target at = form_associated_target(c, "validationMessage");
                      if (!at.id) { return value::undefined(); }
                      {
                          const auto txn = at.owner->doc_->read();
                          if (barred(*at.owner, txn, at.id)) { return c.string(""); }
                      }
                      if (!is_invalid(*at.internals)) { return c.string(""); }
                      const value message = slot(*at.internals, message_key);
                      return message.is_string() ? message : c.string("");
                  });

    set_method(cx, *proto, "checkValidity",
               [check](context & c, std::span<value>) { return check(c, "checkValidity"); });
    set_method(cx, *proto, "reportValidity",
               [check](context & c, std::span<value>) { return check(c, "reportValidity"); });

    define_getter(cx, *proto, "labels", [form_associated_target](context & c, std::span<value>) {
        const target at = form_associated_target(c, "labels");
        if (!at.id) { return value::undefined(); }
        dom_bindings * owner = at.owner;
        const node_id id = at.id;
        // HTML 4.10.4: the <label>s whose labeled control this is - by `for`
        // naming its id, or by being its nearest labelable descendant.
        return owner->make_live_collection(
            c,
            [owner, id] {
                std::vector<node_id> out;
                const auto txn = owner->doc_->read();
                if (!txn.contains(id)) { return out; }
                const atom for_name = owner->atoms_->intern("for");
                const atom id_name = owner->atoms_->intern("id");
                const std::string_view own_id = txn.attribute_value(id, id_name);
                const auto is_label = [&](node_id at) {
                    return txn.kind(at).value_or(node_kind::text) == node_kind::element &&
                           txn.element_ns(at) == node_ns::html && txn.local_name(at) == "label";
                };
                std::vector<node_id> pending{txn.root_of_tree(id, false)};
                while (!pending.empty()) {
                    const node_id at = pending.back();
                    pending.pop_back();
                    if (is_label(at)) {
                        if (txn.has_attribute(at, for_name)) {
                            if (!own_id.empty() && txn.attribute_value(at, for_name) == own_id) {
                                out.push_back(at);
                            }
                        } else {
                            // The first labelable descendant, in tree order.
                            std::vector<node_id> inner{at};
                            node_id first;
                            while (!inner.empty() && !first) {
                                const node_id here = inner.back();
                                inner.pop_back();
                                if (here != at && here == id) { first = here; }
                                if (here != at && txn.kind(here).value_or(node_kind::text) ==
                                                      node_kind::element) {
                                    const std::string_view tag = txn.local_name(here);
                                    for (const std::string_view labelable :
                                         {"button", "input", "meter", "output", "progress",
                                          "select", "textarea"}) {
                                        if (tag == labelable) { first = here; }
                                    }
                                }
                                const std::span<const node_id> kids = txn.children(here);
                                for (std::size_t i = kids.size(); i-- > 0;) {
                                    inner.push_back(kids[i]);
                                }
                            }
                            if (first == id) { out.push_back(at); }
                        }
                    }
                    const std::span<const node_id> children = txn.children(at);
                    for (std::size_t i = children.size(); i-- > 0;) {
                        pending.push_back(children[i]);
                    }
                }
                return out;
            },
            "NodeList");
    });

    // --- CustomStateSet, HTML 4.13.8: a Set of strings, live to its iterators.
    // A REAL Set under a prototype of its own: the entry list, the iteration
    // order and the liveness are the builtin's, and `CustomStateSet.prototype`
    // chains to Set.prototype for add/has/delete/clear/size/@@iterator.
    auto * states_proto = cx.allocate<script::object_object>();
    states_proto->prototype = cx.lookup_property(cx.global("Set"), "prototype");
    states_proto->define("@@toStringTag", cx.string("CustomStateSet"), script::attr_configurable);
    custom_state_set_prototype_ = value::object(states_proto);
    auto * states_ctor =
        cx.allocate<script::native_object>("CustomStateSet", [](context & c, std::span<value>) {
            c.throw_error("TypeError", "Illegal constructor");
            return value::undefined();
        });
    states_ctor->set("prototype", custom_state_set_prototype_);
    states_proto->define("constructor", value::object(states_ctor), script::attr_builtin);
    cx.define_global("CustomStateSet", value::object(states_ctor));

    define_getter(cx, *proto, "states", [this, target_of](context & c, std::span<value>) {
        const target at = target_of(c, "states");
        if (!at.id) { return value::undefined(); }
        value held = slot(*at.internals, states_key);
        if (held.is_object()) { return held; }
        held = c.construct(c.global("Set"), {});
        if (auto * set = object_of(held)) { set->prototype = custom_state_set_prototype_; }
        at.internals->set(std::string{states_key}, held);
        return held;
    });

    // --- the ARIA mixin: default semantics an author sets on the internals,
    // read back as given. Nothing here reflects to an attribute.
    for (const std::string_view name : aria_strings) {
        const std::string key = std::string{aria_prefix} + std::string{name};
        define_getter(
            cx, *proto, std::string{name},
            [target_of, key, name](context & c, std::span<value>) {
                const target at = target_of(c, std::string{name}.c_str());
                if (!at.id) { return value::undefined(); }
                const value held = slot(*at.internals, key);
                return held.is_undefined() ? value::null() : held;
            },
            [target_of, key, name](context & c, std::span<value> args) {
                const target at = target_of(c, std::string{name}.c_str());
                if (!at.id) { return value::undefined(); }
                const value given = arg(args, 0);
                at.internals->set(key, given.is_nullish() ? value::null()
                                                          : c.string(c.to_string(given)));
                return value::undefined();
            });
    }
    for (const std::string_view name : aria_elements) {
        const std::string key = std::string{aria_prefix} + std::string{name};
        define_getter(
            cx, *proto, std::string{name},
            [target_of, key, name](context & c, std::span<value>) {
                const target at = target_of(c, std::string{name}.c_str());
                if (!at.id) { return value::undefined(); }
                const value held = slot(*at.internals, key);
                return held.is_undefined() ? value::null() : held;
            },
            [target_of, key, name](context & c, std::span<value> args) {
                const target at = target_of(c, std::string{name}.c_str());
                if (!at.id) { return value::undefined(); }
                const value given = arg(args, 0);
                at.internals->set(key, given.is_nullish() ? value::null() : given);
                return value::undefined();
            });
    }

    proto->define("@@toStringTag", cx.string("ElementInternals"), script::attr_configurable);
    auto * ctor =
        cx.allocate<script::native_object>("ElementInternals", [](context & c, std::span<value>) {
            c.throw_error("TypeError", "Illegal constructor");
            return value::undefined();
        });
    ctor->set("prototype", element_internals_prototype_);
    proto->define("constructor", value::object(ctor), script::attr_builtin);
    cx.define_global("ElementInternals", value::object(ctor));

    // --- HTMLElement.prototype.attachInternals(), HTML 4.13.7.1 -----------
    set_method(cx, html_element_proto, "attachInternals", [this](context & c, std::span<value>) {
        const value self = c.current_this();
        dom_bindings * owner = owner_of(self);
        const node_id id = owner == nullptr ? node_id{} : owner->handle_of(self);
        if (!id) {
            c.throw_error("TypeError", "Failed to execute 'attachInternals' on 'HTMLElement': "
                                       "Illegal invocation");
            return value::undefined();
        }
        const auto refuse = [&](const char * why) {
            throw_dom_exception(c, "NotSupportedError",
                                std::string{"Failed to execute 'attachInternals' on "
                                            "'HTMLElement': "} +
                                    why);
            return value::undefined();
        };
        auto * wrapper = object_of(self);
        // 1: a customized built-in (an `is` value) has no internals.
        {
            const auto txn = owner->doc_->read();
            if (txn.has_attribute(id, atoms_->intern("is"))) {
                return refuse("the element is a customized built-in element");
            }
        }
        // 2-4: a definition for the element's local name, in its document's
        // registry, that did not disable internals.
        const auto state = owner->custom_elements_.find(id.key());
        if (state == owner->custom_elements_.end() ||
            state->second.state == custom_element_state::status::failed) {
            return refuse("the element is not a defined custom element");
        }
        const custom_element_definition & def =
            primary().custom_definitions_[state->second.definition];
        if (def.name != def.local_name) {
            return refuse("the element is a customized built-in element");
        }
        if (def.disable_internals) { return refuse("the definition disabled internals"); }
        // 5: once.
        if (wrapper == nullptr || slot(*wrapper, internals_key).is_object()) {
            return refuse("ElementInternals for the element was already attached");
        }
        // 6: only while or after the constructor runs.
        auto * internals = c.allocate<script::object_object>();
        internals->prototype = element_internals_prototype_;
        internals->set(std::string{target_key}, self);
        wrapper->set(std::string{internals_key}, value::object(internals));
        return value::object(internals);
    });
}

} // namespace ctbrowser::shell
