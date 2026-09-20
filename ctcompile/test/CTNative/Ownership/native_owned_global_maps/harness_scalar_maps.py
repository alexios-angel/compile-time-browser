"""harness scalar maps: continued from harness_map_results."""

from .harness_map_results import *


def string_payload_lifetime(args, cpp, name, mode, compiler):
    joined = name in {
        "saved_join_string_saved",
        "guarded_saved_string_saved",
        "shortcircuit_string_saved",
    }
    initial_size = 2 if joined else 1
    changed, count = re.subn(r"\bmain\(\)", "ctnative_test_entry()", inline_runtime(cpp))
    if count != 1:
        raise RuntimeError("string lifetime harness needs exactly one entry")
    changed = (
        "#include <memory>\n#include <type_traits>\n#include <vector>\n"
        "static std::vector<std::weak_ptr<const void>> ctn_test_maps;\n" + changed
    )
    changed, count = re.subn(
        r"return std::make_shared<(map_storage<K, V>|number_map<K>)>\(\);",
        lambda match: "auto made = std::make_shared<" + match[1] + ">(); "
        "ctn_test_maps.emplace_back(made); return made;",
        changed,
    )
    if count != 2:
        raise RuntimeError("string Map lifetime observer lost its allocation helpers")
    changed += r"""
int main() {
    const std::string expected = EXPECTED_STRING;
    const std::string other_expected = OTHER_EXPECTED_STRING;
    if (ctnative_test_entry() != 0 || ctn_test_maps.size() != 1) { return 90; }
    auto owner = g_host;
    auto table = owner->slot;
    auto getter = table->m_get;
    auto setter = table->m_set;
    auto size = table->m_size;
    static_assert(std::is_same_v<decltype(getter), std::function<std::string(GETTER_PARAMETERS)>>);
    static_assert(std::is_same_v<decltype(setter), std::function<ctnative::js_num(std::string)>>);
    auto read = [&]([[maybe_unused]] bool other) { return getter(GETTER_ARGUMENT); };
    auto saved = read(false);
    auto saved_other = read(true);
    if (saved != expected || saved_other != other_expected || setter(saved).value() != INITIAL_SIZE ||
        setter(saved_other).value() != INITIAL_SIZE) { return 91; }
    saved.assign(expected.size(), 'x');
    saved_other.assign(other_expected.size(), 'x');
    if (setter(expected).value() != INITIAL_SIZE || setter(other_expected).value() != INITIAL_SIZE) { return 92; }
    std::weak_ptr owner_lifetime = owner;
    std::weak_ptr table_lifetime = table;
    g_host.reset();
    owner.reset();
    table.reset();
    if (!owner_lifetime.expired() || !table_lifetime.expired() ||
        ctn_test_maps[0].expired()) { return 93; }
    if (ctnative_test_entry() != 0 || ctn_test_maps.size() != 2 ||
        ctn_test_maps[0].lock() == ctn_test_maps[1].lock()) { return 94; }
    for (int index = 0; index < 128; ++index) {
        if (read(false) != expected || read(true) != other_expected ||
            setter("saved-" + std::to_string(index)).value() != index + INITIAL_SIZE + 1 ||
            size().value() != index + INITIAL_SIZE + 1 || g_host->slot->m_size().value() != INITIAL_SIZE) {
            return 95;
        }
    }
    auto survivor = read(false);
    auto other_survivor = read(true);
    getter = {};
    setter = {};
    if (ctn_test_maps[0].expired()) { return 96; }
    size = {};
    if (!ctn_test_maps[0].expired() || ctn_test_maps[1].expired()) { return 97; }
    std::vector<std::string> churn;
    for (int index = 0; index < 4096; ++index) {
        churn.emplace_back(expected.size(), 'q');
    }
    if (survivor != expected || other_survivor != other_expected ||
        g_host->slot->m_get(FRESH_FIRST_ARGUMENT) != expected ||
        g_host->slot->m_get(FRESH_OTHER_ARGUMENT) != other_expected) { return 98; }
    g_host.reset();
    if (!ctn_test_maps[1].expired() || survivor != expected || other_survivor != other_expected) {
        return 99;
    }
    return 0;
}
"""
    changed = changed.replace(
        "OTHER_EXPECTED_STRING", json.dumps(OTHER_STRING_RESULT if joined else STRING_RESULT)
    )
    changed = changed.replace("EXPECTED_STRING", json.dumps(STRING_RESULT))
    changed = changed.replace("GETTER_PARAMETERS", "ctnative::js_boolean_t" if joined else "")
    changed = changed.replace("GETTER_ARGUMENT", "ctnative::js_boolean_t{other}" if joined else "")
    changed = changed.replace(
        "FRESH_FIRST_ARGUMENT", "ctnative::js_boolean_t{false}" if joined else ""
    )
    changed = changed.replace(
        "FRESH_OTHER_ARGUMENT", "ctnative::js_boolean_t{true}" if joined else ""
    )
    changed = changed.replace("INITIAL_SIZE", str(initial_size))
    source = args.work / f"{name}.{mode}.lifetime.cpp"
    source.write_text(changed)
    binary = (args.work / f"{name}.{mode}.sanitized").resolve()
    host.run(
        [
            compiler,
            *owned.FLAGS,
            "-O1",
            "-g",
            "-fno-omit-frame-pointer",
            "-fsanitize=address,undefined",
            "-fsanitize-address-use-after-scope",
            str(source),
            "-o",
            str(binary),
        ]
    )
    result = subprocess.run(
        [str(binary)],
        capture_output=True,
        text=True,
        timeout=60,
        env=dict(
            os.environ,
            ASAN_OPTIONS="detect_stack_use_after_return=1:detect_leaks=1",
            UBSAN_OPTIONS="halt_on_error=1:print_stacktrace=1",
        ),
    )
    if result.returncode or result.stdout != f"trace={initial_size}\n" * 2:
        raise RuntimeError(
            f"{name}/{mode}: saved string lifetime failure\n" f"{result.stdout}{result.stderr}"
        )


