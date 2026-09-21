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
SCALAR_EQUALITY = """function scalarEquality(left, right, flag, other) {
  return (left === right ? '1' : '0') + (left == right ? '1' : '0') +
    (left !== right ? '1' : '0') + (left != right ? '1' : '0') +
    (flag === other ? '1' : '0') + (flag == other ? '1' : '0') +
    (flag !== other ? '1' : '0') + (flag != other ? '1' : '0') +
    (left === flag ? '1' : '0') + (left == flag ? '1' : '0') +
    (flag === left ? '1' : '0') + (flag == left ? '1' : '0') +
    (left !== undefined && flag !== undefined ? '1' : '0');
}
"""
# The same input tuples drive Node/VM and the typed native clients.
EQUALITY_INPUTS = (
    ("Zero", "0, -0, false, true", "0.0, -0.0, false, true", "1100001101011"),
    ("One", "1, 1, true, true", "1.0, 1.0, true, true", "1100110001011"),
    ("Unequal", "2, 3, true, false", "2.0, 3.0, true, false", "0011001100001"),
    ("NaN", "0 / 0, 0 / 0, false, false", "nan, nan, false, false", "0011110000001"),
    ("Infinity", "1 / 0, 1 / 0, true, true", "inf, inf, true, true", "1100110000001"),
    ("Opposite", "-1 / 0, 1 / 0, true, false", "-inf, inf, true, false", "0011001100001"),
)
HELPER_EQUALITY = """function helperEquality(number, flag, count) {
  function equal(left, right) { return left == right; }
  function strict(left, right) { return left === right; }
  let matched = flag;
  for (let i = 0; i < count; i++) matched = equal(number, matched);
  return strict(matched, flag) && !strict(number, flag);
}
"""
# Preserve the complete former global-helper refusal as its first positive.
GLOBAL_HELPER = "function helper() { return Symbol.iterator; } function bad() { return helper(); }"
GLOBAL_STATE = """function globalState(first, second, text, count, enabled) {
  const key = cycleGlobal(first, second, count);
  return enabled ? makeGlobal(text) : selectGlobal(key, first, false);
}
function selectGlobal(left, right, takeRight) {
  return takeRight ? copyGlobal(right) : copyGlobal(left);
}
function copyGlobal(value) { return value; }
function cycleGlobal(left, right, limit) {
  let key = left;
  for (let i = 0; i < limit; i++) key = key === left ? right : left;
  return key;
}
function makeGlobal(description) { return Symbol(description); }
"""
GLOBAL_TYPES = """function globalTypes(key, text, number, flag) {
  let after = text;
  const before = firstGlobal(after, after = 'changed');
  return (copyTypeGlobal(key) === key ? 'symbol' : 'wrong') +
    (copyTypeGlobal(text) === text ? ':string' : ':wrong') +
    (copyTypeGlobal(number) < 1 ? ':number' : ':wrong') +
    (copyTypeGlobal(flag) ? ':wrong' : ':boolean') +
    (before === text && after === 'changed' ? ':evaluation' : ':wrong');
}
function firstGlobal(left, right) { return left; }
function copyTypeGlobal(value) { return value; }
"""
GLOBAL_DESCRIPTION = """function globalDescription(key) { return describeGlobal(key); }
function describeGlobal(value) { return value.description; }
"""
GLOBAL_LOCAL_DECLARATION = """function globalLocalDeclaration() { return declaredGlobalLocal(); }
function declaredGlobalLocal() {
  function local() { return Symbol.iterator; }
  function unused(value) { return typeof value; }
  return local();
}
"""
GLOBAL_LOCAL_STATE = """function globalLocalState(first, second, text, count, enabled) {
  const key = cycleGlobalLocal(first, second, count);
  return enabled ? makeGlobalLocal(text) : selectGlobalLocal(key, first, false);
}
function selectGlobalLocal(left, right, takeRight) {
  function copy(value) { return value; }
  return takeRight ? copy(right) : copy(left);
}
function cycleGlobalLocal(left, right, limit) {
  function next(value, first, second) { return value === first ? second : first; }
  let key = left;
  for (let i = 0; i < limit; i++) key = next(key, left, right);
  return key;
}
function makeGlobalLocal(description) {
  function make(value) { return Symbol(value); }
  return make(description);
}
"""
GLOBAL_LOCAL_TYPES = """function globalLocalTypes(key, text, number, flag) {
  let after = text;
  const before = firstGlobalLocal(after, after = 'changed');
  return (copyTypeGlobalLocal(key) === key ? 'symbol' : 'wrong') +
    (copyTypeGlobalLocal(text) === text ? ':string' : ':wrong') +
    (copyTypeGlobalLocal(number) < 1 ? ':number' : ':wrong') +
    (copyTypeGlobalLocal(flag) ? ':wrong' : ':boolean') +
    (before === text && after === 'changed' ? ':evaluation' : ':wrong');
}
function firstGlobalLocal(left, right) {
  function first(a, b) { return a; }
  return first(left, right);
}
function copyTypeGlobalLocal(value) {
  function copy(input) { return input; }
  return copy(value);
}
"""
GLOBAL_LOCAL_DESCRIPTION = """function globalLocalDescription(key) { return describeGlobalLocal(key); }
function describeGlobalLocal(value) {
  function describe(input) { return input.description; }
  return describe(value);
}
"""
# Preserve the three complete former capture refusals as native witnesses.
CAPTURE_WITNESSES = {
    "local": "function bad(key) { function helper() { return key; } return helper(); }\n",
    "global": "function helper(value) { function nested() { return value; } return nested(); } function bad(key) { return helper(key); }",
    "global-local": "function bad(key) { return helper(key); }\nfunction helper(value) { function local() { return value; } return local(); }\n",
}
CAPTURE_SELECTED = """function captureState(first, second, text, count, enabled) {
  function cycle() {
    let key = first;
    for (let i = 0; i < count; i++) key = key === first ? second : first;
    return key;
  }
  function make() { return Symbol(text); }
  return enabled ? make() : cycle();
}
"""
# Conditional callee transport remains a separate refusal. Exercise the
# same captured inputs through exact calls and ordinary result branches.
CAPTURE_MUTABLE_RESULT = CAPTURE_SELECTED.replace(
    "  return enabled ? make() : cycle();",
    "  let key = cycle();\n  if (enabled) key = make();\n  return key;",
)
CAPTURE_STATE = CAPTURE_SELECTED.replace(
    "  return enabled ? make() : cycle();",
    "  const cycled = cycle();\n  const made = make();\n  return enabled ? made : cycled;",
)
CAPTURE_TYPES = """function captureTypes(key, text, number, flag) {
  const saved = text;
  function observe() {
    return (key === key.valueOf() ? 'symbol' : 'wrong') +
      (saved === text ? ':string' : ':wrong') +
      (number < 1 ? ':number' : ':wrong') +
      (flag ? ':wrong' : ':boolean');
  }
  function first(left, right) { return left; }
  let after = text;
  const result = first(observe(), after = 'changed');
  return result + (after === 'changed' ? ':evaluation' : ':wrong');
}
"""
CAPTURE_DESCRIPTION = """function captureDescription(key) {
  function outer() {
    function describe() { return key.description; }
    return describe();
  }
  return outer();
}
"""
DESCRIPTION_GUARDS = {
    "description-type-guard": """function descriptionTypeGuard(key) {
  const text = key.description;
  key = Symbol('changed');
  return typeof text === 'string' ? text.charAt(0) + text.slice(1) : ':absent';
}
""",
    "description-undefined-guard": """function descriptionUndefinedGuard(key) {
  const text = key.description;
  return (!(text === undefined) ? text.charAt(0) : '') +
    (undefined == text ? ':absent' : text.slice(1));
}
""",
    "description-capture-guard": """function descriptionCaptureGuard(key) {
  const text = key.description;
  function restore() {
    return 'undefined' === typeof text ? ':absent' : text.charAt(0) + text.slice(1);
  }
  return restore();
}
""",
}
STRING_METHODS = {
    "string-prefix": r"""function stringPrefix(text) {
  return (text.startsWith('bs') ? 'prefix' : 'other') +
    (text.startsWith('') ? ':empty' : ':wrong') +
    (text.startsWith('a\u0000') ? ':nul' : ':plain');
}
""",
    "description-lowercase": """function descriptionLowercase(key) {
  const text = key.description;
  key = Symbol('changed');
  return typeof text === 'string' ? text.charAt(0).toLowerCase() + text.slice(1) : ':absent';
}
""",
    "description-capture-prefix": """function descriptionCapturePrefix(key) {
  const text = key.description;
  key = Symbol('changed');
  function check() { return text !== undefined ? text.startsWith('bs') : false; }
  return check();
}
""",
}
STRING_INDICES = {
    "string-indices": """function stringIndices(text, unit, tail) {
  return text.charAt(1) === unit && text.slice(2) === tail && text.slice(0) === text &&
    text.charAt(4294967295) === '' && text.slice(4294967295) === '';
}
""",
    "description-slice": """function descriptionSlice(key) {
  const text = key.description;
  key = Symbol('changed');
  return typeof text === 'string' ? text.slice(2) : ':absent';
}
""",
    "description-capture-index": """function descriptionCaptureIndex(key) {
  const text = key.description;
  key = Symbol('changed');
  function unit() { return text !== undefined ? text.charAt(2) : ':absent'; }
  return unit();
}
""",
}
STRING_SLICES = {
    # Preserve the complete former slice-end refusal as its first positive.
    "string-slice-end": "function bad(text, index) { return text.slice(0, 2); }\n",
    "string-slice-bounds": """function stringSliceBounds(text, first, middle, tail) {
  return text.slice(0, 1) === first && text.slice(1, 2) === middle &&
    text.slice(2, 4294967295) === tail && text.slice(0, 4294967295) === text &&
    text.slice(0, 0) === '' && text.slice(1, 1) === '' && text.slice(2, 1) === '' &&
    text.slice(4294967295, 0) === '' && text.slice(4294967295, 4294967295) === '';
}
""",
    "description-capture-slice": """function descriptionCaptureSlice(key) {
  const text = key.description;
  key = Symbol('changed');
  function part() { return text !== undefined ? text.slice(1, 3) : ':absent'; }
  return part();
}
""",
}
SIGNED_SLICES = {
    # Preserve both complete former negative-bound refusals as native witnesses.
    "string-slice-negative": "function bad(text, index) { return text.slice(-1); }\n",
    "string-slice-end-negative": "function bad(text, index) { return text.slice(0, -1); }\n",
    "string-signed-slices": """function stringSignedSlices(text, last, head, middle) {
  return text.slice(-1) === last && text.slice(0, -1) === head &&
    text.slice(1, -1) === middle && text.slice(-4294967295) === text &&
    text.slice(-4294967295, 4294967295) === text &&
    text.slice(-4294967295, -4294967295) === '' && text.slice(4294967295, -1) === '' &&
    text.slice(-1, -2) === '' && text.slice(-0) === text && text.slice(-0, -0) === '';
}
""",
    "description-capture-signed-slice": """function descriptionCaptureSignedSlice(key) {
  const text = key.description;
  key = Symbol('changed');
  function part() { return text !== undefined ? text.slice(-3, -1) : ':absent'; }
  return part();
}
""",
}
SIGNED_CHARAT = {
    # Preserve the complete former negative-index refusal as its first positive.
    "string-charat-negative": "function bad(text, index) { return text.charAt(-1); }\n",
    "string-signed-charat": """function stringSignedCharAt(text, first) {
  return text.charAt(-1) === '' && text.charAt(-4294967295) === '' &&
    text.charAt(-0) === first && text.charAt(0) === first;
}
""",
    "description-capture-signed-charat": """function descriptionCaptureSignedCharAt(key) {
  const text = key.description;
  key = Symbol('changed');
  function unit() { return text !== undefined ? text.charAt(-1) + text.charAt(-0) : ':absent'; }
  return unit();
}
""",
}
# Keep all seven former fractional refusal programs byte-identical.
FRACTIONAL_WITNESSES = {
    "string-index-charat-fraction": (
        "function bad(text, index) { return text.charAt(1.5); }\n",
        ("", "", "b"),
    ),
    "string-index-slice-fraction": (
        "function bad(text, index) { return text.slice(1.5); }\n",
        ("", "", "bc"),
    ),
    "string-slice-end-fraction": (
        "function bad(text, index) { return text.slice(0, 1.5); }\n",
        ("", "a", "a"),
    ),
    "string-slice-start-fraction": (
        "function bad(text, index) { return text.slice(0.5, 2); }\n",
        ("", "a", "ab"),
    ),
    "signed-slice-start-fraction": (
        "function bad(text, index) { return text.slice(-1.5); }\n",
        ("", "a", "c"),
    ),
    "signed-slice-end-fraction": (
        "function bad(text, index) { return text.slice(0, -1.5); }\n",
        ("", "", "ab"),
    ),
    "signed-charat-fraction": (
        "function bad(text, index) { return text.charAt(-1.5); }\n",
        ("", "", ""),
    ),
}
FRACTIONAL_INDICES = {
    **{name: source for name, (source, _) in FRACTIONAL_WITNESSES.items()},
    "string-fractional-indices": """function stringFractionalIndices(text, first, middle, tail) {
  return text.charAt(0.9) === first && text.charAt(-0.9) === first &&
    text.charAt(1.9) === middle && text.charAt(-1.9) === '' &&
    text.slice(0.9, 1.9) === first && text.slice(1.9, 2.9) === middle &&
    text.slice(2.9) === tail && text.slice(-0.9) === text && text.slice(-0.9, -0.9) === '' &&
    text.slice(2.9, 1.9) === '' && text.slice(-4294967294.9) === text &&
    text.charAt(4294967294.9) === '' && text.charAt(-4294967294.9) === '' &&
    text.slice(4294967294.9) === '' && text.slice(0, -4294967294.9) === '';
}
""",
    "description-capture-fractional-index": """function descriptionCaptureFractionalIndex(key) {
  const text = key.description;
  key = Symbol('changed');
  function restore() {
    return text !== undefined ? text.charAt(-0.75) + text.slice(1.5) : ':absent';
  }
  return restore();
}
""",
}
# Preserve all sixteen complete former magnitude/infinity refusal programs.
WIDE_WITNESSES = {
    name: (
        f"function bad(text, index) {{ return text.{expression}; }}\n",
        ("", "a", "abc") if whole else ("", "", ""),
    )
    for name, expression, whole in (
        ("string-index-charat-large", "charAt(4294967296)", False),
        ("string-index-slice-large", "slice(4294967296)", False),
        ("string-index-charat-infinity", "charAt(1e999)", False),
        ("string-index-slice-infinity", "slice(1e999)", False),
        ("string-slice-end-large", "slice(0, 4294967296)", True),
        ("string-slice-end-infinity", "slice(0, 1e999)", True),
        ("signed-slice-start-large", "slice(-4294967296)", True),
        ("signed-slice-end-large", "slice(0, -4294967296)", False),
        ("signed-slice-start-infinity", "slice(-1e999)", True),
        ("signed-slice-end-infinity", "slice(0, -1e999)", False),
        ("signed-charat-large", "charAt(-4294967296)", False),
        ("signed-charat-infinity", "charAt(-1e999)", False),
        ("fractional-index-charat-large", "charAt(4294967295.5)", False),
        ("fractional-index-slice-large", "slice(4294967295.5)", False),
        ("fractional-index-charat-negative-large", "charAt(-4294967295.5)", False),
        ("fractional-index-slice-end-negative-large", "slice(0, -4294967295.5)", False),
    )
}
WIDE_INDICES = {
    **{name: source for name, (source, _) in WIDE_WITNESSES.items()},
    "string-wide-indices": """function stringWideIndices(text) {
  return text.charAt(18446744073709549568) === '' &&
    text.charAt(18446744073709551616) === '' && text.charAt(1.7976931348623157e308) === '' &&
    text.charAt(-18446744073709551616) === '' && text.charAt(1e999) === '' &&
    text.charAt(-1e999) === '' && text.slice(18446744073709549568) === '' &&
    text.slice(18446744073709551616) === '' && text.slice(-18446744073709551616) === text &&
    text.slice(-1.7976931348623157e308, 1.7976931348623157e308) === text &&
    text.slice(-1e999, 1e999) === text && text.slice(1e999, -1e999) === '' &&
    text.slice(-1e999, -1e999) === '' && text.slice(1e999, 1e999) === '' &&
    text.slice(-0.5, 1e999) === text && text.slice(0, -1e999) === '';
}
""",
    "description-capture-wide-index": """function descriptionCaptureWideIndex(key) {
  const text = key.description;
  key = Symbol('changed');
  function restore() { return text !== undefined ? text.slice(-1e999, 1e999) : ':absent'; }
  return restore();
}
""",
}
DEFAULT_WITNESSES = {
    # Preserve both complete former missing-argument refusals as native witnesses.
    "string-index-charat-missing": (
        "function bad(text, index) { return text.charAt(); }\n",
        ("", "a", "a"),
    ),
    "string-index-slice-missing": (
        "function bad(text, index) { return text.slice(); }\n",
        ("", "a", "abc"),
    ),
}
DEFAULT_INDICES = {
    **{name: source for name, (source, _) in DEFAULT_WITNESSES.items()},
    "string-default-indices": """function stringDefaultIndices(text, first) {
  return text.charAt() === first && text.slice() === text;
}
""",
    "description-capture-default-index": """function descriptionCaptureDefaultIndex(key) {
  const text = key.description;
  key = Symbol('changed');
  function restore() {
    return text !== undefined ? text.slice().charAt() + text.slice(1) : ':absent';
  }
  return restore();
}
""",
}
UNDEFINED_WITNESSES = {
    # Preserve the three complete former explicit-undefined refusal bodies.
    "default-index-charat-undefined": (
        "function bad(text) { return text.charAt(undefined); }\n",
        ("", "a", "a"),
    ),
    "default-index-slice-undefined": (
        "function bad(text) { return text.slice(undefined); }\n",
        ("", "a", "abc"),
    ),
    "string-slice-end-undefined": (
        "function bad(text, index) { return text.slice(0, undefined); }\n",
        ("", "a", "abc"),
    ),
}
UNDEFINED_INDICES = {
    **{name: source for name, (source, _) in UNDEFINED_WITNESSES.items()},
    "string-undefined-indices": """function stringUndefinedIndices(text, first, middle, tail) {
  const absent = undefined;
  return text.charAt(absent) === first && text.slice(absent) === text &&
    text.slice(absent, absent) === text && text.slice(absent, 1) === first &&
    text.slice(1, absent) === middle + tail && text.slice(2, absent) === tail &&
    text.slice(absent, 0) === '' && text.slice(absent, -1) === text.slice(0, -1) &&
    text.slice(-1, absent) === text.slice(-1) && text.slice(-0.5, absent) === text &&
    text.slice(-1e999, absent) === text && text.slice(1e999, absent) === '';
}
""",
    "description-capture-undefined-index": """function descriptionCaptureUndefinedIndex(key) {
  const text = key.description;
  const absent = undefined;
  key = Symbol('changed');
  function restore() {
    return text !== absent ? text.slice(absent, absent).charAt(absent) +
      text.slice(1, absent) : ':absent';
  }
  return restore();
}
""",
}
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
    "scalar-equality": ["number", "number", "boolean", "boolean"],
    "helper-equality": ["number", "boolean", "number"],
    "global-state": ["symbol", "symbol", "string", "number", "boolean"],
    "global-types": ["symbol", "string", "number", "boolean"],
    "global-description": ["symbol"],
    "global-local-state": ["symbol", "symbol", "string", "number", "boolean"],
    "global-local-types": ["symbol", "string", "number", "boolean"],
    "global-local-description": ["symbol"],
    "capture-state": ["symbol", "symbol", "string", "number", "boolean"],
    "capture-types": ["symbol", "string", "number", "boolean"],
    "capture-description": ["symbol"],
    "string-prefix": ["string"],
    "description-lowercase": ["symbol"],
    "description-capture-prefix": ["symbol"],
    "string-indices": ["string", "string", "string"],
    "description-slice": ["symbol"],
    "description-capture-index": ["symbol"],
    "string-slice-end": ["string", "number"],
    "string-slice-bounds": ["string", "string", "string", "string"],
    "description-capture-slice": ["symbol"],
    "string-slice-negative": ["string", "number"],
    "string-slice-end-negative": ["string", "number"],
    "string-signed-slices": ["string", "string", "string", "string"],
    "description-capture-signed-slice": ["symbol"],
    "string-charat-negative": ["string", "number"],
    "string-signed-charat": ["string", "string"],
    "description-capture-signed-charat": ["symbol"],
    **{name: ["string", "number"] for name in FRACTIONAL_WITNESSES},
    "string-fractional-indices": ["string", "string", "string", "string"],
    "description-capture-fractional-index": ["symbol"],
    **{name: ["string", "number"] for name in WIDE_WITNESSES},
    "string-wide-indices": ["string"],
    "description-capture-wide-index": ["symbol"],
    **{name: ["string", "number"] for name in DEFAULT_WITNESSES},
    "string-default-indices": ["string", "string"],
    "description-capture-default-index": ["symbol"],
    "default-index-charat-undefined": ["string"],
    "default-index-slice-undefined": ["string"],
    "string-slice-end-undefined": ["string", "number"],
    "string-undefined-indices": ["string", "string", "string", "string"],
    "description-capture-undefined-index": ["symbol"],
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
CASES["scalar-equality"] = (
    SCALAR_EQUALITY,
    r"""
    static_assert(std::is_same_v<decltype(&@ENTRY@), ctnative::js_string (*)(
        ctnative::js_num, ctnative::js_num, ctnative::js_boolean_t, ctnative::js_boolean_t)>);
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double inf = std::numeric_limits<double>::infinity();
"""
    + "".join(
        "    assert(@ENTRY@("
        + ", ".join(
            f"ctnative::{kind}{{{value}}}"
            for kind, value in zip(
                ("js_num", "js_num", "js_boolean_t", "js_boolean_t"), native.split(", ")
            )
        )
        + f').value() == "{expected}");\n'
        for _, _, native, expected in EQUALITY_INPUTS
    )
    + '    std::cout << "true\\n";\n',
    "true\n",
)
CASES["helper-equality"] = (
    HELPER_EQUALITY,
    r"""
    static_assert(std::is_same_v<decltype(&@ENTRY@), ctnative::js_boolean_t (*)(
        ctnative::js_num, ctnative::js_boolean_t, ctnative::js_num)>);
    assert(@ENTRY@(ctnative::js_num{0.0}, ctnative::js_boolean_t{false}, ctnative::js_num{0.0}));
    assert(!@ENTRY@(ctnative::js_num{-0.0}, ctnative::js_boolean_t{false}, ctnative::js_num{1.0}));
    assert(@ENTRY@(ctnative::js_num{0.0}, ctnative::js_boolean_t{false}, ctnative::js_num{2.0}));
    assert(@ENTRY@(ctnative::js_num{1.0}, ctnative::js_boolean_t{true}, ctnative::js_num{3.0}));
    assert(@ENTRY@(ctnative::js_num{ctnative::js_nan_t{}}, ctnative::js_boolean_t{false}, ctnative::js_num{1.0}));
    std::cout << "true\n";
""",
    "true\n",
)
CASES["global-helper"] = (
    GLOBAL_HELPER,
    r"""
    static_assert(std::is_same_v<decltype(@ENTRY@()), ctnative::js_symbol_t>);
    assert(@ENTRY@() == ctnative::Symbol.iterator);
    assert(@ENTRY@() == @ENTRY@());
    std::cout << "true\n";
""",
    "true\n",
)
CASES["global-state"] = (GLOBAL_STATE, *CASES["parameter-state"][1:])
CASES["global-types"] = (GLOBAL_TYPES, *CASES["helper-types"][1:])
CASES["global-description"] = (GLOBAL_DESCRIPTION, *CASES["parameter-description"][1:])
CASES["global-local-declaration"] = (GLOBAL_LOCAL_DECLARATION, *CASES["global-helper"][1:])
CASES["global-local-state"] = (GLOBAL_LOCAL_STATE, *CASES["parameter-state"][1:])
CASES["global-local-types"] = (GLOBAL_LOCAL_TYPES, *CASES["helper-types"][1:])
CASES["global-local-description"] = (GLOBAL_LOCAL_DESCRIPTION, *CASES["parameter-description"][1:])
for name, source in CAPTURE_WITNESSES.items():
    name = "capture-witness-" + name
    PARAMETER_TYPES[name] = ["symbol"]
    CASES[name] = (
        source,
        r"""
    static_assert(std::is_same_v<decltype(&@ENTRY@), ctnative::js_symbol_t (*)(ctnative::js_symbol_t)>);
    const auto key = ctnative::Symbol(ctnative::js_string{"same"});
    const auto other = ctnative::Symbol(ctnative::js_string{"same"});
    assert(@ENTRY@(key) == key && @ENTRY@(other) == other && @ENTRY@(key) != @ENTRY@(other));
    assert(@ENTRY@(ctnative::Symbol.iterator) == ctnative::Symbol.iterator);
    std::cout << "true\n";
""",
        "true\n",
    )
