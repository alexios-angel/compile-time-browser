//===- mutation_types.hpp - Mutation records --------------------*- C++ -*-===//
//
// Plain data records dom_bindings keeps for MutationObserver: observe()
// options, registered observers and node snapshots. The records chain-include
// each other and are all reached through bindings.hpp.
//
//===----------------------------------------------------------------------===//
#pragma once

#include "lifecycle_types.hpp"

namespace ctbrowser::shell {

class dom_bindings;

namespace binding_detail {

using css_declaration = style::css::declaration;

/// One `observe()` call's options, after the dictionary's own defaulting.
/// `attributeOldValue` or `attributeFilter` present with `attributes` absent
/// turns `attributes` on, which is why `attributes` is a field here and not
/// just a read of the dictionary.
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

/// A registered observer (DOM §4.3.1): one (observer, target) pair with its
/// options. `observe()` on a target already registered for this observer
/// replaces the options rather than adding a second entry.
struct mutation_registration {
    std::size_t observer = 0; // Index into mutation_observers_.
    node_id target;
    /// Set for `observe(document, ...)`. This tree builder has no Document
    /// node above `<html>`, so the registration is on the root element and
    /// remembers that it stands for the document.
    bool whole_document = false;
    mutation_options options;
};

/// One observed node as it was last seen; mutation records are computed as a
/// difference from it. All three fields are captured for every observed node
/// regardless of what the registration asked for: a node may be observed by
/// two registrations that want different things, and one snapshot that
/// answers both is cheaper than keeping the union of their options per node.
struct mutation_node_state {
    std::vector<node_id> children;
    std::vector<attribute> attributes;
    std::string text; // CharacterData nodes only.
};

} // namespace binding_detail

} // namespace ctbrowser::shell
