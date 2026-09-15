#pragma once
#include <ctbrowser/core/number_format.hpp>

// Source-compatible Script names; Core owns the conversion implementation.
namespace ctbrowser::script {
using ctbrowser::number_to_exponential;
using ctbrowser::number_to_fixed;
using ctbrowser::number_to_precision;
using ctbrowser::number_to_string;
using ctbrowser::out_of_range_value;
using ctbrowser::string_to_number;
using ctbrowser::string_to_number_prefix;
} // namespace ctbrowser::script
