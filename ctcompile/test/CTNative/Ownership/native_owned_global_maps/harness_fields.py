from .harness_objects import (
    instrument_leaf_objects,
)
from .harness_scalar_maps import (
    STRING_FIELD_BYTES,
    STRING_FIELD_LONG,
    StringValue,
    host,
    join_size_sources,
    json,
    mutation_size_sources,
    one_size_cases,
    os,
    owned,
    re,
    scalar_global_output,
    string_field_cases,
    subprocess,
    zero_size_cases,
)

def string_field_cpp_value(value):
    data = value.encode('utf-8')
    return 'std::string("' + ''.join(f'\\x{byte:02x}' for byte in data) + '", ' + str(len(data)) + ')'


def check_string_field_calls(cpp, name, mode):
    row = string_field_cases()[name]
    source, value = row['source'], row['expected_trace']
    result = 'std::string' if isinstance(value, StringValue) else 'js_num'
    params = 'std::string, std::string, bool' if name == 'field_string_lifetime' else 'std::string'
    if (f'std::function<{result}({params})>' not in cpp
            or 'nullable_string field_76616c7565;' not in cpp
            or 'nullable_scalar field_76616c7565;' in cpp
            or 'std::shared_ptr<ctnative::map_storage<std::string, ctnative::object_value>>' not in cpp
            or cpp.count('std::make_shared<ctnative::identity_object>()') != source.count('{value:')):
        raise RuntimeError(f'{name}/{mode}: lost the independently typed String field/Map/callable owners')
    if name == 'field_string_separate_members' and (
            'nullable_scalar field_636f756e74;' not in cpp or 'nullable_scalar field_666c6167;' not in cpp):
        raise RuntimeError(f'{name}/{mode}: widened independent Number/Boolean members to String storage')
    for method in ('set', 'get', 'has', 'delete', 'clear'):
        before = len(re.findall(rf'\bstate\.{method}\(', source))
        after = len(re.findall(rf'ctnative::map_{method}(?:_\w+)?(?:<[^>]+>)?\(', cpp))
        if before != after:
            raise RuntimeError(f'{name}/{mode}: changed {before} live Map.{method} operations to {after}')
    # Ignore quoted source bytes before counting the original field accesses.
    body = source.split('set(key', 1)[1].split('\n', 1)[0]
    body = re.sub(r'"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'', '""', body)
    accesses = list(re.finditer(r'\b(?:item|alias)\.(?:value|count|flag)\b', body))
    reads = sum(not re.match(r'\s*=(?!=)', body[match.end():]) for match in accesses)
    writes = len(accesses) - reads + len(re.findall(r'\b(?:value|count|flag):', body))
    if (len(re.findall(r'ctnative::object_get_field_[0-9a-f]+\(', cpp)) != reads
            or len(re.findall(r'ctnative::object_set_field_[0-9a-f]+\(', cpp)) != writes):
        raise RuntimeError(f'{name}/{mode}: changed the {reads} read-time field values or {writes} stores')
    entry = re.search(r'\bmain\(\)\s*\{(.*?)^\}', cpp, re.M | re.S)
    if not entry:
        raise RuntimeError(f'{name}/{mode}: lost entry')
    methods_by_value = dict(re.findall(
        r'(\w+)\s*=\s*ctnative::method_get<&[^>\n]+::m_(\w+)>\(', entry[1]))
    calls = re.findall(r'ctnative::invoke_callable\((\w+)([^;\n]*)\);', entry[1])
    if [methods_by_value.get(callee) for callee, _ in calls] != re.findall(r'host\.slot\.(size|set)\(', source):
        raise RuntimeError(f'{name}/{mode}: changed the live published-call order')
    if isinstance(value, StringValue) and entry[1].count('ctnative::global_string(') != 1:
        raise RuntimeError(f'{name}/{mode}: lost the exact String output tag check')


def string_field_observer_source(source, name):
    observed = source + r'''
(function() {
    const seen = [];
    const original = Map.prototype.set;
    Map.prototype.set = function(key, value) {
        seen.push(value); return original.call(this, key, value);
    };
    const before = host.slot.size();
'''
    if name == 'field_string_lifetime':
        observed += ('    const first = host.slot.set("future", ' + json.dumps(STRING_FIELD_BYTES) + ', false);\n'
                     '    const second = host.slot.set("future", ' + json.dumps(STRING_FIELD_LONG) + ', true);\n')
        checks = ['seen.length === 2', 'seen[0] !== seen[1]', 'host.slot.size() === 0',
                  'typeof first === "string" && first === ' + json.dumps(STRING_FIELD_BYTES),
                  'typeof second === "string" && second === ' + json.dumps(STRING_FIELD_LONG),
                  'seen[0].value === "right"', 'seen[1].value === "left"']
    else:
        observed += ('    const first = host.slot.set("future");\n'
                     '    const second = host.slot.set("future");\n')
        count = 4 if name == 'field_string_saved_overwrite' else 2
        second = 2 if count == 4 else 1
        deleted = name in {'field_string_saved', 'field_string_saved_overwrite'}
        checks = [f'seen.length === {count}', f'seen[0] !== seen[{second}]',
                  'host.slot.size() === ' + ('0' if deleted else 'before + 1')]
        expected = string_field_cases()[name]['expected_trace']
        if name == 'leaf_object_string_field':
            checks += ['first === before + 1', 'second === before + 1',
                       'seen[0].value === "instance"', 'seen[1].value === "instance"']
        else:
            checks += ['typeof first === "string" && first === ' + json.dumps(expected),
                       'typeof second === "string" && second === ' + json.dumps(expected)]
            if deleted:
                checks += ['seen[0].value === "changed"', f'seen[{second}].value === "changed"']
                if count == 4:
                    checks += ['seen[1].value === "replacement"', 'seen[3].value === "replacement"']
            else:
                checks += ['seen[0].value === "changed"', 'seen[1].value === "changed"']
    observed += '    Map.prototype.set = original;\n    trace = 0;\n'
    for index, check in enumerate(checks):
        observed += f'    if ({check}) {{ trace = trace + {1 << index}; }}\n'
    return observed + '})();\n', (1 << len(checks)) - 1


def string_field_identity_cpp(cpp):
    changed = instrument_leaf_objects(cpp)
    return changed + r'''
int main() {
    ctnative::identity_object empty;
    if (empty.field_76616c7565.tag != ctnative::nullable_string::kind::undefined) { return 120; }
    if (ctnative_test_entry() != 0 || ctn_test_maps.size() != 1) { return 121; }
    const auto before = g_host->slot->m_size();
    const auto count = ctn_test_objects.size();
    g_host->slot->m_set(std::string{"future"});
    auto first = std::static_pointer_cast<const ctnative::identity_object>(ctn_test_objects.back().lock());
    g_host->slot->m_set(std::string{"future"});
    auto second = std::static_pointer_cast<const ctnative::identity_object>(ctn_test_objects.back().lock());
    if (!first || !second || first == second || ctn_test_objects.size() != count + 2 ||
        g_host->slot->m_size() != before + 1 ||
        first->field_76616c7565.tag != ctnative::nullable_string::kind::string ||
        second->field_76616c7565.tag != ctnative::nullable_string::kind::string ||
        first->field_76616c7565.value != "instance" || second->field_76616c7565.value != "instance") {
        return 122;
    }
    const auto saved = first->field_76616c7565;
    g_host.reset();
    if (!ctn_test_maps[0].expired()) { return 123; }
    first.reset(); second.reset();
    std::vector<std::string> churn(4096, std::string(256, 'w'));
    if (saved.tag != ctnative::nullable_string::kind::string || saved.value != "instance") { return 124; }
    for (const auto & object : ctn_test_objects) { if (!object.expired()) { return 125; } }
    return 0;
}
'''


def string_field_lifetime_cpp(cpp):
    changed = instrument_leaf_objects(cpp)
    changed = changed.replace('template <class T> std::shared_ptr<T> ctn_test_make_leaf() {',
        'static bool ctn_test_capture_next = false;\n'
        'static std::shared_ptr<void> ctn_test_retained;\n'
        'template <class T> std::shared_ptr<T> ctn_test_make_leaf() {')
    changed = changed.replace('ctn_test_objects.emplace_back(made); return made;',
        'ctn_test_objects.emplace_back(made); '
        'if (ctn_test_capture_next) { ctn_test_capture_next = false; ctn_test_retained = made; } '
        'return made;')
    changed += r'''
int main() {
    const auto original = ORIGINAL;
    const auto bytes = BYTES;
    ctnative::identity_object empty;
    if (empty.field_76616c7565.tag != ctnative::nullable_string::kind::undefined) { return 130; }
    if (ctnative_test_entry() != 0 || ctn_test_maps.size() != 1 || ctn_test_objects.size() != 1 ||
        !ctn_test_objects[0].expired() || g_trace.tag != ctnative::nullable_string::kind::string ||
        g_trace.value != original) { return 131; }
    const auto startup = g_trace.value;
    auto owner = g_host;
    auto table = owner->slot;
    auto size = table->m_size;
    auto setter = table->m_set;
    static_assert(std::is_same_v<decltype(size), std::function<js_num()>>);
    static_assert(std::is_same_v<decltype(setter), std::function<std::string(std::string, std::string, bool)>>);
    std::weak_ptr owner_lifetime = owner;
    std::weak_ptr table_lifetime = table;
    g_host.reset(); owner.reset(); table.reset(); g_trace = {};
    if (!owner_lifetime.expired() || !table_lifetime.expired() || ctn_test_maps[0].expired()) { return 132; }
    std::vector<std::string> snapshots;
    std::vector<std::string> expected;
    for (int index = 0; index < 128; ++index) {
        auto caller = original + std::to_string(index) + bytes;
        expected.push_back(caller);
        const auto count = ctn_test_objects.size();
        snapshots.push_back(setter(std::string(160, index % 2 ? 'k' : 'q'), caller, index % 2 != 0));
        caller.assign(caller.size(), 'x');
        if (snapshots.back() != expected.back() || size() != 0 || ctn_test_objects.size() != count + 1 ||
            !ctn_test_objects.back().expired()) { return 133; }
    }
    if (!setter("empty", "", false).empty()) { return 134; }
    auto byte_result = setter("bytes", bytes, true);
    const auto saved_bytes = byte_result;
    byte_result.assign(byte_result.size(), 'z');
    if (saved_bytes != bytes || startup != original || snapshots != expected) { return 135; }
    if (ctnative_test_entry() != 0 || ctn_test_maps.size() != 2 ||
        ctn_test_maps[0].lock() == ctn_test_maps[1].lock()) { return 136; }
    ctn_test_capture_next = true;
    const auto kept_result = setter("retained", original + bytes, true);
    if (!ctn_test_retained || kept_result != original + bytes || size() != 0 ||
        g_host->slot->m_size() != 0) { return 137; }
    setter = {};
    if (ctn_test_maps[0].expired()) { return 138; }
    size = {};
    if (!ctn_test_maps[0].expired() || ctn_test_maps[1].expired()) { return 139; }
    g_host.reset(); g_trace = {};
    if (!ctn_test_maps[1].expired()) { return 140; }
    auto retained = std::static_pointer_cast<ctnative::identity_object>(ctn_test_retained);
    ctn_test_retained.reset();
    if (retained->field_76616c7565.tag != ctnative::nullable_string::kind::string ||
        retained->field_76616c7565.value != "left") { return 141; }
    const auto retained_field = retained->field_76616c7565;
    retained->field_76616c7565.value.assign(8192, 'y');
    retained.reset();
    std::vector<std::string> churn(4096, std::string(original.size(), 'w'));
    if (retained_field.tag != ctnative::nullable_string::kind::string || retained_field.value != "left" ||
        kept_result != original + bytes || snapshots != expected || startup != original || saved_bytes != bytes) {
        return 142;
    }
    for (const auto & object : ctn_test_objects) { if (!object.expired()) { return 143; } }
    return 0;
}
'''
    return changed.replace('ORIGINAL', string_field_cpp_value(STRING_FIELD_LONG)).replace(
        'BYTES', string_field_cpp_value(STRING_FIELD_BYTES))


def string_field_lifetime(args, cpp, name, mode, compiler):
    source = args.work / f'{name}.{mode}.lifetime.cpp'
    source.write_text(string_field_lifetime_cpp(cpp))
    binary = (args.work / f'{name}.{mode}.sanitized').resolve()
    host.run([compiler, *owned.FLAGS, '-O1', '-g', '-fno-omit-frame-pointer',
        '-fsanitize=address,undefined', '-fsanitize-address-use-after-scope', str(source), '-o', str(binary)])
    result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=60,
        env=dict(os.environ, ASAN_OPTIONS='detect_stack_use_after_return=1:detect_leaks=1',
                 UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1'))
    if result.returncode or result.stdout != scalar_global_output(name, STRING_FIELD_LONG) * 2:
        raise RuntimeError(f'{name}/{mode}: owning String field lifetime failed ({result.returncode})\n'
                           f'{result.stdout}{result.stderr}')



def check_zero_size_calls(cpp, name, mode):
    check_exact_size_calls(cpp, name, mode, zero_size_cases()[name])


def check_one_size_calls(cpp, name, mode):
    check_exact_size_calls(cpp, name, mode, one_size_cases()[name])


def check_exact_size_calls(cpp, name, mode, row):
    source = row['source']
    params = 'js_num, bool' if 'set(key, flag)' in source else 'js_num'
    result = 'ctnative::nullable_scalar' if name == 'size_one_present_field_entry_repair' else 'js_num'
    if (f'std::function<{result}({params})>' not in cpp
            or cpp.count('std::make_shared<ctnative::identity_object>()') != 1):
        raise RuntimeError(f'{name}/{mode}: lost the typed size callable or live leaf allocation')
    if (name in ('size_one_present_field_entry_repair', 'size_one_present_field_checked_repair',
                 'size_deleted_zero_present_field', 'size_deleted_one_present_field')
            or (name in {**join_size_sources(), **mutation_size_sources()}
                and 'state.get(saved).value' in source)):
        present = re.search(r'\b(\w+)\s*=\s*ctnative::map_get_present_identity\([^;]+;', cpp)
        if (not present or len(re.findall(r'\bctnative::map_get_present_identity\(', cpp)) != 1
                or f'ctnative::object_get_field_76616c7565({present[1]})' not in cpp):
            raise RuntimeError(f'{name}/{mode}: exact saved one lost its present object field read')
    for method in ('set', 'get', 'has', 'delete', 'clear'):
        original = len(re.findall(rf'\b(?:state|alias)\.{method}\(', source))
        # Existing preparation coalesces the historical identical set arms.
        # Its immutable nine-call source has eight prepared/runtime call sites.
        if method == 'set':
            original -= row['raw_calls'] - row['prepared_calls']
        emitted = len(re.findall(rf'\bctnative::map_{method}(?:_\w+)?(?:<[^>]+>)?\(', cpp))
        if original != emitted:
            raise RuntimeError(f'{name}/{mode}: changed {original} live Map.{method} calls to {emitted}')
    size_reads = len(re.findall(r'\b(?:state|alias)\.size\b', source))
    if len(re.findall(r'\bctnative::map_size\(', cpp)) != size_reads:
        raise RuntimeError(f'{name}/{mode}: exact proof erased an evaluated size read')
    entry = re.search(r'\bmain\(\)\s*\{(.*?)^\}', cpp, re.M | re.S)
    if not entry or entry[1].count('ctnative::invoke_callable(') != 2:
        raise RuntimeError(f'{name}/{mode}: lost original published size/set calls')


def zero_size_observer_source(source):
    observed = source + r"""
(function() {
    const setter = host.slot.set;
    const size = host.slot.size;
    const startup = trace;
    const a = setter(101, false);
    const aSize = size();
    const b = setter(-8, true);
    const bSize = size();
    const c = setter(0, false);
    const d = setter(1, true);
    trace = (typeof startup === 'number' && startup === 1 ? 1 : 0) |
            (typeof a === 'number' && a === 1 ? 2 : 0) |
            (typeof b === 'number' && b === 2 ? 4 : 0) |
            (typeof c === 'number' && c === 1 ? 8 : 0) |
            (typeof d === 'number' && d === 2 ? 16 : 0) |
            (aSize === 1 && bSize === 1 && size() === 1 ? 32 : 0);
})();
"""
    return observed, 63


def zero_size_lifetime_cpp(cpp):
    changed = instrument_leaf_objects(cpp)
    changed = changed.replace('static std::vector<std::weak_ptr<const void>> ctn_test_objects;',
        'static std::vector<std::weak_ptr<const void>> ctn_test_objects;\n'
        'static std::shared_ptr<const void> ctn_test_retained;\n'
        'static bool ctn_test_keep_leaf = false;')
    changed = changed.replace('ctn_test_objects.emplace_back(made); return made;',
        'ctn_test_objects.emplace_back(made);\n'
        '    if (ctn_test_keep_leaf) { ctn_test_retained = made; }\n    return made;')
    return changed + r"""
int main() {
    if (ctnative_test_entry() != 0 || ctn_test_maps.size() != 1 ||
        ctn_test_objects.size() != 1 || ctn_test_objects[0].expired()) { return 180; }
    auto owner = g_host;
    auto table = owner->slot;
    auto setter = table->m_set;
    auto size = table->m_size;
    static_assert(std::is_same_v<decltype(size), std::function<js_num()>>);
    static_assert(std::is_same_v<decltype(setter), std::function<js_num(js_num, bool)>>);
    std::weak_ptr owner_lifetime = owner;
    std::weak_ptr table_lifetime = table;
    g_host.reset(); owner.reset(); table.reset();
    if (!owner_lifetime.expired() || !table_lifetime.expired() || ctn_test_maps[0].expired()) {
        return 181;
    }
    ctn_test_keep_leaf = true;
    std::vector<js_num> results;
    for (int call = 0; call < 128; ++call) {
        const bool flag = call % 2 != 0;
        const auto before = ctn_test_objects.size();
        results.push_back(setter(static_cast<js_num>(call % 3 == 0 ? 0 : 100 + call), flag));
        if (results.back() != (flag ? 2 : 1) || size() != 1 ||
            ctn_test_objects.size() != before + 1) { return 182; }
        for (std::size_t index = 0; index < before; ++index) {
            if (!ctn_test_objects[index].expired()) { return 183; }
        }
        const auto leaf = std::static_pointer_cast<const ctnative::identity_object>(ctn_test_retained);
        if (!leaf || leaf->field_76616c7565.tag != ctnative::nullable_scalar::kind::number ||
            leaf->field_76616c7565.value != 1) { return 184; }
    }
    ctn_test_keep_leaf = false;
    const auto retained_index = ctn_test_objects.size() - 1;
    if (ctnative_test_entry() != 0 || ctn_test_maps.size() != 2 ||
        ctn_test_maps[0].lock() == ctn_test_maps[1].lock() || size() != 1 ||
        g_host->slot->m_size() != 1) { return 185; }
    setter = {};
    if (ctn_test_maps[0].expired()) { return 186; }
    size = {};
    if (!ctn_test_maps[0].expired() || ctn_test_maps[1].expired() ||
        ctn_test_objects[retained_index].expired()) { return 187; }
    g_host.reset();
    if (!ctn_test_maps[1].expired()) { return 188; }
    for (std::size_t index = 0; index < ctn_test_objects.size(); ++index) {
        if (ctn_test_objects[index].expired() != (index != retained_index)) { return 189; }
    }
    for (std::size_t index = 0; index < results.size(); ++index) {
        if (results[index] != (index % 2 != 0 ? 2 : 1)) { return 190; }
    }
    ctn_test_retained.reset();
    if (!ctn_test_objects[retained_index].expired()) { return 191; }
    return 0;
}
"""


def zero_size_lifetime(args, cpp, name, mode, compiler):
    source = args.work / f'{name}.{mode}.lifetime.cpp'
    source.write_text(zero_size_lifetime_cpp(cpp))
    binary = source.with_suffix('.sanitized').resolve()
    host.run([compiler, *owned.FLAGS, '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
              str(source), '-o', str(binary)])
    result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=30,
        env={**os.environ, 'ASAN_OPTIONS': 'detect_leaks=1:halt_on_error=1',
             'UBSAN_OPTIONS': 'halt_on_error=1'})
    if result.returncode or result.stdout != 'trace=1\n' * 2 or result.stderr:
        raise RuntimeError(f'{name}/{mode}: saved zero/leaf lifetime failed\n'
                           f'{result.returncode}: {result.stdout}{result.stderr}')


def one_size_observer_source(source):
    # Both witnesses select an owning saved leaf, branch on the future flag,
    # and finish with one entry. The source's read-time key proof differs.
    return zero_size_observer_source(source)


def one_size_lifetime_cpp(cpp):
    return zero_size_lifetime_cpp(cpp)


def one_size_lifetime(args, cpp, name, mode, compiler):
    source = args.work / f'{name}.{mode}.lifetime.cpp'
    source.write_text(one_size_lifetime_cpp(cpp))
    binary = source.with_suffix('.sanitized').resolve()
    host.run([compiler, *owned.FLAGS, '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
              str(source), '-o', str(binary)])
    result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=30,
        env={**os.environ, 'ASAN_OPTIONS': 'detect_leaks=1:halt_on_error=1',
             'UBSAN_OPTIONS': 'halt_on_error=1'})
    if result.returncode or result.stdout != 'trace=1\n' * 2 or result.stderr:
        raise RuntimeError(f'{name}/{mode}: saved one/leaf lifetime failed\n'
                           f'{result.returncode}: {result.stdout}{result.stderr}')


def delete_size_observer_source(source):
    return zero_size_observer_source(source)


def delete_size_lifetime_cpp(cpp):
    return zero_size_lifetime_cpp(cpp)


def delete_size_lifetime(args, cpp, name, mode, compiler):
    source = args.work / f'{name}.{mode}.lifetime.cpp'
    source.write_text(delete_size_lifetime_cpp(cpp))
    binary = source.with_suffix('.sanitized').resolve()
    host.run([compiler, *owned.FLAGS, '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
              str(source), '-o', str(binary)])
    result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=30,
        env={**os.environ, 'ASAN_OPTIONS': 'detect_leaks=1:halt_on_error=1',
             'UBSAN_OPTIONS': 'halt_on_error=1'})
    if result.returncode or result.stdout != 'trace=1\n' * 2 or result.stderr:
        raise RuntimeError(f'{name}/{mode}: saved deletion size/leaf lifetime failed\n'
                           f'{result.returncode}: {result.stdout}{result.stderr}')
