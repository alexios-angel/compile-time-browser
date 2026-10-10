//===- support.hpp - Value test helpers -------------------------*- C++ -*-===//
//
// Shared helpers for the property-table and value-grammar tests in
// css/values.cpp (the table and grammar that `el.style`, `getComputedStyle`
// and `CSS.supports` all consult). Cases come either from a specification
// rule or from a WPT css/css-values failure (`test_invalid_value` expects ""
// for an invalid value). Accepting cases matter as much as rejecting ones:
// vendored test pages and render goldens write through `el.style`, so
// refusing a value they use turns a render into a blank box.
//
//===----------------------------------------------------------------------===//

#pragma once

#include <ctbrowser.hpp>

#include "check.hpp"

#include <string>
#include <string_view>

using ctbrowser::style::css::check_declaration;
using ctbrowser::style::css::css_name_of;
using ctbrowser::style::css::find_property;
using ctbrowser::style::css::idl_name_of;
using ctbrowser::style::css::known_properties;
using ctbrowser::style::css::supports_condition;
using ctbrowser::style::css::supports_declaration;

namespace {

/// Expects `property: value` to be accepted and to serialize as `serialized`.
void ok(std::string_view property, std::string_view value, std::string_view serialized) {
    const auto answer = check_declaration(property, value);
    CHECK(answer.valid);
    CHECK_EQ(answer.serialized, std::string{serialized});
}

/// Expects `property: value` to be rejected and to read back as "".
void bad(std::string_view property, std::string_view value) {
    const auto answer = check_declaration(property, value);
    CHECK(!answer.valid);
    CHECK_EQ(answer.serialized, std::string{});
}

} // namespace
