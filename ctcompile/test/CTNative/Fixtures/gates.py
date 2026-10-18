# GATES below copies the gate RUN lines from each case's .test until that
# case's switch commit deletes them there; until then, edit both copies.
"""The fixtures' gate RUN lines as pytest tests: %compilation_unit,
%compile_clean and %print_deduced, one test per gate parametrized over the
fixture cases below, with each case's lit path (relative to this folder,
without .test) as its id. So `test_compile_clean[Maps/map]` is lit's
`ctcompile :: CTNative/Fixtures/Maps/map.test`, and --lit-filter selects all
three gates of a case.

GATES holds each case's positive gate RUN lines after the substitution,
copied verbatim; %S is the case's folder. A case's negative gate lines (`not
%compilation_unit ... | FileCheck`) and any other RUN line stay in its .test.
Until a case's switch commit deletes its gate lines from the .test, lit runs
them too, so an edit to one copy belongs in both.
"""

import importlib
from pathlib import Path

import pytest

HERE = Path(__file__).resolve().parent
# The gate scripts' module names are hyphenated, so import_module, not import.
MAINS = {
    gate: importlib.import_module("CTNative.Checks." + gate.replace("_", "-")).main
    for gate in ("compilation_unit", "compile_clean", "print_deduced")
}


def run(lit, case, gate):
    """Every `gate` RUN line of `case`, in order, through the gate's main.
    The substitution itself starts `python3 <script>`, which main drops."""
    folder = str(HERE / case.rpartition("/")[0])
    for line in GATES[case][gate]:
        MAINS[gate](lit.args(f"%{gate} " + line.replace("%S", folder))[2:])


