#!/usr/bin/env python3
"""Check Symbol-only native exports against Node/VM without a DOM input or runtime."""

import argparse
import json
from pathlib import Path
import re
import shutil

from CTNative.Browser import native_dom as dom
from CTNative.HostContract.contract import fingerprint
from CTNative.harness import find_compilers, run
from Target.Cpp.harness import FLAGS

STATE = """function state() {
  let key = Symbol.iterator;
  const saved = key;
  for (let i = 0; i < 3; i++) {
    key = key === Symbol.iterator ? Symbol.hasInstance : Symbol.iterator;
  }
  return saved === Symbol.iterator ? key : Symbol.toStringTag;
}
"""
OBSERVE = """function observe() {
  const key = Symbol.hasInstance;
  return typeof key +
    (key === Symbol.hasInstance ? ':same' : ':wrong') +
    (key !== Symbol.iterator ? ':different' : ':wrong') +
    (key == Symbol['hasInstance'] ? ':loose' : ':wrong') +
    (key ? ':truthy' : ':wrong') +
    (!key ? ':wrong' : ':false');
}
"""
TRANSCRIPT = "symbol:same:different:loose:truthy:false"
CONSTRUCT = """function construct() {
  let description = 'same';
  const key = Symbol(description);
  description = 'changed';
  return key;
}
"""
METHODS = r"""function methods() {
  const make = Symbol;
  const first = make('same');
  const saved = first.valueOf();
  let key = first;
  for (let i = 0; i < 3; i++) key = Symbol('same');
  const absent = Symbol();
  const explicit = Symbol(undefined);
  return (first === saved ? 'copy' : 'wrong') +
    (first !== Symbol('same') ? ':fresh' : ':wrong') +
    (first != key ? ':loop' : ':wrong') +
    (key == key.valueOf() ? ':value' : ':wrong') +
    (absent !== explicit ? ':absent' : ':wrong') +
    (absent.toString() === 'Symbol()' ? ':text' : ':wrong') +
    (explicit.toString() === 'Symbol()' ? ':undefined' : ':wrong') +
    (Symbol('').toString() === 'Symbol()' ? ':empty' : ':wrong') +
    (Symbol('a\u0000b').toString() === 'Symbol(a\u0000b)' ? ':nul' : ':wrong') +
    (Symbol('\ud800\u00e9').toString() === 'Symbol(\ud800\u00e9)' ? ':unicode' : ':wrong') +
    (Symbol.iterator.valueOf() === Symbol.iterator ? ':known' : ':wrong') +
    (Symbol('Symbol.iterator') !== Symbol.iterator ? ':distinct' : ':wrong') +
    (first ? ':truthy' : ':wrong') +
    (typeof first === 'symbol' ? ':type' : ':wrong');
}
"""
METHOD_TRANSCRIPT = (
    "copy:fresh:loop:value:absent:text:undefined:empty:nul:unicode:known:distinct:truthy:type"
)
DESCRIPTIONS = r"""function descriptions() {
  let key = Symbol('saved');
  const saved = key.description;
  key = Symbol('changed');
  const absent = Symbol().description;
  const explicit = Symbol(undefined).description;
  const empty = Symbol('').description;
  let state = absent;
  for (let i = 0; i < 3; i++) state = Symbol('last').description;
  const joined = absent ? 'wrong' : saved;
  const missing = saved ? absent : 'wrong';
  return (absent === undefined ? 'absent' : 'wrong') +
    (explicit === undefined ? ':undefined' : ':wrong') +
    (typeof absent === 'undefined' ? ':type' : ':wrong') +
    (empty === '' ? ':empty' : ':wrong') +
    (typeof empty === 'string' ? ':empty-type' : ':wrong') +
    (empty ? ':wrong' : ':empty-false') +
    (!absent ? ':false' : ':wrong') +
    (saved === 'saved' ? ':snapshot' : ':wrong') +
    (key.description === 'changed' ? ':changed' : ':wrong') +
    (joined === saved ? ':join' : ':wrong') +
    (missing === undefined ? ':missing-join' : ':wrong') +
    (state === 'last' ? ':loop' : ':wrong') +
    (Symbol.iterator.description === 'Symbol.iterator' ? ':known' : ':wrong') +
    (Symbol('a\u0000b').description === 'a\u0000b' ? ':nul' : ':wrong') +
    (Symbol('\ud800\u00e9').description === '\ud800\u00e9' ? ':unicode' : ':wrong') +
    (absent == explicit ? ':loose' : ':wrong') +
    (absent !== null ? ':not-null' : ':wrong');
}
"""
DESCRIPTION_TRANSCRIPT = (
    "absent:undefined:type:empty:empty-type:empty-false:false:snapshot:changed:join:"
    "missing-join:loop:known:nul:unicode:loose:not-null"
)
PARAMETER_STATE = """function parameterState(first, second, text, count, enabled) {
  let key = first;
  for (let i = 0; i < count; i++) {
    key = key === first ? second : first;
  }
  return enabled ? Symbol(text) : key;
}
"""
PARAMETER_DESCRIPTION = "function parameterDescription(key) { return key.description; }\n"
PARAMETER_TEXT = "function parameterText(text) { return text; }\n"
PARAMETER_NUMBER = "function parameterNumber(value) { return value; }\n"
PARAMETER_BOOLEAN = "function parameterBoolean(value) { return value; }\n"
HELPER_STATE = """function helperState(first, second, text, count, enabled) {
  function select(left, right, takeRight) {
    function copy(value) { return value; }
    return takeRight ? copy(right) : copy(left);
  }
  function cycle(left, right, limit) {
    let key = left;
    for (let i = 0; i < limit; i++) key = key === left ? right : left;
    return key;
  }
  function make(description) { return Symbol(description); }
  const key = cycle(first, second, count);
  return enabled ? make(text) : select(key, first, false);
}
"""
HELPER_DESCRIPTION = """function helperDescription(key) {
  function describe(value) { return value.description; }
  return describe(key);
}
"""
HELPER_UNDEFINED = """function helperUndefined(key) {
  function missing(value) { return value; }
  return missing();
}
"""
HELPER_TYPES = """function helperTypes(key, text, number, flag) {
  function copy(value) { return value; }
  function first(left, right) { return left; }
  let after = text;
  const before = first(after, after = 'changed');
  return (copy(key) === key ? 'symbol' : 'wrong') +
    (copy(text) === text ? ':string' : ':wrong') +
    (copy(number) < 1 ? ':number' : ':wrong') +
    (copy(flag) ? ':wrong' : ':boolean') +
    (before === text && after === 'changed' ? ':evaluation' : ':wrong');
}
"""
PARAMETER_TYPES = {
    "parameter-state": ["symbol", "symbol", "string", "number", "boolean"],
    "parameter-description": ["symbol"],
    "parameter-text": ["string"],
    "parameter-number": ["number"],
    "parameter-boolean": ["boolean"],
    "parameters": ["symbol"],
    "helper-state": ["symbol", "symbol", "string", "number", "boolean"],
    "helper-description": ["symbol"],
    "helper-undefined": ["symbol"],
    "helper-types": ["symbol", "string", "number", "boolean"],
}
CASES = {
    "state": (
        STATE,
        """
    static_assert(std::is_same_v<decltype(@ENTRY@()), ctnative::js_symbol_t>);
    const auto saved = @ENTRY@();
    assert(saved == ctnative::Symbol.hasInstance);
    assert(@ENTRY@() == saved);
    assert(saved != ctnative::Symbol.iterator);
    std::cout << "true\\n";
""",
        "true\n",
    ),
    "observe": (
        OBSERVE,
        f"""
    static_assert(std::is_same_v<decltype(@ENTRY@()), ctnative::js_string>);
    const auto saved = @ENTRY@();
    assert(saved.value() == "{TRANSCRIPT}");
    assert(@ENTRY@() == saved);
    std::cout << saved.value() << '\\n';
""",
        TRANSCRIPT + "\n",
    ),
    "construct": (
        CONSTRUCT,
        """
    static_assert(std::is_same_v<decltype(@ENTRY@()), ctnative::js_symbol_t>);
    const auto first = @ENTRY@();
    const auto second = @ENTRY@();
    assert(first != second);
    assert(first.valueOf() == first);
    assert(first.toString().value() == "Symbol(same)");
    std::cout << "true\\n";
""",
        "true\n",
    ),
    "methods": (
        METHODS,
        f"""
    const auto saved = @ENTRY@();
    assert(saved.value() == "{METHOD_TRANSCRIPT}");
    assert(@ENTRY@() == saved);
    std::cout << saved.value() << '\\n';
""",
        METHOD_TRANSCRIPT + "\n",
    ),
    "descriptions": (
        DESCRIPTIONS,
        f"""
    const auto saved = @ENTRY@();
    assert(saved.value() == "{DESCRIPTION_TRANSCRIPT}");
    assert(@ENTRY@() == saved);
    std::cout << saved.value() << '\\n';
""",
        DESCRIPTION_TRANSCRIPT + "\n",
    ),
    "absent-description": (
        "function absentDescription() { return Symbol().description; }\n",
        """
    static_assert(std::is_same_v<decltype(@ENTRY@()), ctnative::nullable_string>);
    const auto saved = @ENTRY@();
    assert(saved.tag == ctnative::nullable_string::kind::undefined);
    std::cout << "true\\n";
""",
        "true\n",
    ),
    # The former refused source body is unchanged, including its direct return.
    "description": (
        "function bad() { return Symbol.iterator.description; }\n",
        """
    static_assert(std::is_same_v<decltype(@ENTRY@()), ctnative::nullable_string>);
    const auto saved = @ENTRY@();
    assert(saved.tag == ctnative::nullable_string::kind::string);
    assert(saved.value == "Symbol.iterator");
    std::cout << "true\\n";
""",
        "true\n",
    ),
    "parameter-state": (
        PARAMETER_STATE,
        r"""
    using namespace ctnative;
    static_assert(std::is_same_v<decltype(&@ENTRY@),
        js_symbol_t (*)(js_symbol_t, js_symbol_t, js_string, ctnative::js_num, js_boolean_t)>);
    const auto first = Symbol(js_string{"same"});
    const auto second = Symbol(js_string{"same"});
    const auto known = Symbol.iterator;
    js_string text{"a\0b"};
    assert(@ENTRY@(first, second, text, ctnative::js_num{0.0}, js_boolean_t{false}) == first);
    assert(@ENTRY@(first, second, text, ctnative::js_num{1.0}, js_boolean_t{false}) == second);
    assert(@ENTRY@(first, second, text, ctnative::js_num{2.0}, js_boolean_t{false}) == first);
    assert(@ENTRY@(known, first, text, ctnative::js_num{3.0}, js_boolean_t{false}) == first);
    assert(@ENTRY@(first, first, text, ctnative::js_num{3.0}, js_boolean_t{false}) == first);
    assert(@ENTRY@(first, second, text, ctnative::js_num{-1.0}, js_boolean_t{false}) == first);
    assert(@ENTRY@(first, second, text, ctnative::js_num{ctnative::js_nan_t{}}, js_boolean_t{false}) == first);
    const auto made = @ENTRY@(first, second, text, ctnative::js_num{1.0}, js_boolean_t{true});
    assert(made != first && made != second);
    assert(made != @ENTRY@(first, second, text, ctnative::js_num{1.0}, js_boolean_t{true}));
    text = js_string{"changed"};
    assert(made.description() == js_string{"a\0b"});
    assert(first.description() == js_string{"same"});
    std::cout << "true\n";
""",
        "true\n",
    ),
    "parameter-description": (
        PARAMETER_DESCRIPTION,
        r"""
    using namespace ctnative;
    static_assert(std::is_same_v<decltype(&@ENTRY@), nullable_string (*)(js_symbol_t)>);
    assert(@ENTRY@(Symbol()).tag == nullable_string::kind::undefined);
    const auto empty = @ENTRY@(Symbol(js_string{""}));
    assert(empty.tag == nullable_string::kind::string && empty.value.empty());
    auto key = Symbol(js_string{"a\0b"});
    const auto saved = @ENTRY@(key);
    key = Symbol(js_string{"changed"});
    assert(saved.tag == nullable_string::kind::string && saved.value == std::string("a\0b", 3));
    assert(@ENTRY@(key).value == "changed");
    assert(@ENTRY@(Symbol.iterator).value == "Symbol.iterator");
    assert(@ENTRY@(Symbol(js_string{"\xed\xa0\x80"})).value == "\xed\xa0\x80");
    std::cout << "true\n";
""",
        "true\n",
    ),
    "parameter-text": (
        PARAMETER_TEXT,
        r"""
    using namespace ctnative;
    static_assert(std::is_same_v<decltype(&@ENTRY@), js_string (*)(js_string)>);
    js_string input{"a\0b"};
    const auto saved = @ENTRY@(input);
    input = js_string{"changed"};
    assert(saved.value() == std::string("a\0b", 3));
    assert(@ENTRY@(input).value() == "changed");
    assert(@ENTRY@(js_string{""}).value().empty());
    assert(@ENTRY@(js_string{"\xed\xa0\x80"}).value() == "\xed\xa0\x80");
    std::cout << "true\n";
""",
        "true\n",
    ),
    "parameter-number": (
        PARAMETER_NUMBER,
        r"""
    using namespace ctnative;
    static_assert(std::is_same_v<decltype(&@ENTRY@), ctnative::js_num (*)(ctnative::js_num)>);
    assert(@ENTRY@(ctnative::js_num{42.5}).value() == 42.5);
    assert(std::isnan(@ENTRY@(ctnative::js_num{ctnative::js_nan_t{}}).value()));
    assert(@ENTRY@(ctnative::js_num{std::numeric_limits<double>::infinity()}).value() == INFINITY);
    assert(@ENTRY@(ctnative::js_num{-std::numeric_limits<double>::infinity()}).value() == -INFINITY);
    assert(@ENTRY@(ctnative::js_num{-0.0}).value() == 0 && std::signbit(@ENTRY@(ctnative::js_num{-0.0}).value()));
    assert(!std::signbit(@ENTRY@(ctnative::js_num{0.0}).value()));
    std::cout << "true\n";
""",
        "true\n",
    ),
    "parameter-boolean": (
        PARAMETER_BOOLEAN,
        r"""
    using namespace ctnative;
    static_assert(std::is_same_v<decltype(&@ENTRY@), js_boolean_t (*)(js_boolean_t)>);
    assert(@ENTRY@(js_boolean_t{true}));
    assert(!@ENTRY@(js_boolean_t{false}));
    std::cout << "true\n";
""",
        "true\n",
    ),
    # The original parameter refusal source now has an explicit category contract.
    "parameters": (
        "function bad(value) { return Symbol.iterator; }",
        'assert(@ENTRY@(ctnative::Symbol()) == ctnative::Symbol.iterator); std::cout << "true\\n";',
        "true\n",
    ),
}
CASES["helper-state"] = (HELPER_STATE, *CASES["parameter-state"][1:])
CASES["helper-description"] = (HELPER_DESCRIPTION, *CASES["parameter-description"][1:])
CASES["helper-undefined"] = (
    HELPER_UNDEFINED,
    r"""
    static_assert(std::is_same_v<decltype(&@ENTRY@), void (*)(ctnative::js_symbol_t)>);
    @ENTRY@(ctnative::Symbol.iterator);
    std::cout << "true\n";
""",
    "true\n",
)
CASES["helper-types"] = (
    HELPER_TYPES,
    r"""
    static_assert(std::is_same_v<decltype(&@ENTRY@), ctnative::js_string (*)(
        ctnative::js_symbol_t, ctnative::js_string, ctnative::js_num, ctnative::js_boolean_t)>);
    const auto key = ctnative::Symbol();
    assert(@ENTRY@(key, ctnative::js_string{"a\0b"}, ctnative::js_num{-0.0},
                  ctnative::js_boolean_t{false}).value() == "symbol:string:number:boolean:evaluation");
    std::cout << "true\n";
""",
    "true\n",
)
# The complete former refused programs now execute unchanged.
for name, body, expected in (
    ("fresh", "return typeof Symbol();", "symbol"),
    ("method", "return Symbol.iterator.toString();", "Symbol(Symbol.iterator)"),
):
    CASES[name] = (
        f"function bad() {{ {body} }}\n",
        f'assert(@ENTRY@().value() == "{expected}"); std::cout << "true\\n";',
        "true\n",
    )