def nullable_payload_lifetime(args, cpp, name, mode, compiler):
    changed, count = re.subn(r"\bmain\(\)", "ctnative_test_entry()", inline_runtime(cpp))
    if count != 1:
        raise RuntimeError("nullable lifetime harness needs exactly one entry")
    changed = (
        "#include <memory>\n#include <type_traits>\n#include <vector>\n"
        "static std::vector<std::weak_ptr<const void>> ctn_test_maps;\n" + changed
    )
    changed, count = re.subn(
        r"return std::make_shared<(map_storage<K, V>|number_map<K>)>\(\);",
        lambda match: "auto made = std::make_shared<" + match[1] + ">(); "
        "ctn_test_maps.emplace_back(made); return made;",
        changed,
    )
    if count != 2:
        raise RuntimeError("nullable lifetime observer lost its allocation helpers")
    changed += r"""
int main() {
    using result_type = ctnative::nullable_string;
    using kind = result_type::kind;
    const std::string expected = EXPECTED_STRING;
    if (ctnative_test_entry() != 0 || ctn_test_maps.size() != 1) { return 90; }
    auto owner = g_host;
    auto table = owner->slot;
    auto getter = table->m_get;
    auto setter = table->m_set;
    auto size = table->m_size;
    static_assert(std::is_same_v<decltype(getter), std::function<result_type(ctnative::js_boolean_t)>>);
    static_assert(std::is_same_v<decltype(setter), std::function<ctnative::js_num(result_type)>>);
    auto saved = getter(ctnative::js_boolean_t{false});
    auto saved_null = getter(ctnative::js_boolean_t{true});
    if (saved.tag != kind::string || saved.value != expected || saved_null.tag != kind::null_value ||
        !saved_null.value.empty() || setter(saved).value() != 2 || setter(saved_null).value() != 2) { return 91; }
    saved.value.assign(expected.size(), 'x');
    if (setter(result_type{expected}).value() != 2 || setter(result_type{std::string{}}).value() != 2) { return 92; }
    std::weak_ptr owner_lifetime = owner;
    std::weak_ptr table_lifetime = table;
    g_host.reset();
    owner.reset();
    table.reset();
    if (!owner_lifetime.expired() || !table_lifetime.expired() ||
        ctn_test_maps[0].expired()) { return 93; }
    if (ctnative_test_entry() != 0 || ctn_test_maps.size() != 2 ||
        ctn_test_maps[0].lock() == ctn_test_maps[1].lock()) { return 94; }
    for (int index = 0; index < 128; ++index) {
        const auto text = getter(ctnative::js_boolean_t{false});
        const auto absent = getter(ctnative::js_boolean_t{true});
        if (text.tag != kind::string || text.value != expected || absent.tag != kind::null_value ||
            !absent.value.empty() || setter(result_type{"saved-" + std::to_string(index)}).value() != index + 3 ||
            size().value() != index + 3 || g_host->slot->m_size().value() != 2) { return 95; }
    }
    const auto survivor = getter(ctnative::js_boolean_t{false});
    const auto null_survivor = getter(ctnative::js_boolean_t{true});
    getter = {};
    setter = {};
    if (ctn_test_maps[0].expired()) { return 96; }
    size = {};
    if (!ctn_test_maps[0].expired() || ctn_test_maps[1].expired()) { return 97; }
    std::vector<std::string> churn;
    for (int index = 0; index < 4096; ++index) { churn.emplace_back(expected.size(), 'q'); }
    const auto fresh = g_host->slot->m_get(ctnative::js_boolean_t{false});
    const auto fresh_null = g_host->slot->m_get(ctnative::js_boolean_t{true});
    if (survivor.tag != kind::string || survivor.value != expected ||
        null_survivor.tag != kind::null_value || !null_survivor.value.empty() ||
        fresh.tag != kind::string || fresh.value != expected || fresh_null.tag != kind::null_value) {
        return 98;
    }
    g_host.reset();
    if (!ctn_test_maps[1].expired() || survivor.value != expected ||
        null_survivor.tag != kind::null_value) { return 99; }
    return 0;
}
"""
    changed = changed.replace("EXPECTED_STRING", json.dumps(STRING_RESULT))
    source = args.work / f"{name}.{mode}.lifetime.cpp"
    source.write_text(changed)
    binary = (args.work / f"{name}.{mode}.sanitized").resolve()
    host.run(
        [
            compiler,
            *owned.FLAGS,
            "-O1",
            "-g",
            "-fno-omit-frame-pointer",
            "-fsanitize=address,undefined",
            "-fsanitize-address-use-after-scope",
            str(source),
            "-o",
            str(binary),
        ]
    )
    result = subprocess.run(
        [str(binary)],
        capture_output=True,
        text=True,
        timeout=60,
        env=dict(
            os.environ,
            ASAN_OPTIONS="detect_stack_use_after_return=1:detect_leaks=1",
            UBSAN_OPTIONS="halt_on_error=1:print_stacktrace=1",
        ),
    )
    if result.returncode or result.stdout != "trace=2\n" * 2:
        raise RuntimeError(
            f"{name}/{mode}: nullable result lifetime failure\n" f"{result.stdout}{result.stderr}"
        )


