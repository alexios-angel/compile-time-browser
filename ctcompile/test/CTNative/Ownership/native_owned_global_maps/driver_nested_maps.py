"""Captured outer Maps retain fresh and reused child Map owners."""

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

    def add(name, source, calls, *, admitted=False, functions=4, children=1, retained=False,
            reused=False, separate=False, repeated=False):
        rows['nested_map_' + name] = dict(source=source, functions=functions, calls=calls,
            sha256=hashlib.sha256(source.encode()).hexdigest(), admitted=admitted,
            children=children, retained=retained, reused=reused, separate=separate, repeated=repeated,
            expected_trace=41)

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
        "child.set('value', value);", "saved.set('value', value);"), 8,
        admitted=True, retained=True, reused=True)
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
''', 9, functions=5, admitted=True, retained=True, separate=True)
    for name, digest in {
        'conditional_initialize': '74539aebaf2ec85ee44e45f9ea5fc4c7e2b37523bb0224507d6cdd171acffe26',
        'cross_invocation': '369d7ceafb8d173d004395715e833aa9ec9f94e494c742e3ddab6a405d735a28',
    }.items():
        if rows['nested_map_' + name]['sha256'] != digest:
            raise RuntimeError(f'{name}: changed the historical child Map source')
    conditional = rows['nested_map_conditional_initialize']['source']
    add('repeated_lookup', conditional.replace("        return saved.get('value');",
        "        const again = t.get(1);\n        return again.get('value');"), 9,
        admitted=True, retained=True, reused=True, repeated=True)
    mixed = "mix(value) { t.set(1, value); return value; }"
    add('conditional_mixed_before', conditional.replace('return { get(value) {',
        'return { ' + mixed + ', get(value) {') + 'host.slot.mix(0);\n', 10, functions=5)
    add('conditional_mixed_after', conditional.replace('    } };',
        '    }, ' + mixed + ' };') + 'host.slot.mix(0);\n', 10, functions=5)
    add('conditional_unknown_contents', conditional.replace("        saved.set('value', value);",
        "        const prior = saved.get('value');\n        saved.set('value', value);").replace(
        "return saved.get('value');", "return prior === void 0 ? saved.get('value') : prior;"), 9)
    add('conditional_alias_delete', conditional.replace("        return saved.get('value');",
        "        t.has(2) || t.set(2, new Map);\n        const other = t.get(2);\n"
        "        other.delete('value');\n        return saved.get('value');"), 12, children=2)
    cross = rows['nested_map_cross_invocation']['source']
    inverted = cross.replace("if (t.has(1)) { return t.get(1).get('value'); } return 0;",
        "if (!t.has(1)) { return 0; } return t.get(1).get('value');")
    add('cross_inverted_guard', inverted, 9, functions=5, admitted=True, retained=True,
        separate=True)
    add('cross_inverted_wrong_key', inverted.replace('!t.has(1)', '!t.has(2)'), 9,
        functions=5)
    rows['nested_map_cross_inverted_wrong_key']['expected_trace'] = 0
    for name, mutation, calls in (
        ('unseeded', "t.set(1, new Map);", 11),
        ('delete', "if (t.has(1)) { t.get(1).delete('value'); }", 13),
        ('clear', "if (t.has(1)) { t.get(1).clear(); }", 13),
        ('mixed', "if (t.has(1)) { t.get(1).set('value', true); }", 13),
    ):
        sibling = 'poison() { ' + mutation + ' return 0; }'
        for order in ('before', 'after'):
            source = (cross.replace('    return {\n', '    return {\n        ' + sibling + ',\n')
                      if order == 'before' else cross.replace('\n    };', ',\n        ' + sibling + '\n    };'))
            source = source.replace('host.slot.set(41);', 'host.slot.poison();\nhost.slot.set(41);')
            add('cross_' + name + '_' + order, source, calls, functions=6)
            rows['nested_map_cross_' + name + '_' + order]['poison'] = (
                mutation, 'true' if name == 'mixed' else 'undefined')
    return rows


def nested_map_observer(source, row):
    observed = source + '''