REFUSALS = {
    "global-replacement": "Symbol=0; return Symbol.iterator;",
    "member-replacement": "Symbol.hasInstance=Symbol.iterator; return false;",
    "alias-mutation": "const saved=Symbol; saved.hasInstance=Symbol.iterator; return false;",
    "escape-constructor": "return Symbol;",
    "constructor-typeof": "return typeof Symbol;",
    "dynamic-key": "return Symbol[typeof Symbol.iterator];",
    "unknown-key": "return Symbol.unknown;",
    "registry": "return typeof Symbol.for('x');",
    "description-mutation": "Symbol.prototype.description=0; return Symbol.iterator.description;",
    "description-own-write": "const key=Symbol(); key.description='changed'; return key.description;",
    "description-call": "return Symbol.iterator.description();",
    "description-key": "const key='description'; return Symbol.iterator[typeof key];",
    "description-mixed-join": "return Symbol.iterator ? Symbol().description : 1;",
    "description-dom-null-join": "return Symbol.iterator ? Symbol().description : null;",
    "new-symbol": "return new Symbol('x');",
    "number-description": "return Symbol(1);",
    "null-description": "return Symbol(null);",
    "symbol-description": "return Symbol(Symbol.iterator);",
    "object-description": "return Symbol({toString() { return 'x'; }});",
    "extra-description": "return Symbol('x', 'y');",
    "detached-method": "const method=Symbol.iterator.toString; return method();",
    "prototype-call": "return Symbol.prototype.valueOf.call(Symbol.iterator);",
    "method-arguments": "return Symbol.iterator.toString(1);",
    "method-mutation": "Symbol.prototype.toString=0; return Symbol.iterator.toString();",
    "value-mutation": "const key=Symbol(); key.toString=0; return key.toString();",
    "object": "return {};",
    "symbol-field": "const item={}; item[Symbol.iterator]=1; return false;",
    "number": "return +Symbol.iterator;",
    "concat": "return '' + Symbol.iterator;",
    "mixed-compare": "return Symbol.iterator === 'iterator';",
    "mixed-join": "return Symbol.iterator ? Symbol.iterator : 0;",
    "instanceof": "return Symbol.iterator instanceof Symbol;",
    "external-call": "unknown(); return Symbol.iterator;",
}
FORBIDDEN = re.compile(
    r"CTNATIVE_DOM|ctbrowser::(?:dom|script|aot)::|ctbrowser::(?:element_ref|document_ref)"
    r"|\bct_aot_|ctbrowser/(?:dom|script|aot)/"
)


