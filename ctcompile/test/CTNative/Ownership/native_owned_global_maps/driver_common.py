"""Execute checked published Map methods and live primitive results without the VM."""

# Split out of native-owned-global-maps.py on 2026-09-08: this is its main(),
# verbatim. The docstring above is the one argparse prints, so it stays here.

import argparse
import json
from pathlib import Path
import re
import shutil
import struct
import subprocess

from .sources import (
    methods,
    owned,
    boundary,
    host,
    SOURCE,
    SHARED,
    parameter_sources,
    result_sources,
    seeded_result_sources,
    key_fact_sources,
    joined_result_sources,
    seeded_carrier_refusals,
    RESULT_SIGNATURES,
    refusal_sources,
    parameter_refusals,
    result_refusals,
    seeded_result_refusals,
    size_result_sources,
    size_result_refusals,
    payload_result_sources,
    payload_result_refusals,
    mixed_result_sources,
    mixed_result_refusals,
    saved_read_sources,
    saved_read_refusals,
    saved_join_sources,
    saved_join_refusals,
    guarded_saved_sources,
    guarded_saved_refusals,
    shortcircuit_sources,
    shortcircuit_refusals,
    nullable_result_sources,
    nullable_result_refusals,
    mixed_nullable_payload_refusals,
    nullable_key_sources,
    nullable_key_refusals,
    NULLABLE_OBSERVATIONS,
    NULLABLE_KEY_CALLS,
    nullable_payload_sources,
    nullable_payload_refusals,
    NULLABLE_PAYLOAD_CALLS,
    NULLABLE_PAYLOAD_READBACKS,
    nullable_host_result_sources,
    nullable_host_result_refusals,
    NULLABLE_HOST_RESULT_CALLS,
    nullable_nested_result_sources,
    nullable_nested_result_refusals,
    NULLABLE_NESTED_RESULT_CALLS,
    leaf_object_sources,
    leaf_object_refusals,
    LEAF_OBJECT_CALLS,
    LEAF_OBJECT_FUNCTIONS,
    leaf_readback_sources,
    leaf_readback_refusals,
    LEAF_READBACK_CALLS,
    LEAF_COMPARISON_REPAIRS,
    LEAF_COMPARISON_CASES,
    LEAF_READBACK_UNOWNED,
    LEAF_FIELD_RESULTS,
    leaf_field_result_refusals,
    leaf_absence_cases,
    leaf_absence_sources,
    leaf_absence_refusals,
    LEAF_ABSENCE_UNOWNED,
    LEAF_ABSENCE_PROMOTED_REFUSALS,
    primitive_absence_sources,
    leaf_clear_cases,
    leaf_clear_sources,
    leaf_clear_refusals,
    LEAF_CLEAR_UNOWNED,
    LEAF_CLEAR_PROMOTED,
    numeric_entry_cases,
    numeric_entry_sources,
    numeric_entry_refusals,
    NUMERIC_ENTRY_PROMOTED,
    NUMERIC_ENTRY_CARRIERS,
    NUMERIC_ENTRY_SAVED_GLOBALS,
    scalar_global_cases,
    scalar_global_sources,
    scalar_global_refusals,
    scalar_global_output,
    SCALAR_GLOBAL_CARRIERS,
    SCALAR_GLOBAL_INITIALIZED,
    constant_global_cases,
    constant_global_sources,
    normalized_scalar_output,
    CONSTANT_GLOBAL_UNOWNED,
    CONSTANT_GLOBAL_CARRIERS,
    CONSTANT_GLOBAL_EXISTING,
    StringValue,
    string_field_cases,
    string_field_sources,
    STRING_FIELD_PROMOTED,
    zero_size_cases,
    zero_size_sources,
    one_size_cases,
    one_size_sources,
    delete_size_cases,
    delete_size_sources,
    join_size_cases,
    join_size_sources,
    mutation_size_cases,
    mutation_size_sources,
    object_argument_cases,
    object_argument_sources,
)
from .harness import (
    source_calls,
    contract,
    resolve_getter,
    lifetime,
    standalone,
    check_call_preservation,
    forge_map_presence,
    check_budgets,
    check_prepared_result_calls,
    nullable_observer_source,
    leaf_object_observer_source,
    forge_leaf_evidence,
    comparison_identity_observer_source,
    LEAF_ABSENCE_LIFETIMES,
    leaf_absence_observer_source,
    primitive_absence_observer_source,
    LEAF_CLEAR_LIFETIMES,
    leaf_clear_observer_source,
    NUMERIC_ENTRY_LIFETIMES,
    numeric_entry_observer_source,
    string_field_observer_source,
    zero_size_observer_source,
    one_size_observer_source,
    delete_size_observer_source,
    object_argument_observer_source,
    retained_key_observer_source,
)


def comparable_provenance(cpp, input_ir):
    # Reparsed forged input has a different filename but the same source
    # locations. Keep every line/column, other comment byte and emitted token.
    filename = re.compile(r"(?<!\S)" + re.escape(str(input_ir)) + r"(?=:\d+:\d+(?:\D|$))")
    return "".join(
        filename.sub("<input>", line) if line.startswith("// ctcompile:") else line
        for line in cpp.splitlines(keepends=True)
    )


def numeric_reference_output(name, value):
    return scalar_global_output(name, value)


def numeric_node_observer(value):
    if isinstance(value, StringValue):
        return CONSTANT_GLOBAL_NODE.replace("JSON.parse(process.argv[2])", '["trace"]')
    if not isinstance(value, bool) and value not in {"NaN", "-Infinity"}:
        return boundary.NODE
    predicate = "typeof trace !== 'number' || !Number.isFinite(trace)"
    if boundary.NODE.count(predicate) != 1:
        raise RuntimeError("exact NaN observer lost the shared finite-number control")
    condition = (
        "typeof trace !== 'boolean'"
        if isinstance(value, bool)
        else (
            "typeof trace !== 'number' || !Number.isNaN(trace)"
            if value == "NaN"
            else "typeof trace !== 'number' || trace !== -Infinity"
        )
    )
    return boundary.NODE.replace(predicate, condition)


CONSTANT_GLOBAL_NODE = r"""const fs = require('node:fs');
const vm = require('node:vm');
const context = vm.createContext({});
vm.runInContext(fs.readFileSync(process.argv[1], 'utf8'), context);
for (const name of JSON.parse(process.argv[2])) {
    const value = vm.runInContext(name, context);
    let text;
    if (typeof value === 'number') {
        text = Number.isNaN(value) ? 'nan' : Object.is(value, -0) ? '-0' :
            value === -Infinity ? '-inf' : String(value);
    } else if (typeof value === 'string') {
        text = '"' + encodeURIComponent(value).replace(/[!'()*]/g,
            c => '%' + c.charCodeAt(0).toString(16).toUpperCase()) + '"';
    } else if (typeof value === 'boolean' || value === undefined || value === null) {
        text = String(value);
    } else {
        throw new Error(name + ': unexpected scalar tag');
    }
    process.stdout.write(name + '=' + text + '\n');
}
"""
