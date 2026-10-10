#!/usr/bin/env python3
"""Confined node catches preserve identity and reads without a native throw."""

import json
from urllib.parse import unquote

from CTNative.Browser.dom import dom
from CTNative.Browser.dom.custom_iteration import INTRINSICS
from CTNative.harness import run

SOURCES = {
    "identity": "function customElements(anchor) { try { throw anchor; } catch (error) { return error === anchor; } }\n",
    "read": "function customElements(anchor) { try { throw anchor; } catch (error) { return error.hasAttribute('data-closed'); } }\n",
    "conditional-node-identity": "function customElements(anchor) { const flag = anchor.hasAttribute('data-closed'); try { if (flag) { throw anchor; } else { throw anchor; } } catch (error) { return error === anchor; } }\n",
    "conditional-node-read": "function customElements(anchor) { const flag = anchor.hasAttribute('data-closed'); try { if (flag) { throw anchor; } else { throw anchor; } } catch (error) { return error.hasAttribute('data-closed'); } }\n",
    "conditional-node-state": "function customElements(anchor) { const flag = anchor.hasAttribute('data-closed'); let saved = false; try { if (flag) { saved = true; throw anchor; } else { throw anchor; } } catch (error) { return error === anchor && saved; } }\n",
    "mixed-node-identity": "function customElements(anchor) { const flag = anchor.hasAttribute('data-closed'); try { if (flag) { throw anchor; } } catch (error) { return error === anchor; } return false; }\n",
    "mixed-node-read": "function customElements(anchor) { const flag = anchor.hasAttribute('data-closed'); try { if (flag) { throw anchor; } } catch (error) { return error.hasAttribute('data-closed'); } return false; }\n",
    "mixed-node-state": "function customElements(anchor) { const flag = anchor.hasAttribute('data-closed'); let saved = false; try { if (flag) { saved = true; throw anchor; } else { saved = true; } } catch (error) { return error === anchor && saved; } return !saved; }\n",
    "mixed-node-normal-return": "function customElements(anchor) { const flag = anchor.hasAttribute('data-closed'); try { if (flag) { throw anchor; } else { return false; } } catch (error) { return error === anchor; } }\n",
    "mixed-node-reversed-state": "function customElements(anchor) { const flag = anchor.hasAttribute('data-closed'); let saved = false; try { if (flag) { saved = true; } else { saved = true; throw anchor; } } catch (error) { return error === anchor && saved; } return !saved; }\n",
    "conditional": "function customElements(anchor) { try { if (anchor) throw anchor; } catch (error) { return error === anchor; } }\n",
    "mixed-node-nested-state": "function customElements(anchor) { const flag = anchor.hasAttribute('data-closed'); const inner = anchor.hasAttribute('data-inner'); let saved = false; try { if (flag) { saved = true; if (inner) { throw anchor; } } else { saved = true; } } catch (error) { return error === anchor && saved; } return !saved; }\n",
    "mixed-node-nested-identity": "function customElements(anchor) { const flag = anchor.hasAttribute('data-closed'); const inner = anchor.hasAttribute('data-inner'); try { if (flag) { if (inner) { throw anchor; } } } catch (error) { return error === anchor; } return false; }\n",
    "mixed-node-nested-read": "function customElements(anchor) { const flag = anchor.hasAttribute('data-closed'); const inner = anchor.hasAttribute('data-inner'); try { if (flag) { if (inner) { throw anchor; } } } catch (error) { return error.hasAttribute('data-closed'); } return false; }\n",
    "protected-read": "function customElements(anchor) { try { anchor.hasAttribute('x'); throw anchor; } catch (error) { return error === anchor; } }\n",
    "mixed-protected-normal-read": "function customElements(anchor) { const flag = anchor.hasAttribute('data-closed'); try { if (flag) { throw anchor; } else { anchor.hasAttribute('x'); } } catch (error) { return error === anchor; } return false; }\n",
    "nested-protected-normal-read": "function customElements(anchor) {  const flag = anchor.hasAttribute('data-closed'); const inner = anchor.hasAttribute('data-inner'); let saved = false; try { if (flag) { saved = true; if (inner) { throw anchor; } } else { anchor.hasAttribute('x'); saved = true; } } catch (error) { return error === anchor && saved; } return !saved;  }\n",
    "protected-conditional-node-state": "function customElements(anchor) { let saved = false; try { if (anchor.hasAttribute('data-closed')) { saved = true; throw anchor; } else { throw anchor; } } catch (error) { return error === anchor && saved; } }\n",
    "invocation-return": "function customElements(anchor) { try { return anchor.hasAttribute('data-closed'); } catch (error) { return error === anchor; } }\n",
    "invocation-saved-state": "function customElements(anchor) { let saved = false; try { saved = anchor.hasAttribute('data-closed'); return saved; } catch (error) { return error === anchor && saved; } }\n",
    "invocation-conditional-return": "function customElements(anchor) { const flag = anchor.hasAttribute('data-closed'); let saved = false; try { if (flag) { saved = anchor.hasAttribute('data-closed'); } return saved; } catch (error) { return error === anchor && saved; } }\n",
    "invocation-multiple-reads": "function customElements(anchor) { try { const first = anchor.hasAttribute('data-closed'); const second = anchor.hasAttribute('x'); return first && !second; } catch (error) { return error === anchor; } }\n",
    "invocation-read-then-throw": "function customElements(anchor) { let saved = false; try { saved = anchor.hasAttribute('data-closed'); throw anchor; } catch (error) { return error === anchor && saved; } }\n",
    "write-throw": "function customElements(anchor) { try { anchor.setAttribute('data-written', 'yes'); throw anchor; } catch (error) { return error === anchor && anchor.hasAttribute('data-written'); } }\n",
    "write-return": "function customElements(anchor) { try { anchor.setAttribute('data-written', 'yes'); return true; } catch (error) { return false; } }\n",
    "write-conditional": "function customElements(anchor) { const flag = anchor.hasAttribute('data-closed'); try { if (flag) { anchor.setAttribute('data-written', 'yes'); } throw anchor; } catch (error) { return error === anchor && anchor.hasAttribute('data-written'); } }\n",
    "write-saved-state": "function customElements(anchor) { let saved = false; try { saved = anchor.hasAttribute('data-closed'); anchor.setAttribute('data-written', 'yes'); throw anchor; } catch (error) { return error === anchor && saved && anchor.hasAttribute('data-written'); } }\n",
    "write-boolean-constants": "function customElements(anchor) { try { anchor.setAttribute('data-first', true); anchor.setAttribute('data-written', false); throw anchor; } catch (error) { return error === anchor; } }\n",
    "write-boolean-read-return": "function customElements(anchor) { try { const saved = anchor.hasAttribute('data-closed'); anchor.setAttribute('data-written', saved); return saved; } catch (error) { return false; } }\n",
    "write-boolean-snapshot": "function customElements(anchor) { let saved = false; try { saved = anchor.hasAttribute('data-closed'); anchor.setAttribute('data-closed', true); anchor.setAttribute('data-written', saved); throw anchor; } catch (error) { return error === anchor && saved; } }\n",
    "write-boolean-conditional": "function customElements(anchor) { let saved = false; try { if (anchor.hasAttribute('data-closed')) { saved = anchor.hasAttribute('data-closed'); } anchor.setAttribute('data-written', saved); throw anchor; } catch (error) { return error === anchor && saved; } }\n",
    "write-number-return": "function customElements(anchor) { try { anchor.setAttribute('data-written', 42); return true; } catch (error) { return false; } }\n",
    "write-number-corners": """function customElements(anchor) {
  const zero = -0, nan = 0 / 0, infinity = 1 / 0, negativeInfinity = -1 / 0;
  try {
    anchor.setAttribute('data-zero', zero);
    anchor.setAttribute('data-nan', nan);
    anchor.setAttribute('data-infinity', infinity);
    anchor.setAttribute('data-negative-infinity', negativeInfinity);
    anchor.setAttribute('data-small-exponent', 1e-7);
    anchor.setAttribute('data-small-decimal', 1e-6);
    anchor.setAttribute('data-large-decimal', 1e20);
    anchor.setAttribute('data-written', 1e21);
    throw anchor;
  } catch (error) { return error === anchor; }
}
""",
    "write-number-snapshot": """function customElements(anchor) {
  const saved = anchor.hasAttribute('data-closed') ? 7.5 : -0;
  try {
    anchor.setAttribute('data-closed', 42);
    anchor.setAttribute('data-written', saved);
    throw anchor;
  } catch (error) { return error === anchor && saved === 7.5; }
}
""",
    "write-number-read": """function customElements(anchor) {
  const saved = Number(anchor.getAttribute('data-number'));
  try {
    anchor.setAttribute('data-written', saved);
    throw anchor;
  } catch (error) { return error === anchor; }
}
""",
    "write-number-read-snapshot": """function customElements(anchor) {
  const saved = Number(anchor.getAttribute('data-number'));
  try {
    anchor.setAttribute('data-number', '99');
    anchor.setAttribute('data-written', saved);
    throw anchor;
  } catch (error) { return error === anchor; }
}
""",
    "write-number-read-protected": """function customElements(anchor) {
  try {
    const saved = Number(anchor.getAttribute('data-number'));
    anchor.setAttribute('data-written', saved);
    throw anchor;
  } catch (error) { return error === anchor; }
}
""",
    "write-number-read-protected-snapshot": """function customElements(anchor) {
  try {
    const saved = Number(anchor.getAttribute('data-number'));
    anchor.setAttribute('data-number', '99');
    anchor.setAttribute('data-written', saved);
    throw anchor;
  } catch (error) { return error === anchor; }
}
""",
    "write-number-read-protected-return": """function customElements(anchor) {
  try {
    const saved = Number(anchor.getAttribute('data-number'));
    anchor.setAttribute('data-written', saved);
    return true;
  } catch (error) { return error === anchor; }
}
""",
    "write-number-read-protected-add": """function customElements(anchor) {
  try {
    const saved = Number(anchor.getAttribute('data-number')) + 1;
    anchor.setAttribute('data-written', saved);
    throw anchor;
  } catch (error) { return error === anchor; }
}
""",
    "write-number-read-protected-arithmetic-snapshot": """function customElements(anchor) {
  try {
    const saved = -(+((Number(anchor.getAttribute('data-number')) * 2 - 1) / 2)) % 3;
    anchor.setAttribute('data-number', '99');
    anchor.setAttribute('data-written', saved);
    throw anchor;
  } catch (error) { return error === anchor; }
}
""",
    "write-number-read-protected-divide-return": """function customElements(anchor) {
  try {
    const saved = 1 / Number(anchor.getAttribute('data-number'));
    anchor.setAttribute('data-written', saved);
    return true;
  } catch (error) { return error === anchor; }
}
""",
    "write-number-read-conditional": """function customElements(anchor) {
  try {
    const saved = Number(anchor.hasAttribute('flag') ? anchor.getAttribute('data-number') : null);
    anchor.setAttribute('data-written', saved);
    throw anchor;
  } catch (error) { return error === anchor; }
}
""",
    "write-number-read-conditional-snapshot": """function customElements(anchor) {
  try {
    const saved = Number(anchor.hasAttribute('flag') ? anchor.getAttribute('data-number') : null);
    anchor.setAttribute('data-number', '99');
    anchor.setAttribute('data-written', saved);
    throw anchor;
  } catch (error) { return error === anchor; }
}
""",
    "write-number-read-conditional-return": """function customElements(anchor) {
  try {
    const saved = Number(anchor.hasAttribute('flag') ? anchor.getAttribute('data-number') : null);
    anchor.setAttribute('data-written', saved);
    return true;
  } catch (error) { return error === anchor; }
}
""",
    "write-number-read-conditional-return-snapshot": """function customElements(anchor) {
  let saved = 0;
  try {
    saved = Number(anchor.hasAttribute('flag') ? anchor.getAttribute('data-number') : null);
    anchor.setAttribute('data-number', '99');
    anchor.setAttribute('data-written', saved);
    return true;
  } catch (error) { return error === anchor; }
}
""",
    "write-number-read-conditional-divide-return": """function customElements(anchor) {
  try {
    const saved = 1 / Number(anchor.hasAttribute('flag') ? anchor.getAttribute('data-number') : null);
    anchor.setAttribute('data-written', saved);
    return true;
  } catch (error) { return error === anchor; }
}
""",
    "write-number-read-arithmetic-conditional-callee": "function customElements(anchor) { try { const saved = Number(anchor.hasAttribute('data-closed') ? anchor.getAttribute('data-number') : null) + 1; anchor.setAttribute('data-written', saved); throw anchor; } catch (error) { return error === anchor; } }\n",
    "write-number-boolean-return": """function customElements(anchor) {
  try {
    const saved = Number(anchor.hasAttribute('flag'));
    anchor.setAttribute('data-written', saved);
    return true;
  } catch (error) { return error === anchor; }
}
""",
    "write-number-boolean-snapshot": """function customElements(anchor) {
  try {
    const saved = anchor.hasAttribute('flag');
    anchor.setAttribute('flag', 'changed');
    anchor.setAttribute('data-written', Number(saved));
    return true;
  } catch (error) { return error === anchor; }
}
""",
    "write-number-boolean-conditional-divide-return": """function customElements(anchor) {
  try {
    const saved = 1 / Number(anchor.hasAttribute('flag') ? false : true);
    anchor.setAttribute('data-written', saved);
    return true;
  } catch (error) { return error === anchor; }
}
""",
    "write-number-boolean-number-return": """function customElements(anchor) {
  try {
    const saved = Number(Number(anchor.hasAttribute('flag')));
    anchor.setAttribute('data-written', saved);
    return true;
  } catch (error) { return error === anchor; }
}
""",
    "write-number-boolean-number-snapshot": """function customElements(anchor) {
  try {
    const saved = Number(anchor.hasAttribute('flag'));
    anchor.setAttribute('flag', 'changed');
    anchor.setAttribute('data-written', Number(saved));
    return true;
  } catch (error) { return error === anchor; }
}
""",
    "write-number-read-number-divide-return": """function customElements(anchor) {
  try {
    const saved = 1 / Number(Number(anchor.getAttribute('data-number')));
    anchor.setAttribute('data-written', saved);
    return true;
  } catch (error) { return error === anchor; }
}
""",
    "write-number-read-string-return": """function customElements(anchor) {
  try {
    const saved = Number(anchor.getAttribute('data-number')).toString();
    anchor.setAttribute('data-written', saved);
    return true;
  } catch (error) { return error === anchor; }
}
""",
    "write-number-read-string-snapshot": """function customElements(anchor) {
  try {
    const saved = Number(anchor.getAttribute('data-number')).toString();
    anchor.setAttribute('data-number', '99');
    anchor.setAttribute('data-written', saved);
    return true;
  } catch (error) { return error === anchor; }
}
""",
    "write-number-read-string-divide-return": """function customElements(anchor) {
  try {
    const saved = (1 / Number(anchor.getAttribute('data-number'))).toString();
    anchor.setAttribute('data-written', saved);
    return true;
  } catch (error) { return error === anchor; }
}
""",
    "write-number-read-string-prefix-return": """function customElements(anchor) {
  const saved = Number(anchor.getAttribute('data-number')).toString();
  try {
    anchor.setAttribute('data-written', saved);
    return true;
  } catch (error) { return error === anchor; }
}
""",
    "write-number-read-string-prefix-snapshot": """function customElements(anchor) {
  const saved = Number(anchor.getAttribute('data-number')).toString();
  try {
    anchor.setAttribute('data-number', '99');
    anchor.setAttribute('data-written', saved);
    return true;
  } catch (error) { return error === anchor; }
}
""",
    "write-number-read-string-prefix-divide-return": """function customElements(anchor) {
  const saved = (1 / Number(anchor.getAttribute('data-number'))).toString();
  try {
    anchor.setAttribute('data-written', saved);
    return true;
  } catch (error) { return error === anchor; }
}
""",
    "write-string-read-snapshot": """function customElements(anchor) {
  try {
    const saved = anchor.getAttribute('data-number');
    anchor.setAttribute('data-number', '99');
    anchor.setAttribute('data-written', saved);
    return true;
  } catch (error) { return error === anchor; }
}
""",
    "write-string-read-prefix-return": """function customElements(anchor) {
  const saved = anchor.getAttribute('data-number');
  try {
    anchor.setAttribute('data-written', saved);
    return true;
  } catch (error) { return error === anchor; }
}
""",
    "write-string-read-prefix-snapshot": """function customElements(anchor) {
  const saved = anchor.getAttribute('data-number');
  try {
    anchor.setAttribute('data-number', '99');
    anchor.setAttribute('data-written', saved);
    return true;
  } catch (error) { return error === anchor; }
}
""",
    "write-string-read-conditional-prefix-return": """function customElements(anchor) {
  const saved = anchor.hasAttribute('flag') ? anchor.getAttribute('data-number') : null;
  try {
    anchor.setAttribute('data-written', saved);
    return true;
  } catch (error) { return error === anchor; }
}
""",
    "write-string-read-conditional-prefix-snapshot": """function customElements(anchor) {
  const saved = anchor.hasAttribute('flag') ? anchor.getAttribute('data-number') : null;
  try {
    anchor.setAttribute('data-number', '99');
    anchor.setAttribute('data-written', saved);
    return true;
  } catch (error) { return error === anchor; }
}
""",
    "write-boolean-read-conditional-prefix-return": """function customElements(anchor) {
  const saved = anchor.hasAttribute('flag') ? anchor.hasAttribute('data-number') : false;
  try {
    anchor.setAttribute('data-written', saved);
    return true;
  } catch (error) { return error === anchor; }
}
""",
    "write-boolean-read-conditional-prefix-snapshot": """function customElements(anchor) {
  const saved = anchor.hasAttribute('flag') ? anchor.hasAttribute('data-number') : false;
  try {
    anchor.setAttribute('data-number', '99');
    anchor.setAttribute('data-written', saved);
    return true;
  } catch (error) { return error === anchor; }
}
""",
    "write-boolean-read-conditional-prefix-nullable-return": """function customElements(anchor) {
  const saved = anchor.hasAttribute('flag') ? anchor.hasAttribute('data-number') : null;
  try {
    anchor.setAttribute('data-written', saved);
    return true;
  } catch (error) { return error === anchor; }
}
""",
    "write-boolean-read-conditional-prefix-nullable-forwarded-null": """function customElements(anchor) {
  const flag = anchor.hasAttribute('flag');
  const empty = flag ? null : null;
  const saved = flag ? anchor.hasAttribute('data-number') : empty;
  try {
    anchor.setAttribute('data-written', saved);
    return true;
  } catch (error) { return error === anchor; }
}
""",
    "write-boolean-read-conditional-prefix-nullable-snapshot": """function customElements(anchor) {
  const saved = anchor.hasAttribute('flag') ? anchor.hasAttribute('data-number') : null;
  try {
    anchor.setAttribute('data-number', '99');
    anchor.setAttribute('data-written', saved);
    return true;
  } catch (error) { return error === anchor; }
}
""",
    "write-nullable-number-conditional-prefix-return": """function customElements(anchor) {
  const saved = anchor.hasAttribute('flag') ? 42 : null;
  try {
    anchor.setAttribute('data-written', saved);
    return true;
  } catch (error) { return error === anchor; }
}
""",
    "write-nullable-number-conditional-prefix-forwarded-null": """function customElements(anchor) {
  const flag = anchor.hasAttribute('flag');
  const empty = flag ? null : null;
  const saved = flag ? 42 : empty;
  try {
    anchor.setAttribute('data-written', saved);
    return true;
  } catch (error) { return error === anchor; }
}
""",
    "write-nullable-number-conditional-prefix-snapshot": """function customElements(anchor) {
  const saved = anchor.hasAttribute('flag') ? 42 : null;
  try {
    anchor.setAttribute('data-number', '99');
    anchor.setAttribute('data-written', saved);
    return true;
  } catch (error) { return error === anchor; }
}
""",
    "helper-next-record": """function customElements(anchor) {
  function next() {
    const done = anchor.hasAttribute('data-yielded');
    anchor.setAttribute('data-next', done);
    anchor.setAttribute('data-yielded', 'yes');
    return {done: done, value: anchor};
  }
  try {
    const result = next();
    return result.done && result.value === anchor;
  } catch (error) { return error === anchor; }
}
""",
}


