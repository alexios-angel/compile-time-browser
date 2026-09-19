#pragma once

#include "animations_types.hpp"

namespace ctbrowser::shell {

class dom_bindings;

namespace binding_detail {

using css_declaration = style::css::declaration;

// ONE CustomElementRegistry: the page's, a frame document's, or a scoped
// one a page made with `new CustomElementRegistry()` (HTML 4.13.3) -
// which belongs to no document, so nothing is looked up in it and only
// `new C()` reaches its definitions. Owned by the primary, so a pointer
// is stable for the life of the page.
struct custom_element_registry {
    dom_bindings * document = nullptr; // whose global registry, or null
    value object;                      // the JS CustomElementRegistry
    flat_map<std::string, value> when_defined;
    bool running = false; // HTML 4.13.4's "element definition is running"
};

struct custom_element_definition {
    std::string name;
    std::string local_name; // the `extends` name, or `name` itself
    // THE REGISTRY IT WAS DEFINED IN. Every definition of the realm lives
    // in the primary's vector, so an index means the same thing everywhere.
    custom_element_registry * registry = nullptr;
    value constructor;
    value prototype;
    // The lifecycle callbacks, captured at define time as the
    // specification says - a prototype edited afterwards changes nothing.
    value connected;
    value disconnected;
    value adopted;
    value attribute_changed;
    value connected_move;
    value form_associated_callback;
    value form_reset;
    value form_disabled;
    value form_state_restore;
    std::vector<std::string> observed_attributes;
    bool form_associated = false;
    bool disable_internals = false;
    bool disable_shadow = false;
    // HTML 4.13.4's CONSTRUCTION STACK: the element an upgrade is running
    // the constructor for, and whether `super()` has reached the HTML
    // element constructor for it yet (the "already constructed marker").
    struct construction {
        node_id element;
        bool constructed = false;
    };
    std::vector<construction> construction_stack;
};

// ONE UPGRADED OR CONSTRUCTED ELEMENT AS IT WAS, which is what a reaction
// is a difference from - the same shape record_mutations diffs against.
struct custom_element_state {
    // HTML 4.13.1's custom element state, the three that matter here: an
    // upgrade that threw is `failed` and is never tried again, one whose
    // constructor is running is `precustomized`, and `custom` is an
    // element whose callbacks fire.
    enum class status : std::uint8_t {
        failed,
        precustomized,
        custom
    };
    std::size_t definition = 0;
    status state = status::custom;
    bool connected = false;
    std::uint32_t seen = 0; // the scan generation that last walked it
    // A FORM-ASSOCIATED element's form owner and disabledness as they
    // were, which formAssociatedCallback and formDisabledCallback are a
    // difference from (HTML 4.13.7.2).
    bool disabled = false;
    node_id parent;
    node_id form;
    std::vector<attribute> attributes; // observed only
};

struct custom_element_reaction {
    enum class kind : std::uint8_t {
        upgrade,
        connected,
        disconnected,
        adopted,
        connected_move,
        attribute_changed,
        form_associated,
        form_disabled
    };
    node_id target;
    std::size_t definition = 0;
    kind what = kind::upgrade;
    node_id form;      // formAssociatedCallback's form, or none
    bool flag = false; // formDisabledCallback's disabled
    // Strings rather than `value`s: a reaction waits in this queue while
    // the ones before it run script, and nothing would root a heap string.
    std::string name; // the attribute's LOCAL name
    std::string ns;   // its namespace, "" for none
    std::string old_value;
    std::string new_value;
    bool has_old = false;
    bool has_new = false;
    // adoptedCallback's two documents: the `document` values of the two
    // bindings, which are roots already.
    value old_document;
    value new_document;
};

} // namespace binding_detail

} // namespace ctbrowser::shell