CASES["capture-state"] = (CAPTURE_STATE, *CASES["parameter-state"][1:])
CASES["capture-types"] = (CAPTURE_TYPES, *CASES["helper-types"][1:])
CASES["capture-description"] = (CAPTURE_DESCRIPTION, *CASES["parameter-description"][1:])
for name, source in DESCRIPTION_GUARDS.items():
    PARAMETER_TYPES[name] = ["symbol"]
    CASES[name] = (
        source,
        r"""
    using namespace ctnative;
    static_assert(std::is_same_v<decltype(&@ENTRY@), js_string (*)(js_symbol_t)>);
    assert(@ENTRY@(Symbol()).value() == ":absent");
    assert(@ENTRY@(Symbol(undefined_t{})).value() == ":absent");
    assert(@ENTRY@(Symbol(js_string{""})).value().empty());
    assert(@ENTRY@(Symbol(js_string{"Ab"})).value() == "Ab");
    assert(@ENTRY@(Symbol(js_string{"a\0b"})).value() == std::string("a\0b", 3));
    assert(@ENTRY@(Symbol(js_string{"\xed\xa0\x80"})).value() == "\xed\xa0\x80");
    assert(@ENTRY@(Symbol.iterator).value() == "Symbol.iterator");
    std::cout << "true\n";
""",
        "true\n",
    )