NUMBER_READ_INPUTS = (
    (None, "0"),
    ("", "0"),
    (" -0 ", "0"),
    ("NaN", "NaN"),
    ("+Infinity", "Infinity"),
    ("-Infinity", "-Infinity"),
    ("0x10", "16"),
    ("0b11", "3"),
    ("7.5", "7.5"),
    ("1e-7", "1e-7"),
    ("1e21", "1e+21"),
    ("invalid", "NaN"),
    ("\u00a042\ufeff", "42"),
)


NUMBER_ARITHMETIC_RESULTS = {
    "write-number-read-protected-add": (
        "1",
        "1",
        "1",
        "NaN",
        "Infinity",
        "-Infinity",
        "17",
        "4",
        "8.5",
        "1.0000001",
        "1e+21",
        "NaN",
        "43",
    ),
    "write-number-read-protected-arithmetic-snapshot": (
        "0.5",
        "0.5",
        "0.5",
        "NaN",
        "NaN",
        "NaN",
        "-0.5",
        "-2.5",
        "-1",
        "0.4999999",
        "-1",
        "NaN",
        "-2.5",
    ),
    "write-number-read-protected-divide-return": (
        "Infinity",
        "Infinity",
        "-Infinity",
        "NaN",
        "0",
        "0",
        "0.0625",
        "0.3333333333333333",
        "0.13333333333333333",
        "10000000",
        "1e-21",
        "NaN",
        "0.023809523809523808",
    ),
}


