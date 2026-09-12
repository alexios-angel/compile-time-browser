"""Captured outer Maps retain distinct, freshly allocated child Maps."""

from concurrent.futures import ThreadPoolExecutor
import hashlib
import os
import re
import subprocess

from .driver_common import (
    boundary, check_budgets, check_call_preservation, comparable_provenance, contract,
    forge_leaf_evidence, host, methods, owned, source_calls,
)
from .harness_objects import instrument_leaf_objects


def nested_map_cases():
    # These are bounded ownership witnesses, not Bootstrap's exact Data source.
    base = '''var host = {};
(function(factory) { host.slot = factory(); })(function() {
    const t = new Map;
    return { get(value) {
        const child = new Map;
        child.set('value', 0);
        t.set(1, child);
        const saved = t.get(1);
        child.set('value', value);
        return saved.get('value');
    } };
});
var trace = host.slot.get(41);
'''
    rows = {}

    def add(name, source, calls, *, admitted=False, functions=4, children=1, retained=False):
        rows['nested_map_' + name] = dict(source=source, functions=functions, calls=calls,
            sha256=hashlib.sha256(source.encode()).hexdigest(), admitted=admitted,
            children=children, retained=retained, expected_trace=41)

    add('retained', base, 8, admitted=True, retained=True)
    add('saved_delete', base.replace("        child.set('value', value);",
        "        t.delete(1);\n        child.set('value', value);"), 9, admitted=True)
    distinct = base.replace('''        const child = new Map;
        child.set('value', 0);
        t.set(1, child);
        const saved = t.get(1);
        child.set('value', value);
        return saved.get('value');''', '''        const first = new Map, second = new Map;
        first.set('value', 17);
        second.set('value', 23);
        t.set(1, first);
        t.set(2, second);
        const saved = t.get(1), other = t.get(2);
        t.set(1, second);
        t.delete(2);
        t.clear();
        saved.set('value', value);
        return other.get('value') === 23 ? saved.get('value') : 0;''')
    add('distinct_saved', distinct, 15, admitted=True, children=2)
    add('distinct_absence', distinct.replace("second.set('value', 23);",
        "second.set('other', 23);").replace("other.get('value') === 23",
        "other.get('value') === void 0"), 15, admitted=True, children=2)
    add('unproved_key', base.replace('get(value)', 'get(value, key)').replace(
        'const saved = t.get(1);', 'const saved = t.get(key);').replace(
        'host.slot.get(41)', 'host.slot.get(41, 1)'), 8)
    for action in ('delete(1)', 'clear()'):
        add('stale_' + action.split('(')[0], base.replace('get(value)', 'get(value, flag)').replace(
            'const saved = t.get(1);',
            'if (flag) { t.' + action + '; }\n        const saved = t.get(1);').replace(
            'host.slot.get(41)', 'host.slot.get(41, false)'), 9)
    for name, payload in (('self_cycle', 'child'), ('outer_cycle', 't'),
                          ('child_owns_map', 'new Map')):
        add(name, base.replace("        child.set('value', value);",
            "        child.set('edge', " + payload + ");\n        child.set('value', value);"), 9)
    add('child_as_key', base.replace('        const saved = t.get(1);',
        '        t.set(child, child);\n        const saved = t.get(1);'), 9)
    add('nonstandard_child', base.replace('const child = new Map;',
                                        'const child = new Map([]);'), 8)
    add('foreign_consumer', 'function consume(item) { return 0; }\n' + base.replace(
        '        const saved = t.get(1);',
        '        consume(child);\n        const saved = t.get(1);'), 9, functions=5)
    add('returned_child', base.replace("return saved.get('value');", 'return saved;').replace(
        'host.slot.get(41);', "host.slot.get(41).get('value');"), 8)
    add('conditional_initialize', base.replace('''        const child = new Map;
        child.set('value', 0);
        t.set(1, child);''', '        t.has(1) || t.set(1, new Map);').replace(
        "child.set('value', value);", "saved.set('value', value);"), 8)
    add('cross_invocation', '''var host = {};
(function(factory) { host.slot = factory(); })(function() {
    const t = new Map;
    return {
        set(value) { const child = new Map; child.set('value', value); t.set(1, child); return value; },
        get() { if (t.has(1)) { return t.get(1).get('value'); } return 0; }
    };
});
host.slot.set(41);
var trace = host.slot.get();
''', 9, functions=5)
    return rows