CASES["string-prefix"] = (
    STRING_METHODS["string-prefix"],
    r"""
    using namespace ctnative;
    static_assert(std::is_same_v<decltype(&@ENTRY@), js_string (*)(js_string)>);
    assert(@ENTRY@(js_string{""}).value() == "other:empty:plain");
    assert(@ENTRY@(js_string{"bs"}).value() == "prefix:empty:plain");
    assert(@ENTRY@(js_string{"bs\xc3\xa9"}).value() == "prefix:empty:plain");
    assert(@ENTRY@(js_string{"Bs"}).value() == "other:empty:plain");
    assert(@ENTRY@(js_string{"a\0b"}).value() == "other:empty:nul");
    assert(@ENTRY@(js_string{"a"}).value() == "other:empty:plain");
    assert(@ENTRY@(js_string{"\xed\xa0\x80"}).value() == "other:empty:plain");
    std::cout << "true\n";
""",
    "true\n",
)
CASES["description-lowercase"] = (
    STRING_METHODS["description-lowercase"],
    r"""
    using namespace ctnative;
    static_assert(std::is_same_v<decltype(&@ENTRY@), js_string (*)(js_symbol_t)>);
    assert(@ENTRY@(Symbol()).value() == ":absent");
    assert(@ENTRY@(Symbol(undefined_t{})).value() == ":absent");
    assert(@ENTRY@(Symbol(js_string{""})).value().empty());
    assert(@ENTRY@(Symbol(js_string{"Ab"})).value() == "ab");
    assert(@ENTRY@(Symbol(js_string{"A\0B"})).value() == std::string("a\0B", 3));
    assert(@ENTRY@(Symbol(js_string{"\xc4\xb0X"})).value() == "i\xcc\x87X");
    assert(@ENTRY@(Symbol(js_string{"\xce\xa3X"})).value() == "\xcf\x83X");
    assert(@ENTRY@(Symbol(js_string{"\xf0\x90\x90\x80X"})).value() == "\xf0\x90\x90\x80X");
    assert(@ENTRY@(Symbol(js_string{"\xed\xa0\x80X"})).value() == "\xed\xa0\x80X");
    assert(@ENTRY@(Symbol.iterator).value() == "symbol.iterator");
    std::cout << "true\n";
""",
    "true\n",
)
CASES["description-capture-prefix"] = (
    STRING_METHODS["description-capture-prefix"],
    r"""
    using namespace ctnative;
    static_assert(std::is_same_v<decltype(&@ENTRY@), js_boolean_t (*)(js_symbol_t)>);
    assert(!@ENTRY@(Symbol()));
    assert(!@ENTRY@(Symbol(undefined_t{})));
    assert(!@ENTRY@(Symbol(js_string{""})));
    assert(@ENTRY@(Symbol(js_string{"bs"})));
    assert(@ENTRY@(Symbol(js_string{"bs\0"})));
    assert(!@ENTRY@(Symbol(js_string{"Bs"})));
    assert(!@ENTRY@(Symbol.iterator));
    std::cout << "true\n";
""",
    "true\n",
)
CASES["string-indices"] = (
    STRING_INDICES["string-indices"],
    r"""
    using namespace ctnative;
    static_assert(std::is_same_v<decltype(&@ENTRY@),
        js_boolean_t (*)(js_string, js_string, js_string)>);
    assert(@ENTRY@(js_string{""}, js_string{""}, js_string{""}));
    assert(@ENTRY@(js_string{"a"}, js_string{""}, js_string{""}));
    assert(@ENTRY@(js_string{"abc"}, js_string{"b"}, js_string{"c"}));
    assert(@ENTRY@(js_string{"a\0b"}, js_string{"\0"}, js_string{"b"}));
    assert(@ENTRY@(js_string{"\xc3\x89xy"}, js_string{"x"}, js_string{"y"}));
    assert(@ENTRY@(js_string{"\xf0\x90\x90\x80x"}, js_string{"\xed\xb0\x80"}, js_string{"x"}));
    assert(@ENTRY@(js_string{"\xed\xa0\x80xy"}, js_string{"x"}, js_string{"y"}));
    assert(@ENTRY@(js_string{"A\xed\xb0\x80x"}, js_string{"\xed\xb0\x80"}, js_string{"x"}));
    assert(!@ENTRY@(js_string{"abc"}, js_string{"a"}, js_string{"c"}));
    assert(!@ENTRY@(js_string{"abc"}, js_string{"b"}, js_string{"bc"}));
    std::cout << "true\n";
""",
    "true\n",
)
CASES["description-slice"] = (
    STRING_INDICES["description-slice"],
    r"""
    using namespace ctnative;
    static_assert(std::is_same_v<decltype(&@ENTRY@), js_string (*)(js_symbol_t)>);
    assert(@ENTRY@(Symbol()).value() == ":absent");
    assert(@ENTRY@(Symbol(undefined_t{})).value() == ":absent");
    assert(@ENTRY@(Symbol(js_string{""})).value().empty());
    assert(@ENTRY@(Symbol(js_string{"a"})).value().empty());
    assert(@ENTRY@(Symbol(js_string{"abc"})).value() == "c");
    assert(@ENTRY@(Symbol(js_string{"a\0b"})).value() == "b");
    assert(@ENTRY@(Symbol(js_string{"\xc3\x89xy"})).value() == "y");
    assert(@ENTRY@(Symbol(js_string{"\xf0\x90\x90\x80x"})).value() == "x");
    assert(@ENTRY@(Symbol(js_string{"\xed\xa0\x80xy"})).value() == "y");
    assert(@ENTRY@(Symbol.iterator).value() == "mbol.iterator");
    std::cout << "true\n";
""",
    "true\n",
)
CASES["description-capture-index"] = (
    STRING_INDICES["description-capture-index"],
    r"""
    using namespace ctnative;
    static_assert(std::is_same_v<decltype(&@ENTRY@), js_string (*)(js_symbol_t)>);
    assert(@ENTRY@(Symbol()).value() == ":absent");
    assert(@ENTRY@(Symbol(undefined_t{})).value() == ":absent");
    assert(@ENTRY@(Symbol(js_string{""})).value().empty());
    assert(@ENTRY@(Symbol(js_string{"ab"})).value().empty());
    assert(@ENTRY@(Symbol(js_string{"abcd"})).value() == "c");
    assert(@ENTRY@(Symbol(js_string{"ab\0d"})).value() == std::string("\0", 1));
    assert(@ENTRY@(Symbol(js_string{"\xc3\x89xyz"})).value() == "y");
    assert(@ENTRY@(Symbol(js_string{"A\xf0\x90\x90\x80x"})).value() == "\xed\xb0\x80");
    assert(@ENTRY@(Symbol(js_string{"AB\xed\xa0\x80x"})).value() == "\xed\xa0\x80");
    assert(@ENTRY@(Symbol.iterator).value() == "m");
    std::cout << "true\n";
""",
    "true\n",
)
CASES["string-slice-end"] = (
    STRING_SLICES["string-slice-end"],
    r"""
    using namespace ctnative;
    static_assert(std::is_same_v<decltype(&@ENTRY@), js_string (*)(js_string, ctnative::js_num)>);
    assert(@ENTRY@(js_string{""}, ctnative::js_num{0.0}).value().empty());
    assert(@ENTRY@(js_string{"a"}, ctnative::js_num{1.0}).value() == "a");
    assert(@ENTRY@(js_string{"abc"}, ctnative::js_num{2.0}).value() == "ab");
    assert(@ENTRY@(js_string{"a\0b"}, ctnative::js_num{3.0}).value() == std::string("a\0", 2));
    assert(@ENTRY@(js_string{"\xc3\x89xy"}, ctnative::js_num{0.0}).value() == "\xc3\x89x");
    assert(@ENTRY@(js_string{"\xf0\x90\x90\x80x"}, ctnative::js_num{0.0}).value() == "\xf0\x90\x90\x80");
    assert(@ENTRY@(js_string{"A\xf0\x90\x90\x80x"}, ctnative::js_num{0.0}).value() == "A\xed\xa0\x81");
    assert(@ENTRY@(js_string{"\xed\xa0\x80xy"}, ctnative::js_num{0.0}).value() == "\xed\xa0\x80x");
    std::cout << "true\n";
""",
    "true\n",
)
CASES["string-slice-bounds"] = (
    STRING_SLICES["string-slice-bounds"],
    r"""
    using namespace ctnative;
    static_assert(std::is_same_v<decltype(&@ENTRY@),
        js_boolean_t (*)(js_string, js_string, js_string, js_string)>);
    assert(@ENTRY@(js_string{""}, js_string{""}, js_string{""}, js_string{""}));
    assert(@ENTRY@(js_string{"a"}, js_string{"a"}, js_string{""}, js_string{""}));
    assert(@ENTRY@(js_string{"abcd"}, js_string{"a"}, js_string{"b"}, js_string{"cd"}));
    assert(@ENTRY@(js_string{"a\0b"}, js_string{"a"}, js_string{"\0"}, js_string{"b"}));
    assert(@ENTRY@(js_string{"\xc3\x89xy"}, js_string{"\xc3\x89"}, js_string{"x"}, js_string{"y"}));
    assert(@ENTRY@(js_string{"\xf0\x90\x90\x80x"}, js_string{"\xed\xa0\x81"}, js_string{"\xed\xb0\x80"}, js_string{"x"}));
    assert(@ENTRY@(js_string{"\xed\xa0\x80xy"}, js_string{"\xed\xa0\x80"}, js_string{"x"}, js_string{"y"}));
    assert(@ENTRY@(js_string{"A\xed\xb0\x80x"}, js_string{"A"}, js_string{"\xed\xb0\x80"}, js_string{"x"}));
    assert(!@ENTRY@(js_string{"abc"}, js_string{"a"}, js_string{"bc"}, js_string{"c"}));
    assert(!@ENTRY@(js_string{"abc"}, js_string{"a"}, js_string{"b"}, js_string{"bc"}));
    std::cout << "true\n";
""",
    "true\n",
)
CASES["description-capture-slice"] = (
    STRING_SLICES["description-capture-slice"],
    r"""
    using namespace ctnative;
    static_assert(std::is_same_v<decltype(&@ENTRY@), js_string (*)(js_symbol_t)>);
    assert(@ENTRY@(Symbol()).value() == ":absent");
    assert(@ENTRY@(Symbol(undefined_t{})).value() == ":absent");
    assert(@ENTRY@(Symbol(js_string{""})).value().empty());
    assert(@ENTRY@(Symbol(js_string{"a"})).value().empty());
    assert(@ENTRY@(Symbol(js_string{"abcd"})).value() == "bc");
    assert(@ENTRY@(Symbol(js_string{"a\0bc"})).value() == std::string("\0b", 2));
    assert(@ENTRY@(Symbol(js_string{"\xc3\x89xyz"})).value() == "xy");
    assert(@ENTRY@(Symbol(js_string{"\xf0\x90\x90\x80xy"})).value() == "\xed\xb0\x80x");
    assert(@ENTRY@(Symbol(js_string{"A\xed\xa0\x80xy"})).value() == "\xed\xa0\x80x");
    assert(@ENTRY@(Symbol.iterator).value() == "ym");
    std::cout << "true\n";
""",
    "true\n",
)
CASES["string-slice-negative"] = (
    SIGNED_SLICES["string-slice-negative"],
    r"""
    using namespace ctnative;
    static_assert(std::is_same_v<decltype(&@ENTRY@), js_string (*)(js_string, ctnative::js_num)>);
    assert(@ENTRY@(js_string{""}, ctnative::js_num{0.0}).value().empty());
    assert(@ENTRY@(js_string{"a"}, ctnative::js_num{1.0}).value() == "a");
    assert(@ENTRY@(js_string{"abc"}, ctnative::js_num{2.0}).value() == "c");
    assert(@ENTRY@(js_string{"a\0"}, ctnative::js_num{3.0}).value() == std::string("\0", 1));
    assert(@ENTRY@(js_string{"\xc3\x89"}, ctnative::js_num{0.0}).value() == "\xc3\x89");
    assert(@ENTRY@(js_string{"\xf0\x90\x90\x80"}, ctnative::js_num{0.0}).value() == "\xed\xb0\x80");
    assert(@ENTRY@(js_string{"A\xed\xa0\x80"}, ctnative::js_num{0.0}).value() == "\xed\xa0\x80");
    std::cout << "true\n";
""",
    "true\n",
)
CASES["string-slice-end-negative"] = (
    SIGNED_SLICES["string-slice-end-negative"],
    r"""
    using namespace ctnative;
    static_assert(std::is_same_v<decltype(&@ENTRY@), js_string (*)(js_string, ctnative::js_num)>);
    assert(@ENTRY@(js_string{""}, ctnative::js_num{0.0}).value().empty());
    assert(@ENTRY@(js_string{"a"}, ctnative::js_num{1.0}).value().empty());
    assert(@ENTRY@(js_string{"abc"}, ctnative::js_num{2.0}).value() == "ab");
    assert(@ENTRY@(js_string{"a\0b"}, ctnative::js_num{3.0}).value() == std::string("a\0", 2));
    assert(@ENTRY@(js_string{"\xc3\x89x"}, ctnative::js_num{0.0}).value() == "\xc3\x89");
    assert(@ENTRY@(js_string{"\xf0\x90\x90\x80"}, ctnative::js_num{0.0}).value() == "\xed\xa0\x81");
    assert(@ENTRY@(js_string{"\xed\xa0\x80x"}, ctnative::js_num{0.0}).value() == "\xed\xa0\x80");
    std::cout << "true\n";
""",
    "true\n",
)
CASES["string-signed-slices"] = (
    SIGNED_SLICES["string-signed-slices"],
    r"""
    using namespace ctnative;
    static_assert(std::is_same_v<decltype(&@ENTRY@),
        js_boolean_t (*)(js_string, js_string, js_string, js_string)>);
    assert(@ENTRY@(js_string{""}, js_string{""}, js_string{""}, js_string{""}));
    assert(@ENTRY@(js_string{"a"}, js_string{"a"}, js_string{""}, js_string{""}));
    assert(@ENTRY@(js_string{"abcd"}, js_string{"d"}, js_string{"abc"}, js_string{"bc"}));
    assert(@ENTRY@(js_string{"a\0b"}, js_string{"b"}, js_string{"a\0"}, js_string{"\0"}));
    assert(@ENTRY@(js_string{"\xc3\x89xy"}, js_string{"y"}, js_string{"\xc3\x89x"}, js_string{"x"}));
    assert(@ENTRY@(js_string{"\xf0\x90\x90\x80"}, js_string{"\xed\xb0\x80"}, js_string{"\xed\xa0\x81"}, js_string{""}));
    assert(@ENTRY@(js_string{"A\xed\xa0\x80x"}, js_string{"x"}, js_string{"A\xed\xa0\x80"}, js_string{"\xed\xa0\x80"}));
    assert(!@ENTRY@(js_string{"abc"}, js_string{"a"}, js_string{"ab"}, js_string{"b"}));
    assert(!@ENTRY@(js_string{"abc"}, js_string{"c"}, js_string{"bc"}, js_string{"b"}));
    assert(!@ENTRY@(js_string{"abc"}, js_string{"c"}, js_string{"ab"}, js_string{"bc"}));
    std::cout << "true\n";
""",
    "true\n",
)
CASES["description-capture-signed-slice"] = (
    SIGNED_SLICES["description-capture-signed-slice"],
    r"""
    using namespace ctnative;
    static_assert(std::is_same_v<decltype(&@ENTRY@), js_string (*)(js_symbol_t)>);
    assert(@ENTRY@(Symbol()).value() == ":absent");
    assert(@ENTRY@(Symbol(undefined_t{})).value() == ":absent");
    assert(@ENTRY@(Symbol(js_string{""})).value().empty());
    assert(@ENTRY@(Symbol(js_string{"a"})).value().empty());
    assert(@ENTRY@(Symbol(js_string{"abcd"})).value() == "bc");
    assert(@ENTRY@(Symbol(js_string{"a\0bc"})).value() == std::string("\0b", 2));
    assert(@ENTRY@(Symbol(js_string{"\xc3\x89xyz"})).value() == "xy");
    assert(@ENTRY@(Symbol(js_string{"\xf0\x90\x90\x80xy"})).value() == "\xed\xb0\x80x");
    assert(@ENTRY@(Symbol(js_string{"A\xed\xa0\x80xy"})).value() == "\xed\xa0\x80x");
    assert(@ENTRY@(Symbol.iterator).value() == "to");
    std::cout << "true\n";
""",
    "true\n",
)
CASES["string-charat-negative"] = (
    SIGNED_CHARAT["string-charat-negative"],
    r"""
    using namespace ctnative;
    static_assert(std::is_same_v<decltype(&@ENTRY@), js_string (*)(js_string, ctnative::js_num)>);
    assert(@ENTRY@(js_string{""}, ctnative::js_num{0.0}).value().empty());
    assert(@ENTRY@(js_string{"a"}, ctnative::js_num{1.0}).value().empty());
    assert(@ENTRY@(js_string{"abc"}, ctnative::js_num{2.0}).value().empty());
    assert(@ENTRY@(js_string{"a\0b"}, ctnative::js_num{3.0}).value().empty());
    assert(@ENTRY@(js_string{"\xc3\x89"}, ctnative::js_num{0.0}).value().empty());
    assert(@ENTRY@(js_string{"\xf0\x90\x90\x80"}, ctnative::js_num{0.0}).value().empty());
    assert(@ENTRY@(js_string{"\xed\xa0\x80"}, ctnative::js_num{0.0}).value().empty());
    std::cout << "true\n";
""",
    "true\n",
)
CASES["string-signed-charat"] = (
    SIGNED_CHARAT["string-signed-charat"],
    r"""
    using namespace ctnative;
    static_assert(std::is_same_v<decltype(&@ENTRY@), js_boolean_t (*)(js_string, js_string)>);
    assert(@ENTRY@(js_string{""}, js_string{""}));
    assert(@ENTRY@(js_string{"a"}, js_string{"a"}));
    assert(@ENTRY@(js_string{"abc"}, js_string{"a"}));
    assert(@ENTRY@(js_string{"\0ab"}, js_string{"\0"}));
    assert(@ENTRY@(js_string{"\xc3\x89xy"}, js_string{"\xc3\x89"}));
    assert(@ENTRY@(js_string{"\xf0\x90\x90\x80x"}, js_string{"\xed\xa0\x81"}));
    assert(@ENTRY@(js_string{"\xed\xa0\x80xy"}, js_string{"\xed\xa0\x80"}));
    assert(@ENTRY@(js_string{"\xed\xb0\x80xy"}, js_string{"\xed\xb0\x80"}));
    assert(!@ENTRY@(js_string{"abc"}, js_string{""}));
    assert(!@ENTRY@(js_string{"abc"}, js_string{"b"}));
    std::cout << "true\n";
""",
    "true\n",
)
CASES["description-capture-signed-charat"] = (
    SIGNED_CHARAT["description-capture-signed-charat"],
    r"""
    using namespace ctnative;
    static_assert(std::is_same_v<decltype(&@ENTRY@), js_string (*)(js_symbol_t)>);
    assert(@ENTRY@(Symbol()).value() == ":absent");
    assert(@ENTRY@(Symbol(undefined_t{})).value() == ":absent");
    assert(@ENTRY@(Symbol(js_string{""})).value().empty());
    assert(@ENTRY@(Symbol(js_string{"abc"})).value() == "a");
    assert(@ENTRY@(Symbol(js_string{"\0ab"})).value() == std::string("\0", 1));
    assert(@ENTRY@(Symbol(js_string{"\xc3\x89xy"})).value() == "\xc3\x89");
    assert(@ENTRY@(Symbol(js_string{"\xf0\x90\x90\x80x"})).value() == "\xed\xa0\x81");
    assert(@ENTRY@(Symbol(js_string{"\xed\xa0\x80xy"})).value() == "\xed\xa0\x80");
    assert(@ENTRY@(Symbol.iterator).value() == "S");
    std::cout << "true\n";
""",
    "true\n",
)
for name, (source, expected) in (FRACTIONAL_WITNESSES | WIDE_WITNESSES | DEFAULT_WITNESSES).items():
    CASES[name] = (
        source,
        """
    using namespace ctnative;
    static_assert(std::is_same_v<decltype(&@ENTRY@), js_string (*)(js_string, ctnative::js_num)>);
"""
        + "".join(
            f"    assert(@ENTRY@(js_string{{{json.dumps(text)}}}, ctnative::js_num{{0.0}}).value() == {json.dumps(value)});\n"
            for text, value in zip(("", "a", "abc"), expected, strict=True)
        )
        + '    std::cout << "true\\n";\n',
        "true\n",
    )