def nullable_key_lifetime(args, cpp, name, mode, compiler):
    changed, count = re.subn(r"\bmain\(\)", "ctnative_test_entry()", inline_runtime(cpp))
    if count != 1:
        raise RuntimeError("nullable key lifetime harness needs exactly one entry")
    changed = (
        "#include <memory>\n#include <type_traits>\n#include <vector>\n"
        "static std::vector<std::weak_ptr<const void>> ctn_test_maps;\n" + changed
    )
    changed, count = re.subn(
        r"return std::make_shared<(map_storage<K, V>|number_map<K>)>\(\);",
        lambda match: "auto made = std::make_shared<" + match[1] + ">(); "
        "ctn_test_maps.emplace_back(made); return made;",
        changed,
    )
    if count != 2:
        raise RuntimeError("nullable key lifetime observer lost its allocation helpers")
    changed += r"""
int main() {
    using key_type = ctnative::nullable_string;
    using kind = key_type::kind;
    const std::string expected = EXPECTED_STRING;
    if (ctnative_test_entry() != 0 || ctn_test_maps.size() != 1) { return 90; }
    auto owner = g_host;
    auto table = owner->slot;
    auto getter = table->m_get;
    auto setter = table->m_set;
    auto size = table->m_size;
    static_assert(std::is_same_v<decltype(getter), std::function<key_type(ctnative::js_boolean_t)>>);
    static_assert(std::is_same_v<decltype(setter), std::function<ctnative::js_num(key_type)>>);
    auto saved = getter(ctnative::js_boolean_t{false});
    const auto absent = getter(ctnative::js_boolean_t{true});
    if (saved.tag != kind::string || saved.value != expected || absent.tag != kind::null_value ||
        !absent.value.empty() || setter(saved).value() != 2 || setter(absent).value() != 3) { return 91; }
    saved.value.assign(expected.size(), 'x');
    if (setter(key_type{expected}).value() != 3 || setter(key_type{std::string{}}).value() != 4 ||
        setter(key_type{}).value() != 4 || setter(absent).value() != 4 || size().value() != 4) { return 92; }
    std::weak_ptr owner_lifetime = owner;
    std::weak_ptr table_lifetime = table;
    g_host.reset();
    owner.reset();
    table.reset();
    if (!owner_lifetime.expired() || !table_lifetime.expired() ||
        ctn_test_maps[0].expired()) { return 93; }
    if (ctnative_test_entry() != 0 || ctn_test_maps.size() != 2 ||
        ctn_test_maps[0].lock() == ctn_test_maps[1].lock()) { return 94; }
    for (int index = 0; index < 128; ++index) {
        const std::string text = expected + std::to_string(index);
        auto caller = key_type{text};
        if (setter(caller).value() != index + 5) { return 95; }
        caller.value.assign(text.size(), 'q');
        if (setter(key_type{text}).value() != index + 5 || setter(absent).value() != index + 5 ||
            setter(key_type{}).value() != index + 5 || setter(key_type{std::string{}}).value() != index + 5 ||
            getter(ctnative::js_boolean_t{false}).value != expected || getter(ctnative::js_boolean_t{true}).tag != kind::null_value ||
            size().value() != index + 5 || g_host->slot->m_size().value() != 2) { return 96; }
    }
    const auto survivor = getter(ctnative::js_boolean_t{false});
    const auto null_survivor = getter(ctnative::js_boolean_t{true});
    getter = {};
    setter = {};
    if (ctn_test_maps[0].expired()) { return 97; }
    size = {};
    if (!ctn_test_maps[0].expired() || ctn_test_maps[1].expired()) { return 98; }
    std::vector<std::string> churn;
    for (int index = 0; index < 4096; ++index) { churn.emplace_back(expected.size(), 'z'); }
    const auto fresh = g_host->slot->m_get(ctnative::js_boolean_t{false});
    const auto fresh_null = g_host->slot->m_get(ctnative::js_boolean_t{true});
    g_host.reset();
    if (!ctn_test_maps[1].expired() || survivor.tag != kind::string || survivor.value != expected ||
        null_survivor.tag != kind::null_value || !null_survivor.value.empty() ||
        fresh.tag != kind::string || fresh.value != expected || fresh_null.tag != kind::null_value) {
        return 99;
    }
    return 0;
}
"""
    changed = changed.replace("EXPECTED_STRING", json.dumps(STRING_RESULT))
    source = args.work / f"{name}.{mode}.lifetime.cpp"
    source.write_text(changed)
    binary = (args.work / f"{name}.{mode}.sanitized").resolve()
    host.run(
        [
            compiler,
            *owned.FLAGS,
            "-O1",
            "-g",
            "-fno-omit-frame-pointer",
            "-fsanitize=address,undefined",
            "-fsanitize-address-use-after-scope",
            str(source),
            "-o",
            str(binary),
        ]
    )
    result = subprocess.run(
        [str(binary)],
        capture_output=True,
        text=True,
        timeout=60,
        env=dict(
            os.environ,
            ASAN_OPTIONS="detect_stack_use_after_return=1:detect_leaks=1",
            UBSAN_OPTIONS="halt_on_error=1:print_stacktrace=1",
        ),
    )
    if result.returncode or result.stdout != "trace=2\n" * 2:
        raise RuntimeError(
            f"{name}/{mode}: nullable key lifetime failure\n" f"{result.stdout}{result.stderr}"
        )