(function() {
    const get = host.slot.get;
    host = {};
    const seen = [], original = Map.prototype.METHOD;
    Map.prototype.METHOD = function(key, value) {
        const result = original.call(this, key, value);
        if (ITEM instanceof Map) { seen.push([this, key, ITEM]); }
        return result;
    };
    let ok = get(17) === 17 && get(-3) === -3;
    Map.prototype.METHOD = original;
    const stride = STRIDE;
    ok = ok && seen.length === stride * 2 && seen[0][0] === seen[stride][0] &&
         seen[0][2] IDENTITY seen[stride][2] && seen[0][2].get('value') === FIRST_VALUE &&
         seen[stride][2].get('value') === -3 && seen[0][0].size === OUTER_SIZE;
    DISTINCT
    for (let i = 0; i < 128; ++i) {
        const value = i % 2 === 0 ? -i : i + 0.5;
        const answer = get(value);
        if (typeof answer !== 'number' || answer !== value) { ok = false; }
        REUSE_CHECK
    }
    RECREATE
    trace = ok ? 1 : 0;
})();
'''
    distinct = '' if row['children'] == 1 else '''ok = ok && seen[0][2] !== seen[1][2] &&
        seen[1][2] === seen[2][2] && seen[stride + 1][2] === seen[stride + 2][2];'''
    if row['repeated']:
        distinct = 'ok = ok && seen[0][2] === seen[1][2] && seen[2][2] === seen[3][2];'
    reused = row['reused']
    recreate = '''const outer = seen[0][0], saved = seen[0][2];
    outer.clear(); saved.set('value', 47);
    ok = ok && get(19) === 19 && outer.size === 1 && outer.get(1) !== saved &&
         saved.get('value') === 47;''' if reused else ''
    if row['separate']:
        observed = observed.replace('const get = host.slot.get;',
            'const set = host.slot.set, get = host.slot.get;').replace(
            'get(17) === 17 && get(-3) === -3',
            'set(17) === 17 && get() === 17 && set(-3) === -3 && get() === -3').replace(
            'const answer = get(value);', 'const answer = set(value) === value ? get() : NaN;')
        recreate = '''const outer = seen[0][0], saved = seen[0][2];
    outer.clear(); saved.set('value', 47);
    ok = ok && get() === 0 && set(19) === 19 && get() === 19 && outer.size === 1 &&
         outer.get(1) !== saved && saved.get('value') === 47;'''
    stride = 2 if row['repeated'] else 1 if row['children'] == 1 else 3
    return observed.replace('STRIDE', str(stride)).replace(
        'OUTER_SIZE', '1' if row['retained'] else '0').replace('DISTINCT', distinct).replace(
        'METHOD', 'get' if reused else 'set').replace('ITEM', 'result' if reused else 'value').replace(
        'IDENTITY', '===' if reused else '!==').replace('FIRST_VALUE', '-3' if reused else '17').replace(
        'REUSE_CHECK', "if (seen[0][2].get('value') !== value) { ok = false; }" if reused else '').replace(
        'RECREATE', recreate)


def nested_map_lifetime_cpp(cpp, row):
    # Reuse the existing weak allocation observer; generated ownership is unchanged.
    changed = instrument_leaf_objects(cpp, allocations=0) + r'''