def prepare(args, name, source, *, entry_name=None, parameter_types=None):
    ir, contract = dom.prepare(args, name, source, 0, entry_name=entry_name)
    del contract["element_parameters"]
    contract.update(provider="ctbrowser-intrinsics-v1", initial_intrinsics=["Symbol"])
    if parameter_types is not None:
        contract["parameter_types"] = parameter_types
    return ir, contract


def oracle(args):
    source = (
        STATE
        + OBSERVE
        + CONSTRUCT
        + METHODS
        + DESCRIPTIONS
        + PARAMETER_STATE
        + PARAMETER_DESCRIPTION
        + PARAMETER_TEXT
        + PARAMETER_NUMBER
        + PARAMETER_BOOLEAN
        + HELPER_STATE
        + HELPER_DESCRIPTION
        + HELPER_UNDEFINED
        + HELPER_TYPES
        + f"""
var symbol01State = state() === Symbol.hasInstance;
var symbol02Repeat = state() === Symbol.hasInstance;
var symbol03Observe = observe() === {json.dumps(TRANSCRIPT)};
var symbol04Fresh = construct() !== construct();
var symbol05Snapshot = construct().toString() === 'Symbol(same)';
var symbol06Methods = methods() === {json.dumps(METHOD_TRANSCRIPT)};
var symbol07FreshType = typeof Symbol() === 'symbol';
var symbol08KnownText = Symbol.iterator.toString() === 'Symbol(Symbol.iterator)';
var symbol09Descriptions = descriptions() === {json.dumps(DESCRIPTION_TRANSCRIPT)};
var symbol10AbsentDescription = Symbol().description === undefined;
var symbol11KnownDescription = Symbol.iterator.description === 'Symbol.iterator';
function observeParameters() {{
const firstInput = Symbol('same');
const secondInput = Symbol('same');
symbol12ParameterIdentity = parameterState(firstInput, secondInput, 'x', 0, false) === firstInput &&
  parameterState(firstInput, secondInput, 'x', 1, false) === secondInput &&
  parameterState(firstInput, secondInput, 'x', 2, false) === firstInput &&
  parameterState(Symbol.iterator, firstInput, 'x', 3, false) === firstInput &&
  parameterState(firstInput, firstInput, 'x', 3, false) === firstInput &&
  parameterState(firstInput, secondInput, 'x', -1, false) === firstInput &&
  parameterState(firstInput, secondInput, 'x', 0 / 0, false) === firstInput;
let textInput = 'a\\u0000b';
const createdInput = parameterState(firstInput, secondInput, textInput, 1, true);
symbol13ParameterFresh = createdInput !== firstInput && createdInput !== secondInput &&
  createdInput !== parameterState(firstInput, secondInput, textInput, 1, true);
textInput = 'changed';
symbol14ParameterSnapshot = createdInput.description === 'a\\u0000b' &&
  firstInput.description === 'same';
symbol15ParameterDescription = parameterDescription(Symbol()) === undefined &&
  parameterDescription(Symbol('')) === '' && parameterDescription(Symbol('a\\u0000b')) === 'a\\u0000b' &&
  parameterDescription(Symbol.iterator) === 'Symbol.iterator' &&
  parameterDescription(Symbol('\\ud800')) === '\\ud800';
symbol16ParameterText = parameterText('a\\u0000b') === 'a\\u0000b' &&
  parameterText('') === '' && parameterText('\\ud800') === '\\ud800';
const nanInput = parameterNumber(0 / 0);
symbol17ParameterNumber = parameterNumber(42.5) === 42.5 && nanInput !== nanInput &&
  parameterNumber(1 / 0) === 1 / 0 && parameterNumber(-1 / 0) === -1 / 0 &&
  1 / parameterNumber(-0) === -1 / 0 && 1 / parameterNumber(0) === 1 / 0;
symbol18ParameterBoolean = parameterBoolean(true) === true && parameterBoolean(false) === false;
}}
observeParameters();
function observeHelpers() {{
const key = Symbol('input');
const other = Symbol('input');
symbol19HelperState = helperState(key, other, 'fresh', 0, false) === key &&
  helperState(key, other, 'fresh', 1, false) === other &&
  helperState(key, other, 'fresh', 2, false) === key &&
  helperState(key, other, 'fresh', 1, true).description === 'fresh' &&
  helperState(key, other, 'fresh', 1, true) !== helperState(key, other, 'fresh', 1, true);
symbol20HelperDescription = helperDescription(Symbol()) === undefined &&
  helperDescription(Symbol('a\\u0000b')) === 'a\\u0000b' &&
  helperDescription(Symbol.iterator) === 'Symbol.iterator';
symbol21HelperTypes = helperTypes(key, 'a\\u0000b', -0, false) === 'symbol:string:number:boolean:evaluation';
symbol22HelperUndefined = helperUndefined(key) === undefined;
}}
observeHelpers();
"""
    )
    vm = args.work / "oracle.js"
    vm.write_text(source)
    observations = (
        "State",
        "Repeat",
        "Observe",
        "Fresh",
        "Snapshot",
        "Methods",
        "FreshType",
        "KnownText",
        "Descriptions",
        "AbsentDescription",
        "KnownDescription",
        "ParameterIdentity",
        "ParameterFresh",
        "ParameterSnapshot",
        "ParameterDescription",
        "ParameterText",
        "ParameterNumber",
        "ParameterBoolean",
        "HelperState",
        "HelperDescription",
        "HelperTypes",
        "HelperUndefined",
    )
    expected = "".join(f"symbol{i:02}{name}=true\n" for i, name in enumerate(observations, 1))
    actual = run([args.reference, str(vm)]).stdout
    if actual != expected:
        raise RuntimeError(f"VM Symbol export observations differ: {actual}")
    node = args.work / "oracle.cjs"
    node.write_text(
        source
        + "".join(
            f"\nconsole.log('symbol{i:02}{name}=' + symbol{i:02}{name});"
            for i, name in enumerate(observations, 1)
        )
    )
    if run([args.node, str(node)]).stdout != expected:
        raise RuntimeError("Node Symbol export observations differ")