def nullable_stored_payload_lifetime(args, cpp, name, mode, compiler):
    changed, count = re.subn(r"\bmain\(\)", "ctnative_test_entry()", inline_runtime(cpp))
    if count != 1:
        raise RuntimeError("nullable stored-payload harness needs exactly one entry")
    changed = (
        "#include <memory>\n#include <type_traits>\n#include <vector>\n"
        "static std::vector<std::weak_ptr<const void>> ctn_test_maps;\n" + changed
    )
    changed, count = re.subn(
        r"return std::make_shared<(map_storage<K, V>|number_map<K>)>\(\);",
        lambda match: "auto made = std::make_shared<" + match[1] + ">(); "
        "ctn_test_maps.emplace_back(made); return made;",
        changed,
    )
    if count != 2:
        raise RuntimeError("nullable stored-payload observer lost its allocation helpers")
    changed += r"""
int main() {
    using result_type = ctnative::nullable_string;
    using kind = result_type::kind;
    const std::string expected = EXPECTED_STRING;
    const auto equal = [](const result_type & value, kind tag, const std::string & text = {}) {
        return value.tag == tag && value.value == text;
    };
    if (ctnative_test_entry() != 0 || ctn_test_maps.size() != 1) { return 90; }
    auto owner = g_host;
    auto table = owner->slot;
    auto getter = table->m_get;
    auto setter = table->m_set;
    auto size = table->m_size;
    static_assert(std::is_same_v<decltype(getter), std::function<result_type(ctnative::js_boolean_t)>>);
    static_assert(std::is_same_v<decltype(setter), std::function<result_type(result_type)>>);
    auto caller = getter(ctnative::js_boolean_t{false});
    const auto saved = setter(caller);
    const auto absent = getter(ctnative::js_boolean_t{true});
    caller.value.assign(expected.size(), 'x');
    if (!equal(saved, kind::string, expected) || !equal(setter(absent), kind::null_value) ||
        !equal(setter(result_type{}), kind::undefined) ||
        !equal(setter(result_type{std::string{}}), kind::string) || size().value() != 0) { return 91; }
    std::weak_ptr owner_lifetime = owner;
    std::weak_ptr table_lifetime = table;
    g_host.reset();
    owner.reset();
    table.reset();
    if (!owner_lifetime.expired() || !table_lifetime.expired() ||
        ctn_test_maps[0].expired()) { return 92; }
    if (ctnative_test_entry() != 0 || ctn_test_maps.size() != 2 ||
        ctn_test_maps[0].lock() == ctn_test_maps[1].lock()) { return 93; }
    for (int index = 0; index < 128; ++index) {
        const std::string text = expected + std::to_string(index);
        auto input = result_type{text};
        auto returned = setter(input);
        input.value.assign(text.size(), 'q');
        const auto copied = returned;
        returned.value.assign(text.size(), 'z');
        if (!equal(copied, kind::string, text) || !equal(getter(ctnative::js_boolean_t{false}), kind::string, expected) ||
            !equal(getter(ctnative::js_boolean_t{true}), kind::null_value) || !equal(setter(absent), kind::null_value) ||
            !equal(setter(result_type{}), kind::undefined) ||
            !equal(setter(result_type{std::string{}}), kind::string) ||
            size().value() != 0 || g_host->slot->m_size().value() != 0) { return 94; }
    }
    const auto survivor = setter(getter(ctnative::js_boolean_t{false}));
    const auto null_survivor = setter(getter(ctnative::js_boolean_t{true}));
    const auto undefined_survivor = setter(result_type{});
    const auto empty_survivor = setter(result_type{std::string{}});
    getter = {};
    setter = {};
    if (ctn_test_maps[0].expired()) { return 95; }
    size = {};
    if (!ctn_test_maps[0].expired() || ctn_test_maps[1].expired()) { return 96; }
    std::vector<std::string> churn;
    for (int index = 0; index < 4096; ++index) { churn.emplace_back(expected.size(), 'w'); }
    const auto fresh = g_host->slot->m_set(g_host->slot->m_get(ctnative::js_boolean_t{false}));
    g_host.reset();
    if (!ctn_test_maps[1].expired() || !equal(saved, kind::string, expected) ||
        !equal(survivor, kind::string, expected) || !equal(null_survivor, kind::null_value) ||
        !equal(undefined_survivor, kind::undefined) || !equal(empty_survivor, kind::string) ||
        !equal(fresh, kind::string, expected)) { return 97; }
    return 0;
}
"""
    changed = changed.replace("EXPECTED_STRING", json.dumps(STRING_RESULT))
    if name == "nullable_nested_result_saved":
        generated, separator, observer = changed.rpartition("\nint main() {\n")
        if not separator:
            raise RuntimeError("nested nullable observer lost its main function")
        # Exercise later same-method results with long caller strings and each
        # nullish tag after releasing the published owner. Only the appended
        # observer changes; emitted method bodies and helpers remain intact.
        for before, after in (
            ("const auto saved = setter(caller);", "const auto saved = setter(setter(caller));"),
            ("auto returned = setter(input);", "auto returned = setter(setter(input));"),
            (
                "const auto survivor = setter(getter(ctnative::js_boolean_t{false}));",
                "const auto survivor = setter(setter(getter(ctnative::js_boolean_t{false})));",
            ),
            (
                "const auto null_survivor = setter(getter(ctnative::js_boolean_t{true}));",
                "const auto null_survivor = setter(setter(getter(ctnative::js_boolean_t{true})));",
            ),
            (
                "const auto undefined_survivor = setter(result_type{});",
                "const auto undefined_survivor = setter(setter(result_type{}));",
            ),
            (
                "const auto empty_survivor = setter(result_type{std::string{}});",
                "const auto empty_survivor = setter(setter(result_type{std::string{}}));",
            ),
            (
                "const auto fresh = g_host->slot->m_set(g_host->slot->m_get(ctnative::js_boolean_t{false}));",
                "const auto fresh = g_host->slot->m_set(g_host->slot->m_set(g_host->slot->m_get(ctnative::js_boolean_t{false})));",
            ),
        ):
            if observer.count(before) != 1:
                raise RuntimeError("nested nullable observer lost an owning result call")
            observer = observer.replace(before, after)
        changed = generated + separator + observer
    if name == "nullable_host_result_saved":
        # This saved size callable itself consumes a nullable result. A fixed
        # distinct String entry observes both independent Maps without keeping
        # the overwritten payload alive or changing the setter's result.
        # Rewrite only the appended observer; generated helpers may contain
        # their own member size() calls and must remain byte-identical.
        generated, separator, observer = changed.rpartition("\nint main() {\n")
        if not separator:
            raise RuntimeError("nullable host-result observer lost its main function")
        size_call = 'size(result_type{std::string{"anchor"}}).value()'
        observer, count = re.subn(r"(?<![.>\w])size\(\)\.value\(\)", size_call, observer)
        if count != 2 or observer.count("g_host->slot->m_size().value()") != 1:
            raise RuntimeError("nullable host-result observer lost its three size calls")
        observer = observer.replace(
            "g_host->slot->m_size().value()", "g_host->slot->m_" + size_call
        )
        observer = observer.replace(size_call + " != 0", size_call + " != 1")
        observer = observer.replace(
            "    auto caller = getter(ctnative::js_boolean_t{false});",
            "    static_assert(std::is_same_v<decltype(size), std::function<ctnative::js_num(result_type)>>);\n"
            "    auto caller = getter(ctnative::js_boolean_t{false});",
        )
        observer = observer.replace(
            "    for (int index = 0; index < 128; ++index) {",
            "    g_host->slot->m_set(g_host->slot->m_get(ctnative::js_boolean_t{false}));\n"
            "    for (int index = 0; index < 128; ++index) {",
        )
        changed = generated + separator + observer
    source = args.work / f"{name}.{mode}.lifetime.cpp"
    source.write_text(changed)
    binary = (args.work / f"{name}.{mode}.sanitized").resolve()
    host.run(
        [
            compiler,
            *owned.FLAGS,
            "-O1",
            "-g",
            "-fno-omit-frame-pointer",
            "-fsanitize=address,undefined",
            "-fsanitize-address-use-after-scope",
            str(source),
            "-o",
            str(binary),
        ]
    )
    result = subprocess.run(
        [str(binary)],
        capture_output=True,
        text=True,
        timeout=60,
        env=dict(
            os.environ,
            ASAN_OPTIONS="detect_stack_use_after_return=1:detect_leaks=1",
            UBSAN_OPTIONS="halt_on_error=1:print_stacktrace=1",
        ),
    )
    expected_trace = 1 if name == "nullable_host_result_saved" else 0
    if result.returncode or result.stdout != f"trace={expected_trace}\n" * 2:
        raise RuntimeError(
            f"{name}/{mode}: nullable stored-payload lifetime failure\n"
            f"{result.stdout}{result.stderr}"
        )