int main() {
    using Child = ctnative::number_map<std::string>;
    using Outer = ctnative::map_storage<js_num, std::shared_ptr<Child>>;
    constexpr std::size_t children = CHILDREN;
    constexpr bool retained = RETAINED;
    constexpr bool reused = REUSED;
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
        if (get(value) != value ||
            ctn_test_maps.size() != before + (reused ? 0U : children)) { return 274; }
        for (std::size_t index = before; index < ctn_test_maps.size(); ++index) {
            if (ctn_test_maps[index].expired() == retained) { return 275; }
        }
        if constexpr (retained) {
            auto child = outer->at(js_num{1});
            if ((reused ? child != saved : child == saved) || child != ctn_test_maps.back().lock() ||
                child->at("value") != value || saved->at("value") != (reused ? value : 41)) {
                return 276;
            }
        }
    }
    ctnative::map_clear(outer);
    if constexpr (retained) {
        if (outer->size() != 0U || saved_lifetime.expired() ||
            (!reused && !ctn_test_maps.back().expired())) { return 277; }
        ctnative::map_set(saved, std::string{"value"}, js_num{47});
        if (ctnative::map_get_present(saved, std::string{"value"}) != 47) { return 278; }
        saved.reset();
        if (!saved_lifetime.expired()) { return 279; }
    }
    const auto cleared = ctn_test_maps.size();
    if (get(19) != 19 || ctn_test_maps.size() != cleared + children) { return 280; }
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
    if row['separate']:
        changed = changed.replace('auto get = table->m_get;',
            'auto get = table->m_get;\n    auto set = table->m_set;').replace(
            'static_assert(std::is_same_v<decltype(get), std::function<js_num(js_num)>>);',
            'static_assert(std::is_same_v<decltype(get), std::function<js_num()>>);\n'
            '    static_assert(std::is_same_v<decltype(set), std::function<js_num(js_num)>>);').replace(
            'get(value) != value', 'set(value) != value || get() != value').replace(
            'const auto cleared = ctn_test_maps.size();',
            'if (get() != 0) { return 285; }\n    const auto cleared = ctn_test_maps.size();').replace(
            'get(19) != 19', 'set(19) != 19 || get() != 19').replace(
            'get(23) != 23', 'set(23) != 23 || get() != 23').replace(
            'get = {};', 'get = {};\n    if (ctn_test_maps[0].expired() || set(29) != 29) '
            '{ return 286; }\n    set = {};')
    return changed.replace('CHILDREN', str(row['children'])).replace(
        'RETAINED', str(row['retained']).lower()).replace('REUSED', str(row['reused']).lower())


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
        if 'poison' in row:
            mutation, expected = row['poison']
            assert row['source'].count(mutation) == 1, (name, mutation)
            observer = ('\nhost.slot.set(73); host.slot.poison();\n'
                        'trace = host.slot.get() === ' + expected + ' ? 1 : 0;\n')
            observe(name + '-poisoned', row['source'] + observer, 1)
            observe(name + '-mutation-removed', row['source'].replace(mutation, '') + observer, 0)
            observations += 2
            mutations += 1
        if not row['admitted']:
            continue
        observed = nested_map_observer(row['source'], row)
        observe(name + '-future', observed, 1)
        observations += 1
        readback = "again.get('value')" if row['repeated'] else "saved.get('value')"
        replacements = [("t.get(1).get('value')" if row['separate'] else readback, '41'),
                        (".set('value', value);", ".set('value', 0);")]
        if row['children'] == 2:
            replacements.append(('second = new Map;', 'second = first;'))
        if row['retained']:
            replacements.append(('t.set(1, child);', 't.set(1, child); t.clear();') if row['separate']
                                else ('return ' + readback, 't.clear(); return ' + readback))
        if row['separate']:
            replacements.append(('return value;', 'return 0;'))
        if row['reused']:
            replacements.append(('t.has(1) || t.set(1, new Map);', 't.set(1, new Map);'))
        if row['repeated']:
            replacements.append(('const again = t.get(1);',
                                 't.set(1, new Map); const again = t.get(1);'))
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
        if name in {'nested_map_retained', 'nested_map_distinct_saved',
                    'nested_map_conditional_initialize', 'nested_map_cross_invocation',
                    'nested_map_repeated_lookup', 'nested_map_cross_inverted_guard'}:
            check_budgets(args, ir, config, name, functions=functions)

    with ThreadPoolExecutor(max_workers=args.jobs) as executor:
        list(executor.map(check_case, cases.items()))
    positives = sum(row['admitted'] for row in cases.values())
    print(f'nested Maps: {positives} native programs, {len(cases) - positives} refusals, '
          f'{observations} typed observations, {mutations} distinguishing mutations')