CASES["string-fractional-indices"] = (
    FRACTIONAL_INDICES["string-fractional-indices"],
    *CASES["string-slice-bounds"][1:],
)
CASES["description-capture-fractional-index"] = (
    FRACTIONAL_INDICES["description-capture-fractional-index"],
    *CASES["description-type-guard"][1:],
)
CASES["string-wide-indices"] = (
    WIDE_INDICES["string-wide-indices"],
    r"""
    using namespace ctnative;
    static_assert(std::is_same_v<decltype(&@ENTRY@), js_boolean_t (*)(js_string)>);
    for (const auto * text : {"", "a", "abc", "\xc3\x89xy", "\xf0\x90\x90\x80x",
                             "\xed\xa0\x80xy", "A\xed\xb0\x80x"}) {
        assert(@ENTRY@(js_string{std::string{text}}));
    }
    assert(@ENTRY@(js_string{"a\0b"}));
    std::cout << "true\n";
""",
    "true\n",
)
CASES["description-capture-wide-index"] = (
    WIDE_INDICES["description-capture-wide-index"],
    *CASES["description-type-guard"][1:],
)
CASES["string-default-indices"] = (
    DEFAULT_INDICES["string-default-indices"],
    *CASES["string-signed-charat"][1:],
)
CASES["description-capture-default-index"] = (
    DEFAULT_INDICES["description-capture-default-index"],
    *CASES["description-type-guard"][1:],
)
for name, (source, _) in UNDEFINED_WITNESSES.items():
    # The former charAt/slice default refusals have one source parameter.
    method = "charat" if name == "default-index-charat-undefined" else "slice"
    checks = CASES[f"string-index-{method}-missing"][1]
    if len(PARAMETER_TYPES[name]) == 1:
        checks = checks.replace(", ctnative::js_num)", ")").replace(", ctnative::js_num{0.0}", "")
    CASES[name] = source, checks, "true\n"
CASES["string-undefined-indices"] = (
    UNDEFINED_INDICES["string-undefined-indices"],
    *CASES["string-slice-bounds"][1:],
)
CASES["description-capture-undefined-index"] = (
    UNDEFINED_INDICES["description-capture-undefined-index"],
    *CASES["description-type-guard"][1:],
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
        + SCALAR_EQUALITY
        + HELPER_EQUALITY
        + GLOBAL_HELPER
        + GLOBAL_STATE
        + GLOBAL_TYPES
        + GLOBAL_DESCRIPTION
        + GLOBAL_LOCAL_DECLARATION
        + GLOBAL_LOCAL_STATE
        + GLOBAL_LOCAL_TYPES
        + GLOBAL_LOCAL_DESCRIPTION
        + CAPTURE_STATE
        + CAPTURE_TYPES
        + CAPTURE_DESCRIPTION
        + "".join(DESCRIPTION_GUARDS.values())
        + "".join(STRING_METHODS.values())
        + "".join(STRING_INDICES.values())
        + "".join(body for name, body in STRING_SLICES.items() if name != "string-slice-end")
        + "".join(
            body
            for name, body in SIGNED_SLICES.items()
            if name not in ("string-slice-negative", "string-slice-end-negative")
        )
        + "".join(body for name, body in SIGNED_CHARAT.items() if name != "string-charat-negative")
        + "".join(
            body for name, body in FRACTIONAL_INDICES.items() if name not in FRACTIONAL_WITNESSES
        )
        + "".join(body for name, body in WIDE_INDICES.items() if name not in WIDE_WITNESSES)
        + "".join(body for name, body in DEFAULT_INDICES.items() if name not in DEFAULT_WITNESSES)
        + "".join(
            body for name, body in UNDEFINED_INDICES.items() if name not in UNDEFINED_WITNESSES
        )
        + "function observeNegativeCharAt() {\n"
        + SIGNED_CHARAT["string-charat-negative"]
        + r"""
  return bad('', 0) === '' && bad('a', 1) === '' && bad('abc', 2) === '' &&
    bad('a\u0000b', 3) === '' && bad('\u00c9', 0) === '' &&
    bad('\ud801\udc00', 0) === '' && bad('\ud800', 0) === '';
}
"""
        + "function observeNegativeSlice() {\n"
        + SIGNED_SLICES["string-slice-negative"]
        + r"""
  return bad('', 0) === '' && bad('a', 1) === 'a' && bad('abc', 2) === 'c' &&
    bad('a\u0000', 3) === '\u0000';
}
"""
        + "function observeNegativeSliceEnd() {\n"
        + SIGNED_SLICES["string-slice-end-negative"]
        + r"""
  return bad('', 0) === '' && bad('a', 1) === '' && bad('abc', 2) === 'ab' &&
    bad('a\u0000b', 3) === 'a\u0000';
}
"""
        + "function observeSliceEnd() {\n"
        + STRING_SLICES["string-slice-end"]
        + r"""
  return bad('', 0) === '' && bad('a', 1) === 'a' && bad('abc', 2) === 'ab' &&
    bad('a\u0000b', 3) === 'a\u0000';
}
"""
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
symbol30GlobalHelper = bad() === Symbol.iterator;
symbol31GlobalState = globalState(key, other, 'fresh', 0, false) === key &&
  globalState(key, other, 'fresh', 1, false) === other &&
  globalState(key, other, 'fresh', 2, false) === key &&
  globalState(key, other, 'fresh', 1, true).description === 'fresh' &&
  globalState(key, other, 'fresh', 1, true) !== globalState(key, other, 'fresh', 1, true);
symbol32GlobalTypes = globalTypes(key, 'a\\u0000b', -0, false) === 'symbol:string:number:boolean:evaluation';
symbol33GlobalDescription = globalDescription(Symbol()) === undefined &&
  globalDescription(Symbol('a\\u0000b')) === 'a\\u0000b' &&
  globalDescription(Symbol.iterator) === 'Symbol.iterator';
symbol34GlobalLocalDeclaration = globalLocalDeclaration() === Symbol.iterator;
symbol35GlobalLocalState = globalLocalState(key, other, 'fresh', 0, false) === key &&
  globalLocalState(key, other, 'fresh', 1, false) === other &&
  globalLocalState(key, other, 'fresh', 2, false) === key &&
  globalLocalState(key, other, 'fresh', 1, true).description === 'fresh' &&
  globalLocalState(key, other, 'fresh', 1, true) !== globalLocalState(key, other, 'fresh', 1, true);
symbol36GlobalLocalTypes = globalLocalTypes(key, 'a\\u0000b', -0, false) === 'symbol:string:number:boolean:evaluation';
symbol37GlobalLocalDescription = globalLocalDescription(Symbol()) === undefined &&
  globalLocalDescription(Symbol('')) === '' &&
  globalLocalDescription(Symbol('a\\u0000b')) === 'a\\u0000b' &&
  globalLocalDescription(Symbol('\\ud800')) === '\\ud800' &&
  globalLocalDescription(Symbol.iterator) === 'Symbol.iterator';
symbol41CaptureState = captureState(key, other, 'fresh', 0, false) === key &&
  captureState(key, other, 'fresh', 1, false) === other &&
  captureState(key, other, 'fresh', 2, false) === key &&
  captureState(key, other, 'fresh', 1, true).description === 'fresh' &&
  captureState(key, other, 'fresh', 1, true) !== captureState(key, other, 'fresh', 1, true);
symbol42CaptureTypes = captureTypes(key, 'a\\u0000b', -0, false) === 'symbol:string:number:boolean:evaluation';
symbol43CaptureDescription = captureDescription(Symbol()) === undefined &&
  captureDescription(Symbol('')) === '' &&
  captureDescription(Symbol('a\\u0000b')) === 'a\\u0000b' &&
  captureDescription(Symbol('\\ud800')) === '\\ud800' &&
  captureDescription(Symbol.iterator) === 'Symbol.iterator';
}}
observeHelpers();
var symbol29HelperEquality = helperEquality(0, false, 0) && !helperEquality(-0, false, 1) &&
  helperEquality(0, false, 2) && helperEquality(1, true, 3) && helperEquality(0 / 0, false, 1);