def nested_map_observer(source, row):
    observed = source + '''
(function() {
    const get = host.slot.get;
    host = {};
    const seen = [], original = Map.prototype.set;
    Map.prototype.set = function(key, value) {
        if (value instanceof Map) { seen.push([this, key, value]); }
        return original.call(this, key, value);
    };
    let ok = get(17) === 17 && get(-3) === -3;
    Map.prototype.set = original;
    const stride = STRIDE;
    ok = ok && seen.length === stride * 2 && seen[0][0] === seen[stride][0] &&
         seen[0][2] !== seen[stride][2] && seen[0][2].get('value') === 17 &&
         seen[stride][2].get('value') === -3 && seen[0][0].size === OUTER_SIZE;
    DISTINCT
    for (let i = 0; i < 128; ++i) {
        const value = i % 2 === 0 ? -i : i + 0.5;
        const answer = get(value);
        if (typeof answer !== 'number' || answer !== value) { ok = false; }
    }
    trace = ok ? 1 : 0;
})();
'''
    distinct = '' if row['children'] == 1 else '''ok = ok && seen[0][2] !== seen[1][2] &&
        seen[1][2] === seen[2][2] && seen[stride + 1][2] === seen[stride + 2][2];'''
    return observed.replace('STRIDE', str(1 if row['children'] == 1 else 3)).replace(
        'OUTER_SIZE', '1' if row['retained'] else '0').replace('DISTINCT', distinct)


def nested_map_lifetime_cpp(cpp, row):
    # Reuse the existing weak allocation observer; generated ownership is unchanged.
    changed = instrument_leaf_objects(cpp, allocations=0) + r'''
int main() {
    using Child = ctnative::number_map<std::string>;
    using Outer = ctnative::map_storage<js_num, std::shared_ptr<Child>>;
    constexpr std::size_t children = CHILDREN;
    constexpr bool retained = RETAINED;
    if (ctnative_test_entry() != 0 || ctn_test_maps.size() != 1 + children) { return 270; }
    auto owner = g_host;
    auto table = owner->slot;
    auto get = table->m_get;
    static_assert(std::is_same_v<decltype(get), std::function<js_num(js_num)>>);
    std::weak_ptr owner_lifetime = owner;
    std::weak_ptr table_lifetime = table;
    g_host.reset(); owner.reset(); table.reset();
    if (!owner_lifetime.expired() || !table_lifetime.expired() ||
        ctn_test_maps[0].expired()) { return 271; }
    auto outer = std::const_pointer_cast<Outer>(
        std::static_pointer_cast<const Outer>(ctn_test_maps[0].lock()));
    if (outer->size() != (retained ? 1U : 0U)) { return 272; }
    std::shared_ptr<Child> saved;
    std::weak_ptr<Child> saved_lifetime;
    if constexpr (retained) {
        saved = outer->at(js_num{1}); saved_lifetime = saved;
        if (saved != ctn_test_maps[1].lock() || saved->at("value") != 41) { return 273; }
    }
    for (int call = 0; call < 128; ++call) {
        const auto before = ctn_test_maps.size();
        const js_num value = call % 2 == 0 ? -call : call + 0.5;
        if (get(value) != value || ctn_test_maps.size() != before + children) { return 274; }
        for (std::size_t index = before; index < ctn_test_maps.size(); ++index) {
            if (ctn_test_maps[index].expired() == retained) { return 275; }
        }
        if constexpr (retained) {
            auto child = outer->at(js_num{1});
            if (child == saved || child != ctn_test_maps.back().lock() ||
                child->at("value") != value || saved->at("value") != 41) { return 276; }
        }
    }
    ctnative::map_clear(outer);
    if constexpr (retained) {
        if (saved_lifetime.expired() || !ctn_test_maps.back().expired()) { return 277; }
        ctnative::map_set(saved, std::string{"value"}, js_num{47});
        if (ctnative::map_get_present(saved, std::string{"value"}) != 47) { return 278; }
        saved.reset();
        if (!saved_lifetime.expired()) { return 279; }
    }
    if (get(19) != 19) { return 280; }
    const auto next = ctn_test_maps.size();
    if (ctnative_test_entry() != 0 || ctn_test_maps.size() != next + children + 1 ||
        ctn_test_maps[0].lock() == ctn_test_maps[next].lock()) { return 281; }
    outer.reset();
    if (ctn_test_maps[0].expired() || get(23) != 23) { return 282; }
    get = {};
    if (!ctn_test_maps[0].expired() || ctn_test_maps[next].expired()) { return 283; }
    g_host.reset();
    for (const auto & map : ctn_test_maps) {
        if (!map.expired()) { return 284; }
    }
    return 0;
}
'''
    return changed.replace('CHILDREN', str(row['children'])).replace(
        'RETAINED', str(row['retained']).lower())