NUMBER_ARITHMETIC_RESULTS["write-number-read-arithmetic-conditional-callee"] = (
    NUMBER_ARITHMETIC_RESULTS["write-number-read-protected-add"]
)
NUMBER_ARITHMETIC_RESULTS["write-number-read-conditional-divide-return"] = (
    NUMBER_ARITHMETIC_RESULTS["write-number-read-protected-divide-return"]
)
NUMBER_ARITHMETIC_RESULTS["write-number-read-number-divide-return"] = NUMBER_ARITHMETIC_RESULTS[
    "write-number-read-protected-divide-return"
]
NUMBER_ARITHMETIC_RESULTS["write-number-read-string-divide-return"] = NUMBER_ARITHMETIC_RESULTS[
    "write-number-read-protected-divide-return"
]
NUMBER_ARITHMETIC_RESULTS["write-number-read-string-prefix-divide-return"] = (
    NUMBER_ARITHMETIC_RESULTS["write-number-read-protected-divide-return"]
)
NUMBER_CONDITIONAL_READS = {
    "write-number-read-conditional": ("flag", "0"),
    "write-number-read-conditional-snapshot": ("flag", "0"),
    "write-number-read-conditional-return": ("flag", "0"),
    "write-number-read-conditional-return-snapshot": ("flag", "0"),
    "write-number-read-conditional-divide-return": ("flag", "Infinity"),
    "write-number-read-arithmetic-conditional-callee": ("data-closed", "1"),
    "write-string-read-conditional-prefix-return": ("flag", "null"),
    "write-string-read-conditional-prefix-snapshot": ("flag", "null"),
    "write-boolean-read-conditional-prefix-return": ("flag", "false"),
    "write-boolean-read-conditional-prefix-snapshot": ("flag", "false"),
    "write-boolean-read-conditional-prefix-nullable-return": ("flag", "null"),
    "write-boolean-read-conditional-prefix-nullable-forwarded-null": ("flag", "null"),
    "write-boolean-read-conditional-prefix-nullable-snapshot": ("flag", "null"),
    "write-nullable-number-conditional-prefix-return": ("flag", "null"),
    "write-nullable-number-conditional-prefix-forwarded-null": ("flag", "null"),
    "write-nullable-number-conditional-prefix-snapshot": ("flag", "null"),
}


WRITE_EXPECTED = {
    "write-throw": 'result0="true1:yes:1"\nresult1="true1:yes:1"\n',
    "write-return": 'result0="true0:yes:1"\nresult1="true0:yes:1"\n',
    "write-conditional": 'result0="false2:undefined:0"\nresult1="true2:yes:1"\n',
    "write-saved-state": 'result0="false1:yes:1"\nresult1="true2:yes:1"\n',
    "write-boolean-constants": 'result0="true0:false:2"\nresult1="true0:false:2"\n',
    "write-boolean-read-return": 'result0="false1:false:1"\nresult1="true1:true:1"\n',
    "write-boolean-snapshot": 'result0="false1:false:2"\nresult1="true1:true:2"\n',
    "write-boolean-conditional": 'result0="false1:false:1"\nresult1="true2:true:1"\n',
    "write-number-return": 'result0="true:write:data-written=42;"\nresult1="true:write:data-written=42;"\n',
    "write-number-corners": (
        'result0="true:write:data-zero=0;write:data-nan=NaN;write:data-infinity=Infinity;'
        "write:data-negative-infinity=-Infinity;write:data-small-exponent=1e-7;"
        "write:data-small-decimal=0.000001;write:data-large-decimal=100000000000000000000;"
        'write:data-written=1e+21;"\n'
        'result1="true:write:data-zero=0;write:data-nan=NaN;write:data-infinity=Infinity;'
        "write:data-negative-infinity=-Infinity;write:data-small-exponent=1e-7;"
        "write:data-small-decimal=0.000001;write:data-large-decimal=100000000000000000000;"
        'write:data-written=1e+21;"\n'
    ),
    "write-number-snapshot": (
        'result0="false:read:data-closed=false;write:data-closed=42;write:data-written=0;"\n'
        'result1="true:read:data-closed=true;write:data-closed=42;write:data-written=7.5;"\n'
    ),
    "write-number-boolean-return": (
        'result0="true:read:flag=false;write:data-written=0;"\n'
        'result1="true:read:flag=true;write:data-written=1;"\n'
    ),
    "write-number-boolean-snapshot": (
        'result0="true:read:flag=false;write:flag=changed;write:data-written=0;"\n'
        'result1="true:read:flag=true;write:flag=changed;write:data-written=1;"\n'
    ),
    "write-number-boolean-conditional-divide-return": (
        'result0="true:read:flag=false;write:data-written=1;"\n'
        'result1="true:read:flag=true;write:data-written=Infinity;"\n'
    ),
}
WRITE_EXPECTED["write-number-boolean-number-return"] = WRITE_EXPECTED["write-number-boolean-return"]
WRITE_EXPECTED["write-number-boolean-number-snapshot"] = WRITE_EXPECTED[
    "write-number-boolean-snapshot"
]

WRITE_OBSERVER = """function observe(closed) {
  const saved = {};
  if (closed) saved['data-closed'] = 'yes';
  let reads = 0, writes = 0;
  const anchor = {
    hasAttribute(name) { reads++; return name in saved; },
    setAttribute(name, value) { writes++; saved[name] = '' + value; }
  };
  const result = customElements(anchor);
  return '' + result + reads + ':' + saved['data-written'] + ':' + writes;
}
var result0 = observe(false), result1 = observe(true);
"""

HELPER_OBSERVER = """function observe(repeat) {
  const saved = {};
  let trace = '';
  const anchor = {
    hasAttribute(name) {
      const value = name in saved;
      trace += 'read:' + name + '=' + value + ';';
      return value;
    },
    setAttribute(name, value) {
      saved[name] = '' + value;
      trace += 'write:' + name + '=' + saved[name] + ';';
    }
  };
  const first = customElements(anchor);
  if (!repeat) return first + ':' + trace;
  trace = '';
  const second = customElements(anchor);
  return first + ',' + second + ':' + trace;
}
var result0 = observe(false), result1 = observe(true);
"""

NUMBER_WRITE_OBSERVER = """function observe(closed, number) {
  const saved = {};
  if (closed) saved['data-closed'] = 'yes';
  if (number !== undefined && number !== null) saved['data-number'] = number;
  let trace = '';
  const anchor = {
    getAttribute(name) {
      const value = name in saved ? saved[name] : null;
      trace += 'get:' + name + '=' + value + ';';
      return value;
    },
    hasAttribute(name) {
      const value = name in saved;
      trace += 'read:' + name + '=' + value + ';';
      return value;
    },
    setAttribute(name, value) {
      saved[name] = '' + value;
      trace += 'write:' + name + '=' + saved[name] + ';';
    }
  };
  return customElements(anchor) + ':' + trace;
}
var result0 = observe(false), result1 = observe(true);
"""


