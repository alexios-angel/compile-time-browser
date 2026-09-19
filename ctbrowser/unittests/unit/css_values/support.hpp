#pragma once

// The property table and the value grammar - what `el.style`, `getComputedStyle`
// and `CSS.supports` all three ask.
//
// EVERY CASE HERE IS EITHER A SPECIFICATION RULE OR A CORPUS FAILURE. The corpus
// half comes from `css/css-values`, whose `test_invalid_value` is one assertion
// per file: set the property, read it back, and expect `""`. Before this table
// existed `el.style` recorded whatever it was given and handed it back
// unchanged, so `width: round()` read back as `round()` - measured as ~600
// failing subtests in `docs/css-conformance.md` §4.
//
// The half that is NOT about refusing things is the more important one. Three
// vendored corpora and every render golden write through `el.style`, so a
// grammar that refuses a value they use turns a render into a blank box. The
// cases below marked ACCEPTED-ON-PURPOSE are that guard: a shorthand, an
// unknown property, a `var()` and a comparison function must all survive.

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

void ok(std::string_view property, std::string_view value, std::string_view serialized) {
    const auto answer = check_declaration(property, value);
    CHECK(answer.valid);
    CHECK_EQ(answer.serialized, std::string{serialized});
}

void bad(std::string_view property, std::string_view value) {
    const auto answer = check_declaration(property, value);
    CHECK(!answer.valid);
    CHECK_EQ(answer.serialized, std::string{});
}

} // namespace