"""
        + "".join(
            f"\nvar symbol{i}Equality{name} = scalarEquality({inputs}) === '{expected}';"
            for i, (name, inputs, _, expected) in enumerate(EQUALITY_INPUTS, 23)
        )
        + "".join(
            f"\nfunction observeCapture{i}() {{ {body}\n"
            "const key = Symbol('same'); const other = Symbol('same');\n"
            "return bad(key) === key && bad(other) === other && bad(key) !== bad(other) && "
            "bad(Symbol.iterator) === Symbol.iterator; }\n"
            f"var symbol{i}Capture{name.title().replace('-', '')} = observeCapture{i}();\n"
            for i, (name, body) in enumerate(CAPTURE_WITNESSES.items(), 38)
        )
        + "".join(
            f"\nvar symbol{i}Description{kind}Guard = "
            + " && ".join(
                f"description{kind}Guard({value}) === {expected}"
                for value, expected in (
                    ("Symbol()", "':absent'"),
                    ("Symbol(undefined)", "':absent'"),
                    ("Symbol('')", "''"),
                    ("Symbol('Ab')", "'Ab'"),
                    (r"Symbol('a\u0000b')", r"'a\u0000b'"),
                    (r"Symbol('\ud800')", r"'\ud800'"),
                    ("Symbol.iterator", "'Symbol.iterator'"),
                )
            )
            + ";\n"
            for i, kind in enumerate(("Type", "Undefined", "Capture"), 44)
        )
        + r"""
var symbol47StringPrefix = stringPrefix('') === 'other:empty:plain' &&
  stringPrefix('bs') === 'prefix:empty:plain' && stringPrefix('bs\u00e9') === 'prefix:empty:plain' &&
  stringPrefix('Bs') === 'other:empty:plain' && stringPrefix('a\u0000b') === 'other:empty:nul' &&
  stringPrefix('a') === 'other:empty:plain' && stringPrefix('\ud800') === 'other:empty:plain';
var symbol48DescriptionLowercase = descriptionLowercase(Symbol()) === ':absent' &&
  descriptionLowercase(Symbol(undefined)) === ':absent' && descriptionLowercase(Symbol('')) === '' &&
  descriptionLowercase(Symbol('Ab')) === 'ab' && descriptionLowercase(Symbol('A\u0000B')) === 'a\u0000B' &&
  descriptionLowercase(Symbol('\ud801\udc00X')) === '\ud801\udc00X' &&
  descriptionLowercase(Symbol('\ud800X')) === '\ud800X' &&
  descriptionLowercase(Symbol.iterator) === 'symbol.iterator';
var symbol49DescriptionCapturePrefix = !descriptionCapturePrefix(Symbol()) &&
  !descriptionCapturePrefix(Symbol(undefined)) && !descriptionCapturePrefix(Symbol('')) &&
  descriptionCapturePrefix(Symbol('bs')) && descriptionCapturePrefix(Symbol('bs\u0000')) &&
  !descriptionCapturePrefix(Symbol('Bs')) && !descriptionCapturePrefix(Symbol.iterator);
var symbol50DescriptionExpandedCase = descriptionLowercase(Symbol('\u0130X')) === 'i\u0307X';
var symbol51DescriptionGreekCase = descriptionLowercase(Symbol('\u03a3X')) === '\u03c3X';
var symbol52StringIndices = stringIndices('', '', '') && stringIndices('a', '', '') &&
  stringIndices('abc', 'b', 'c') && stringIndices('a\u0000b', '\u0000', 'b') &&
  !stringIndices('abc', 'a', 'c') && !stringIndices('abc', 'b', 'bc');
var symbol53DescriptionSlice = descriptionSlice(Symbol()) === ':absent' &&
  descriptionSlice(Symbol(undefined)) === ':absent' && descriptionSlice(Symbol('')) === '' &&
  descriptionSlice(Symbol('a')) === '' && descriptionSlice(Symbol('abc')) === 'c' &&
  descriptionSlice(Symbol('a\u0000b')) === 'b' && descriptionSlice(Symbol.iterator) === 'mbol.iterator';
var symbol54DescriptionCaptureIndex = descriptionCaptureIndex(Symbol()) === ':absent' &&
  descriptionCaptureIndex(Symbol(undefined)) === ':absent' && descriptionCaptureIndex(Symbol('')) === '' &&
  descriptionCaptureIndex(Symbol('ab')) === '' && descriptionCaptureIndex(Symbol('abcd')) === 'c' &&
  descriptionCaptureIndex(Symbol('ab\u0000d')) === '\u0000' && descriptionCaptureIndex(Symbol.iterator) === 'm';
var symbol55StringUTF16Indices = stringIndices('\u00c9xy', 'x', 'y') &&
  stringIndices('\ud801\udc00x', '\udc00', 'x') && stringIndices('\ud800xy', 'x', 'y') &&
  stringIndices('A\udc00x', '\udc00', 'x') && descriptionSlice(Symbol('\u00c9xy')) === 'y' &&
  descriptionSlice(Symbol('\ud801\udc00x')) === 'x' && descriptionSlice(Symbol('\ud800xy')) === 'y' &&
  descriptionCaptureIndex(Symbol('\u00c9xyz')) === 'y' &&
  descriptionCaptureIndex(Symbol('A\ud801\udc00x')) === '\udc00' &&
  descriptionCaptureIndex(Symbol('AB\ud800x')) === '\ud800';
var symbol56StringSliceEnd = observeSliceEnd();
var symbol57StringSliceBounds = stringSliceBounds('', '', '', '') &&
  stringSliceBounds('a', 'a', '', '') && stringSliceBounds('abcd', 'a', 'b', 'cd') &&
  stringSliceBounds('a\u0000b', 'a', '\u0000', 'b') &&
  !stringSliceBounds('abc', 'a', 'bc', 'c') && !stringSliceBounds('abc', 'a', 'b', 'bc');
var symbol58DescriptionCaptureSlice = descriptionCaptureSlice(Symbol()) === ':absent' &&
  descriptionCaptureSlice(Symbol(undefined)) === ':absent' && descriptionCaptureSlice(Symbol('')) === '' &&
  descriptionCaptureSlice(Symbol('a')) === '' && descriptionCaptureSlice(Symbol('abcd')) === 'bc' &&
  descriptionCaptureSlice(Symbol('a\u0000bc')) === '\u0000b' && descriptionCaptureSlice(Symbol.iterator) === 'ym';
var symbol59StringUTF16SliceBounds = stringSliceBounds('\u00c9xy', '\u00c9', 'x', 'y') &&
  stringSliceBounds('\ud801\udc00x', '\ud801', '\udc00', 'x') &&
  stringSliceBounds('\ud800xy', '\ud800', 'x', 'y') &&
  stringSliceBounds('A\udc00x', 'A', '\udc00', 'x') &&
  descriptionCaptureSlice(Symbol('\u00c9xyz')) === 'xy' &&
  descriptionCaptureSlice(Symbol('\ud801\udc00xy')) === '\udc00x' &&
  descriptionCaptureSlice(Symbol('A\ud800xy')) === '\ud800x';
var symbol60StringSliceNegative = observeNegativeSlice();
var symbol61StringSliceEndNegative = observeNegativeSliceEnd();
var symbol62StringSignedSlices = stringSignedSlices('', '', '', '') &&
  stringSignedSlices('a', 'a', '', '') && stringSignedSlices('abcd', 'd', 'abc', 'bc') &&
  stringSignedSlices('a\u0000b', 'b', 'a\u0000', '\u0000') &&
  !stringSignedSlices('abc', 'a', 'ab', 'b') && !stringSignedSlices('abc', 'c', 'bc', 'b') &&
  !stringSignedSlices('abc', 'c', 'ab', 'bc');
var symbol63DescriptionCaptureSignedSlice = descriptionCaptureSignedSlice(Symbol()) === ':absent' &&
  descriptionCaptureSignedSlice(Symbol(undefined)) === ':absent' &&
  descriptionCaptureSignedSlice(Symbol('')) === '' && descriptionCaptureSignedSlice(Symbol('a')) === '' &&
  descriptionCaptureSignedSlice(Symbol('abcd')) === 'bc' &&
  descriptionCaptureSignedSlice(Symbol('a\u0000bc')) === '\u0000b' &&
  descriptionCaptureSignedSlice(Symbol.iterator) === 'to';
var symbol64StringUTF16SignedSlices = stringSignedSlices('\u00c9xy', 'y', '\u00c9x', 'x') &&
  stringSignedSlices('\ud801\udc00', '\udc00', '\ud801', '') &&
  stringSignedSlices('A\ud800x', 'x', 'A\ud800', '\ud800') &&
  descriptionCaptureSignedSlice(Symbol('\u00c9xyz')) === 'xy' &&
  descriptionCaptureSignedSlice(Symbol('\ud801\udc00xy')) === '\udc00x' &&
  descriptionCaptureSignedSlice(Symbol('A\ud800xy')) === '\ud800x';
var symbol65StringCharAtNegative = observeNegativeCharAt();
var symbol66StringSignedCharAt = stringSignedCharAt('', '') && stringSignedCharAt('a', 'a') &&
  stringSignedCharAt('abc', 'a') && stringSignedCharAt('\u0000ab', '\u0000') &&
  !stringSignedCharAt('abc', '') && !stringSignedCharAt('abc', 'b');
var symbol67DescriptionCaptureSignedCharAt = descriptionCaptureSignedCharAt(Symbol()) === ':absent' &&
  descriptionCaptureSignedCharAt(Symbol(undefined)) === ':absent' &&
  descriptionCaptureSignedCharAt(Symbol('')) === '' &&
  descriptionCaptureSignedCharAt(Symbol('abc')) === 'a' &&
  descriptionCaptureSignedCharAt(Symbol('\u0000ab')) === '\u0000' &&
  descriptionCaptureSignedCharAt(Symbol.iterator) === 'S';
var symbol68StringUTF16SignedCharAt = stringSignedCharAt('\u00c9xy', '\u00c9') &&
  stringSignedCharAt('\ud801\udc00x', '\ud801') && stringSignedCharAt('\ud800xy', '\ud800') &&
  stringSignedCharAt('\udc00xy', '\udc00') &&
  descriptionCaptureSignedCharAt(Symbol('\u00c9xy')) === '\u00c9' &&
  descriptionCaptureSignedCharAt(Symbol('\ud801\udc00x')) === '\ud801' &&
  descriptionCaptureSignedCharAt(Symbol('\ud800xy')) === '\ud800';
var symbol69StringFractionalIndices = stringFractionalIndices('', '', '', '') &&
  stringFractionalIndices('a', 'a', '', '') && stringFractionalIndices('abcd', 'a', 'b', 'cd') &&
  stringFractionalIndices('a\u0000b', 'a', '\u0000', 'b') &&
  !stringFractionalIndices('abc', 'a', 'bc', 'c') && !stringFractionalIndices('abc', 'a', 'b', 'bc');