def main():
    args = dom.parse_args(__doc__, oracles=True)
    compilers = args.compilers
    includes, libraries = dom.link_options(args)
    for name, source in SOURCES.items():
        string_read = name.startswith("write-string-read-")
        boolean_read = name.startswith("write-boolean-read-conditional-prefix-")
        nullable_number = name.startswith("write-nullable-number-conditional-prefix-")
        attribute_read = string_read or boolean_read or nullable_number
        number_inputs = (
            tuple(
                zip(
                    (value for value, _ in NUMBER_READ_INPUTS),
                    NUMBER_ARITHMETIC_RESULTS[name],
                    strict=True,
                )
            )
            if name in NUMBER_ARITHMETIC_RESULTS
            else NUMBER_READ_INPUTS
        )
        if string_read:
            number_inputs = tuple(
                (value, "null" if value is None else value) for value, _ in NUMBER_READ_INPUTS
            )
        if boolean_read:
            number_inputs = ((None, "false"), ("", "true"))
        if nullable_number:
            number_inputs = ((None, "42"), ("", "42"))
        script = (
            source
            + "function observe(closed) { let reads = 0; "
            + "const anchor = { hasAttribute(key) { reads++; return key === 'data-closed' && closed; } }; "
            + "return '' + customElements(anchor) + reads; }\n"
            + "var result0 = observe(false), result1 = observe(true);\n"
        )
        if name.startswith("write-"):
            script = source + WRITE_OBSERVER
        if name.startswith("write-number-") or attribute_read:
            script = source + NUMBER_WRITE_OBSERVER
        number_boolean = name.startswith("write-number-boolean-")
        if number_boolean:
            script = script.replace("saved['data-closed'] = 'yes'", "saved['flag'] = 'yes'")
        condition = NUMBER_CONDITIONAL_READS.get(name)
        if condition:
            script = script.replace(
                "saved['data-closed'] = 'yes'", f"saved[{condition[0]!r}] = 'yes'"
            )
        number_read = name.startswith("write-number-read")
        if number_read or attribute_read:
            script = script.replace(
                "var result0 = observe(false), result1 = observe(true);",
                "\n".join(
                    f"var result{index} = ["
                    + ", ".join(
                        f"observe({closed}, {json.dumps(value)})" for value, _ in number_inputs
                    )
                    + "].join('|');"
                    for index, closed in enumerate(("false", "true"))
                ),
            )
        if name == "helper-next-record":
            script = source + HELPER_OBSERVER
        nested = name.startswith("mixed-node-nested-") or name == "nested-protected-normal-read"
        if nested:
            script = (
                script.replace("observe(closed)", "observe(closed, inner)")
                .replace(
                    "key === 'data-closed' && closed",
                    "(key === 'data-closed' && closed) || (key === 'data-inner' && inner)",
                )
                .replace(
                    "var result0 = observe(false), result1 = observe(true);",
                    "var result0 = observe(false, false), result1 = observe(false, true), "
                    "result2 = observe(true, false), result3 = observe(true, true);",
                )
            )
        oracle = args.work / f"{name}.oracle.js"
        oracle.write_text(script)
        node = args.work / f"{name}.node.js"
        node.write_text(
            script
            + "console.log('result0=' + JSON.stringify(result0));\n"
            + "console.log('result1=' + JSON.stringify(result1));\n"
            + (
                "console.log('result2=' + JSON.stringify(result2));\n"
                "console.log('result3=' + JSON.stringify(result3));\n"
                if nested
                else ""
            )
        )
        expected = (
            'result0="true0"\nresult1="true0"\n'
            if name in ("identity", "conditional-node-identity", "conditional")
            else 'result0="false1"\nresult1="true1"\n'
        )
        if name.startswith("conditional-"):
            expected = {
                "conditional-node-identity": 'result0="true1"\nresult1="true1"\n',
                "conditional-node-read": 'result0="false2"\nresult1="true2"\n',
                "conditional-node-state": 'result0="false1"\nresult1="true1"\n',
            }[name]
        if name == "invocation-multiple-reads":
            expected = 'result0="false2"\nresult1="true2"\n'
        if name == "protected-read":
            expected = 'result0="true1"\nresult1="true1"\n'
        if name == "mixed-protected-normal-read":
            expected = 'result0="false2"\nresult1="true1"\n'
        if name in ("mixed-node-read", "invocation-conditional-return"):
            expected = 'result0="false1"\nresult1="true2"\n'
        if name == "mixed-node-reversed-state":
            expected = 'result0="true1"\nresult1="false1"\n'
        if nested:
            expected = 'result0="false2"\nresult1="false2"\nresult2="false2"\n' + (
                'result3="true3"\n' if name == "mixed-node-nested-read" else 'result3="true2"\n'
            )
            if name == "nested-protected-normal-read":
                expected = expected.replace('result0="false2"', 'result0="false3"').replace(
                    'result1="false2"', 'result1="false3"'
                )
        if name in WRITE_EXPECTED:
            expected = WRITE_EXPECTED[name]
        if number_read or attribute_read:
            expected = ""
            for index in (0, 1):
                trace = "|".join(
                    "true:"
                    + (f"read:{condition[0]}={'true' if index else 'false'};" if condition else "")
                    + (
                        ("read" if boolean_read else "get")
                        + ":data-number="
                        + (result if boolean_read else ("null" if value is None else value))
                        + ";"
                        if (not condition or index) and not nullable_number
                        else ""
                    )
                    + ("write:data-number=99;" if name.endswith("-snapshot") else "")
                    + "write:data-written="
                    + (condition[1] if condition and not index else result)
                    + ";"
                    for value, result in number_inputs
                )
                expected += f"result{index}={json.dumps(trace, ensure_ascii=False)}\n"
        if name == "helper-next-record":
            expected = (
                'result0="false:read:data-yielded=false;write:data-next=false;write:data-yielded=yes;"\n'
                'result1="false,true:read:data-yielded=true;write:data-next=true;write:data-yielded=yes;"\n'
            )
        for command in ([args.node, str(node)], [args.reference, str(oracle)]):
            result = run(command)
            output = unquote(result.stdout) if command[0] == args.reference else result.stdout
            if output != expected:
                raise RuntimeError(f"{name}: catch oracle differs: {result}")
        ir, contract = dom.prepare(args, name, source, 1, entry_name="customElements")
        if number_read or number_boolean:
            contract["initial_intrinsics"] = ["Number"]
        checks = (
            "assert(@ENTRY@(element)); assert(@ENTRY@(alias)); assert(@ENTRY@(foreign));"
            if name in ("identity", "conditional-node-identity", "conditional", "protected-read")
            else "assert(!@ENTRY@(element)); "
            'assert(doc.set_attribute(button, atoms.intern("data-closed"), "yes")); '
            "assert(@ENTRY@(alias)); assert(!@ENTRY@(foreign));"
        )
        if name == "conditional-node-identity":
            checks += ' assert(doc.set_attribute(button, atoms.intern("data-closed"), "yes")); assert(@ENTRY@(alias));'
        if name == "mixed-node-reversed-state":
            checks = (
                "assert(@ENTRY@(element)); "
                + 'assert(doc.set_attribute(button, atoms.intern("data-closed"), "yes")); '
                + "assert(!@ENTRY@(alias)); assert(@ENTRY@(foreign));"
            )
        if nested:
            checks = (
                "assert(!@ENTRY@(element)); "
                'assert(doc.set_attribute(button, atoms.intern("data-inner"), "yes")); '
                "assert(!@ENTRY@(alias)); "
                'assert(doc.remove_attribute(button, atoms.intern("data-inner"))); '
                'assert(doc.set_attribute(button, atoms.intern("data-closed"), "yes")); '
                "assert(!@ENTRY@(alias)); "
                'assert(doc.set_attribute(button, atoms.intern("data-inner"), "yes")); '
                "assert(@ENTRY@(alias)); assert(!@ENTRY@(foreign));"
            )
        if name.startswith("write-"):
            answer = "closed" if name in ("write-conditional", "write-saved-state") else "true"
            count = "(closed ? 1u : 0u)" if name == "write-conditional" else "1u"
            text = '"yes"'
            first_name = '"data-written"'
            extra = ""
            if name.startswith("write-boolean-"):
                answer = "true" if name == "write-boolean-constants" else "closed"
                count = (
                    "2u" if name in ("write-boolean-constants", "write-boolean-snapshot") else "1u"
                )
                text = (
                    '"false"'
                    if name == "write-boolean-constants"
                    else '(closed ? "true" : "false")'
                )
                if name == "write-boolean-constants":
                    first_name = '"data-first"'
                elif name == "write-boolean-snapshot":
                    first_name = '"data-closed"'
                if count == "2u":
                    extra = 'assert(writes[1].name == written); assert(doc.read().attribute_value(button, atoms.intern(@FIRST@)) == "true");'.replace(
                        "@FIRST@", first_name
                    )
            checks = (
                """
                const auto written = atoms.intern("data-written");
                for (bool closed : {false, true}) {
                    assert(doc.remove_attribute(button, written));
                    if (closed) {
                        assert(doc.set_attribute(button, atoms.intern("data-closed"), "yes"));
                    }
                    (void)doc.take_writes();
                    assert(static_cast<bool>(@ENTRY@(alias)) == @ANSWER@);
                    const auto writes = doc.take_writes();
                    assert(writes.size() == @COUNT@);
                    assert(doc.read().has_attribute(button, written) == !writes.empty());
                    if (!writes.empty()) {
                        assert(writes[0].name == atoms.intern(@FIRST@));
                        assert(doc.read().attribute_value(button, written) == @TEXT@);
                        @EXTRA@
                    }
                }
                (void)foreign;
            """.replace("@ANSWER@", answer)
                .replace("@COUNT@", count)
                .replace("@FIRST@", first_name)
                .replace("@TEXT@", text)
                .replace("@EXTRA@", extra)
            )
        if name == "helper-next-record":
            checks = r"""
                const auto exercise = [&](ctbrowser::document & target, node_id id, auto && call) {
                    const auto next = target.atoms().intern("data-next");
                    const auto yielded = target.atoms().intern("data-yielded");
                    target.log_writes(true);
                    for (bool repeated : {false, true}) {
                        (void)target.take_writes();
                        assert(static_cast<bool>(call()) == repeated);
                        const auto writes = target.take_writes();
                        assert(writes.size() == 2 && writes[0].name == next && writes[1].name == yielded);
                        for (const auto & write : writes) { assert(write.node == id && !write.text); }
                        assert(target.read().attribute_value(id, next) == (repeated ? "true" : "false"));
                        assert(target.read().attribute_value(id, yielded) == "yes");
                    }
                };
                exercise(doc, button, [&] { return @ENTRY@(alias); });
                exercise(foreign_doc, other_button, [&] { return @ENTRY@(foreign); });
            """
        if name.startswith("write-number-") and not number_read:
            expected_writes = {
                "write-number-return": [("data-written", '"42"')],
                "write-number-corners": [
                    ("data-zero", '"0"'),
                    ("data-nan", '"NaN"'),
                    ("data-infinity", '"Infinity"'),
                    ("data-negative-infinity", '"-Infinity"'),
                    ("data-small-exponent", '"1e-7"'),
                    ("data-small-decimal", '"0.000001"'),
                    ("data-large-decimal", '"100000000000000000000"'),
                    ("data-written", '"1e+21"'),
                ],
                "write-number-snapshot": [
                    ("data-closed", '"42"'),
                    ("data-written", '(closed ? "7.5" : "0")'),
                ],
                "write-number-boolean-return": [("data-written", '(closed ? "1" : "0")')],
                "write-number-boolean-snapshot": [
                    ("flag", '"changed"'),
                    ("data-written", '(closed ? "1" : "0")'),
                ],
                "write-number-boolean-conditional-divide-return": [
                    ("data-written", '(closed ? "Infinity" : "1")')
                ],
                "write-number-boolean-number-return": [("data-written", '(closed ? "1" : "0")')],
                "write-number-boolean-number-snapshot": [
                    ("flag", '"changed"'),
                    ("data-written", '(closed ? "1" : "0")'),
                ],
            }[name]
            write_checks = "\n".join(
                f'assert(writes[{index}].name == target.atoms().intern("{attribute}")); '
                f'assert(target.read().attribute_value(id, target.atoms().intern("{attribute}")) == {value});'
                for index, (attribute, value) in enumerate(expected_writes)
            )
            checks = (
                r"""
                const auto exercise = [&](ctbrowser::document & target, node_id id, auto && call) {
                    const auto closed_name = target.atoms().intern("data-closed");
                    target.log_writes(true);
                    for (bool closed : {false, true}) {
                        assert(target.remove_attribute(id, closed_name));
                        if (closed) { assert(target.set_attribute(id, closed_name, "yes")); }
                        (void)target.take_writes();
                        assert(static_cast<bool>(call()) == @ANSWER@);
                        const auto writes = target.take_writes();
                        assert(writes.size() == @COUNT@);
                        for (const auto & write : writes) { assert(write.node == id && !write.text); }
                        @WRITES@
                    }
                };
                exercise(doc, button, [&] { return @ENTRY@(alias); });
                exercise(foreign_doc, other_button, [&] { return @ENTRY@(foreign); });
                """.replace("@ANSWER@", "closed" if name == "write-number-snapshot" else "true")
                .replace("@COUNT@", str(len(expected_writes)))
                .replace("@WRITES@", write_checks)
            )
            if number_boolean:
                checks = checks.replace('"data-closed"', '"flag"')
        if number_read or attribute_read:
            inputs = ",\n".join(
                "{"
                + ("nullptr" if value is None else json.dumps(value, ensure_ascii=False))
                + ", "
                + json.dumps(result)
                + "}"
                for value, result in number_inputs
            )
            checks = (
                r"""
                const auto exercise = [&](ctbrowser::document & target, node_id id, auto && call) {
                    const auto number = target.atoms().intern("data-number");
                    const auto written = target.atoms().intern("data-written");
                    const char * cases[][2] = {@INPUTS@};
                    target.log_writes(true);
                    for (const auto & test : cases) {
                        assert(target.remove_attribute(id, number));
                        assert(target.remove_attribute(id, written));
                        if (test[0]) { assert(target.set_attribute(id, number, test[0])); }
                        (void)target.take_writes();
                        assert(call());
                        const auto writes = target.take_writes();
                        assert(writes.size() == @COUNT@);
                        for (const auto & write : writes) { assert(write.node == id && !write.text); }
                        assert(writes.back().name == written);
                        assert(target.read().attribute_value(id, written) == test[1]);
                        @SNAPSHOT@
                    }
                };
                exercise(doc, button, [&] { return @ENTRY@(alias); });
                exercise(foreign_doc, other_button, [&] { return @ENTRY@(foreign); });
            """.replace("@INPUTS@", inputs)
                .replace("@COUNT@", "2" if name.endswith("-snapshot") else "1")
                .replace(
                    "@SNAPSHOT@",
                    (
                        'assert(writes.front().name == number); assert(target.read().attribute_value(id, number) == "99");'
                        if name.endswith("-snapshot")
                        else "assert(target.read().has_attribute(id, number) == (test[0] != nullptr)); if (test[0]) { assert(target.read().attribute_value(id, number) == test[0]); }"
                    ),
                )
            )
        if condition:
            checks = (
                checks.replace(
                    "for (const auto & test : cases) {",
                    "for (bool enabled : {false, true}) { "
                    f'const auto flag = target.atoms().intern("{condition[0]}"); '
                    "assert(target.remove_attribute(id, flag)); "
                    'if (enabled) { assert(target.set_attribute(id, flag, "yes")); } '
                    "for (const auto & test : cases) {",
                )
                .replace("== test[1]", f'== (enabled ? test[1] : "{condition[1]}")')
                .replace("                };", "                } };", 1)
            )
        checks += " (void)pressed; (void)alias;"
        for provider in dom.PROVIDERS:
            for optimize in (False, True):
                label = f"caught-node-{name}-{provider}-{optimize}"
                native = dom.lower(
                    args, ir, dict(contract, provider=provider), label, optimize=optimize
                )
                cpp = run([args.translate, "--mlir-to-cpp", str(native)]).stdout
                # The existing session wrapper rejects foreign documents with
                # std::invalid_argument; the source catch emits no C++ exception.
                source_cpp = cpp.replace(
                    'throw std::invalid_argument("DOM element belongs to another session");', ""
                )
                if "throw " in source_cpp or "catch (" in source_cpp:
                    raise RuntimeError("confined node escaped into a native exception")
                dom.standalone(
                    args,
                    native,
                    label,
                    checks,
                    compilers,
                    includes,
                    libraries,
                    allow_nullable_scalar=nullable_number
                    or name.startswith("write-boolean-read-conditional-prefix-nullable-"),
                )
                if (
                    name == "helper-next-record"
                    or name.startswith("write-number-")
                    or attribute_read
                ):
                    dom.lower(
                        args,
                        ir,
                        dict(contract, provider=provider),
                        label + "-zero-budget",
                        optimize=optimize,
                        success=False,
                        max_steps=0,
                    )
    refusals = {
        "write-number-invalid-name": "try { anchor.setAttribute('bad name', 42); throw anchor; } catch (error) { return error === anchor; }",
        "write-number-mixed-join": "const saved = anchor.hasAttribute('data-closed') ? 42 : anchor; try { anchor.setAttribute('data-written', saved); throw anchor; } catch (error) { return error === anchor; }",
        "write-number-coercing-input": "const saved = +anchor; try { anchor.setAttribute('data-written', saved); throw anchor; } catch (error) { return error === anchor; }",
        "write-number-throwing-producer": "try { const saved = JSON.parse('42'); anchor.setAttribute('data-written', saved); throw anchor; } catch (error) { return error === anchor; }",
        "write-boolean-unknown-value": "try { anchor.setAttribute('data-written', anchor.unknown); throw anchor; } catch (error) { return error === anchor; }",
        "write-boolean-forged-read": "try { const saved = ({hasAttribute() { return true; }}).hasAttribute('data-closed'); anchor.setAttribute('data-written', saved); throw anchor; } catch (error) { return error === anchor; }",
        "write-boolean-mixed-join": "let saved = anchor; try { if (anchor.hasAttribute('data-closed')) { saved = true; } anchor.setAttribute('data-written', saved); throw anchor; } catch (error) { return error === anchor; }",
        "write-boolean-invalid-name": "try { anchor.setAttribute('bad name', true); throw anchor; } catch (error) { return error === anchor; }",
        "write-invalid-name": "try { anchor.setAttribute('bad name', 'yes'); throw anchor; } catch (error) { return error === anchor; }",
        "write-coercing-value": "try { anchor.setAttribute('data-written', anchor); throw anchor; } catch (error) { return error === anchor; }",
        "write-forged-method": "try { ({setAttribute() { throw false; }}).setAttribute('data-written', 'yes'); throw anchor; } catch (error) { return error === anchor; }",
        "write-wrong-receiver": "try { const write = anchor.setAttribute; write('data-written', 'yes'); throw anchor; } catch (error) { return error === anchor; }",
        "invocation-coercing-argument": "try { return anchor.hasAttribute(1); } catch (error) { return error === anchor; }",
        "invocation-wrong-receiver": "try { const read = anchor.hasAttribute; return read('data-closed'); } catch (error) { return error === anchor; }",
        "invocation-forged-method": "try { return ({hasAttribute() { throw false; }}).hasAttribute('data-closed'); } catch (error) { return error === anchor; }",
        "invocation-unknown-call": "try { return decodeURIComponent('%'); } catch (error) { return error === anchor; }",
        "invocation-without-contract": "try { return anchor.hasAttribute('data-closed'); } catch (error) { return error === anchor; }",
        "protected-forged-method": "try { ({hasAttribute() { throw false; }}).hasAttribute('x'); throw anchor; } catch (error) { return error === anchor; }",
        "rethrow": "try { throw anchor; } catch (error) { throw error; }",
        "return-borrow": "try { throw anchor; } catch (error) { return error; }",
        "protected-call": "try { decodeURIComponent('%'); throw anchor; } catch (error) { return error === anchor; }",
    }
    refusals.update(
        {
            "write-number-read-without-intrinsic": "const saved = Number(anchor.getAttribute('data-number')); try { anchor.setAttribute('data-written', saved); throw anchor; } catch (error) { return error === anchor; }",
            "write-number-read-rebound-intrinsic": "Number = anchor; const saved = Number(anchor.getAttribute('data-number')); try { anchor.setAttribute('data-written', saved); throw anchor; } catch (error) { return error === anchor; }",
            "write-number-read-coercing-input": "const saved = Number(anchor); try { anchor.setAttribute('data-written', saved); throw anchor; } catch (error) { return error === anchor; }",
            "write-number-read-unknown-getter": "const saved = Number(anchor.unknown); try { anchor.setAttribute('data-written', saved); throw anchor; } catch (error) { return error === anchor; }",
            "write-number-read-invalid-name": "const saved = Number(anchor.getAttribute('data-number')); try { anchor.setAttribute('bad name', saved); throw anchor; } catch (error) { return error === anchor; }",
            "write-number-read-mixed-join": "const saved = anchor.hasAttribute('data-closed') ? Number(anchor.getAttribute('data-number')) : anchor; try { anchor.setAttribute('data-written', saved); throw anchor; } catch (error) { return error === anchor; }",
            "write-number-read-protected-without-intrinsic": "try { const saved = Number(anchor.getAttribute('data-number')); anchor.setAttribute('data-written', saved); throw anchor; } catch (error) { return error === anchor; }",
            "write-number-read-protected-rebound-intrinsic": "try { Number = anchor; const saved = Number(anchor.getAttribute('data-number')); anchor.setAttribute('data-written', saved); throw anchor; } catch (error) { return error === anchor; }",
            "write-number-read-protected-coercing-input": "try { const saved = Number(anchor); anchor.setAttribute('data-written', saved); throw anchor; } catch (error) { return error === anchor; }",
            "write-number-read-protected-unknown-getter": "try { const saved = Number(anchor.unknown); anchor.setAttribute('data-written', saved); throw anchor; } catch (error) { return error === anchor; }",
            "write-number-read-protected-unknown-producer": "try { const saved = Number(anchor.unknown()); anchor.setAttribute('data-written', saved); throw anchor; } catch (error) { return error === anchor; }",
            "write-number-read-protected-rebound-getter": "try { anchor.getAttribute = anchor.setAttribute; const saved = Number(anchor.getAttribute('data-number')); anchor.setAttribute('data-written', saved); throw anchor; } catch (error) { return error === anchor; }",
            "write-number-read-protected-wrong-getter-receiver": "try { const get = anchor.getAttribute; const saved = Number(get('data-number')); anchor.setAttribute('data-written', saved); throw anchor; } catch (error) { return error === anchor; }",
            "write-number-read-protected-missing-getter-argument": "try { const saved = Number(anchor.getAttribute()); anchor.setAttribute('data-written', saved); throw anchor; } catch (error) { return error === anchor; }",
            "write-number-read-protected-extra-getter-argument": "try { const saved = Number(anchor.getAttribute('data-number', 'extra')); anchor.setAttribute('data-written', saved); throw anchor; } catch (error) { return error === anchor; }",
            "write-number-read-protected-coercing-getter-name": "try { const saved = Number(anchor.getAttribute(anchor)); anchor.setAttribute('data-written', saved); throw anchor; } catch (error) { return error === anchor; }",
            "write-number-read-protected-extra-number-argument": "try { const saved = Number(anchor.getAttribute('data-number'), 42); anchor.setAttribute('data-written', saved); throw anchor; } catch (error) { return error === anchor; }",
            "write-number-read-protected-wrong-number-receiver": "try { const saved = Number.call(anchor, anchor.getAttribute('data-number')); anchor.setAttribute('data-written', saved); throw anchor; } catch (error) { return error === anchor; }",
            "write-number-read-protected-invalid-name": "try { const saved = Number(anchor.getAttribute('data-number')); anchor.setAttribute('bad name', saved); throw anchor; } catch (error) { return error === anchor; }",
            "write-number-read-protected-mixed-join": "try { const saved = Number(anchor.hasAttribute('data-closed') ? anchor.getAttribute('data-number') : anchor); anchor.setAttribute('data-written', saved); throw anchor; } catch (error) { return error === anchor; }",
            "write-number-read-arithmetic-without-intrinsic": "try { const saved = Number(anchor.getAttribute('data-number')) + 1; anchor.setAttribute('data-written', saved); throw anchor; } catch (error) { return error === anchor; }",
            "write-number-read-arithmetic-rebound-intrinsic": "try { Number = anchor; const saved = Number(anchor.getAttribute('data-number')) + 1; anchor.setAttribute('data-written', saved); throw anchor; } catch (error) { return error === anchor; }",
            "write-number-read-arithmetic-rebound-getter": "try { anchor.getAttribute = anchor.setAttribute; const saved = Number(anchor.getAttribute('data-number')) + 1; anchor.setAttribute('data-written', saved); throw anchor; } catch (error) { return error === anchor; }",
            "write-number-read-arithmetic-coercing-input": "try { const saved = Number(anchor) + 1; anchor.setAttribute('data-written', saved); throw anchor; } catch (error) { return error === anchor; }",
            "write-number-read-arithmetic-coercing-add": "try { const saved = Number(anchor.getAttribute('data-number')) + anchor; anchor.setAttribute('data-written', saved); throw anchor; } catch (error) { return error === anchor; }",
            "write-number-read-arithmetic-string-add": "try { const saved = Number(anchor.getAttribute('data-number')) + '1'; anchor.setAttribute('data-written', saved); throw anchor; } catch (error) { return error === anchor; }",
            "write-number-read-arithmetic-bigint-add": "try { const saved = Number(anchor.getAttribute('data-number')) + 1n; anchor.setAttribute('data-written', saved); throw anchor; } catch (error) { return error === anchor; }",
            "write-number-read-arithmetic-mixed-join": "try { const saved = (anchor.hasAttribute('data-closed') ? Number(anchor.getAttribute('data-number')) : '1') + 1; anchor.setAttribute('data-written', saved); throw anchor; } catch (error) { return error === anchor; }",
            "write-number-read-arithmetic-unknown-getter": "try { const saved = Number(anchor.getAttribute('data-number')) + anchor.unknown; anchor.setAttribute('data-written', saved); throw anchor; } catch (error) { return error === anchor; }",
            "write-number-read-arithmetic-unknown-producer": "try { const saved = Number(anchor.getAttribute('data-number')) + anchor.unknown(); anchor.setAttribute('data-written', saved); throw anchor; } catch (error) { return error === anchor; }",
            "write-number-read-arithmetic-unary-coercion": "try { const saved = +anchor; anchor.setAttribute('data-written', saved); throw anchor; } catch (error) { return error === anchor; }",
            "write-number-read-arithmetic-negate-coercion": "try { const saved = -anchor; anchor.setAttribute('data-written', saved); throw anchor; } catch (error) { return error === anchor; }",
            "write-number-read-arithmetic-divide-coercion": "try { const saved = 1 / anchor; anchor.setAttribute('data-written', saved); throw anchor; } catch (error) { return error === anchor; }",
            "write-number-read-conditional-without-intrinsic": "try { const saved = Number(anchor.hasAttribute('flag') ? anchor.getAttribute('data-number') : null); anchor.setAttribute('data-written', saved); throw anchor; } catch (error) { return error === anchor; }",
            "write-number-read-conditional-rebound-intrinsic": "try { Number = anchor; const saved = Number(anchor.hasAttribute('flag') ? anchor.getAttribute('data-number') : null); anchor.setAttribute('data-written', saved); throw anchor; } catch (error) { return error === anchor; }",
            "write-number-read-conditional-different-callee": "try { const saved = (anchor.hasAttribute('flag') ? Number : anchor)(anchor.getAttribute('data-number')); anchor.setAttribute('data-written', saved); throw anchor; } catch (error) { return error === anchor; }",
            "write-number-read-conditional-mutable-callee": "try { let convert = Number; if (anchor.hasAttribute('flag')) convert = anchor; const saved = convert(anchor.getAttribute('data-number')); anchor.setAttribute('data-written', saved); throw anchor; } catch (error) { return error === anchor; }",
            "write-number-read-conditional-then-effect": "try { const saved = Number(anchor.hasAttribute('flag') ? (anchor.unknown(), anchor.getAttribute('data-number')) : null); anchor.setAttribute('data-written', saved); throw anchor; } catch (error) { return error === anchor; }",
            "write-number-read-conditional-else-effect": "try { const saved = Number(anchor.hasAttribute('flag') ? anchor.getAttribute('data-number') : (anchor.unknown(), null)); anchor.setAttribute('data-written', saved); throw anchor; } catch (error) { return error === anchor; }",
            "write-number-read-conditional-then-coercion": "try { const saved = Number(anchor.hasAttribute('flag') ? anchor : null); anchor.setAttribute('data-written', saved); throw anchor; } catch (error) { return error === anchor; }",
            "write-number-read-conditional-else-coercion": "try { const saved = Number(anchor.hasAttribute('flag') ? anchor.getAttribute('data-number') : anchor); anchor.setAttribute('data-written', saved); throw anchor; } catch (error) { return error === anchor; }",
            "write-number-read-conditional-then-invalid-write": "try { const saved = Number(anchor.hasAttribute('flag') ? (anchor.setAttribute('bad name', 'yes'), anchor.getAttribute('data-number')) : null); anchor.setAttribute('data-written', saved); throw anchor; } catch (error) { return error === anchor; }",
            "write-number-read-conditional-else-invalid-write": "try { const saved = Number(anchor.hasAttribute('flag') ? anchor.getAttribute('data-number') : (anchor.setAttribute('bad name', 'yes'), null)); anchor.setAttribute('data-written', saved); throw anchor; } catch (error) { return error === anchor; }",
            "write-number-read-conditional-arm-rebound-intrinsic": "try { const saved = Number(anchor.hasAttribute('flag') ? (Number = anchor, anchor.getAttribute('data-number')) : null); anchor.setAttribute('data-written', saved); throw anchor; } catch (error) { return error === anchor; }",
            "write-number-read-conditional-unknown-condition": "try { const saved = Number(anchor.unknown ? anchor.getAttribute('data-number') : null); anchor.setAttribute('data-written', saved); throw anchor; } catch (error) { return error === anchor; }",
            "mixed-protected-throw-call": "const flag = anchor.hasAttribute('data-closed'); try { if (flag) { decodeURIComponent('%'); throw anchor; } } catch (error) { return error === anchor; } return false;",
            "mixed-rethrow": "const flag = anchor.hasAttribute('data-closed'); try { if (flag) { throw anchor; } } catch (error) { throw error; } return false;",
            "mixed-return-borrow": "const flag = anchor.hasAttribute('data-closed'); try { if (flag) { throw anchor; } } catch (error) { return error; } return false;",
        }
    )
    # The same effects and callable origins must refuse with a normal protected return.
    refusals.update(
        {
            name.replace("conditional-", "conditional-return-", 1): body.replace(
                "throw anchor;", "return true;"
            )
            for name, body in refusals.items()
            if name.startswith("write-number-read-conditional-")
        }
    )
    boolean_body = SOURCES["write-number-boolean-return"].split("{", 1)[1].rsplit("}", 1)[0]
    refusals.update(
        {
            "write-number-boolean-" + name: boolean_body.replace(before, after)
            for name, before, after in (
                ("without-intrinsic", "Number", "Number"),
                ("rebound-intrinsic", "try {", "try { Number = anchor;"),
                ("coercing-input", "anchor.hasAttribute('flag')", "anchor"),
                ("unknown-getter", "anchor.hasAttribute('flag')", "anchor.unknown"),
                ("unknown-producer", "anchor.hasAttribute('flag')", "anchor.unknown()"),
                (
                    "rebound-read",
                    "try {",
                    "try { anchor.hasAttribute = anchor.getAttribute;",
                ),
                (
                    "wrong-read-receiver",
                    "const saved = Number(anchor.hasAttribute('flag'))",
                    "const read = anchor.hasAttribute; const saved = Number(read('flag'))",
                ),
                ("coercing-read-name", "hasAttribute('flag')", "hasAttribute(anchor)"),
                (
                    "different-callee",
                    "Number(anchor.hasAttribute('flag'))",
                    "(anchor.hasAttribute('flag') ? Number : anchor)(true)",
                ),
                (
                    "coercing-branch",
                    "anchor.hasAttribute('flag')",
                    "anchor.hasAttribute('flag') ? true : anchor",
                ),
                (
                    "effectful-branch",
                    "anchor.hasAttribute('flag')",
                    "anchor.hasAttribute('flag') ? (anchor.unknown(), true) : false",
                ),
                ("invalid-name", "'data-written'", "'bad name'"),
            )
        }
    )
    number_body = SOURCES["write-number-boolean-number-return"].split("{", 1)[1].rsplit("}", 1)[0]
    refusals.update(
        {
            "write-number-boolean-number-" + name: number_body.replace(before, after)
            for name, before, after in (
                ("without-intrinsic", "Number", "Number"),
                ("rebound-intrinsic", "try {", "try { Number = anchor;"),
                ("coercing-input", "anchor.hasAttribute('flag')", "anchor"),
                ("unknown-getter", "anchor.hasAttribute('flag')", "anchor.unknown"),
                ("unknown-producer", "anchor.hasAttribute('flag')", "anchor.unknown()"),
                (
                    "extra-argument",
                    "Number(Number(anchor.hasAttribute('flag')))",
                    "Number(Number(anchor.hasAttribute('flag')), 0)",
                ),
                (
                    "wrong-receiver",
                    "Number(Number(anchor.hasAttribute('flag')))",
                    "Number.call(anchor, Number(anchor.hasAttribute('flag')))",
                ),
                (
                    "coercing-branch",
                    "Number(anchor.hasAttribute('flag'))",
                    "anchor.hasAttribute('flag') ? 1 : anchor",
                ),
                (
                    "effectful-branch",
                    "Number(anchor.hasAttribute('flag'))",
                    "anchor.hasAttribute('flag') ? (anchor.unknown(), 1) : 0",
                ),
                (
                    "different-callee",
                    "Number(Number(anchor.hasAttribute('flag')))",
                    "(anchor.hasAttribute('flag') ? Number : anchor)(1)",
                ),
                ("bigint-input", "Number(anchor.hasAttribute('flag'))", "1n"),
                ("invalid-name", "'data-written'", "'bad name'"),
            )
        }
    )
    string_body = SOURCES["write-number-read-string-return"].split("{", 1)[1].rsplit("}", 1)[0]
    refusals.update(
        {
            "write-number-read-string-" + name: string_body.replace(before, after)
            for name, before, after in (
                ("without-intrinsic", "Number", "Number"),
                ("rebound-intrinsic", "try {", "try { Number = anchor;"),
                ("coercing-input", "anchor.getAttribute('data-number')", "anchor"),
                (
                    "wrong-receiver",
                    "const saved = Number(anchor.getAttribute('data-number')).toString()",
                    "const number = Number(anchor.getAttribute('data-number')); const stringify = number.toString; const saved = stringify()",
                ),
                ("different-callee", ".toString()", ".toFixed()"),
                ("radix", ".toString()", ".toString(16)"),
                ("coercing-radix", ".toString()", ".toString(anchor)"),
                (
                    "effectful-branch",
                    "Number(anchor.getAttribute('data-number'))",
                    "(anchor.hasAttribute('flag') ? (anchor.unknown(), 1) : Number(anchor.getAttribute('data-number')))",
                ),
                (
                    "rebound-method",
                    "try {",
                    "try { Number.prototype.toString = anchor;",
                ),
                ("invalid-name", "'data-written'", "'bad name'"),
            )
        }
    )
    prefix_body = (
        SOURCES["write-number-read-string-prefix-return"].split("{", 1)[1].rsplit("}", 1)[0]
    )
    refusals.update(
        {
            "write-number-read-string-prefix-" + name: prefix_body.replace(before, after)
            for name, before, after in (
                ("without-intrinsic", "Number", "Number"),
                ("rebound-intrinsic", "const saved =", "Number = anchor; const saved ="),
                ("coercing-input", "anchor.getAttribute('data-number')", "anchor"),
                (
                    "wrong-receiver",
                    "const saved = Number(anchor.getAttribute('data-number')).toString()",
                    "const number = Number(anchor.getAttribute('data-number')); const stringify = number.toString; const saved = stringify()",
                ),
                ("different-callee", ".toString()", ".toFixed()"),
                ("radix", ".toString()", ".toString(16)"),
                ("coercing-radix", ".toString()", ".toString(anchor)"),
                (
                    "effectful-branch",
                    "Number(anchor.getAttribute('data-number'))",
                    "(anchor.hasAttribute('flag') ? (anchor.unknown(), 1) : Number(anchor.getAttribute('data-number')))",
                ),
                (
                    "rebound-method",
                    "const saved =",
                    "Number.prototype.toString = anchor; const saved =",
                ),
                ("invalid-name", "'data-written'", "'bad name'"),
            )
        }
    )
    raw_string_body = SOURCES["write-string-read-prefix-return"].split("{", 1)[1].rsplit("}", 1)[0]
    refusals.update(
        {
            "write-string-read-prefix-" + name: raw_string_body.replace(before, after)
            for name, before, after in (
                ("coercing-input", "anchor.getAttribute('data-number')", "anchor"),
                (
                    "wrong-receiver",
                    "const saved = anchor.getAttribute('data-number')",
                    "const read = anchor.getAttribute; const saved = read('data-number')",
                ),
                (
                    "rebound-getter",
                    "const saved =",
                    "anchor.getAttribute = anchor.setAttribute; const saved =",
                ),
                ("missing-argument", "getAttribute('data-number')", "getAttribute()"),
                (
                    "extra-argument",
                    "getAttribute('data-number')",
                    "getAttribute('data-number', 'extra')",
                ),
                (
                    "coercing-name",
                    "getAttribute('data-number')",
                    "getAttribute(anchor)",
                ),
                (
                    "unknown-getter",
                    "anchor.getAttribute('data-number')",
                    "anchor.unknown",
                ),
                (
                    "unknown-producer",
                    "anchor.getAttribute('data-number')",
                    "anchor.unknown()",
                ),
                ("prefix-effect", "const saved =", "anchor.unknown(); const saved ="),
                ("invalid-name", "'data-written'", "'bad name'"),
            )
        }
    )
    conditional_string_body = (
        SOURCES["write-string-read-conditional-prefix-return"].split("{", 1)[1].rsplit("}", 1)[0]
    )
    refusals.update(
        {
            "write-string-read-conditional-prefix-"
            + name: conditional_string_body.replace(before, after)
            for name, before, after in (
                ("unknown-condition", "anchor.hasAttribute('flag')", "anchor.unknown"),
                (
                    "condition-effect",
                    "anchor.hasAttribute('flag')",
                    "(anchor.unknown(), anchor.hasAttribute('flag'))",
                ),
                (
                    "then-effect",
                    "anchor.getAttribute('data-number')",
                    "(anchor.unknown(), anchor.getAttribute('data-number'))",
                ),
                ("else-effect", ": null;", ": (anchor.unknown(), null);"),
                ("then-coercion", "anchor.getAttribute('data-number')", "anchor"),
                ("else-coercion", ": null;", ": anchor;"),
                (
                    "wrong-receiver",
                    "anchor.getAttribute('data-number')",
                    "(0, anchor.getAttribute)('data-number')",
                ),
                (
                    "rebound-getter",
                    "const saved =",
                    "anchor.getAttribute = anchor.setAttribute; const saved =",
                ),
                ("missing-argument", "getAttribute('data-number')", "getAttribute()"),
                (
                    "extra-argument",
                    "getAttribute('data-number')",
                    "getAttribute('data-number', 'extra')",
                ),
                (
                    "coercing-name",
                    "getAttribute('data-number')",
                    "getAttribute(anchor)",
                ),
                ("invalid-name", "'data-written'", "'bad name'"),
            )
        }
    )
    conditional_boolean_body = (
        SOURCES["write-boolean-read-conditional-prefix-return"].split("{", 1)[1].rsplit("}", 1)[0]
    )
    refusals.update(
        {
            "write-boolean-read-conditional-prefix-"
            + name: conditional_boolean_body.replace(before, after)
            for name, before, after in (
                ("unknown-condition", "anchor.hasAttribute('flag')", "anchor.unknown"),
                (
                    "condition-effect",
                    "anchor.hasAttribute('flag')",
                    "(anchor.unknown(), anchor.hasAttribute('flag'))",
                ),
                (
                    "then-effect",
                    "anchor.hasAttribute('data-number')",
                    "(anchor.unknown(), anchor.hasAttribute('data-number'))",
                ),
                ("else-effect", ": false;", ": (anchor.unknown(), false);"),
                ("then-coercion", "anchor.hasAttribute('data-number')", "anchor"),
                ("else-coercion", ": false;", ": anchor;"),
                (
                    "wrong-receiver",
                    "anchor.hasAttribute('data-number')",
                    "(0, anchor.hasAttribute)('data-number')",
                ),
                (
                    "rebound-predicate",
                    "const saved =",
                    "anchor.hasAttribute = anchor.setAttribute; const saved =",
                ),
                ("missing-argument", "hasAttribute('data-number')", "hasAttribute()"),
                (
                    "extra-argument",
                    "hasAttribute('data-number')",
                    "hasAttribute('data-number', 'extra')",
                ),
                ("coercing-name", "hasAttribute('data-number')", "hasAttribute(anchor)"),
                ("invalid-name", "'data-written'", "'bad name'"),
            )
        }
    )
    refusals.update(
        {
            name.replace("prefix-", "prefix-nullable-", 1): body.replace(
                ": false;", ": null;"
            ).replace("(anchor.unknown(), false)", "(anchor.unknown(), null)")
            for name, body in refusals.items()
            if name.startswith("write-boolean-read-conditional-prefix-")
        }
    )
    # Null never authorizes a Number method; undefined and String arms are not
    # Number/null alternatives.
    nullable_number_body = (
        SOURCES["write-nullable-number-conditional-prefix-return"]
        .split("{", 1)[1]
        .rsplit("}", 1)[0]
    )
    refusals.update(
        {
            "write-nullable-number-conditional-prefix-"
            + name: nullable_number_body.replace(before, after)
            for name, before, after in (
                ("unknown-condition", "anchor.hasAttribute('flag')", "anchor.unknown"),
                (
                    "condition-effect",
                    "anchor.hasAttribute('flag')",
                    "(anchor.unknown(), anchor.hasAttribute('flag'))",
                ),
                ("then-effect", "? 42 :", "? (anchor.unknown(), 42) :"),
                ("else-effect", ": null;", ": (anchor.unknown(), null);"),
                ("then-coercion", "? 42 :", "? -anchor :"),
                ("else-coercion", ": null;", ": anchor;"),
                ("else-undefined", ": null;", ": undefined;"),
                ("else-string", ": null;", ": 'text';"),
                ("number-method", "'data-written', saved)", "'data-written', saved.toString())"),
                (
                    "wrong-receiver",
                    "anchor.hasAttribute('flag')",
                    "(0, anchor.hasAttribute)('flag')",
                ),
                (
                    "rebound-predicate",
                    "const saved =",
                    "anchor.hasAttribute = anchor.setAttribute; const saved =",
                ),
                ("invalid-name", "'data-written'", "'bad name'"),
            )
        }
    )
    nested_body = SOURCES["mixed-node-nested-state"].split("{", 1)[1].rsplit("}", 1)[0]
    refusals["nested-protected-throw-call"] = nested_body.replace(
        "throw anchor;", "decodeURIComponent('%'); throw anchor;"
    )
    helper_body = SOURCES["helper-next-record"].split("{", 1)[1].rsplit("}", 1)[0]
    refusals.update(
        {
            "helper-invalid-name": helper_body.replace("'data-next'", "'bad name'"),
            "helper-impure-call": helper_body.replace(
                "const done =", "anchor.unknown(); const done ="
            ),
            "helper-protected-getter": helper_body.replace(
                "const result = next();", "anchor.unknown; const result = next();"
            ),
        }
    )
    for name, body in refusals.items():
        ir, contract = dom.prepare(
            args,
            name,
            f"function customElements(anchor) {{ {body} }}\n",
            1,
            entry_name="customElements",
        )
        for provider in dom.PROVIDERS:
            for optimize in (False, True):
                manifest = dict(
                    contract, provider=provider, initial_intrinsics=["decodeURIComponent"]
                )
                if name.startswith(
                    ("write-number-read-", "write-number-boolean-")
                ) and not name.endswith("-without-intrinsic"):
                    manifest["initial_intrinsics"].append("Number")
                if name == "invocation-without-contract":
                    manifest["element_parameters"] = []
                dom.lower(
                    args,
                    ir,
                    manifest,
                    f"{name}-{provider}-{optimize}",
                    optimize=optimize,
                    success=False,
                )
    check_outer_catches(args)
    print("confined node catch checks passed")