def check_call_preservation(original, output, name):
    if source_calls(original) != source_calls(output):
        raise RuntimeError(f"{name}: failed ownership changed live source call operands")
    if name.startswith(
        (
            "saved_join",
            "guarded_saved",
            "shortcircuit",
            "nullable",
            "leaf_object",
            "leaf_readback",
            "local_",
            "historical_object",
            "field_",
            "zero_size_",
            "size_one_",
            "startup_empty_",
            "size_deleted_",
            "size_saved_",
            "joined_",
            "object_argument_",
        )
    ):
        pattern = (
            r"^\s*(?:%[-\w.$]+(?::\d+)? = )?((?:ctjs\.(?:truthy|cond_br|br)|"
            r"scf\.(?:if|yield))\b[^\n]*)"
        )
        if re.findall(pattern, original, re.M) != re.findall(pattern, output, re.M):
            raise RuntimeError(f"{name}: failed ownership changed live branch/yield operands")
    if name.startswith(
        (
            "leaf_object",
            "leaf_readback",
            "local_",
            "historical_object",
            "field_",
            "zero_size_",
            "size_one_",
            "startup_empty_",
            "size_deleted_",
            "size_saved_",
            "joined_",
            "object_argument_",
        )
    ):
        pattern = r"^\s*((?:%[-\w.$]+ = )?ctjs\.(?:create_object|set_property|get_property|compare|unary|binary|load_global|store_global)\b[^\n{]*)"
        if [match.strip() for match in re.findall(pattern, original, re.M)] != [
            match.strip() for match in re.findall(pattern, output, re.M)
        ]:
            raise RuntimeError(
                f"{name}: failed ownership changed leaf allocations or field operands"
            )