def standalone(args, native, name, checks, expected, compilers, includes, libraries):
    text = native.read_text()
    entries = dom.NATIVE.findall(text)
    if (
        len(entries) != 1
        or entries[0] == "main"
        or dom.FUNCTION.search(text)
        or "ctnative.not_native" in text
    ):
        raise RuntimeError(f"{name}: expected one typed native export without a launcher")
    deduced = args.work / f"{name}.deduced.mlir"
    run([args.opt, str(native), "--ctnative-print-deduced", "-o", str(deduced)])
    client = (
        "\n#include <cassert>\n#include <cmath>\n#include <limits>\n#include <iostream>\n#include <type_traits>\nint main() {\n"
        + checks.replace("@ENTRY@", entries[0])
        + "}\n"
    )
    for mode, ir in (("explicit", native), ("deduced", deduced)):
        cpp = run([args.translate, "--mlir-to-cpp", str(ir)]).stdout
        if FORBIDDEN.search(cpp) or re.search(r"\bmain\s*\(", cpp):
            raise RuntimeError(f"{name}/{mode}: intrinsic export acquired a DOM/VM dependency")
        source = args.work / f"{name}.{mode}.cpp"
        source.write_text(cpp + client)
        for index, compiler in enumerate(compilers):
            binary = args.work / f"{name}.{mode}.{index}"
            run([compiler, *FLAGS, *includes, str(source), *libraries, "-o", str(binary)])
            if FORBIDDEN.search(run([args.nm, "-C", str(binary)]).stdout):
                raise RuntimeError(f"{name}/{mode}: intrinsic binary links DOM/Script/AOT")
            if run([str(binary)]).stdout != expected:
                raise RuntimeError(f"{name}/{mode}: native Symbol observations differ")
            if name == "absent-description-False" and mode == "explicit" and index == 0:
                mutant, count = re.subn(r"ctnative::undefined_t\{\}", "ctnative::js_null_t{}", cpp)
                if not count:
                    raise RuntimeError("description absence mutation did not replace undefined")
                mutant_source = args.work / "null-description.cpp"
                mutant_binary = args.work / "null-description"
                mutant_source.write_text(mutant + client)
                run(
                    [
                        compiler,
                        *FLAGS,
                        *includes,
                        str(mutant_source),
                        *libraries,
                        "-o",
                        str(mutant_binary),
                    ]
                )
                if (
                    "saved.tag == ctnative::nullable_string::kind::undefined"
                    not in run([str(mutant_binary)], success=False).stderr
                ):
                    raise RuntimeError(
                        "description absence mutation failed for an unrelated reason"
                    )
            if name == "construct-False" and mode == "explicit" and index == 0:
                mutant, count = re.subn(r"\bctnative::Symbol\(", "symbol_mutant(", cpp)
                if count != 1:
                    raise RuntimeError("fresh identity mutation did not replace one construction")
                mutant_source = args.work / "shared-identity.cpp"
                mutant_binary = args.work / "shared-identity"
                mutant_source.write_text(
                    '#include "ctcompile/CTNative/Runtime/ctnative.hpp"\n'
                    "ctnative::js_symbol_t symbol_mutant(const ctnative::js_string & text) {\n"
                    "  static const auto shared = ctnative::Symbol(text); return shared;\n}\n"
                    + mutant
                    + client
                )
                run(
                    [
                        compiler,
                        *FLAGS,
                        *includes,
                        str(mutant_source),
                        *libraries,
                        "-o",
                        str(mutant_binary),
                    ]
                )
                if "first != second" not in run([str(mutant_binary)], success=False).stderr:
                    raise RuntimeError("fresh identity mutation failed for an unrelated reason")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("translate", "opt", "clang", "node", "reference"):
        parser.add_argument("--" + name, required=True)
    for name in ("build", "include", "work"):
        parser.add_argument("--" + name, type=Path, required=True)
    parser.add_argument("--nm", default=shutil.which("nm"))
    args = parser.parse_args()
    args.work = args.work.resolve()
    args.work.mkdir(parents=True, exist_ok=True)
    oracle(args)
    compilers = find_compilers()
    compilers[1] = args.clang
    includes, libraries = dom.link_options(args, core_only=True)
    accepted = {}
    for name, (source, checks, expected) in CASES.items():
        entry_name = (
            re.search(r"function (\w+)\(", source)[1] if name.startswith("helper-") else None
        )
        ir, contract = prepare(
            args, name, source, entry_name=entry_name, parameter_types=PARAMETER_TYPES.get(name)
        )
        accepted[name] = ir, contract
        for optimize in (False, True):
            output_name = f"{name}-{optimize}"
            native = dom.lower(args, ir, contract, output_name, optimize=optimize)
            standalone(args, native, output_name, checks, expected, compilers, includes, libraries)

    refusals = 0

    def refuse(ir, contract, name, **options):
        nonlocal refusals
        result = dom.lower(args, ir, contract, name, success=False, **options)
        refusals += 1
        return result

    for name, body in REFUSALS.items():
        ir, contract = prepare(args, name, f"function bad() {{ {body} }}\n", entry_name="bad")
        for optimize in (False, True):
            refuse(ir, contract, name + str(optimize), optimize=optimize)
    source_refusals = {
        "wrapper-effect": "var changed=1; function bad() { return Symbol.iterator; }",
        "wrapper-replacement": "Symbol=0; function bad() { return Symbol.iterator; }",
        "intrinsic-declaration": "function Symbol() { return 1; }",
        "helper": "function helper() { return Symbol.iterator; } function bad() { return helper(); }",
    }
    for name, source in source_refusals.items():
        entry = "Symbol" if name == "intrinsic-declaration" else "bad"
        ir, contract = prepare(args, name, source, entry_name=entry)
        refuse(ir, contract, name + "-refused")

    helper_refusals = {
        "capture": "function helper() { return key; } return helper();",
        "recursive": "function helper(value) { return helper(value); } return helper(key);",
        "write": "function helper(value) { saved=value; return value; } return helper(key);",
        "unknown": "function helper(value) { unknown(); return value; } return helper(key);",
        "discarded-argument": "function helper(value) { return Symbol.iterator; } return helper(unknown());",
        "mixed-return": "function helper(value) { return value ? value : 1; } return helper(key);",
        "missing-argument": "function helper(value) { return value.description; } return helper();",
        "extra-argument": "function helper(value) { return value; } return helper(key, key);",
        "escape": "function helper(value) { return value; } return helper;",
        "indirect": "function helper(value) { return value(); } return helper(key);",
        "receiver": "function helper(value) { return this; } return helper(key);",
        "object-field": "function helper(value) { const item={key:value}; return item.key; } return helper(key);",
        "dead-effect": "function helper(value) { if (false) unknown(); return value; } return helper(key);",
        "unused-effect": "function helper(value) { unknown(); return value; } return key;",
        "shadowed-intrinsic": "function helper(Symbol) { return Symbol.iterator; } return helper(key);",
    }
    for name, body in helper_refusals.items():
        ir, contract = prepare(
            args,
            "helper-" + name,
            f"function bad(key) {{ {body} }}\n",
            entry_name="bad",
            parameter_types=["symbol"],
        )
        for optimize in (False, True):
            refuse(ir, contract, f"helper-{name}-{optimize}", optimize=optimize)
    ir, contract = accepted["helper-state"]
    for budget in (0, 1, 100, 500):
        refuse(ir, contract, f"helper-budget-{budget}", max_steps=budget)

    ir, contract = accepted["state"]
    refuse(ir, dict(contract, entry="_script_$0"), "script-entry")
    for name, fields in {
        "empty": {"initial_intrinsics": []},
        "extra": {"initial_intrinsics": ["Symbol", "Number"]},
        "duplicate": {"initial_intrinsics": ["Symbol", "Symbol"]},
        "roots": {"roots": []},
        "elements": {"element_parameters": []},
        "datasets": {"dataset_parameters": []},
        "realm": {"realm_global_this": True},
        "parameter-object": {"parameter_types": ["object"]},
        "parameter-union": {"parameter_types": [["symbol", "string"]]},
        "parameter-unknown": {"parameter_types": ["Symbol"]},
        "parameter-undefined": {"parameter_types": ["undefined"]},
        "parameter-null": {"parameter_types": ["null"]},
        "parameter-boolean-value": {"parameter_types": [True]},
        "parameter-no-array": {"parameter_types": "symbol"},
    }.items():
        refuse(ir, dict(contract, **fields), "schema-" + name)
    missing = dict(contract)
    del missing["initial_intrinsics"]
    refuse(ir, missing, "schema-missing")
    for budget in (0, 1):
        refuse(ir, contract, f"budget-{budget}", max_steps=budget)

    stale = args.work / "stale.mlir"
    changed = ir.read_text().replace('"hasInstance"', '"iterator"', 1)
    if changed == ir.read_text():
        raise RuntimeError("stale fingerprint control did not mutate an identity")
    stale.write_text(changed)
    if "fingerprint mismatch" not in refuse(stale, contract, "stale"):
        raise RuntimeError("changed Symbol identity accepted a stale fingerprint")

    forged, manifest = prepare(args, "forged", "function bad() { return typeof Symbol.for('x'); }")
    text, count = re.subn(
        r"\bmodule( attributes)? \{",
        lambda match: 'module attributes {ctnative.host_proved = true, ctnative.host_reason = ""'
        + (", " if match[1] else "} {"),
        forged.read_text(),
        count=1,
    )
    if count != 1:
        raise RuntimeError("forged metadata control was not installed")
    forged.write_text(text)
    manifest["module_sha256"] = fingerprint(args.opt, forged)
    refuse(forged, manifest, "forged-refused")
    ir, contract = accepted["parameter-state"]
    missing = dict(contract)
    del missing["parameter_types"]
    refuse(ir, missing, "parameter-missing-types")
    for name, types in {
        "short": contract["parameter_types"][:-1],
        "long": contract["parameter_types"] + ["boolean"],
        "reordered": ["string", "symbol", "symbol", "number", "boolean"],
        "wrong-loop": ["symbol", "symbol", "string", "symbol", "boolean"],
    }.items():
        refuse(ir, dict(contract, parameter_types=types), "parameter-" + name)
    refuse(ir, contract, "parameter-zero-budget", max_steps=0)
    for name, body in {
        "coercion": "return +key;",
        "mutation": "key.toString=0; return key;",
        "publication": "saved=key; return key;",
        "call": "return key();",
        "browser": "return key.classList;",
    }.items():
        ir, manifest = prepare(
            args,
            "parameter-" + name,
            f"function bad(key) {{ {body} }}\n",
            parameter_types=["symbol"],
        )
        for optimize in (False, True):
            refuse(ir, manifest, f"parameter-{name}-{optimize}", optimize=optimize)
    ir, manifest = prepare(
        args,
        "parameter-shadow",
        "function bad(Symbol) { return Symbol.iterator; }\n",
        parameter_types=["symbol"],
    )
    refuse(ir, manifest, "parameter-shadow-refused")
    print(
        f"Symbol exports: typed parameters/helpers, branch/loop return and primitive transcript agree with Node/VM; "
        f"{8 * len(CASES)} native executions, {refusals} refusals, 2 mutations; Core only, no DOM inputs"
    )


if __name__ == "__main__":
    main()