OUTER_SOURCES = {
    "caught-node-getter-identity": "function customElements(anchor) {\n  const values = {\n    [Symbol.iterator]() { return this; },\n    next() {\n      const done = anchor.hasAttribute('data-yielded');\n      anchor.setAttribute('data-next', done);\n      anchor.setAttribute('data-yielded', 'yes');\n      return {done: done, value: anchor};\n    },\n    get return() {\n      anchor.setAttribute('data-closed', 'yes');\n      throw anchor;\n    }\n  };\n  try {\n  for (const node of values) {\n    return 1;\n    if (anchor.hasAttribute('stop')) break;\n  }\n  } catch (error) { return error === anchor; }\n  return anchor.hasAttribute('data-visited');\n}\n",
    "caught-node-method-identity": "function customElements(anchor) {\n  const values = {\n    [Symbol.iterator]() { return this; },\n    next() {\n      const done = anchor.hasAttribute('data-yielded');\n      anchor.setAttribute('data-next', done);\n      anchor.setAttribute('data-yielded', 'yes');\n      return {done: done, value: anchor};\n    },\n    return() {\n      anchor.setAttribute('data-closed', 'yes');\n      throw anchor;\n    }\n  };\n  try {\n  for (const node of values) {\n    return 1;\n    if (anchor.hasAttribute('stop')) break;\n  }\n  } catch (error) { return error === anchor; }\n  return anchor.hasAttribute('data-visited');\n}\n",
}