var symbol70DescriptionCaptureFractionalIndex = descriptionCaptureFractionalIndex(Symbol()) === ':absent' &&
  descriptionCaptureFractionalIndex(Symbol(undefined)) === ':absent' &&
  descriptionCaptureFractionalIndex(Symbol('')) === '' &&
  descriptionCaptureFractionalIndex(Symbol('Ab')) === 'Ab' &&
  descriptionCaptureFractionalIndex(Symbol('a\u0000b')) === 'a\u0000b' &&
  descriptionCaptureFractionalIndex(Symbol('\ud800')) === '\ud800' &&
  descriptionCaptureFractionalIndex(Symbol.iterator) === 'Symbol.iterator';
var symbol71StringUTF16FractionalIndices = stringFractionalIndices('\u00c9xy', '\u00c9', 'x', 'y') &&
  stringFractionalIndices('\ud801\udc00x', '\ud801', '\udc00', 'x') &&
  stringFractionalIndices('\ud800xy', '\ud800', 'x', 'y') &&
  stringFractionalIndices('A\udc00x', 'A', '\udc00', 'x');
"""
        + "".join(
            f"\nfunction observeFractional{i}() {{ {body}\nreturn "
            + " && ".join(
                f"bad({json.dumps(text)}, 0) === {json.dumps(value)}"
                for text, value in zip(("", "a", "abc"), expected, strict=True)
            )
            + f"; }}\nvar symbol{i}FractionalWitness{i} = observeFractional{i}();\n"
            for i, (body, expected) in enumerate(FRACTIONAL_WITNESSES.values(), 72)
        )
        + r"""
var symbol79StringWideIndices = stringWideIndices('') && stringWideIndices('a') &&
  stringWideIndices('abc') && stringWideIndices('a\u0000b') && stringWideIndices('\u00c9xy') &&
  stringWideIndices('\ud801\udc00x') && stringWideIndices('\ud800xy') && stringWideIndices('A\udc00x');
var symbol80DescriptionCaptureWideIndex = descriptionCaptureWideIndex(Symbol()) === ':absent' &&
  descriptionCaptureWideIndex(Symbol(undefined)) === ':absent' &&
  descriptionCaptureWideIndex(Symbol('')) === '' && descriptionCaptureWideIndex(Symbol('Ab')) === 'Ab' &&
  descriptionCaptureWideIndex(Symbol('a\u0000b')) === 'a\u0000b' &&
  descriptionCaptureWideIndex(Symbol('\ud800')) === '\ud800' &&
  descriptionCaptureWideIndex(Symbol.iterator) === 'Symbol.iterator';
"""
        + "".join(
            f"\nfunction observeWide{i}() {{ {body}\nreturn "
            + " && ".join(
                f"bad({json.dumps(text)}, 0) === {json.dumps(value)}"
                for text, value in zip(("", "a", "abc"), expected, strict=True)
            )
            + f"; }}\nvar symbol{i}WideWitness{i} = observeWide{i}();\n"
            for i, (body, expected) in enumerate(WIDE_WITNESSES.values(), 81)
        )
        + "".join(
            f"\nfunction observeDefault{i}() {{ {body}\nreturn "
            + " && ".join(
                f"bad({json.dumps(text)}, 0) === {json.dumps(value)}"
                for text, value in zip(("", "a", "abc"), expected, strict=True)
            )
            + f"; }}\nvar symbol{i}DefaultWitness{i} = observeDefault{i}();\n"
            for i, (body, expected) in enumerate(DEFAULT_WITNESSES.values(), 97)
        )
        + r"""
var symbol99StringDefaultIndices = stringDefaultIndices('', '') &&
  stringDefaultIndices('a', 'a') && stringDefaultIndices('abc', 'a') &&
  stringDefaultIndices('\u0000ab', '\u0000') &&
  !stringDefaultIndices('abc', '') && !stringDefaultIndices('abc', 'b');
var symbol100DescriptionCaptureDefaultIndex = descriptionCaptureDefaultIndex(Symbol()) === ':absent' &&
  descriptionCaptureDefaultIndex(Symbol(undefined)) === ':absent' &&
  descriptionCaptureDefaultIndex(Symbol('')) === '' &&
  descriptionCaptureDefaultIndex(Symbol('Ab')) === 'Ab' &&
  descriptionCaptureDefaultIndex(Symbol('a\u0000b')) === 'a\u0000b' &&
  descriptionCaptureDefaultIndex(Symbol('\ud800')) === '\ud800' &&
  descriptionCaptureDefaultIndex(Symbol.iterator) === 'Symbol.iterator';
var symbol101StringUTF16DefaultIndices = stringDefaultIndices('\u00c9xy', '\u00c9') &&
  stringDefaultIndices('\ud801\udc00x', '\ud801') && stringDefaultIndices('\ud800xy', '\ud800') &&
  stringDefaultIndices('\udc00xy', '\udc00');
"""
        + "".join(
            f"\nfunction observeUndefined{i}() {{ {body}\nreturn "
            + " && ".join(
                f"bad({json.dumps(text)}, 0) === {json.dumps(value)}"
                for text, value in zip(("", "a", "abc"), expected, strict=True)
            )
            + f"; }}\nvar symbol{i}UndefinedWitness{i} = observeUndefined{i}();\n"
            for i, (body, expected) in enumerate(UNDEFINED_WITNESSES.values(), 102)
        )
        + r"""
var symbol105StringUndefinedIndices = stringUndefinedIndices('', '', '', '') &&
  stringUndefinedIndices('a', 'a', '', '') && stringUndefinedIndices('abcd', 'a', 'b', 'cd') &&
  stringUndefinedIndices('a\u0000b', 'a', '\u0000', 'b') &&
  !stringUndefinedIndices('abc', 'a', 'bc', 'c') && !stringUndefinedIndices('abc', 'a', 'b', 'bc');
var symbol106DescriptionCaptureUndefinedIndex = descriptionCaptureUndefinedIndex(Symbol()) === ':absent' &&
  descriptionCaptureUndefinedIndex(Symbol(undefined)) === ':absent' &&
  descriptionCaptureUndefinedIndex(Symbol('')) === '' &&
  descriptionCaptureUndefinedIndex(Symbol('Ab')) === 'Ab' &&
  descriptionCaptureUndefinedIndex(Symbol('a\u0000b')) === 'a\u0000b' &&
  descriptionCaptureUndefinedIndex(Symbol('\ud800')) === '\ud800' &&
  descriptionCaptureUndefinedIndex(Symbol.iterator) === 'Symbol.iterator';
