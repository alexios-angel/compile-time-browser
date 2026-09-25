#!/usr/bin/env python3
"""Exercise emitted DOM key storage independently of the Data session provider."""

import argparse
import os
from pathlib import Path
import re
import shutil

from CTNative.Browser import native_dom as dom
from CTNative.harness import find_compilers, run
from Target.Cpp.harness import FLAGS

ROOT = Path(__file__).resolve().parents[5]
# The runtime as a DOM program includes it, with the identity struct a program
# would spell after the include.
PREAMBLE = r"""
#define CTNATIVE_DOM 1
#include "ctcompile/CTNative/Runtime/ctnative.hpp"
#include <array>
#include <cassert>
namespace ctnative { struct identity_object {}; }
"""
CLIENT = r"""
void check_payloads(ctbrowser::element_ref key, ctbrowser::element_ref missing) {
    using namespace ctnative;
    using K = ctbrowser::element_ref;
    auto numbers = make_number_map<K>();
    auto booleans = make_map<K, js_boolean_t>();
    auto strings = make_map<K, std::string>();
    auto optional = make_map<K, nullable_string>();
    auto scalars = make_map<K, nullable_scalar>();
    auto mixed = make_map<K, std::variant<js_boolean_t, std::string>>();
    auto nullable_mixed = make_map<K, std::variant<js_boolean_t, nullable_string>>();
    auto objects = make_map<K, object_value>();
    auto identity = std::make_shared<identity_object>();
    const std::string text = "an owning saved payload longer than the small String buffer";
    map_set(numbers, key, -0.0);
    map_set(booleans, key, js_boolean_t{false});
    map_set(strings, key, text);
    map_set(optional, key, nullable_string{text});
    map_set(mixed, key, std::variant<js_boolean_t, std::string>{text});
    map_set(nullable_mixed, key, std::variant<js_boolean_t, nullable_string>{nullable_string{text}});
    map_set(objects, key, object_value{identity});
    const auto check = [&](const auto & borrow) {
        const auto number = map_get(borrow(numbers), key);
        assert(number.tag == nullable_scalar::kind::number && std::signbit(number.value));
        assert(map_get(borrow(numbers), missing).tag == nullable_scalar::kind::undefined);
        const auto boolean = map_get(borrow(booleans), key);
        assert(boolean.tag == nullable_scalar::kind::boolean && boolean.value == 0);
        assert(map_get(borrow(booleans), missing).tag == nullable_scalar::kind::undefined);
        assert(global_string(map_get(borrow(strings), key)) == text);
        assert(map_get(borrow(strings), missing).tag == nullable_string::kind::undefined);
        assert(global_string(map_get(borrow(optional), key)) == text);
        assert(map_get(borrow(optional), missing).tag == nullable_string::kind::undefined);
        assert(map_get_present_as<std::string>(borrow(mixed), key) == text);
        assert(map_get_present_nullable_as<std::string>(borrow(optional), key) == text);
        assert(map_get_present_nullable_as<std::string>(borrow(nullable_mixed), key) == text);
        assert(map_get(borrow(objects), key).object == identity);
        assert(map_get(borrow(objects), missing).scalar.tag == nullable_scalar::kind::undefined);
        assert(map_get_present_identity(borrow(objects), key) == identity);
#ifdef CTCOMPILE_TEST_ORDERED
        const auto values = map_values(borrow(numbers));
        assert(values.size() == 1 && std::signbit(values[0]));
        assert(map_values(borrow(strings)) == std::vector<std::string>{text});
#endif
    };
    check([](const auto & owner) { return owner; });
    check([](const auto & owner) { return owner.get(); });
    // Payloads retain their tags and sign; only keys normalize negative zero.
    for (auto value : {nullable_scalar{}, nullable_scalar::null(), nullable_scalar{-0.0},
                       nullable_scalar{std::numeric_limits<double>::quiet_NaN()},
                       nullable_scalar{js_boolean_t{false}}, nullable_scalar{js_boolean_t{true}}}) {
        map_set(scalars, key, value);
        assert(map_has(scalars, key) && !map_has(scalars, missing));
        const auto saved_scalar = map_get(scalars, key);
        const auto raw = map_get(scalars.get(), key);
        assert(saved_scalar.tag == value.tag && raw.tag == value.tag);
        assert(map_get(scalars, missing).tag == nullable_scalar::kind::undefined);
        assert(map_get_present_nullable_as<nullable_scalar>(scalars, key).tag == value.tag);
        if (value.tag == nullable_scalar::kind::number) {
            const auto number = map_get_present_nullable_as<double>(scalars.get(), key);
            assert(std::isnan(value.value) ? std::isnan(number) : std::signbit(number));
        } else if (value.tag == nullable_scalar::kind::boolean) {
            assert(map_get_present_nullable_as<js_boolean_t>(scalars, key) ==
                   js_boolean_t{value.value != 0});
        }
        map_delete(scalars, key);
        assert(!map_has(scalars, key));
        assert(map_get(scalars.get(), key).tag == nullable_scalar::kind::undefined);
        assert(saved_scalar.tag == value.tag);
    }
    const auto saved = map_get(strings.get(), key);
    const auto saved_object = map_get_present_identity(objects.get(), key);
#ifdef CTCOMPILE_TEST_ORDERED
    const auto saved_values = map_values(strings.get());
#endif
    map_clear(strings);
    map_clear(objects);
    strings.reset();
    objects.reset();
    assert(global_string(saved) == text && saved_object == identity);
#ifdef CTCOMPILE_TEST_ORDERED
    assert(saved_values == std::vector<std::string>{text});
#endif
}
int main() {
    using ctbrowser::element_ref;
    // Deliberately unconstructed documents: key lookup must never resolve an owner.
    auto first = std::make_unique<std::byte[]>(sizeof(ctbrowser::document));
    auto second = std::make_unique<std::byte[]>(sizeof(ctbrowser::document));
    auto * owner = reinterpret_cast<ctbrowser::document *>(first.get());
    auto * foreign = reinterpret_cast<ctbrowser::document *>(second.get());
    const std::array<element_ref, 6> keys{{
        {owner, {1, 1}}, {owner, {1, 2}}, {owner, {2, 1}},
        {foreign, {1, 1}}, {nullptr, {1, 1}}, {}
    }};
    check_payloads(keys[0], keys[3]);
    ctnative::map_storage<element_ref, std::size_t> map;
    for (std::size_t i = 0; i < keys.size(); ++i) { map.insert_or_assign(keys[i], i); }
    assert(map.size() == keys.size());
    const element_ref alias = keys[0];
    map.insert_or_assign(alias, keys.size());
    assert(map.size() == keys.size());
    for (int lifetime = 0; lifetime < 2; ++lifetime) {
        for (std::size_t i = 0; i < keys.size(); ++i) {
            const auto found = map.find(keys[i]);
            assert(found != map.end() && found->first == keys[i]);
            assert(found->second == (i == 0 ? keys.size() : i));
        }
        assert(map.erase(alias) == 1);
        assert(map.find(keys[0]) == map.end());
        assert(map.find(keys[1]) != map.end() && map.find(keys[3]) != map.end());
        map.insert_or_assign(keys[0], keys.size());
        // Existing keys survive without dereferencing their now-dangling owners.
        first.reset();
        second.reset();
    }
    map.clear();
    assert(map.size() == 0);
}
"""


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--opt", required=True)
    parser.add_argument("--work", type=Path, required=True)
    args = parser.parse_args()
    work = args.work.resolve()
    work.mkdir(parents=True, exist_ok=True)
    binary = Path(shutil.which(args.opt) or args.opt).resolve()
    args.build = next(path for path in binary.parents if (path / "CMakeCache.txt").is_file())
    args.include = ROOT / "ctbrowser/include"
    includes, _ = dom.link_options(args)
    configured = re.search(
        r"^CMAKE_CXX_COMPILER:[^=\n]+=(.*)$", (args.build / "CMakeCache.txt").read_text(), re.M
    ).group(1)
    # Match Native.cmake: the older scalar-test Clang lacks DOM std::expected.
    clang = Path(configured)
    if "clang" not in clang.name:
        clang = ROOT / "tools/clang-std-embed/bin/clang++"
    compilers = (find_compilers()[0], str(clang))
    for layout in ("associative", "ordered"):
        source = work / f"{layout}.cpp"
        ordered = layout == "ordered"
        source.write_text(
            ("#define CTNATIVE_ORDERED_MAPS 1\n#define CTCOMPILE_TEST_ORDERED\n" if ordered else "")
            + PREAMBLE
            + CLIENT
        )
        for index, compiler in enumerate(compilers):
            binary = work / f"{layout}-{index}"
            flags = [] if index == 0 else ["-O1", "-g", "-fsanitize=address,undefined"]
            run([compiler, *FLAGS, *flags, *includes, str(source), "-o", str(binary)])
            run(
                [str(binary)],
                environment=dict(
                    os.environ,
                    ASAN_OPTIONS="detect_leaks=1",
                    UBSAN_OPTIONS="halt_on_error=1",
                ),
            )
    print(
        "DOM Map keys: both layouts preserve owner/slot/generation identity; "
        "raw/owned scalar/String/object reads and snapshots; GCC/Clang ASan/UBSan"
    )


if __name__ == "__main__":
    main()