# Each helper's iterator, step and close results, in the order helper_results() replaces them.
HELPER_RESULTS = ("return this;", "return {done: done, value: anchor};", "throw anchor;")


def helper_results(source, *results):
    for original, result in zip(HELPER_RESULTS, results, strict=True):
        source = source.replace(original, result)
    return source


# Keep the original witnesses intact; each helper gains a live unconditional edge.
RAW_HELPER_SOURCES = {
    name + "-helper-edges": source.replace("() {", "() { do { break; } while (false);")
    for name, source in OUTER_SOURCES.items()
}
RAW_HELPER_SOURCES |= {
    name + "-helper-branches": source.replace("() {", "() { if (false) { unknown(); }")
    for name, source in OUTER_SOURCES.items()
}
RAW_HELPER_SOURCES |= {
    name
    + "-helper-results": source.replace("return this;", "return true ? this : null;")
    .replace(
        "return {done: done, value: anchor};",
        "return false ? null : {done: done, value: anchor};",
    )
    .replace("throw anchor;", "throw true ? anchor : null;")
    for name, source in OUTER_SOURCES.items()
}
RAW_HELPER_SOURCES |= {
    name
    + "-helper-primitives": source.replace(
        "() {", "() { if (0) { unknown(); } if (null) { unknown(); }"
    )
    .replace("return this;", "return 1 ? this : null;")
    .replace(
        "return {done: done, value: anchor};",
        "return '' ? null : {done: done, value: anchor};",
    )
    .replace("throw anchor;", "throw 'close' ? anchor : null;")
    for name, source in OUTER_SOURCES.items()
}