def check_prepared_result_calls(text, original, name):
    calls = re.findall(
        r"^\s*(%[-\w.$]+) = ctjs\.call_direct @([-\w.$]+)\(([^\n]+)\) "
        r"\{ctnative\.stored_call = 1 : i32\}",
        text,
        re.M,
    )
    if name == "nullable_nested_sibling":
        actuals = [arguments.split(", ") for _, _, arguments in calls]
        if (
            len(source_calls(text)) != len(source_calls(original))
            or [target for _, target, _ in calls]
            != ["fn$4", "fn$5", "fn$5", "fn$4", "fn$5", "fn$3"]
            or [len(arguments) for arguments in actuals] != [5, 5, 5, 5, 5, 4]
            or any(
                actuals[consumer][-1] != calls[producer][0]
                for producer, consumer in ((0, 1), (1, 2), (3, 4))
            )
            or f'ctjs.store_global "trace", {calls[-1][0]}' not in text
        ):
            raise RuntimeError(
                f"{name}: Object/String carrier refusal lost prepared nested result operands"
            )
        return
    pairs = 2 if name.startswith("nullable") else 1
    getter_arguments = 5 if name.startswith("nullable") else 4
    if (
        len(source_calls(text)) != len(source_calls(original))
        or [target for _, target, _ in calls] != ["fn$4", "fn$5"] * pairs + ["fn$3"]
        or [len(arguments.split(", ")) for _, _, arguments in calls]
        != [getter_arguments, 5] * pairs + [4]
        or any(
            calls[index + 1][2].split(", ")[-1] != calls[index][0]
            for index in range(0, pairs * 2, 2)
        )
        or f'ctjs.store_global "trace", {calls[-1][0]}' not in text
    ):
        raise RuntimeError(f"{name}: carrier refusal lost the prepared live result edge")


