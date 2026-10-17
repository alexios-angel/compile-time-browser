//===- ctcompile/test/Runtime/NativeHandlers.cpp - Tables and identities --===//
//
// Tests ctnative::dictionary (CTNative/Runtime/Dictionary.hpp) and
// ctnative::callable_identity (CTNative/Runtime/Handlers.hpp), the runtime
// half of an object used as a string-keyed table and of `===` on function
// values. The cases pin the property order JavaScript enumerates (array
// indices ascending, then the other keys in creation order), that an
// overwrite keeps a key's position while delete and re-add moves it to the
// end, which keys count as array indices, that a dictionary held twice is one
// table, and that copies of a function value keep its identity while a
// second evaluation of the same expression does not.
//
//===----------------------------------------------------------------------===//

#include "ctcompile/CTNative/Runtime/Dictionary.hpp"
#include "ctcompile/CTNative/Runtime/Handlers.hpp"

#include <string>
#include <variant>
#include <vector>

#include "check.hpp"

namespace {

using ctnative::Object;

std::vector<std::string> keys_of(const std::shared_ptr<ctnative::dictionary<double>> & o) {
    std::vector<std::string> out;
    for (const ctnative::js_string & key : Object.keys(o)) { out.emplace_back(key.value()); }
    return out;
}

void test_indices_come_first_and_ascending() {
    // `o = {}; o.b = 1; o[10] = 2; o.a = 3; o[2] = 4; Object.keys(o)` is
    // ["2", "10", "b", "a"].
    const auto o = ctnative::make_dictionary<double>();
    o->set("b", 1);
    o->set("10", 2);
    o->set("a", 3);
    o->set("2", 4);
    CHECK((keys_of(o) == std::vector<std::string>{"2", "10", "b", "a"}));
    const std::vector<double> values = Object.values(o);
    CHECK((values == std::vector<double>{4, 2, 1, 3}));
    const auto entries = Object.entries(o);
    CHECK(entries.size() == 4);
    CHECK(entries[1].first.value() == "10" && entries[1].second == 2);
}

void test_only_canonical_indices_are_indices() {
    CHECK(ctnative::array_index_of("0") == 0u);
    CHECK(ctnative::array_index_of("4294967294") == 4294967294u);
    CHECK(!ctnative::array_index_of("4294967295"));
    CHECK(!ctnative::array_index_of("01"));
    CHECK(!ctnative::array_index_of("-0"));
    CHECK(!ctnative::array_index_of("1e3"));
    CHECK(!ctnative::array_index_of(""));
    CHECK(!ctnative::array_index_of("1.5"));
    // EventHandler's handler keys: a namespaced uid is a string key.
    CHECK(!ctnative::array_index_of("click.bs.modal::3"));
    const auto o = ctnative::make_dictionary<double>();
    o->set("01", 1);
    o->set("1", 2);
    CHECK((keys_of(o) == std::vector<std::string>{"1", "01"}));
}

void test_overwrite_keeps_position_and_delete_moves_it() {
    const auto o = ctnative::make_dictionary<double>();
    o->set("x", 1);
    o->set("y", 2);
    o->set("x", 3);
    CHECK((keys_of(o) == std::vector<std::string>{"x", "y"}));
    CHECK(*o->get("x") == 3);
    CHECK(o->remove("x"));
    CHECK(o->remove("absent"));
    CHECK(!o->has("x") && o->get("x") == nullptr);
    o->set("x", 4);
    CHECK((keys_of(o) == std::vector<std::string>{"y", "x"}));
    CHECK(o->size() == 2);
}

void test_a_nested_table_is_shared() {
    // `E[e] = E[e] || {}; const l = E[e]; l[a] = ...` writes into E's table.
    using inner = ctnative::dictionary<double>;
    const auto registry = ctnative::make_dictionary<std::shared_ptr<inner>>();
    auto * slot = registry->get("1");
    if (slot == nullptr) { slot = &registry->set("1", ctnative::make_dictionary<double>()); }
    const std::shared_ptr<inner> held = *slot;
    held->set("click", 7);
    CHECK(*(*registry->get("1"))->get("click") == 7);
}

void test_generated_table_operations() {
    using ctnative::js_num;
    using ctnative::js_string;
    const auto o = ctnative::make_dictionary<js_num>();
    // ToPropertyKey: the Number 7 and the String "7" are one key.
    ctnative::dictionary_set(o, js_num{7.0}, js_num{1.0});
    ctnative::dictionary_set(o, js_string{"7"}, js_num{2.0});
    CHECK(o->size() == 1);
    const ctnative::nullable_scalar seven = ctnative::dictionary_get(o, js_num{7.0});
    CHECK(seven.tag == ctnative::nullable_scalar::kind::number && seven.value == 2.0);
    CHECK(ctnative::property_key(js_num{-0.0}) == "0");
    CHECK(ctnative::property_key(js_num{1.5}) == "1.5");
    // A missing key reads undefined.
    CHECK(ctnative::dictionary_get(o, js_string{"x"}).tag ==
          ctnative::nullable_scalar::kind::undefined);
    // A read under `toString` the table lacks stops the program (it would be
    // a function); the compiler refuses reads it cannot prove are not one.
    // `in` sees Object.prototype's keys; `__proto__ = primitive` makes nothing.
    CHECK(static_cast<bool>(ctnative::dictionary_has(o, js_string{"constructor"})));
    CHECK(!static_cast<bool>(ctnative::dictionary_has(o, js_string{"x"})));
    ctnative::dictionary_set(o, js_string{"__proto__"}, js_num{3.0});
    CHECK(o->size() == 1);
    CHECK(static_cast<bool>(ctnative::dictionary_delete(o, js_num{7.0})));
    CHECK(static_cast<bool>(ctnative::dictionary_delete(o, js_num{7.0})));
    CHECK(o->size() == 0);
    // A Number|String table reads either back.
    const auto mixed = ctnative::make_dictionary<ctnative::number_string>();
    ctnative::dictionary_set(mixed, js_string{"w"}, js_string{"10px"});
    const ctnative::nullable_number_string w = ctnative::dictionary_get(mixed, js_string{"w"});
    CHECK(std::holds_alternative<js_string>(w) && std::get<js_string>(w).value() == "10px");
}

void test_copies_of_a_function_are_the_same_function() {
    const auto make = [] { return ctnative::identify([](double v) { return v + 1; }); };
    const auto first = make();
    const auto copy = first;
    const auto second = make();
    CHECK(ctnative::same_function(first, copy));
    CHECK(!ctnative::same_function(first, second));
    CHECK(copy(1) == 2);
    CHECK(first.identity.value != 0);
}

} // namespace

int main() {
    test_indices_come_first_and_ascending();
    test_only_canonical_indices_are_indices();
    test_overwrite_keeps_position_and_delete_moves_it();
    test_a_nested_table_is_shared();
    test_generated_table_operations();
    test_copies_of_a_function_are_the_same_function();
    REPORT("native_handlers");
}