for suffix, *results in (
    (
        "not",
        "return !0 ? this : null;",
        "return !'next' ? null : {done: done, value: anchor};",
        "throw !!'close' ? anchor : null;",
    ),
    (
        "transported-not",
        "return !(true ? 0 : 1) ? this : null;",
        "return !(false ? '' : 'next') ? null : {done: done, value: anchor};",
        "throw !!(true ? 'close' : '') ? anchor : null;",
    ),
    (
        "number-comparisons",
        "return 1 < 2 ? this : null;",
        "return 2 === 1 ? null : {done: done, value: anchor};",
        "throw (true ? 2 : 0) >= 1 ? anchor : null;",
    ),
    (
        "number-arithmetic",
        "return (1 + 2) * 0 ? null : this;",
        "return (5 % 2) - 1 ? null : {done: done, value: anchor};",
        "throw (true ? 8 : 0) / 2 ? anchor : null;",
    ),
    (
        "number-unary",
        "return +0 ? null : this;",
        "return -(+0) ? null : {done: done, value: anchor};",
        "throw -(true ? 2 : 0) < +0 ? anchor : null;",
    ),
    (
        "number-bitwise",
        "return (~0 & 1) ^ 1 ? null : this;",
        "return (1 << 33) | (8 >> 2) ? {done: done, value: anchor} : null;",
        "throw (true ? -1 : 0) >>> 31 ? anchor : null;",
    ),
    (
        "primitive-equality",
        "return null === false ? null : this;",
        "return 'next' == 'next' ? {done: done, value: anchor} : null;",
        "throw (true ? false : 0) !== 0 ? anchor : null;",
    ),
    (
        "number-power",
        "return 1 ** (1 / 0) ? null : this;",
        "return (-0) ** -3 < 0 ? {done: done, value: anchor} : null;",
        "throw (true ? -2 : 0) ** 3 === -8 ? anchor : null;",
    ),
    (
        "scalar-loose-equality",
        "return null == undefined ? this : null;",
        "return false == 0 ? {done: done, value: anchor} : null;",
        "throw (true ? true : 0) != 1 ? null : anchor;",
    ),
    (
        "coercing-comparisons",
        "return '0x10' == 16 ? this : null;",
        "return null >= false ? {done: done, value: anchor} : null;",
        "throw (true ? '1' : '') >= true ? anchor : null;",
    ),
    (
        "string-order",
        "return '10' < '2' ? this : null;",
        "return '\\uD800\\uDC01' >= '\\uD800\\uDC00' ? {done: done, value: anchor} : null;",
        "throw (true ? 'a\\0' : '') > 'a' ? anchor : null;",
    ),
):
    RAW_HELPER_SOURCES |= {
        f"{name}-helper-{suffix}": helper_results(source, *results)
        for name, source in OUTER_SOURCES.items()
    }