def forge_map_presence(text, payload="bool"):
    if payload not in {"bool", "string", "nullable_string"}:
        raise ValueError("forged presence needs a valid read tag")
    # Nullable alternatives are a read proof, not a scalar write/key proof.
    # Forge each accepted vocabulary independently, so parsing cannot reject
    # the control before its live read/presence evidence is rederived.
    scalar = "string" if payload == "nullable_string" else payload
    marked, count = re.subn(
        r"(^\s*%[-\w.$]+ = ctjs\.call [^\n{]+)(\{)?",
        lambda match: match[1].rstrip()
        + ' {ctnative.map_present = true, ctnative.map_read_type = "'
        + payload
        + '", ctnative.map_write_type = "'
        + scalar
        + '"'
        + ', ctnative.map_key_type = "'
        + scalar
        + '"'
        + (", " if match[2] else "}"),
        methods.forge_reports(text),
        flags=re.M,
    )
    if count == 0:
        raise RuntimeError("forged-presence control lost every live Map call")
    return marked


def forge_leaf_evidence(text, payload="bool"):
    marked = forge_map_presence(text, payload)
    marked, count = re.subn(
        r"(\bctjs\.create_object)(\s*\{)?",
        lambda match: match[1] + " {ctnative.object_identity" + (", " if match[2] else "}"),
        marked,
    )
    if count == 0:
        raise RuntimeError("forged leaf evidence lost every allocation")
    marked, count = re.subn(
        r"(^\s*(?:%[-\w.$]+ = )?ctjs\.(?:get|set)_property [^\n{]+)(\{)?",
        lambda match: match[1].rstrip()
        + " {ctnative.object_field_group = 99 : i64"
        + (", " if match[2] else "}"),
        marked,
        flags=re.M,
    )
    if count == 0:
        raise RuntimeError("forged leaf evidence lost every field/publication access")
    return marked