def nested_map_census(args, ir, name, row):
    raw = (args.work / f'{name}.raw.mlir').read_text()
    prepared = ir.read_text()
    if len(boundary.FUNCTION.findall(raw)) != row['functions']:
        raise RuntimeError(f'{name}: changed source function census')
    for text in (raw, prepared):
        if len(source_calls(text)) != row['calls']:
            raise RuntimeError(f'{name}: changed source call census')
    for operation in ('create_object', 'construct', 'get_property', 'set_property',
                      'load_global', 'store_global'):
        if raw.count(operation) != prepared.count(operation):
            raise RuntimeError(f'{name}: preparation changed {operation} census')


def nested_map_preserved(original, output, name):
    check_call_preservation(original, output, name)
    pattern = (r'^\s*((?:%[-\w.$]+(?::\d+)? = )?(?:ctjs\.(?:create_object|construct|'
               r'get_property|set_property|load_global|store_global|compare|unary|binary|truthy)|'
               r'scf\.(?:if|yield))\b[^\n{]*)')
    if ([line.strip() for line in re.findall(pattern, original, re.M)]
            != [line.strip() for line in re.findall(pattern, output, re.M)]):
        raise RuntimeError(f'{name}: refusal changed original nested Map or branch edges')


def check_nested_maps(args, node, reference, compilers, nm):
    cases = nested_map_cases()
    observations = mutations = 0

    def observe(name, source, expected):
        js = args.work / f'{name}-observed.js'
        js.write_text(source)
        result = host.run([str(reference), str(js)]) if reference else None
        if (host.run([node, '-e', boundary.NODE, str(js)]).stdout != f'trace={expected}\n'
                or result and (result.stdout != f'trace={expected}\n' or
                    '(1 number, 0 boolean, 0 string, 0 null, 0 undefined)' not in result.stderr)):
            raise RuntimeError(f'{name}: typed source observation changed')

    for name, row in cases.items():
        observe(name, row['source'], row['expected_trace'])
        observations += 1
        if not row['admitted']:
            continue
        observed = nested_map_observer(row['source'], row)
        observe(name + '-future', observed, 1)
        observations += 1
        replacements = [("saved.get('value')", '41'),
                        (".set('value', value);", ".set('value', 0);")]
        if row['children'] == 2:
            replacements.append(('second = new Map;', 'second = first;'))
        if row['retained']:
            replacements.append(('return saved.get', 't.clear(); return saved.get'))
        for index, (old, replacement) in enumerate(replacements):
            assert row['source'].count(old) == 1, (name, old)
            blinded = args.work / f'{name}-blinded-{index}.js'
            blinded.write_text(nested_map_observer(row['source'].replace(old, replacement), row))
            result = subprocess.run([node, '-e', boundary.NODE, str(blinded)],
                                    capture_output=True, text=True, timeout=30)
            if not result.returncode and result.stdout == 'trace=1\n':
                raise RuntimeError(f'{name}: observer cannot distinguish {replacement}')
            mutations += 1

    def check_case(item):
        name, row = item
        _, ir, functions = boundary.prepare(args, name, row['source'])
        assert functions == row['functions']
        nested_map_census(args, ir, name, row)
        config = contract(args, ir, name)
        for policy, options in (('default', ''), ('disabled', 'optimize=false')):
            label = name + '-' + policy
            if not row['admitted']:
                output = methods.refused(args, ir, label, config, options=options, admitted=0)
                nested_map_preserved(ir.read_text(), output.read_text(), label)
                if 'ctnative.host_owner_proved = false' not in output.read_text():
                    raise RuntimeError(f'{label}: unproved nested Map acquired an owner')
            else:
                output = owned.lower(args, ir, label, config, options=options)
                text = methods.census(output, functions, label, admitted=functions)
                if 'ctnative.host_owner_proved = true' not in text:
                    raise RuntimeError(f'{label}: lost captured child ownership')
                if policy == 'default':
                    default = output
                elif output.read_text() != default.read_text():
                    raise RuntimeError(f'{label}: child proof depends on optimization policy')
            forged = args.work / f'{label}-forged.mlir'
            forged.write_text(forge_leaf_evidence(ir.read_text()))
            stale = methods.refused(args, forged, label + '-stale', config,
                options=options, reason='fingerprint mismatch', admitted=0)
            nested_map_preserved(forged.read_text(), stale.read_text(), label)
            fresh = contract(args, forged, label + '-forged')
            checked = owned.lower(args, forged, label + '-fresh', fresh,
                                  options=options, cleanup=row['admitted'])
            methods.census(checked, functions, label, admitted=functions if row['admitted'] else 0)
            if not row['admitted']:
                nested_map_preserved(forged.read_text(), checked.read_text(), label)
                if 'ctnative.host_owner_proved = false' not in checked.read_text():
                    raise RuntimeError(f'{label}: forged presence acquired nested ownership')
            elif comparable_provenance(host.run([args.translate, '--mlir-to-cpp', str(checked)]).stdout,
                                       forged) != comparable_provenance(
                    host.run([args.translate, '--mlir-to-cpp', str(output)]).stdout, ir):
                raise RuntimeError(f'{label}: forged leaf/presence facts changed nested C++')
        if not row['admitted']:
            return
        deduced = args.work / f'{name}.deduced.mlir'
        host.run([args.opt, str(default), '--ctnative-print-deduced', '-o', str(deduced)])
        for mode, native in (('explicit', default), ('deduced', deduced)):
            cpp = host.run([args.translate, '--mlir-to-cpp', str(native)]).stdout
            if owned.VM.search(cpp) or 'std::function<js_num(js_num)>' not in cpp:
                raise RuntimeError(f'{name}/{mode}: lost typed standalone nested Map output')
            generated = args.work / f'{name}.{mode}.cpp'
            generated.write_text(cpp)
            source = args.work / f'{name}.{mode}.lifetime.cpp'
            source.write_text(nested_map_lifetime_cpp(cpp, row))
            for index, compiler in enumerate(compilers):
                binary = source.with_suffix(f'.{index}').resolve()
                host.run([compiler, *owned.FLAGS, str(source), '-o', str(binary)])
                if (owned.VM.search(host.run([nm, '-C', str(binary)]).stdout)
                        or host.run([str(binary)]).stdout != 'trace=41\n' * 2):
                    raise RuntimeError(f'{name}/{mode}: standalone child lifetime mismatch')
            binary = source.with_suffix('.sanitized').resolve()
            host.run([compilers[1], *owned.FLAGS, '-O1', '-g', '-fno-omit-frame-pointer',
                '-fsanitize=address,undefined', '-fsanitize-address-use-after-scope',
                str(source), '-o', str(binary)])
            result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=60,
                env={**os.environ, 'ASAN_OPTIONS': 'detect_stack_use_after_return=1:detect_leaks=1',
                     'UBSAN_OPTIONS': 'halt_on_error=1:print_stacktrace=1'})
            if result.returncode or result.stdout != 'trace=41\n' * 2 or result.stderr:
                raise RuntimeError(f'{name}/{mode}: sanitized child lifetime failed\n'
                                   f'{result.returncode}: {result.stdout}{result.stderr}')
        if name in {'nested_map_retained', 'nested_map_distinct_saved'}:
            check_budgets(args, ir, config, name, functions=functions)

    with ThreadPoolExecutor(max_workers=args.jobs) as executor:
        list(executor.map(check_case, cases.items()))
    positives = sum(row['admitted'] for row in cases.values())
    print(f'nested Maps: {positives} native programs, {len(cases) - positives} refusals, '
          f'{observations} typed observations, {mutations} distinguishing mutations')