# Canonical Strings whose UTF-16 order differs from their WTF-8 byte order.
RAW_HELPER_SOURCES |= {
    name.replace("-helper-string-order", "-helper-unicode-order"): source.replace(
        "'10' < '2'", "'\\uD800\\uDC00' < '\\uE000'"
    )
    for name, source in RAW_HELPER_SOURCES.items()
    if name.endswith("-helper-string-order")
}


for suffix, *results in (
    (
        "wide-string-number",
        "return '000000000000000000000000000000000000000016' == 16 ? this : null;",
        "return '\\u00A0                                 +Infinity\\uFEFF' >= 1 ? {done: done, value: anchor} : null;",
        "throw (true ? '0b000000000000000000000000000000000000000001' : '') >= true ? anchor : null;",
    ),
    (
        "primitive-unary",
        "return +'000000000000000000000000000000000000000000' ? null : this;",
        "return -'\\u00A0+Infinity\\uFEFF' < -true ? {done: done, value: anchor} : null;",
        "throw ~(true ? '0b11111111111111111111111111111111' : '') === +null ? anchor : null;",
    ),
    (
        "primitive-binary",
        "return '1' ** true ? this : null;",
        "return '000000000000000000000000000000000000000008' / '2' ? {done: done, value: anchor} : null;",
        "throw (true ? '0b11111111111111111111111111111111' : '') >>> '31' ? anchor : null;",
    ),
    (
        "string-add",
        "return ('prefix:' + 1e21) === 'prefix:1e+21' ? this : null;",
        "return (true + '') === 'true' ? {done: done, value: anchor} : null;",
        "throw ((true ? '\\uD800' : '') + '\\uDC00') === '\\uD800\\uDC00' ? anchor : null;",
    ),
    (
        "template-concat",
        "return `${1e21}` === '1e+21' ? this : null;",
        "return `${false}` === 'false' ? {done: done, value: anchor} : null;",
        "throw `${true ? '\\uD800' : ''}\\uDC00` === '\\uD800\\uDC00' ? anchor : null;",
    ),
    (
        "typeof-void",
        "return typeof null === 'object' ? this : null;",
        "return void 'next' ? null : {done: done, value: anchor};",
        "throw typeof (true ? false : 0) === 'boolean' ? anchor : null;",
    ),
):
    RAW_HELPER_SOURCES |= {
        f"{name}-helper-{suffix}": helper_results(source, *results)
        for name, source in OUTER_SOURCES.items()
    }


# Retain the complete source-global witnesses; the initialized provider and
# absence of source writes must prove the binding before helper projection.
RAW_HELPER_SOURCES |= {
    name.replace("-primitive-equality", "-global-undefined"): source.replace(
        "null === false", "null === undefined"
    )
    for name, source in RAW_HELPER_SOURCES.items()
    if name.endswith("-helper-primitive-equality")
}


def check_outer_catches(args):
    compilers = args.compilers
    includes, libraries = dom.link_options(args)
    checks = r"""
        const auto exercise = [&](ctbrowser::document & target, node_id id, auto && call) {
            const auto next = target.atoms().intern("data-next");
            const auto yielded = target.atoms().intern("data-yielded");
            const auto closed = target.atoms().intern("data-closed");
            const auto visited = target.atoms().intern("data-visited");
            target.log_writes(true);
            for (bool exhausted : {false, true}) {
                for (bool seen : {false, true}) {
                    assert(target.remove_attribute(id, yielded));
                    assert(target.remove_attribute(id, closed));
                    assert(target.remove_attribute(id, visited));
                    if (exhausted) { assert(target.set_attribute(id, yielded, "yes")); }
                    if (seen) { assert(target.set_attribute(id, visited, "yes")); }
                    (void)target.take_writes();
                    assert(static_cast<bool>(call()) == (!exhausted || seen));
                    const auto writes = target.take_writes();
                    assert(writes.size() == (exhausted ? 2u : 3u));
                    assert(writes[0].name == next && writes[1].name == yielded);
                    if (!exhausted) { assert(writes[2].name == closed); }
                    for (const auto & write : writes) { assert(write.node == id && !write.text); }
                    assert(target.read().attribute_value(id, next) == (exhausted ? "true" : "false"));
                    assert(target.read().attribute_value(id, yielded) == "yes");
                    assert(target.read().has_attribute(id, closed) == !exhausted);
                }
            }
        };
        exercise(doc, button, [&] { return @ENTRY@(alias); });
        exercise(foreign_doc, other_button, [&] { return @ENTRY@(foreign); });
        (void)pressed;
    """
    for name, source in (OUTER_SOURCES | RAW_HELPER_SOURCES).items():
        script = source + """
function observe(exhausted) {
  const saved = {};
  if (exhausted) saved['data-yielded'] = 'yes';
  let trace = '';
  const anchor = {
    hasAttribute(name) {
      const value = name in saved;
      trace += 'read:' + name + '=' + value + ';';
      return value;
    },
    setAttribute(name, value) {
      saved[name] = '' + value;
      trace += 'write:' + name + '=' + saved[name] + ';';
    }
  };
  try { return 'return:' + customElements(anchor) + ':' + trace; }
  catch (error) { return 'throw:' + (error === anchor) + ':' + trace; }
}
var outcome0 = observe(false), outcome1 = observe(true);
"""
        oracle = args.work / f"{name}.oracle.js"
        oracle.write_text(script)
        node = args.work / f"{name}.node.js"
        node.write_text(
            script
            + "console.log('outcome0=' + JSON.stringify(outcome0));\n"
            + "console.log('outcome1=' + JSON.stringify(outcome1));\n"
        )
        expected = (
            'outcome0="return:true:read:data-yielded=false;write:data-next=false;'
            'write:data-yielded=yes;write:data-closed=yes;"\n'
            'outcome1="return:false:read:data-yielded=true;write:data-next=true;'
            'write:data-yielded=yes;read:data-visited=false;"\n'
        )
        for command in ([args.node, str(node)], [args.reference, str(oracle)]):
            result = run(command)
            output = unquote(result.stdout) if command[0] == args.reference else result.stdout
            if output != expected:
                raise RuntimeError(f"{name}: outer catch oracle differs: {result}")
        ir, contract = dom.prepare(args, name, source, 1, entry_name="customElements")
        raw = args.work / f"{name}.raw.mlir"
        raw_manifest = dict(
            contract,
            module_sha256=dom.fingerprint(args.opt, raw),
            initial_intrinsics=INTRINSICS,
        )
        raw_label = f"{name}-raw-helpers"
        raw_steps = (
            1_000_000
            if name.endswith(
                (
                    "-helper-primitives",
                    "-helper-transported-not",
                    "-helper-number-bitwise",
                    "-helper-number-power",
                    "-helper-string-add",
                    "-helper-template-concat",
                )
            )
            else None
        )
        if raw_steps is not None:
            refused = dom.lower(
                args, raw, raw_manifest, raw_label + "-default-budget", success=False
            )
            if "DOM custom iterator budget exhausted" not in refused:
                raise RuntimeError(f"{name}: larger raw helpers lost their budget refusal")
        native = dom.lower(args, raw, raw_manifest, raw_label, max_steps=raw_steps)
        dom.standalone(args, native, raw_label, checks, compilers, includes, libraries)
        dom.lower(args, raw, raw_manifest, raw_label + "-zero-budget", success=False, max_steps=0)
        for provider in dom.PROVIDERS:
            for optimize in (False, True):
                label = f"{name}-{provider}-{optimize}"
                manifest = dict(contract, provider=provider, initial_intrinsics=INTRINSICS)
                native = dom.lower(
                    args,
                    ir,
                    manifest,
                    label,
                    optimize=optimize,
                )
                cpp = run([args.translate, "--mlir-to-cpp", str(native)]).stdout
                source_cpp = cpp.replace(
                    'throw std::invalid_argument("DOM element belongs to another session");', ""
                )
                if "throw " in source_cpp or "catch (" in source_cpp:
                    raise RuntimeError("iterator catch escaped into a native exception")
                dom.standalone(args, native, label, checks, compilers, includes, libraries)
                dom.lower(
                    args,
                    ir,
                    manifest,
                    label + "-zero-budget",
                    optimize=optimize,
                    success=False,
                    max_steps=0,
                )


if __name__ == "__main__":
    main()