def check_budgets(args, ir, config, name, functions=4):
    original = ir.read_text()
    signatures = re.findall(
        r"^\s*ctjs\.func (.*?) -> !ctjs.value attributes \{.*?" r"upvalue_count = (\d+) : i32",
        original,
        re.M,
    )
    operations = (
        "create_object",
        "create_cell",
        "cell_set",
        "create_closure",
        "load_upvalue",
        "construct",
        "set_property",
        "get_property",
        "call",
        "call_direct",
        "store_global",
    )
    counts = {op: len(re.findall(rf"\bctjs\.{op}\b", original)) for op in operations}
    checked = {}
    rollback = []

    def admitted(budget):
        if budget in checked:
            return checked[budget]
        output = owned.lower(
            args,
            ir,
            f"{name}-budget-{budget}",
            config,
            options=f"host-max-steps={budget}",
            cleanup=False,
        )
        text = methods.census(output, functions, name)
        native = len(boundary.NATIVE.findall(text))
        if native not in (0, functions):
            raise RuntimeError(f"{name}/{budget}: published an incomplete native component")
        if native == 0:
            if (
                re.findall(
                    r"^\s*ctjs\.func (.*?) -> !ctjs.value attributes \{.*?"
                    r"upvalue_count = (\d+) : i32",
                    text,
                    re.M,
                )
                != signatures
            ):
                raise RuntimeError(
                    f"{name}/{budget}: leaked speculative capture/signature rewrites"
                )
            check_call_preservation(original, text, f"{name}/{budget}")
            for op, count in counts.items():
                if len(re.findall(rf"\bctjs\.{op}\b", text)) != count:
                    raise RuntimeError(f"{name}/{budget}: leaked speculative {op} rewrites")
            if "ctnative.host_owner_proved = true" in text:
                rollback.append(budget)
        checked[budget] = native == functions
        return checked[budget]

    low, high = 0, 100000
    if not admitted(high):
        raise RuntimeError(f"{name}: budget witness never admits its complete component")
    while low < high:
        middle = (low + high) // 2
        if admitted(middle):
            high = middle
        else:
            low = middle + 1
    for budget in range(max(0, high - 16), high + 1):
        if admitted(budget) != (budget >= high):
            raise RuntimeError(f"{name}: inconsistent admission at the measured budget boundary")
    print(
        f"{name}: first complete budget {high}; {len(checked)} cutoffs checked; "
        f"{len(rollback)} discard a speculative rewrite after original owner proof"
    )
    return rollback
