#pragma once

#include "lifecycle_types.hpp"

namespace ctbrowser::shell {

class dom_bindings;

namespace binding_detail {

using css_declaration = style::css::declaration;

// ONE `observe()` CALL'S OPTIONS, after the dictionary's own defaulting.
// `attributeOldValue` or `attributeFilter` PRESENT with `attributes`
// ABSENT turns `attributes` on - which is why `attributes` is a field here
// and not just a read of the dictionary.
struct mutation_options {
    bool child_list = false;
    bool attributes = false;
    bool character_data = false;
    bool attribute_old_value = false;
    bool character_data_old_value = false;
    bool subtree = false;
    bool has_attribute_filter = false;
    std::vector<std::string> attribute_filter;
};

// A REGISTERED OBSERVER, DOM §4.3.1: one (observer, target) pair with its
// options. `observe()` on a target already registered for this observer
// REPLACES the options rather than adding a second entry.
struct mutation_registration {
    std::size_t observer = 0; // index into mutation_observers_
    node_id target;
    // `observe(document, ...)`: the document object carries no node handle
    // here - this tree builder has no Document node above `<html>` - so the
    // registration is on the root element and remembers that it stands for
    // the document.
    bool whole_document = false;
    mutation_options options;
};

// ONE OBSERVED NODE AS IT WAS, which is what a record is a difference from.
// All three are captured for every observed node regardless of which the
// registration asked for: a node may be observed by two registrations that
// want different things, and one snapshot that answers both is cheaper than
// keeping the union of their options per node.
struct mutation_node_state {
    std::vector<node_id> children;
    std::vector<attribute> attributes;
    std::string text; // CharacterData nodes only
};

} // namespace binding_detail

} // namespace ctbrowser::shell