var symbol107StringUTF16UndefinedIndices = stringUndefinedIndices('\u00c9xy', '\u00c9', 'x', 'y') &&
  stringUndefinedIndices('\ud801\udc00x', '\ud801', '\udc00', 'x') &&
  stringUndefinedIndices('\ud800xy', '\ud800', 'x', 'y') &&
  stringUndefinedIndices('A\udc00x', 'A', '\udc00', 'x');
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
        *("Equality" + item[0] for item in EQUALITY_INPUTS),
        "HelperEquality",
        "GlobalHelper",
        "GlobalState",
        "GlobalTypes",
        "GlobalDescription",
        "GlobalLocalDeclaration",
        "GlobalLocalState",
        "GlobalLocalTypes",
        "GlobalLocalDescription",
        "CaptureLocal",
        "CaptureGlobal",
        "CaptureGlobalLocal",
        "CaptureState",
        "CaptureTypes",
        "CaptureDescription",
        "DescriptionTypeGuard",
        "DescriptionUndefinedGuard",
        "DescriptionCaptureGuard",
        "StringPrefix",
        "DescriptionLowercase",
        "DescriptionCapturePrefix",
        "DescriptionExpandedCase",
        "DescriptionGreekCase",
        "StringIndices",
        "DescriptionSlice",
        "DescriptionCaptureIndex",
        "StringUTF16Indices",
        "StringSliceEnd",
        "StringSliceBounds",
        "DescriptionCaptureSlice",
        "StringUTF16SliceBounds",
        "StringSliceNegative",
        "StringSliceEndNegative",
        "StringSignedSlices",
        "DescriptionCaptureSignedSlice",
        "StringUTF16SignedSlices",
        "StringCharAtNegative",
        "StringSignedCharAt",
        "DescriptionCaptureSignedCharAt",
        "StringUTF16SignedCharAt",
        "StringFractionalIndices",
        "DescriptionCaptureFractionalIndex",
        "StringUTF16FractionalIndices",
        *(f"FractionalWitness{i}" for i in range(72, 79)),
        "StringWideIndices",
        "DescriptionCaptureWideIndex",
        *(f"WideWitness{i}" for i in range(81, 97)),
        *(f"DefaultWitness{i}" for i in range(97, 99)),
        "StringDefaultIndices",
        "DescriptionCaptureDefaultIndex",
        "StringUTF16DefaultIndices",
        *(f"UndefinedWitness{i}" for i in range(102, 105)),
        "StringUndefinedIndices",
        "DescriptionCaptureUndefinedIndex",
        "StringUTF16UndefinedIndices",
    )
    expected = "".join(f"symbol{i:02}{name}=true\n" for i, name in enumerate(observations, 1))
    # The VM uses ASCII casing and byte indexing (Script/builtins/text/string.cpp).
    # Preserve those known differences; native uses the existing Unicode Core API.
    vm_expected = expected.replace(
        "symbol50DescriptionExpandedCase=true", "symbol50DescriptionExpandedCase=false"
    ).replace("symbol51DescriptionGreekCase=true", "symbol51DescriptionGreekCase=false")
    vm_expected = vm_expected.replace(
        "symbol55StringUTF16Indices=true", "symbol55StringUTF16Indices=false"
    ).replace("symbol59StringUTF16SliceBounds=true", "symbol59StringUTF16SliceBounds=false")
    vm_expected = vm_expected.replace(
        "symbol64StringUTF16SignedSlices=true", "symbol64StringUTF16SignedSlices=false"
    ).replace("symbol68StringUTF16SignedCharAt=true", "symbol68StringUTF16SignedCharAt=false")
    vm_expected = vm_expected.replace(
        "symbol71StringUTF16FractionalIndices=true", "symbol71StringUTF16FractionalIndices=false"
    ).replace("symbol101StringUTF16DefaultIndices=true", "symbol101StringUTF16DefaultIndices=false")
    vm_expected = vm_expected.replace(
        "symbol107StringUTF16UndefinedIndices=true", "symbol107StringUTF16UndefinedIndices=false"
    )
    actual = run([args.reference, str(vm)]).stdout
    if actual != "".join(sorted(vm_expected.splitlines(keepends=True))):
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
    agreements = sum(
        a == b for a, b in zip(expected.splitlines(), vm_expected.splitlines(), strict=True)
    )
    return agreements, len(observations) - agreements


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
    agreements, differences = oracle(args)
    compilers = find_compilers()
    compilers[1] = args.clang
    includes, libraries = dom.link_options(args, core_only=True)
    accepted = {}
    for name, (source, checks, expected) in CASES.items():
        entry_name = (
            "bad"
            if name == "global-helper" or name.startswith("capture-witness-")
            else re.search(r"function (\w+)\(", source)[1]
        )
        ir, contract = prepare(
            args, name, source, entry_name=entry_name, parameter_types=PARAMETER_TYPES.get(name)
        )
        if (
            name
            in DESCRIPTION_GUARDS
            | STRING_METHODS
            | STRING_INDICES
            | STRING_SLICES
            | SIGNED_SLICES
            | SIGNED_CHARAT
            | FRACTIONAL_INDICES
            | WIDE_INDICES
            | DEFAULT_INDICES
            | UNDEFINED_INDICES
        ):
            contract["initial_intrinsics"] = ["Symbol", "String"]
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
        "wrapper-capture": "const key=Symbol.iterator; function bad() { function helper() { return key; } return helper(); }",
        "intrinsic-declaration": "function Symbol() { return 1; }",
    }
    for name, source in source_refusals.items():
        entry = "Symbol" if name == "intrinsic-declaration" else "bad"
        ir, contract = prepare(args, name, source, entry_name=entry)
        refuse(ir, contract, name + "-refused")

    for name, source in {
        "replacement": "function helper(value) { return value; } function bad(key) { helper=key; return helper(key); }",
        "late-replacement": "function helper(value) { return value; } function bad(key) { return helper(key); } helper=Symbol.iterator;",
        "wrapper-call": "function helper(value) { return value; } helper(Symbol.iterator); function bad(key) { return helper(key); }",
        "wrapper-effect": "var saved=1; function helper(value) { return value; } function bad(key) { return helper(key); }",
        "identity": "function helper(value) { return value; } function bad(key) { return helper === helper; }",
        "escape": "function helper(value) { return value; } function bad(key) { return helper; }",
        "argument": "function helper(value) { return value; } function bad(key) { return helper(helper); }",
        "recursive": "function helper(value) { return helper(value); } function bad(key) { return helper(key); }",
        "mutual": "function helper(value) { return other(value); } function other(value) { return helper(value); } function bad(key) { return helper(key); }",
        "unknown": "function helper(value) { unknown(); return value; } function bad(key) { return helper(key); }",
        "discarded-argument": "function helper(value) { return value; } function bad(key) { return helper(unknown()); }",
        "unused-effect": "function helper(value) { unknown(); return value; } function bad(key) { return key; }",
        "foreign-global": "function helper(value) { return saved; } function bad(key) { return helper(key); }",
        "receiver": "function helper(value) { return this; } function bad(key) { return helper(key); }",
        "receiver-call": "function helper(value) { return value; } function bad(key) { return helper.call(key, key); }",
        "transitive-write": "function helper(value) { return other(value); } function other(value) { saved=value; return value; } function bad(key) { return helper(key); }",
        "extra-argument": "function helper(value) { return value; } function bad(key) { return helper(key, key); }",
    }.items():
        ir, contract = prepare(
            args, "global-" + name, source, entry_name="bad", parameter_types=["symbol"]
        )
        for optimize in (False, True):
            refuse(ir, contract, f"global-{name}-{optimize}", optimize=optimize)

    helper_refusals = {
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
    for name, body in {
        "receiver": "function local(input) { return this; } return local(value);",
        "unused-receiver": "function local(input) { return this; } return value;",
        "recursive": "function local(input) { return local(input); } return local(value);",
        "escape": "function local(input) { return input; } return local;",
        "discarded-argument": "function local(input) { return Symbol.iterator; } return local(unknown());",
        "unused-effect": "function local(input) { unknown(); return input; } return value;",
        "shadowed-intrinsic": "function local(Symbol) { return Symbol.iterator; } return local(value);",
    }.items():
        ir, contract = prepare(
            args,
            "global-local-" + name,
            "function bad(key) { return helper(key); }\n" f"function helper(value) {{ {body} }}\n",
            entry_name="bad",
            parameter_types=["symbol"],
        )
        for optimize in (False, True):
            refuse(ir, contract, f"global-local-{name}-{optimize}", optimize=optimize)
    for name, body in {
        "outer-write": "let value=key; function helper() { return value; } const saved=helper(); value=Symbol.iterator; return saved === helper();",
        "inner-write": "function helper() { key=Symbol.iterator; return key; } return helper();",
        "sibling-write": "function read() { return key; } function write() { key=Symbol.iterator; } write(); return read();",
        "early-call": "function helper() { return value; } const saved=helper(); const value=key; return saved;",
        "escape": "function helper() { return key; } return helper;",
        "identity": "function helper() { return key; } return helper === helper;",
        "receiver": "function helper() { return this || key; } return helper();",
        "recursive": "function helper() { return key && helper(); } return helper();",
        "unused": "function helper() { return key; } return key;",
        "unused-effect": "function helper() { unknown(); return key; } return key;",
        "discarded-argument": "function helper(value) { return key; } return helper(unknown());",
        "coercion": "function helper() { return +key; } return helper();",
        "shadowed-intrinsic": "const Symbol=key; function helper() { return Symbol.iterator; } return helper();",
    }.items():
        ir, contract = prepare(
            args,
            "capture-" + name,
            f"function bad(key) {{ {body} }}\n",
            entry_name="bad",
            parameter_types=["symbol"],
        )
        for optimize in (False, True):
            refuse(ir, contract, f"capture-{name}-{optimize}", optimize=optimize)
    for name, source, entry, types in (
        ("capture-selected", CAPTURE_SELECTED, "captureState", "capture-state"),
        ("capture-mutable-result", CAPTURE_MUTABLE_RESULT, "captureState", "capture-state"),
    ):
        ir, contract = prepare(
            args, name, source, entry_name=entry, parameter_types=PARAMETER_TYPES[types]
        )
        for optimize in (False, True):
            refuse(ir, contract, f"{name}-{optimize}", optimize=optimize)
    for name, body in {
        "strict-symbol": "return number === Symbol.iterator;",
        "loose-symbol": "return number == Symbol.iterator;",
        "strict-string": "return number === '1';",
        "loose-string": "return number == '1';",
        "loose-undefined": "return number == undefined;",
        "strict-null": "return number === null;",
        "loose-null": "return number == null;",
        "description": "return Symbol().description == number;",
        "object": "return {valueOf() { return 1; }} == number;",
        "mixed-join": "return (flag ? number : flag) == number;",
    }.items():
        ir, contract = prepare(
            args,
            "equality-" + name,
            f"function bad(number, flag) {{ {body} }}\n",
            entry_name="bad",
            parameter_types=["number", "boolean"],
        )
        for optimize in (False, True):
            refuse(ir, contract, f"equality-{name}-{optimize}", optimize=optimize)
    ir, contract = accepted["helper-state"]
    for budget in (0, 1, 100, 500):
        refuse(ir, contract, f"helper-budget-{budget}", max_steps=budget)
    ir, contract = accepted["global-helper"]
    for budget in (0, 1, 100):
        refuse(ir, contract, f"global-budget-{budget}", max_steps=budget)
    changed = args.work / "global-stale.mlir"
    changed.write_text(ir.read_text().replace('"iterator"', '"hasInstance"', 1))
    if changed.read_text() == ir.read_text():
        raise RuntimeError("global helper fingerprint control did not change its body")
    if "fingerprint mismatch" not in refuse(changed, contract, "global-stale"):
        raise RuntimeError("changed global helper body accepted a stale fingerprint")
    ir, contract = accepted["global-local-declaration"]
    for budget in (0, 1, 100, 500):
        refuse(ir, contract, f"global-local-budget-{budget}", max_steps=budget)
    changed = args.work / "global-local-stale.mlir"
    changed.write_text(ir.read_text().replace('"iterator"', '"hasInstance"', 1))
    if changed.read_text() == ir.read_text():
        raise RuntimeError("global local helper fingerprint control did not change its body")
    if "fingerprint mismatch" not in refuse(changed, contract, "global-local-stale"):
        raise RuntimeError("changed global local helper body accepted a stale fingerprint")
    ir, contract = accepted["capture-types"]
    for budget in (0, 1, 100, 500):
        refuse(ir, contract, f"capture-budget-{budget}", max_steps=budget)
    changed = args.work / "capture-stale.mlir"
    changed.write_text(ir.read_text().replace('"symbol"', '"changed"', 1))
    if changed.read_text() == ir.read_text():
        raise RuntimeError("capture helper fingerprint control did not change its body")
    if "fingerprint mismatch" not in refuse(changed, contract, "capture-stale"):
        raise RuntimeError("changed captured helper body accepted a stale fingerprint")
    ir, contract = accepted["capture-witness-local"]
    captures = [
        name for name in dom.FUNCTION.findall(ir.read_text()) if name.rsplit("$", 1)[0] == "helper"
    ]
    if len(captures) != 1:
        raise RuntimeError("captured entry control did not find the source helper")
    refuse(ir, dict(contract, entry=captures[0], parameter_types=[]), "captured-entry")

    for name in (
        *DESCRIPTION_GUARDS,
        *STRING_METHODS,
        *STRING_INDICES,
        *STRING_SLICES,
        *SIGNED_SLICES,
        *SIGNED_CHARAT,
        *FRACTIONAL_INDICES,
        *WIDE_INDICES,
        *DEFAULT_INDICES,
        *UNDEFINED_INDICES,
    ):
        ir, contract = accepted[name]
        for optimize in (False, True):
            refuse(
                ir,
                dict(contract, initial_intrinsics=["Symbol"]),
                f"{name}-missing-string-{optimize}",
                optimize=optimize,
            )
    for name, body in {
        "unguarded": "return key.description.charAt(0);",
        "absent-arm": "const text=key.description; return text === undefined ? text.charAt(0) : '';",
        "wrong-typeof": "const text=key.description; return typeof text === 'object' ? '' : text.charAt(0);",
        "false-truthy": "const text=key.description; return !text ? text.charAt(0) : '';",
        "different-read": "const text=key.description; return typeof text === 'string' ? key.description.charAt(0) : '';",
        "after-guard": "const text=key.description; const first=typeof text === 'string' ? text.charAt(0) : ''; return text.slice(1);",
        "prototype-write": "String.prototype.charAt=0; const text=key.description; return typeof text === 'string' ? text.charAt(0) : '';",
        "implicit-coercion": "const text=key.description; return typeof text === 'string' ? text.charAt(key) : '';",
    }.items():
        ir, contract = prepare(
            args,
            "description-guard-" + name,
            f"function bad(key) {{ {body} }}\n",
            entry_name="bad",
            parameter_types=["symbol"],
        )
        contract["initial_intrinsics"] = ["Symbol", "String"]
        for optimize in (False, True):
            refuse(ir, contract, f"description-guard-{name}-{optimize}", optimize=optimize)
    ir, contract = accepted["description-type-guard"]
    refuse(ir, contract, "description-guard-budget", max_steps=100)
    changed = args.work / "description-guard-stale.mlir"
    changed.write_text(ir.read_text().replace('"string"', '"object"', 1))
    if changed.read_text() == ir.read_text():
        raise RuntimeError("description guard fingerprint control did not change its predicate")
    if "fingerprint mismatch" not in refuse(changed, contract, "description-guard-stale"):
        raise RuntimeError("changed description guard accepted a stale fingerprint")

    for name, body in {
        "prefix-coercion": "return text.startsWith(key);",
        "prefix-unicode": "return text.startsWith('é');",
        "prefix-position": "return text.startsWith('bs', 1);",
        "detached-prefix": "const method=text.startsWith; return method('bs');",
        "prefix-prototype-write": "String.prototype.startsWith=0; return text.startsWith('bs');",
        "lowercase-prototype-write": "String.prototype.toLowerCase=0; return text.charAt(0).toLowerCase();",
        "lowercase-whole-string": "return text.toLowerCase();",
        "lowercase-argument": "return text.charAt(0).toLowerCase('unused');",
        "description-unguarded": "return key.description.startsWith('bs');",
        "description-absent-arm": "const saved=key.description; return saved === undefined ? saved.startsWith('bs') : false;",
        "description-different-read": "const saved=key.description; return typeof saved === 'string' ? key.description.startsWith('bs') : false;",
        "description-after-guard": "const saved=key.description; const prefix=typeof saved === 'string' ? saved.startsWith('bs') : false; return saved.startsWith('bs');",
        "charat-index": "return text.charAt(1).toLowerCase();",
        "slice-index": "return text.slice(0).toLowerCase();",
    }.items():
        ir, contract = prepare(
            args,
            "string-method-" + name,
            f"function bad(key, text) {{ {body} }}\n",
            entry_name="bad",
            parameter_types=["symbol", "string"],
        )
        contract["initial_intrinsics"] = ["Symbol", "String"]
        for optimize in (False, True):
            refuse(ir, contract, f"string-method-{name}-{optimize}", optimize=optimize)
    ir, contract = accepted["description-capture-prefix"]
    refuse(ir, contract, "string-method-budget", max_steps=100)
    changed = args.work / "string-method-stale.mlir"
    changed.write_text(ir.read_text().replace('"bs"', '"other"', 1))
    if changed.read_text() == ir.read_text():
        raise RuntimeError("String method fingerprint control did not change its prefix")
    if "fingerprint mismatch" not in refuse(changed, contract, "string-method-stale"):
        raise RuntimeError("changed String method accepted a stale fingerprint")

    for name, body in {
        "charat-dynamic": "return text.charAt(index);",
        "slice-dynamic": "return text.slice(index);",
        "charat-coercion": "return text.charAt('1');",
        "slice-coercion": "return text.slice('2');",
        "charat-extra": "return text.charAt(1, 2);",
        "slice-prototype-write": "String.prototype.slice=0; return text.slice(2);",
    }.items():
        ir, contract = prepare(
            args,
            "string-index-" + name,
            f"function bad(text, index) {{ {body} }}\n",
            entry_name="bad",
            parameter_types=["string", "number"],
        )
        contract["initial_intrinsics"] = ["Symbol", "String"]
        for optimize in (False, True):
            refuse(ir, contract, f"string-index-{name}-{optimize}", optimize=optimize)
    ir, contract = accepted["description-capture-index"]
    refuse(ir, contract, "string-index-budget", max_steps=100)
    changed = args.work / "string-index-stale.mlir"
    # Number attributes store IEEE-754 bits: change the literal 2 to 1.
    changed.write_text(
        ir.read_text().replace(
            "#ctjs.number<4611686018427387904>", "#ctjs.number<4607182418800017408>", 1
        )
    )
    if changed.read_text() == ir.read_text():
        raise RuntimeError("String index fingerprint control did not change its index")
    if "fingerprint mismatch" not in refuse(changed, contract, "string-index-stale"):
        raise RuntimeError("changed String index accepted a stale fingerprint")

    for name, body in {
        "end-dynamic": "return text.slice(0, index);",
        "end-coercion": "return text.slice(0, '2');",
        "end-object": "return text.slice(0, {valueOf() { return 2; }});",
        "end-effect": "return text.slice(0, unknown());",
        "start-dynamic": "return text.slice(index, 2);",
        "extra": "return text.slice(0, 2, 3);",
        "detached": "const method=text.slice; return method(0, 2);",
        "replacement": "String.prototype.slice=0; return text.slice(0, 2);",
        "first-unit-authority": "return text.slice(0, 1).toLowerCase();",
    }.items():
        ir, contract = prepare(
            args,
            "string-slice-" + name,
            f"function bad(text, index) {{ {body} }}\n",
            entry_name="bad",
            parameter_types=["string", "number"],
        )
        contract["initial_intrinsics"] = ["Symbol", "String"]
        for optimize in (False, True):
            refuse(ir, contract, f"string-slice-{name}-{optimize}", optimize=optimize)
    ir, contract = accepted["description-capture-slice"]
    for budget in (0, 1, 100):
        refuse(ir, contract, f"string-slice-budget-{budget}", max_steps=budget)
    changed = args.work / "string-slice-stale.mlir"
    # Change only the literal end bound from 3 to 2, preserving the start.
    changed.write_text(
        ir.read_text().replace(
            "#ctjs.number<4613937818241073152>", "#ctjs.number<4611686018427387904>", 1
        )
    )
    if changed.read_text() == ir.read_text():
        raise RuntimeError("String slice fingerprint control did not change its end")
    if "fingerprint mismatch" not in refuse(changed, contract, "string-slice-stale"):
        raise RuntimeError("changed String slice accepted a stale fingerprint")

    for name, body in {
        "start-dynamic": "return text.slice(-index);",
        "end-dynamic": "return text.slice(0, -index);",
        "start-coercion": "return text.slice(-'1');",
        "end-object": "return text.slice(0, -{valueOf() { return 1; }});",
        "nested-negation": "return text.slice(-(-1));",
        "negation-escape": "const bound=-1; text.slice(bound); return bound;",
        "replacement": "String.prototype.slice=0; return text.slice(-1);",
        "first-unit-authority": "return text.slice(-1).toLowerCase();",
    }.items():
        ir, contract = prepare(
            args,
            "signed-slice-" + name,
            f"function bad(text, index) {{ {body} }}\n",
            entry_name="bad",
            parameter_types=["string", "number"],
        )
        contract["initial_intrinsics"] = ["Symbol", "String"]
        for optimize in (False, True):
            refuse(ir, contract, f"signed-slice-{name}-{optimize}", optimize=optimize)
    ir, contract = accepted["description-capture-signed-slice"]
    for budget in (0, 1, 100):
        refuse(ir, contract, f"signed-slice-budget-{budget}", max_steps=budget)
    changed = args.work / "signed-slice-stale.mlir"
    # Source -3 is an original literal 3 followed by its own Neg operation.
    changed.write_text(
        ir.read_text().replace(
            "#ctjs.number<4613937818241073152>", "#ctjs.number<4611686018427387904>", 1
        )
    )
    if changed.read_text() == ir.read_text():
        raise RuntimeError("signed String slice fingerprint control did not change its start")
    if "fingerprint mismatch" not in refuse(changed, contract, "signed-slice-stale"):
        raise RuntimeError("changed signed String slice accepted a stale fingerprint")

    for name, body in {
        "dynamic": "return text.charAt(-index);",
        "coercion": "return text.charAt(-'1');",
        "object": "return text.charAt(-{valueOf() { return 1; }});",
        "nested-negation": "return text.charAt(-(-1));",
        "negation-escape": "const bound=-1; text.charAt(bound); return bound;",
        "replacement": "String.prototype.charAt=0; return text.charAt(-1);",
        "first-unit-authority": "return text.charAt(-1).toLowerCase();",
        "negative-zero-authority": "return text.charAt(-0).toLowerCase();",
        "extra-bound": "return text.charAt(0, -1);",
        "extra-argument": "return text.charAt(-1, 0);",
        "detached": "const method=text.charAt; return method(-1);",
    }.items():
        ir, contract = prepare(
            args,
            "signed-charat-" + name,
            f"function bad(text, index) {{ {body} }}\n",
            entry_name="bad",
            parameter_types=["string", "number"],
        )
        contract["initial_intrinsics"] = ["Symbol", "String"]
        for optimize in (False, True):
            refuse(ir, contract, f"signed-charat-{name}-{optimize}", optimize=optimize)
    ir, contract = accepted["description-capture-signed-charat"]
    for budget in (0, 1, 100):
        refuse(ir, contract, f"signed-charat-budget-{budget}", max_steps=budget)
    changed = args.work / "signed-charat-stale.mlir"
    # Change the literal magnitude 1 to 2 while retaining its original Neg.
    changed.write_text(
        ir.read_text().replace(
            "#ctjs.number<4607182418800017408>", "#ctjs.number<4611686018427387904>", 1
        )
    )
    if changed.read_text() == ir.read_text():
        raise RuntimeError("signed charAt fingerprint control did not change its index")
    if "fingerprint mismatch" not in refuse(changed, contract, "signed-charat-stale"):
        raise RuntimeError("changed signed charAt accepted a stale fingerprint")

    for name, body in {
        "charat-coercion": "return text.charAt('1.5');",
        "slice-coercion": "return text.slice(-'1.5');",
        "charat-dynamic": "return text.charAt(index + 0.5);",
        "slice-dynamic": "return text.slice(0, index - 0.5);",
        "negation-escape": "const bound=-0.5; text.charAt(bound); return bound;",
        "extra-argument": "return text.charAt(0, -0.5);",
        "detached": "const method=text.slice; return method(-0.5);",
        "first-unit-authority": "return text.charAt(0.5).toLowerCase();",
        "negative-zero-authority": "return text.charAt(-0.5).toLowerCase();",
        "description-unguarded": "return Symbol(text).description.charAt(-0.5);",
    }.items():
        ir, contract = prepare(
            args,
            "fractional-index-" + name,
            f"function bad(text, index) {{ {body} }}\n",
            entry_name="bad",
            parameter_types=["string", "number"],
        )
        contract["initial_intrinsics"] = ["Symbol", "String"]
        for optimize in (False, True):
            refuse(ir, contract, f"fractional-index-{name}-{optimize}", optimize=optimize)
    ir, contract = accepted["description-capture-fractional-index"]
    for budget in (0, 1, 100):
        refuse(ir, contract, f"fractional-index-budget-{budget}", max_steps=budget)
    changed = args.work / "fractional-index-stale.mlir"
    # Change only the literal 0.75 to 1.5 under its original Neg operation.
    changed.write_text(
        ir.read_text().replace(
            "#ctjs.number<4604930618986332160>", "#ctjs.number<4609434218613702656>", 1
        )
    )
    if changed.read_text() == ir.read_text():
        raise RuntimeError("fractional index fingerprint control did not change its magnitude")
    if "fingerprint mismatch" not in refuse(changed, contract, "fractional-index-stale"):
        raise RuntimeError("changed fractional index accepted a stale fingerprint")

    for name, body in {
        "charat-nan-global": "return text.charAt(NaN);",
        "slice-nan-global": "return text.slice(0, NaN);",
        "charat-infinity-global": "return text.charAt(Infinity);",
        "slice-infinity-global": "return text.slice(-Infinity);",
        "charat-nan-expression": "return text.charAt(0 / 0);",
        "slice-infinity-expression": "return text.slice(1 / 0);",
        "coercion": "return text.charAt('1e999');",
        "object": "return text.slice(0, {valueOf() { return 1e999; }});",
        "negation-escape": "const bound=-1e999; text.slice(bound); return bound;",
        "extra-bound": "return text.charAt(0, -1e999);",
        "first-unit-authority": "return text.charAt(1e999).toLowerCase();",
        "slice-authority": "return text.slice(-1e999).toLowerCase();",
    }.items():
        ir, contract = prepare(
            args,
            "wide-index-" + name,
            f"function bad(text, index) {{ {body} }}\n",
            entry_name="bad",
            parameter_types=["string", "number"],
        )
        contract["initial_intrinsics"] = ["Symbol", "String"]
        for optimize in (False, True):
            refuse(ir, contract, f"wide-index-{name}-{optimize}", optimize=optimize)
    ir, contract = accepted["description-capture-wide-index"]
    for budget in (0, 1, 100):
        refuse(ir, contract, f"wide-index-budget-{budget}", max_steps=budget)
    changed = args.work / "wide-index-stale.mlir"
    # Replace literal +infinity with 1, retaining the original Neg operation.
    changed.write_text(
        ir.read_text().replace(
            "#ctjs.number<9218868437227405312>", "#ctjs.number<4607182418800017408>", 1
        )
    )
    if changed.read_text() == ir.read_text():
        raise RuntimeError("wide index fingerprint control did not change its infinity")
    if "fingerprint mismatch" not in refuse(changed, contract, "wide-index-stale"):
        raise RuntimeError("changed wide index accepted a stale fingerprint")
    ir, contract = accepted["string-index-charat-infinity"]
    changed = args.work / "wide-index-nan.mlir"
    changed.write_text(
        ir.read_text().replace(
            "#ctjs.number<9218868437227405312>", "#ctjs.number<9221120237041090560>", 1
        )
    )
    if changed.read_text() == ir.read_text():
        raise RuntimeError("wide index NaN control did not replace its infinity")
    contract = dict(contract, module_sha256=fingerprint(args.opt, changed))
    for optimize in (False, True):
        refuse(changed, contract, f"wide-index-nan-literal-{optimize}", optimize=optimize)

    for name, body in {
        "charat-detached": "const method=text.charAt; return method();",
        "slice-detached": "const method=text.slice; return method();",
        "charat-replacement": "String.prototype.charAt=0; return text.charAt();",
        "slice-replacement": "String.prototype.slice=0; return text.slice();",
        "charat-authority": "return text.charAt().toLowerCase();",
        "slice-authority": "return text.slice().toLowerCase();",
        "description-unguarded": "return Symbol(text).description.charAt();",
        "description-different-read": "const key=Symbol(text); const saved=key.description; return saved !== undefined ? key.description.slice() : '';",
    }.items():
        ir, contract = prepare(
            args,
            "default-index-" + name,
            f"function bad(text) {{ {body} }}\n",
            entry_name="bad",
            parameter_types=["string"],
        )
        contract["initial_intrinsics"] = ["Symbol", "String"]
        for optimize in (False, True):
            refuse(ir, contract, f"default-index-{name}-{optimize}", optimize=optimize)
    ir, contract = accepted["description-capture-default-index"]
    for budget in (0, 1, 100):
        refuse(ir, contract, f"default-index-budget-{budget}", max_steps=budget)
    changed = args.work / "default-index-stale.mlir"
    changed.write_text(ir.read_text().replace('"slice"', '"charAt"', 1))
    if changed.read_text() == ir.read_text():
        raise RuntimeError("default index fingerprint control did not change its method")
    if "fingerprint mismatch" not in refuse(changed, contract, "default-index-stale"):
        raise RuntimeError("changed default index accepted a stale fingerprint")

    for name, body in {
        "charat-coercion": "return text.charAt(null);",
        "slice-coercion": "return text.slice(undefined, null);",
        "end-dynamic": "return text.slice(undefined, text);",
        "extra": "return text.slice(undefined, undefined, 1);",
        "charat-authority": "return text.charAt(undefined).toLowerCase();",
        "slice-authority": "return text.slice(1, undefined).toLowerCase();",
        "charat-detached": "const method=text.charAt; return method(undefined);",
        "slice-replacement": "String.prototype.slice=0; return text.slice(undefined);",
        "changed-local": "let index=undefined; index=text; return text.slice(index);",
        "dead-effect": "function unused() { unknown(); } return text.slice(undefined);",
        "description-unguarded": "return Symbol(text).description.slice(undefined);",
        "description-different-read": "const key=Symbol(text); const saved=key.description; return saved !== undefined ? key.description.slice(undefined) : '';",
    }.items():
        ir, contract = prepare(
            args,
            "undefined-index-" + name,
            f"function bad(text) {{ {body} }}\n",
            entry_name="bad",
            parameter_types=["string"],
        )
        contract["initial_intrinsics"] = ["Symbol", "String"]
        for optimize in (False, True):
            refuse(ir, contract, f"undefined-index-{name}-{optimize}", optimize=optimize)
    ir, contract = accepted["description-capture-undefined-index"]
    for budget in (0, 1, 100):
        refuse(ir, contract, f"undefined-index-budget-{budget}", max_steps=budget)
    changed = args.work / "undefined-index-stale.mlir"
    changed.write_text(ir.read_text().replace('"slice"', '"charAt"', 1))
    if changed.read_text() == ir.read_text():
        raise RuntimeError("undefined index fingerprint control did not change its method")
    if "fingerprint mismatch" not in refuse(changed, contract, "undefined-index-stale"):
        raise RuntimeError("changed undefined index accepted a stale fingerprint")

    ir, contract = accepted["state"]
    refuse(ir, dict(contract, entry="_script_$0"), "script-entry")
    for name, fields in {
        "empty": {"initial_intrinsics": []},
        "extra": {"initial_intrinsics": ["Symbol", "Number"]},
        "duplicate": {"initial_intrinsics": ["Symbol", "Symbol"]},
        "string-only": {"initial_intrinsics": ["String"]},
        "duplicate-string": {"initial_intrinsics": ["Symbol", "String", "String"]},
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
        f"Symbol exports: typed parameters/helpers/captures, guarded String methods, scalar equality and branch/loop return; "
        f"{agreements} Node/VM agreements, {differences} known VM casing/index differences; "
        f"{8 * len(CASES)} native executions, {refusals} refusals, 2 mutations; Core only, no DOM inputs"
    )


if __name__ == "__main__":
    main()