GATES = {
    "Closures/callback": {
        "compilation_unit": [
            "--module %{obj}/callbacks.pipeline.emitc.mlir --js %S/callback.js --work %t --name callbacks",
            "--module %{obj}/callbacks.pipeline.deduced.emitc.mlir --js %S/callback.js --work %t --name callbacks_deduced",
        ],
        "compile_clean": [
            "--module %{obj}/callbacks.pipeline.emitc.mlir --work %t --name callbacks",
            "--module %{obj}/callbacks.pipeline.deduced.emitc.mlir --work %t --name callbacks_deduced",
        ],
        "print_deduced": [
            "--plain %{obj}/callbacks.pipeline.emitc.mlir --deduced %{obj}/callbacks.pipeline.deduced.emitc.mlir --mutated %{obj}/callbacks.pipeline.mutated.emitc.mlir --work %t --name callbacks",
        ],
    },
    "Closures/closure": {
        "compilation_unit": [
            "--module %{obj}/closures.pipeline.emitc.mlir --js %S/closure.js --work %t --name closures",
            "--module %{obj}/closures.pipeline.deduced.emitc.mlir --js %S/closure.js --work %t --name closures_deduced",
        ],
        "compile_clean": [
            "--module %{obj}/closures.pipeline.emitc.mlir --work %t --name closures",
            "--module %{obj}/closures.pipeline.deduced.emitc.mlir --work %t --name closures_deduced",
        ],
        "print_deduced": [
            "--plain %{obj}/closures.pipeline.emitc.mlir --deduced %{obj}/closures.pipeline.deduced.emitc.mlir --mutated %{obj}/closures.pipeline.mutated.emitc.mlir --work %t --name closures",
        ],
    },
    "Closures/deep-binding": {
        "compilation_unit": [
            "--module %{obj}/deep_bindings.pipeline.emitc.mlir --js %S/deep-binding.js --work %t --name deep_bindings",
            "--module %{obj}/deep_bindings.pipeline.deduced.emitc.mlir --js %S/deep-binding.js --work %t --name deep_bindings_deduced",
        ],
        "compile_clean": [
            "--module %{obj}/deep_bindings.pipeline.emitc.mlir --work %t --name deep_bindings",
            "--module %{obj}/deep_bindings.pipeline.deduced.emitc.mlir --work %t --name deep_bindings_deduced",
        ],
        "print_deduced": [
            "--plain %{obj}/deep_bindings.pipeline.emitc.mlir --deduced %{obj}/deep_bindings.pipeline.deduced.emitc.mlir --mutated %{obj}/deep_bindings.pipeline.mutated.emitc.mlir --work %t --name deep_bindings",
        ],
    },
    "Closures/hoisted-capture": {
        "compilation_unit": [
            "--module %{obj}/hoisted_captures.pipeline.emitc.mlir --js %S/hoisted-capture.js --work %t --name hoisted_captures",
            "--module %{obj}/hoisted_captures.pipeline.deduced.emitc.mlir --js %S/hoisted-capture.js --work %t --name hoisted_captures_deduced",
        ],
        "compile_clean": [
            "--module %{obj}/hoisted_captures.pipeline.emitc.mlir --work %t --name hoisted_captures",
            "--module %{obj}/hoisted_captures.pipeline.deduced.emitc.mlir --work %t --name hoisted_captures_deduced",
        ],
        "print_deduced": [
            "--plain %{obj}/hoisted_captures.pipeline.emitc.mlir --deduced %{obj}/hoisted_captures.pipeline.deduced.emitc.mlir --mutated %{obj}/hoisted_captures.pipeline.mutated.emitc.mlir --work %t --name hoisted_captures",
        ],
    },
    "Closures/local-function": {
        "compilation_unit": [
            "--module %{obj}/local_functions.pipeline.emitc.mlir --js %S/local-function.js --work %t --name local_functions",
            "--module %{obj}/local_functions.pipeline.deduced.emitc.mlir --js %S/local-function.js --work %t --name local_functions_deduced",
        ],
        "compile_clean": [
            "--module %{obj}/local_functions.pipeline.emitc.mlir --work %t --name local_functions",
            "--module %{obj}/local_functions.pipeline.deduced.emitc.mlir --work %t --name local_functions_deduced",
        ],
        "print_deduced": [
            "--plain %{obj}/local_functions.pipeline.emitc.mlir --deduced %{obj}/local_functions.pipeline.deduced.emitc.mlir --mutated %{obj}/local_functions.pipeline.mutated.emitc.mlir --work %t --name local_functions",
        ],
    },
    "Closures/method-table": {
        "compilation_unit": [
            "--module %{obj}/method_tables.pipeline.emitc.mlir --js %S/method-table.js --work %t --name method_tables",
            "--module %{obj}/method_tables.pipeline.deduced.emitc.mlir --js %S/method-table.js --work %t --name method_tables_deduced",
        ],
        "compile_clean": [
            "--module %{obj}/method_tables.pipeline.emitc.mlir --work %t --name method_tables",
            "--module %{obj}/method_tables.pipeline.deduced.emitc.mlir --work %t --name method_tables_deduced",
        ],
        "print_deduced": [
            "--plain %{obj}/method_tables.pipeline.emitc.mlir --deduced %{obj}/method_tables.pipeline.deduced.emitc.mlir --mutated %{obj}/method_tables.pipeline.mutated.emitc.mlir --work %t --name method_tables",
        ],
    },
    "Closures/nested-closure": {
        "compilation_unit": [
            "--module %{obj}/nested_closures.pipeline.emitc.mlir --js %S/nested-closure.js --work %t --name nested_closures",
            "--module %{obj}/nested_closures.pipeline.deduced.emitc.mlir --js %S/nested-closure.js --work %t --name nested_closures_deduced",
        ],
        "compile_clean": [
            "--module %{obj}/nested_closures.pipeline.emitc.mlir --work %t --name nested_closures",
            "--module %{obj}/nested_closures.pipeline.deduced.emitc.mlir --work %t --name nested_closures_deduced",
        ],
        "print_deduced": [
            "--plain %{obj}/nested_closures.pipeline.emitc.mlir --deduced %{obj}/nested_closures.pipeline.deduced.emitc.mlir --mutated %{obj}/nested_closures.pipeline.mutated.emitc.mlir --work %t --name nested_closures",
        ],
    },
    "Closures/owned-method-table-slot": {
        "compilation_unit": [
            "--module %{obj}/owned_method_table_slots.pipeline.emitc.mlir --js %S/owned-method-table-slot.js --work %t --name owned_method_table_slots",
            "--module %{obj}/owned_method_table_slots.pipeline.deduced.emitc.mlir --js %S/owned-method-table-slot.js --work %t --name owned_method_table_slots_deduced",
        ],
        "compile_clean": [
            "--module %{obj}/owned_method_table_slots.pipeline.emitc.mlir --work %t --name owned_method_table_slots",
            "--module %{obj}/owned_method_table_slots.pipeline.deduced.emitc.mlir --work %t --name owned_method_table_slots_deduced",
        ],
        "print_deduced": [
            "--plain %{obj}/owned_method_table_slots.pipeline.emitc.mlir --deduced %{obj}/owned_method_table_slots.pipeline.deduced.emitc.mlir --mutated %{obj}/owned_method_table_slots.pipeline.mutated.emitc.mlir --work %t --name owned_method_table_slots",
        ],
    },
    "Closures/returned-closure": {
        "compilation_unit": [
            "--module %{obj}/returned_closures.pipeline.emitc.mlir --js %S/returned-closure.js --work %t --name returned_closures",
            "--module %{obj}/returned_closures.pipeline.deduced.emitc.mlir --js %S/returned-closure.js --work %t --name returned_closures_deduced",
        ],
        "compile_clean": [
            "--module %{obj}/returned_closures.pipeline.emitc.mlir --work %t --name returned_closures",
            "--module %{obj}/returned_closures.pipeline.deduced.emitc.mlir --work %t --name returned_closures_deduced",
        ],
        "print_deduced": [
            "--plain %{obj}/returned_closures.pipeline.emitc.mlir --deduced %{obj}/returned_closures.pipeline.deduced.emitc.mlir --mutated %{obj}/returned_closures.pipeline.mutated.emitc.mlir --work %t --name returned_closures",
        ],
    },
    "Closures/shared-cell": {
        "compilation_unit": [
            "--module %{obj}/shared_cells.pipeline.emitc.mlir --js %S/shared-cell.js --work %t --name shared_cells",
            "--module %{obj}/shared_cells.pipeline.deduced.emitc.mlir --js %S/shared-cell.js --work %t --name shared_cells_deduced",
        ],
        "compile_clean": [
            "--module %{obj}/shared_cells.pipeline.emitc.mlir --work %t --name shared_cells",
            "--module %{obj}/shared_cells.pipeline.deduced.emitc.mlir --work %t --name shared_cells_deduced",
        ],
        "print_deduced": [
            "--plain %{obj}/shared_cells.pipeline.emitc.mlir --deduced %{obj}/shared_cells.pipeline.deduced.emitc.mlir --mutated %{obj}/shared_cells.pipeline.mutated.emitc.mlir --work %t --name shared_cells",
        ],
    },
    "ControlFlow/functions": {
        "compilation_unit": [
            "--module %{obj}/functions.pipeline.emitc.mlir --js %S/functions.js --work %t --name functions",
            "--module %{obj}/functions.pipeline.deduced.emitc.mlir --js %S/functions.js --work %t --name functions_deduced",
        ],
        "compile_clean": [
            "--module %{obj}/functions.pipeline.emitc.mlir --work %t --name functions",
            "--module %{obj}/functions.pipeline.deduced.emitc.mlir --work %t --name functions_deduced",
        ],
        "print_deduced": [
            "--plain %{obj}/functions.pipeline.emitc.mlir --deduced %{obj}/functions.pipeline.deduced.emitc.mlir --mutated %{obj}/functions.pipeline.mutated.emitc.mlir --work %t --name functions",
        ],
    },
    "ControlFlow/globals": {
        "compilation_unit": [
            "--module %{obj}/numeric.pipeline.emitc.mlir --js %S/globals.js --work %t --name numeric",
            "--module %{obj}/numeric.pipeline.deduced.emitc.mlir --js %S/globals.js --work %t --name numeric_deduced",
        ],
        "compile_clean": [
            "--module %{obj}/numeric.pipeline.emitc.mlir --work %t --name numeric",
            "--module %{obj}/numeric.pipeline.deduced.emitc.mlir --work %t --name numeric_deduced",
        ],
        "print_deduced": [
            "--plain %{obj}/numeric.pipeline.emitc.mlir --deduced %{obj}/numeric.pipeline.deduced.emitc.mlir --mutated %{obj}/numeric.pipeline.mutated.emitc.mlir --work %t --name numeric",
        ],
    },
    "Maps/flow": {
        "compilation_unit": [
            "--module %{obj}/map_flow.pipeline.emitc.mlir --js %S/flow.js --work %t --name map_flow",
            "--module %{obj}/map_flow.pipeline.deduced.emitc.mlir --js %S/flow.js --work %t --name map_flow_deduced",
        ],
        "compile_clean": [
            "--module %{obj}/map_flow.pipeline.emitc.mlir --work %t --name map_flow",
            "--module %{obj}/map_flow.pipeline.deduced.emitc.mlir --work %t --name map_flow_deduced",
        ],
        "print_deduced": [
            "--plain %{obj}/map_flow.pipeline.emitc.mlir --deduced %{obj}/map_flow.pipeline.deduced.emitc.mlir --mutated %{obj}/map_flow.pipeline.mutated.emitc.mlir --work %t --name map_flow",
        ],
    },
    "Maps/map": {
        "compilation_unit": [
            "--module %{obj}/maps.pipeline.emitc.mlir --js %S/map.js --work %t --name maps",
            "--module %{obj}/maps.pipeline.deduced.emitc.mlir --js %S/map.js --work %t --name maps_deduced",
        ],
        "compile_clean": [
            "--module %{obj}/maps.pipeline.emitc.mlir --work %t --name maps",
            "--module %{obj}/maps.pipeline.deduced.emitc.mlir --work %t --name maps_deduced",
        ],
        "print_deduced": [
            "--plain %{obj}/maps.pipeline.emitc.mlir --deduced %{obj}/maps.pipeline.deduced.emitc.mlir --mutated %{obj}/maps.pipeline.mutated.emitc.mlir --work %t --name maps",
        ],
    },
    "Maps/nested-map": {
        "compilation_unit": [
            "--module %{obj}/nested_maps.pipeline.emitc.mlir --js %S/nested-map.js --work %t --name nested_maps",
            "--module %{obj}/nested_maps.pipeline.deduced.emitc.mlir --js %S/nested-map.js --work %t --name nested_maps_deduced",
        ],
        "compile_clean": [
            "--module %{obj}/nested_maps.pipeline.emitc.mlir --work %t --name nested_maps",
            "--module %{obj}/nested_maps.pipeline.deduced.emitc.mlir --work %t --name nested_maps_deduced",
        ],
        "print_deduced": [
            "--plain %{obj}/nested_maps.pipeline.emitc.mlir --deduced %{obj}/nested_maps.pipeline.deduced.emitc.mlir --mutated %{obj}/nested_maps.pipeline.mutated.emitc.mlir --work %t --name nested_maps",
        ],
    },
    "Maps/object-key": {
        "compilation_unit": [
            "--module %{obj}/object_keys.pipeline.emitc.mlir --js %S/object-key.js --work %t --name object_keys",
            "--module %{obj}/object_keys.pipeline.deduced.emitc.mlir --js %S/object-key.js --work %t --name object_keys_deduced",
        ],
        "compile_clean": [
            "--module %{obj}/object_keys.pipeline.emitc.mlir --work %t --name object_keys",
            "--module %{obj}/object_keys.pipeline.deduced.emitc.mlir --work %t --name object_keys_deduced",
        ],
        "print_deduced": [
            "--plain %{obj}/object_keys.pipeline.emitc.mlir --deduced %{obj}/object_keys.pipeline.deduced.emitc.mlir --mutated %{obj}/object_keys.pipeline.mutated.emitc.mlir --work %t --name object_keys",
        ],
    },
    "Maps/presence": {
        "compilation_unit": [
            "--module %{obj}/map_presence.pipeline.emitc.mlir --js %S/presence.js --work %t --name map_presence",
            "--module %{obj}/map_presence.pipeline.deduced.emitc.mlir --js %S/presence.js --work %t --name map_presence_deduced",
        ],
        "compile_clean": [
            "--module %{obj}/map_presence.pipeline.emitc.mlir --work %t --name map_presence",
            "--module %{obj}/map_presence.pipeline.deduced.emitc.mlir --work %t --name map_presence_deduced",
        ],
        "print_deduced": [
            "--plain %{obj}/map_presence.pipeline.emitc.mlir --deduced %{obj}/map_presence.pipeline.deduced.emitc.mlir --mutated %{obj}/map_presence.pipeline.mutated.emitc.mlir --work %t --name map_presence",
        ],
    },
    "Maps/string-snapshots": {
        "compilation_unit": [
            "--module %{obj}/string_snapshots.pipeline.emitc.mlir --js %S/string-snapshots.js --work %t --name string_snapshots",
            "--module %{obj}/string_snapshots.pipeline.deduced.emitc.mlir --js %S/string-snapshots.js --work %t --name string_snapshots_deduced",
        ],
        "compile_clean": [
            "--module %{obj}/string_snapshots.pipeline.emitc.mlir --work %t --name string_snapshots",
            "--module %{obj}/string_snapshots.pipeline.deduced.emitc.mlir --work %t --name string_snapshots_deduced",
        ],
        "print_deduced": [
            "--plain %{obj}/string_snapshots.pipeline.emitc.mlir --deduced %{obj}/string_snapshots.pipeline.deduced.emitc.mlir --mutated %{obj}/string_snapshots.pipeline.mutated.emitc.mlir --work %t --name string_snapshots",
        ],
    },
    "Objects/argument": {
        "compilation_unit": [
            "--module %{obj}/object_arguments.pipeline.emitc.mlir --js %S/argument.js --work %t --name object_arguments",
            "--module %{obj}/object_arguments.pipeline.deduced.emitc.mlir --js %S/argument.js --work %t --name object_arguments_deduced",
        ],
        "compile_clean": [
            "--module %{obj}/object_arguments.pipeline.emitc.mlir --work %t --name object_arguments",
            "--module %{obj}/object_arguments.pipeline.deduced.emitc.mlir --work %t --name object_arguments_deduced",
        ],
        "print_deduced": [
            "--plain %{obj}/object_arguments.pipeline.emitc.mlir --deduced %{obj}/object_arguments.pipeline.deduced.emitc.mlir --mutated %{obj}/object_arguments.pipeline.mutated.emitc.mlir --work %t --name object_arguments",
        ],
    },
    "Objects/array": {
        "compilation_unit": [
            "--module %{obj}/arrays.pipeline.emitc.mlir --js %S/array.js --work %t --name arrays",
            "--module %{obj}/arrays.pipeline.deduced.emitc.mlir --js %S/array.js --work %t --name arrays_deduced",
        ],
        "compile_clean": [
            "--module %{obj}/arrays.pipeline.emitc.mlir --work %t --name arrays",
            "--module %{obj}/arrays.pipeline.deduced.emitc.mlir --work %t --name arrays_deduced",
        ],
        "print_deduced": [
            "--plain %{obj}/arrays.pipeline.emitc.mlir --deduced %{obj}/arrays.pipeline.deduced.emitc.mlir --mutated %{obj}/arrays.pipeline.mutated.emitc.mlir --work %t --name arrays",
        ],
    },
    "Objects/constructor": {
        "compilation_unit": [
            "--module %{obj}/constructors.pipeline.emitc.mlir --js %S/constructor.js --work %t --name constructors",
            "--module %{obj}/constructors.pipeline.deduced.emitc.mlir --js %S/constructor.js --work %t --name constructors_deduced",
        ],
        "compile_clean": [
            "--module %{obj}/constructors.pipeline.emitc.mlir --work %t --name constructors",
            "--module %{obj}/constructors.pipeline.deduced.emitc.mlir --work %t --name constructors_deduced",
        ],
        "print_deduced": [
            "--plain %{obj}/constructors.pipeline.emitc.mlir --deduced %{obj}/constructors.pipeline.deduced.emitc.mlir --mutated %{obj}/constructors.pipeline.mutated.emitc.mlir --work %t --name constructors",
        ],
    },
    "Objects/fields": {
        "compilation_unit": [
            "--module %{obj}/object_fields.pipeline.emitc.mlir --js %S/fields.js --work %t --name object_fields",
            "--module %{obj}/object_fields.pipeline.deduced.emitc.mlir --js %S/fields.js --work %t --name object_fields_deduced",
        ],
        "compile_clean": [
            "--module %{obj}/object_fields.pipeline.emitc.mlir --work %t --name object_fields",
            "--module %{obj}/object_fields.pipeline.deduced.emitc.mlir --work %t --name object_fields_deduced",
        ],
        "print_deduced": [
            "--plain %{obj}/object_fields.pipeline.emitc.mlir --deduced %{obj}/object_fields.pipeline.deduced.emitc.mlir --mutated %{obj}/object_fields.pipeline.mutated.emitc.mlir --work %t --name object_fields",
        ],
    },
    "Objects/index-truncation": {
        "compilation_unit": [
            "--module %{obj}/index_truncation.pipeline.emitc.mlir --js %S/index-truncation.js --work %t --name index_truncation",
            "--module %{obj}/index_truncation.pipeline.deduced.emitc.mlir --js %S/index-truncation.js --work %t --name index_truncation_deduced",
        ],
        "compile_clean": [
            "--module %{obj}/index_truncation.pipeline.emitc.mlir --work %t --name index_truncation",
            "--module %{obj}/index_truncation.pipeline.deduced.emitc.mlir --work %t --name index_truncation_deduced",
        ],
        "print_deduced": [
            "--plain %{obj}/index_truncation.pipeline.emitc.mlir --deduced %{obj}/index_truncation.pipeline.deduced.emitc.mlir --mutated %{obj}/index_truncation.pipeline.mutated.emitc.mlir --work %t --name index_truncation",
        ],
    },
    "Objects/receiver": {
        "compilation_unit": [
            "--module %{obj}/receivers.pipeline.emitc.mlir --js %S/receiver.js --work %t --name receivers",
            "--module %{obj}/receivers.pipeline.deduced.emitc.mlir --js %S/receiver.js --work %t --name receivers_deduced",
        ],
        "compile_clean": [
            "--module %{obj}/receivers.pipeline.emitc.mlir --work %t --name receivers",
            "--module %{obj}/receivers.pipeline.deduced.emitc.mlir --work %t --name receivers_deduced",
        ],
        "print_deduced": [
            "--plain %{obj}/receivers.pipeline.emitc.mlir --deduced %{obj}/receivers.pipeline.deduced.emitc.mlir --mutated %{obj}/receivers.pipeline.mutated.emitc.mlir --work %t --name receivers",
        ],
    },
    "Objects/records": {
        "compilation_unit": [
            "--module %{obj}/records.pipeline.emitc.mlir --js %S/records.js --work %t --name records",
            "--module %{obj}/records.pipeline.deduced.emitc.mlir --js %S/records.js --work %t --name records_deduced",
        ],
        "compile_clean": [
            "--module %{obj}/records.pipeline.emitc.mlir --work %t --name records",
            "--module %{obj}/records.pipeline.deduced.emitc.mlir --work %t --name records_deduced",
        ],
        "print_deduced": [
            "--plain %{obj}/records.pipeline.emitc.mlir --deduced %{obj}/records.pipeline.deduced.emitc.mlir --mutated %{obj}/records.pipeline.mutated.emitc.mlir --work %t --name records",
        ],
    },
    "Objects/struct": {
        "compilation_unit": [
            "--module %{obj}/structs.pipeline.emitc.mlir --js %S/struct.js --work %t --name structs",
            "--module %{obj}/structs.pipeline.deduced.emitc.mlir --js %S/struct.js --work %t --name structs_deduced",
        ],
        "compile_clean": [
            "--module %{obj}/structs.pipeline.emitc.mlir --work %t --name structs",
            "--module %{obj}/structs.pipeline.deduced.emitc.mlir --work %t --name structs_deduced",
        ],
        "print_deduced": [
            "--plain %{obj}/structs.pipeline.emitc.mlir --deduced %{obj}/structs.pipeline.deduced.emitc.mlir --mutated %{obj}/structs.pipeline.mutated.emitc.mlir --work %t --name structs",
        ],
    },
    "Objects/values": {
        "compilation_unit": [
            "--module %{obj}/object_values.pipeline.emitc.mlir --js %S/values.js --work %t --name object_values",
            "--module %{obj}/object_values.pipeline.deduced.emitc.mlir --js %S/values.js --work %t --name object_values_deduced",
        ],
        "compile_clean": [
            "--module %{obj}/object_values.pipeline.emitc.mlir --work %t --name object_values",
            "--module %{obj}/object_values.pipeline.deduced.emitc.mlir --work %t --name object_values_deduced",
        ],
        "print_deduced": [
            "--plain %{obj}/object_values.pipeline.emitc.mlir --deduced %{obj}/object_values.pipeline.deduced.emitc.mlir --mutated %{obj}/object_values.pipeline.mutated.emitc.mlir --work %t --name object_values",
        ],
    },
    "Optimization/deforestation": {
        "compilation_unit": [
            "--module %{obj}/deforestation.pipeline.emitc.mlir --js %S/deforestation.js --work %t --name deforestation",
            "--module %{obj}/deforestation.pipeline.deduced.emitc.mlir --js %S/deforestation.js --work %t --name deforestation_deduced",
        ],
        "compile_clean": [
            "--module %{obj}/deforestation.pipeline.emitc.mlir --work %t --name deforestation",
            "--module %{obj}/deforestation.pipeline.deduced.emitc.mlir --work %t --name deforestation_deduced",
        ],
        "print_deduced": [
            "--plain %{obj}/deforestation.pipeline.emitc.mlir --deduced %{obj}/deforestation.pipeline.deduced.emitc.mlir --mutated %{obj}/deforestation.pipeline.mutated.emitc.mlir --work %t --name deforestation",
        ],
    },
    "Optimization/reachability": {
        "compilation_unit": [
            "--module %{obj}/reachability.pipeline.emitc.mlir --js %S/reachability.js --work %t --name reachability",
            "--module %{obj}/reachability.pipeline.deduced.emitc.mlir --js %S/reachability.js --work %t --name reachability_deduced",
        ],
        "compile_clean": [
            "--module %{obj}/reachability.pipeline.emitc.mlir --work %t --name reachability",
            "--module %{obj}/reachability.pipeline.deduced.emitc.mlir --work %t --name reachability_deduced",
        ],
        "print_deduced": [
            "--plain %{obj}/reachability.pipeline.emitc.mlir --deduced %{obj}/reachability.pipeline.deduced.emitc.mlir --mutated %{obj}/reachability.pipeline.mutated.emitc.mlir --work %t --name reachability",
        ],
    },
    "Optimization/specialization": {
        "compilation_unit": [
            "--module %{obj}/specialization.pipeline.emitc.mlir --js %S/specialization.js --work %t --name specialization",
            "--module %{obj}/specialization.pipeline.deduced.emitc.mlir --js %S/specialization.js --work %t --name specialization_deduced",
        ],
        "compile_clean": [
            "--module %{obj}/specialization.pipeline.emitc.mlir --work %t --name specialization",
            "--module %{obj}/specialization.pipeline.deduced.emitc.mlir --work %t --name specialization_deduced",
        ],
        "print_deduced": [
            "--plain %{obj}/specialization.pipeline.emitc.mlir --deduced %{obj}/specialization.pipeline.deduced.emitc.mlir --mutated %{obj}/specialization.pipeline.mutated.emitc.mlir --work %t --name specialization",
        ],
    },
    "Optimization/supercompilation-generalization": {
        "compilation_unit": [
            "--module %{obj}/supercompilation_generalization.pipeline.emitc.mlir --js %S/supercompilation-generalization.js --work %t --name supercompilation_generalization",
            "--module %{obj}/supercompilation_generalization.pipeline.deduced.emitc.mlir --js %S/supercompilation-generalization.js --work %t --name supercompilation_generalization_deduced",
        ],
        "compile_clean": [
            "--module %{obj}/supercompilation_generalization.pipeline.emitc.mlir --work %t --name supercompilation_generalization",
            "--module %{obj}/supercompilation_generalization.pipeline.deduced.emitc.mlir --work %t --name supercompilation_generalization_deduced",
        ],
        "print_deduced": [
            "--plain %{obj}/supercompilation_generalization.pipeline.emitc.mlir --deduced %{obj}/supercompilation_generalization.pipeline.deduced.emitc.mlir --mutated %{obj}/supercompilation_generalization.pipeline.mutated.emitc.mlir --work %t --name supercompilation_generalization",
        ],
    },
    "Optimization/supercompilation": {
        "compilation_unit": [
            "--module %{obj}/supercompilation.pipeline.emitc.mlir --js %S/supercompilation.js --work %t --name supercompilation",
            "--module %{obj}/supercompilation.pipeline.deduced.emitc.mlir --js %S/supercompilation.js --work %t --name supercompilation_deduced",
        ],
        "compile_clean": [
            "--module %{obj}/supercompilation.pipeline.emitc.mlir --work %t --name supercompilation",
            "--module %{obj}/supercompilation.pipeline.deduced.emitc.mlir --work %t --name supercompilation_deduced",
        ],
        "print_deduced": [
            "--plain %{obj}/supercompilation.pipeline.emitc.mlir --deduced %{obj}/supercompilation.pipeline.deduced.emitc.mlir --mutated %{obj}/supercompilation.pipeline.mutated.emitc.mlir --work %t --name supercompilation",
        ],
    },
    "Optimization/symbolic": {
        "compilation_unit": [
            "--module %{obj}/symbolic.pipeline.emitc.mlir --js %S/symbolic.js --work %t --name symbolic",
            "--module %{obj}/symbolic.pipeline.deduced.emitc.mlir --js %S/symbolic.js --work %t --name symbolic_deduced",
        ],
        "compile_clean": [
            "--module %{obj}/symbolic.pipeline.emitc.mlir --work %t --name symbolic",
            "--module %{obj}/symbolic.pipeline.deduced.emitc.mlir --work %t --name symbolic_deduced",
        ],
        "print_deduced": [
            "--plain %{obj}/symbolic.pipeline.emitc.mlir --deduced %{obj}/symbolic.pipeline.deduced.emitc.mlir --mutated %{obj}/symbolic.pipeline.mutated.emitc.mlir --work %t --name symbolic",
        ],
    },
    "Scalars/divergence": {
        "compilation_unit": [
            "--module %{obj}/divergence.pipeline.emitc.mlir --js %S/divergence.js --work %t --name divergence",
            "--module %{obj}/divergence.pipeline.deduced.emitc.mlir --js %S/divergence.js --work %t --name divergence_deduced",
        ],
        "compile_clean": [
            "--module %{obj}/divergence.pipeline.emitc.mlir --work %t --name divergence",
            "--module %{obj}/divergence.pipeline.deduced.emitc.mlir --work %t --name divergence_deduced",
        ],
        "print_deduced": [
            "--plain %{obj}/divergence.pipeline.emitc.mlir --deduced %{obj}/divergence.pipeline.deduced.emitc.mlir --mutated %{obj}/divergence.pipeline.mutated.emitc.mlir --work %t --name divergence",
        ],
    },
    "Scalars/optional-scalars": {
        "compilation_unit": [
            "--module %{obj}/optional_scalars.pipeline.emitc.mlir --js %S/optional-scalars.js --work %t --name optional_scalars",
            "--module %{obj}/optional_scalars.pipeline.deduced.emitc.mlir --js %S/optional-scalars.js --work %t --name optional_scalars_deduced",
        ],
        "compile_clean": [
            "--module %{obj}/optional_scalars.pipeline.emitc.mlir --work %t --name optional_scalars",
            "--module %{obj}/optional_scalars.pipeline.deduced.emitc.mlir --work %t --name optional_scalars_deduced",
        ],
        "print_deduced": [
            "--plain %{obj}/optional_scalars.pipeline.emitc.mlir --deduced %{obj}/optional_scalars.pipeline.deduced.emitc.mlir --mutated %{obj}/optional_scalars.pipeline.mutated.emitc.mlir --work %t --name optional_scalars",
        ],
    },
    "Scalars/scalar-unions": {
        "compilation_unit": [
            "--module %{obj}/scalar_unions.pipeline.emitc.mlir --js %S/scalar-unions.js --work %t --name scalar_unions",
            "--module %{obj}/scalar_unions.pipeline.deduced.emitc.mlir --js %S/scalar-unions.js --work %t --name scalar_unions_deduced",
        ],
        "compile_clean": [
            "--module %{obj}/scalar_unions.pipeline.emitc.mlir --work %t --name scalar_unions",
            "--module %{obj}/scalar_unions.pipeline.deduced.emitc.mlir --work %t --name scalar_unions_deduced",
        ],
        "print_deduced": [
            "--plain %{obj}/scalar_unions.pipeline.emitc.mlir --deduced %{obj}/scalar_unions.pipeline.deduced.emitc.mlir --mutated %{obj}/scalar_unions.pipeline.mutated.emitc.mlir --work %t --name scalar_unions",
        ],
    },
    "Scalars/string": {
        "compilation_unit": [
            "--module %{obj}/strings.pipeline.emitc.mlir --js %S/string.js --work %t --name strings",
            "--module %{obj}/strings.pipeline.deduced.emitc.mlir --js %S/string.js --work %t --name strings_deduced",
        ],
        "compile_clean": [
            "--module %{obj}/strings.pipeline.emitc.mlir --work %t --name strings",
            "--module %{obj}/strings.pipeline.deduced.emitc.mlir --work %t --name strings_deduced",
        ],
        "print_deduced": [
            "--plain %{obj}/strings.pipeline.emitc.mlir --deduced %{obj}/strings.pipeline.deduced.emitc.mlir --mutated %{obj}/strings.pipeline.mutated.emitc.mlir --work %t --name strings",
        ],
    },
    "Optimization/partial/closures": {
        "compilation_unit": [
            "--module %{obj}/partial_closures.pipeline.emitc.mlir --js %S/closures.js --work %t --name partial_closures",
            "--module %{obj}/partial_closures.pipeline.deduced.emitc.mlir --js %S/closures.js --work %t --name partial_closures_deduced",
        ],
        "compile_clean": [
            "--module %{obj}/partial_closures.pipeline.emitc.mlir --work %t --name partial_closures",
            "--module %{obj}/partial_closures.pipeline.deduced.emitc.mlir --work %t --name partial_closures_deduced",
        ],
        "print_deduced": [
            "--plain %{obj}/partial_closures.pipeline.emitc.mlir --deduced %{obj}/partial_closures.pipeline.deduced.emitc.mlir --mutated %{obj}/partial_closures.pipeline.mutated.emitc.mlir --work %t --name partial_closures",
        ],
    },
    "Optimization/partial/evaluation": {
        "compilation_unit": [
            "--module %{obj}/partial_evaluation.pipeline.emitc.mlir --js %S/evaluation.js --work %t --name partial_evaluation",
            "--module %{obj}/partial_evaluation.pipeline.deduced.emitc.mlir --js %S/evaluation.js --work %t --name partial_evaluation_deduced",
        ],
        "compile_clean": [
            "--module %{obj}/partial_evaluation.pipeline.emitc.mlir --work %t --name partial_evaluation",
            "--module %{obj}/partial_evaluation.pipeline.deduced.emitc.mlir --work %t --name partial_evaluation_deduced",
        ],
        "print_deduced": [
            "--plain %{obj}/partial_evaluation.pipeline.emitc.mlir --deduced %{obj}/partial_evaluation.pipeline.deduced.emitc.mlir --mutated %{obj}/partial_evaluation.pipeline.mutated.emitc.mlir --work %t --name partial_evaluation",
        ],
    },
    "Optimization/partial/prefix": {
        "compilation_unit": [
            "--module %{obj}/partial_prefix.pipeline.emitc.mlir --js %S/prefix.js --work %t --name partial_prefix",
            "--module %{obj}/partial_prefix.pipeline.deduced.emitc.mlir --js %S/prefix.js --work %t --name partial_prefix_deduced",
        ],
        "compile_clean": [
            "--module %{obj}/partial_prefix.pipeline.emitc.mlir --work %t --name partial_prefix",
            "--module %{obj}/partial_prefix.pipeline.deduced.emitc.mlir --work %t --name partial_prefix_deduced",
        ],
        "print_deduced": [
            "--plain %{obj}/partial_prefix.pipeline.emitc.mlir --deduced %{obj}/partial_prefix.pipeline.deduced.emitc.mlir --mutated %{obj}/partial_prefix.pipeline.mutated.emitc.mlir --work %t --name partial_prefix",
        ],
    },
    "Objects/array/overwrite-loop": {
        "compilation_unit": [
            "--module %{obj}/array_overwrite_loop.pipeline.emitc.mlir --js %S/overwrite-loop.js --work %t --name array_overwrite_loop",
            "--module %{obj}/array_overwrite_loop.pipeline.deduced.emitc.mlir --js %S/overwrite-loop.js --work %t --name array_overwrite_loop_deduced",
            "--module %{obj}/array_overwrite_loop_unoptimized.pipeline.emitc.mlir --js %S/overwrite-loop.js --work %t --name array_overwrite_loop_unoptimized",
            "--module %{obj}/array_overwrite_loop_unoptimized.pipeline.deduced.emitc.mlir --js %S/overwrite-loop.js --work %t --name array_overwrite_loop_unoptimized_deduced",
        ],
        "compile_clean": [
            "--module %{obj}/array_overwrite_loop.pipeline.emitc.mlir --work %t --name array_overwrite_loop",
            "--module %{obj}/array_overwrite_loop.pipeline.deduced.emitc.mlir --work %t --name array_overwrite_loop_deduced",
            "--module %{obj}/array_overwrite_loop_unoptimized.pipeline.emitc.mlir --work %t --name array_overwrite_loop_unoptimized",
            "--module %{obj}/array_overwrite_loop_unoptimized.pipeline.deduced.emitc.mlir --work %t --name array_overwrite_loop_unoptimized_deduced",
        ],
        "print_deduced": [
            "--plain %{obj}/array_overwrite_loop.pipeline.emitc.mlir --deduced %{obj}/array_overwrite_loop.pipeline.deduced.emitc.mlir --mutated %{obj}/array_overwrite_loop.pipeline.mutated.emitc.mlir --work %t --name array_overwrite_loop",
            "--plain %{obj}/array_overwrite_loop_unoptimized.pipeline.emitc.mlir --deduced %{obj}/array_overwrite_loop_unoptimized.pipeline.deduced.emitc.mlir --mutated %{obj}/array_overwrite_loop_unoptimized.pipeline.mutated.emitc.mlir --work %t --name array_overwrite_loop_unoptimized",
        ],
    },
    "Objects/array/overwrite": {
        "compilation_unit": [
            "--module %{obj}/array_overwrite.pipeline.emitc.mlir --js %S/overwrite.js --work %t --name array_overwrite",
            "--module %{obj}/array_overwrite.pipeline.deduced.emitc.mlir --js %S/overwrite.js --work %t --name array_overwrite_deduced",
            "--module %{obj}/array_overwrite_unoptimized.pipeline.emitc.mlir --js %S/overwrite.js --work %t --name array_overwrite_unoptimized",
            "--module %{obj}/array_overwrite_unoptimized.pipeline.deduced.emitc.mlir --js %S/overwrite.js --work %t --name array_overwrite_unoptimized_deduced",
        ],
        "compile_clean": [
            "--module %{obj}/array_overwrite.pipeline.emitc.mlir --work %t --name array_overwrite",
            "--module %{obj}/array_overwrite.pipeline.deduced.emitc.mlir --work %t --name array_overwrite_deduced",
            "--module %{obj}/array_overwrite_unoptimized.pipeline.emitc.mlir --work %t --name array_overwrite_unoptimized",
            "--module %{obj}/array_overwrite_unoptimized.pipeline.deduced.emitc.mlir --work %t --name array_overwrite_unoptimized_deduced",
        ],
        "print_deduced": [
            "--plain %{obj}/array_overwrite.pipeline.emitc.mlir --deduced %{obj}/array_overwrite.pipeline.deduced.emitc.mlir --mutated %{obj}/array_overwrite.pipeline.mutated.emitc.mlir --work %t --name array_overwrite",
            "--plain %{obj}/array_overwrite_unoptimized.pipeline.emitc.mlir --deduced %{obj}/array_overwrite_unoptimized.pipeline.deduced.emitc.mlir --mutated %{obj}/array_overwrite_unoptimized.pipeline.mutated.emitc.mlir --work %t --name array_overwrite_unoptimized",
        ],
    },
    "Objects/array/read-overwrite": {
        "compilation_unit": [
            "--module %{obj}/array_read_overwrite.pipeline.emitc.mlir --js %S/read-overwrite.js --work %t --name array_read_overwrite",
            "--module %{obj}/array_read_overwrite.pipeline.deduced.emitc.mlir --js %S/read-overwrite.js --work %t --name array_read_overwrite_deduced",
            "--module %{obj}/array_read_overwrite_unoptimized.pipeline.emitc.mlir --js %S/read-overwrite.js --work %t --name array_read_overwrite_unoptimized",
            "--module %{obj}/array_read_overwrite_unoptimized.pipeline.deduced.emitc.mlir --js %S/read-overwrite.js --work %t --name array_read_overwrite_unoptimized_deduced",
        ],
        "compile_clean": [
            "--module %{obj}/array_read_overwrite.pipeline.emitc.mlir --work %t --name array_read_overwrite",
            "--module %{obj}/array_read_overwrite.pipeline.deduced.emitc.mlir --work %t --name array_read_overwrite_deduced",
            "--module %{obj}/array_read_overwrite_unoptimized.pipeline.emitc.mlir --work %t --name array_read_overwrite_unoptimized",
            "--module %{obj}/array_read_overwrite_unoptimized.pipeline.deduced.emitc.mlir --work %t --name array_read_overwrite_unoptimized_deduced",
        ],
        "print_deduced": [
            "--plain %{obj}/array_read_overwrite.pipeline.emitc.mlir --deduced %{obj}/array_read_overwrite.pipeline.deduced.emitc.mlir --mutated %{obj}/array_read_overwrite.pipeline.mutated.emitc.mlir --work %t --name array_read_overwrite",
            "--plain %{obj}/array_read_overwrite_unoptimized.pipeline.emitc.mlir --deduced %{obj}/array_read_overwrite_unoptimized.pipeline.deduced.emitc.mlir --mutated %{obj}/array_read_overwrite_unoptimized.pipeline.mutated.emitc.mlir --work %t --name array_read_overwrite_unoptimized",
        ],
    },
    "Objects/array/shrink": {
        "compilation_unit": [
            "--module %{obj}/array_shrink.pipeline.emitc.mlir --js %S/shrink.js --work %t --name array_shrink",
            "--module %{obj}/array_shrink.pipeline.deduced.emitc.mlir --js %S/shrink.js --work %t --name array_shrink_deduced",
            "--module %{obj}/array_shrink_unoptimized.pipeline.emitc.mlir --js %S/shrink.js --work %t --name array_shrink_unoptimized",
            "--module %{obj}/array_shrink_unoptimized.pipeline.deduced.emitc.mlir --js %S/shrink.js --work %t --name array_shrink_unoptimized_deduced",
        ],
        "compile_clean": [
            "--module %{obj}/array_shrink.pipeline.emitc.mlir --work %t --name array_shrink",
            "--module %{obj}/array_shrink.pipeline.deduced.emitc.mlir --work %t --name array_shrink_deduced",
            "--module %{obj}/array_shrink_unoptimized.pipeline.emitc.mlir --work %t --name array_shrink_unoptimized",
            "--module %{obj}/array_shrink_unoptimized.pipeline.deduced.emitc.mlir --work %t --name array_shrink_unoptimized_deduced",
        ],
        "print_deduced": [
            "--plain %{obj}/array_shrink.pipeline.emitc.mlir --deduced %{obj}/array_shrink.pipeline.deduced.emitc.mlir --mutated %{obj}/array_shrink.pipeline.mutated.emitc.mlir --work %t --name array_shrink",
            "--plain %{obj}/array_shrink_unoptimized.pipeline.emitc.mlir --deduced %{obj}/array_shrink_unoptimized.pipeline.deduced.emitc.mlir --mutated %{obj}/array_shrink_unoptimized.pipeline.mutated.emitc.mlir --work %t --name array_shrink_unoptimized",
        ],
    },
}


def cases(gate):
    return [case for case, gates in GATES.items() if gate in gates]


@pytest.mark.parametrize("case", cases("compilation_unit"))
def test_compilation_unit(lit, case):
    run(lit, case, "compilation_unit")


@pytest.mark.parametrize("case", cases("compile_clean"))
def test_compile_clean(lit, case):
    run(lit, case, "compile_clean")


@pytest.mark.parametrize("case", cases("print_deduced"))
def test_print_deduced(lit, case):
    run(lit, case, "print_deduced")
