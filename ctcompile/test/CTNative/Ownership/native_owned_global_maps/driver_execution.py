"""Execute checked published Map methods and live primitive results without the VM."""

from .driver_common import *
from .driver_object_maps import *
from .driver_globals import *
from .driver_observations import *
from .driver_fields import *
from .driver_map_sizes import *
from .driver_object_keys import *
from .driver_nested_maps import check_nested_maps
from .driver_recorder import check_recorders
from .driver_umd import check_umd

from CTNative.harness import find_compilers


def check_positive(args, node, reference, compilers, nm, name, spec):
    """One native program: its source chain and call boundaries, the Node and
    interpreter observations, the owning proof, the standalone build and the
    emitted C++, returning what the forgery controls read back."""
    source, binding, value = spec
    js, ir, count = boundary.prepare(args, name, source)
    functions = (
        object_argument_cases()[name]["functions"]
        if name in object_argument_sources()
        else (
            LEAF_OBJECT_FUNCTIONS[name]
            if name in LEAF_OBJECT_FUNCTIONS
            else (
                5
                if name in LEAF_READBACK_CALLS
                or name in leaf_absence_sources()
                or name in leaf_clear_sources()
                or name in numeric_entry_sources()
                or name in scalar_global_sources()
                or name in constant_global_sources()
                or name in string_field_sources()
                or name in zero_size_sources()
                or name in one_size_sources()
                or name in delete_size_sources()
                or name in join_size_sources()
                or name in mutation_size_sources()
                else (
                    RESULT_SIGNATURES[name][2]
                    if name in RESULT_SIGNATURES
                    else 6 if name == "shared_three" else 5 if name.startswith("shared") else 4
                )
            )
        )
    )
    if count != functions:
        raise RuntimeError(f"{name}: lost the {functions}-function source chain")
    if name == "boolean_result" and (
        len(source_calls((args.work / f"{name}.raw.mlir").read_text())) != 5
        or len(source_calls(ir.read_text())) != 5
    ):
        raise RuntimeError("boolean_result: changed the historical five-call source")
    if name == "saved_read_write" and len(source_calls(ir.read_text())) != 12:
        raise RuntimeError("saved_read_write: changed the exact 12-call boundary")
    if name == "saved_join" and len(source_calls(ir.read_text())) != 16:
        raise RuntimeError("saved_join: changed the exact 16-call boundary")
    if (
        name in {"guarded_saved_read", "shortcircuit_same_tag", "nullable_normalized"}
        and len(source_calls(ir.read_text())) != 18
    ):
        raise RuntimeError(f"{name}: changed the exact 18-call boundary")
    if name == "nullable_homogeneous_key" and len(source_calls(ir.read_text())) != 11:
        raise RuntimeError("nullable_homogeneous_key: changed the exact 11-call control")
    if name in NULLABLE_KEY_CALLS and len(source_calls(ir.read_text())) != NULLABLE_KEY_CALLS[name]:
        raise RuntimeError(f"{name}: changed the exact {NULLABLE_KEY_CALLS[name]}-call boundary")
    if (
        name in NULLABLE_PAYLOAD_CALLS
        and len(source_calls(ir.read_text())) != NULLABLE_PAYLOAD_CALLS[name]
    ):
        raise RuntimeError(
            f"{name}: changed the exact {NULLABLE_PAYLOAD_CALLS[name]}-call boundary"
        )
    if (
        name in NULLABLE_HOST_RESULT_CALLS
        and len(source_calls(ir.read_text())) != NULLABLE_HOST_RESULT_CALLS[name]
    ):
        raise RuntimeError(
            f"{name}: changed the exact {NULLABLE_HOST_RESULT_CALLS[name]}-call boundary"
        )
    if (
        name in NULLABLE_NESTED_RESULT_CALLS
        and len(source_calls(ir.read_text())) != NULLABLE_NESTED_RESULT_CALLS[name]
    ):
        raise RuntimeError(
            f"{name}: changed the exact {NULLABLE_NESTED_RESULT_CALLS[name]}-call boundary"
        )
    if name in LEAF_OBJECT_CALLS and len(source_calls(ir.read_text())) != LEAF_OBJECT_CALLS[name]:
        raise RuntimeError(
            f"{name}: changed the exact {LEAF_OBJECT_CALLS[name]}-call leaf boundary"
        )
    if (
        name in LEAF_READBACK_CALLS
        and len(source_calls(ir.read_text())) != LEAF_READBACK_CALLS[name]
    ):
        raise RuntimeError(
            f"{name}: changed the exact {LEAF_READBACK_CALLS[name]}-call readback boundary"
        )
    check_leaf_absence_census(args, ir, name)
    check_string_field_census(args, ir, name)
    check_zero_size_census(args, ir, name)
    check_one_size_census(args, ir, name)
    check_delete_size_census(args, ir, name)
    check_join_size_census(args, ir, name)
    check_mutation_size_census(args, ir, name)
    check_object_argument_census(args, ir, name)
    if name in primitive_absence_sources() and (
        len(source_calls((args.work / f"{name}.raw.mlir").read_text())) != 10
        or len(source_calls(ir.read_text())) != 10
    ):
        raise RuntimeError(f"{name}: changed the exact ten-call primitive absence source")
    if name == "already_resolved":
        ir = resolve_getter(args, ir)
    if name.startswith("legacy_"):
        ir = methods.legacy_marker(args, ir, name)
    expected = f"trace={str(value).lower() if isinstance(value, bool) else value}\n"
    if isinstance(value, StringValue):
        expected = scalar_global_output(name, value)
    node_command = [node, "-e", numeric_node_observer(value), str(js)]
    if name in constant_global_cases():
        names = json.dumps(sorted(["trace", *constant_global_cases()[name]["saved"]]))
        node_command = [node, "-e", CONSTANT_GLOBAL_NODE, str(js), names]
        expected = numeric_reference_output(name, value)
    reference_result = host.run([str(reference), str(js)])
    if host.run(node_command).stdout != expected or normalized_scalar_output(
        reference_result.stdout
    ) != numeric_reference_output(name, value):
        raise RuntimeError(f"{name}: Node/interpreter source observation mismatch")
    if name == "boolean_result" and (
        "(0 number, 1 boolean, 0 string, 0 null, 0 undefined)" not in reference_result.stderr
    ):
        raise RuntimeError("boolean_result: reference lost its independently observed Boolean tag")
    config = contract(args, ir, name, binding)
    original, manifest = ir.read_text(), config.read_text()
    output = owned.lower(args, ir, name, config)
    text = methods.census(output, functions, name, admitted=functions)
    if "ctnative.host_owner_proved = true" not in text:
        raise RuntimeError(f"{name}: lost live owning proof")
    if ir.read_text() != original or config.read_text() != manifest:
        raise RuntimeError(f"{name}: changed supplied source or manifest")
    if name == "boolean_result" or name in {
        **saved_read_sources(),
        **saved_join_sources(),
        **guarded_saved_sources(),
        **shortcircuit_sources(),
        **nullable_result_sources(),
        **nullable_key_sources(),
        **nullable_payload_sources(),
        **nullable_host_result_sources(),
        **nullable_nested_result_sources(),
        **leaf_object_sources(),
        **leaf_readback_sources(),
        **leaf_absence_sources(),
        **primitive_absence_sources(),
        **leaf_clear_sources(),
        **numeric_entry_sources(),
        **scalar_global_sources(),
        **constant_global_sources(),
        **string_field_sources(),
        **join_size_sources(),
        **mutation_size_sources(),
        **object_argument_sources(),
    }:
        disabled = owned.lower(args, ir, name + "-disabled", config, options="optimize=false")
        if disabled.read_text() != output.read_text():
            raise RuntimeError(f"{name}: saved scalar proof depends on optimization policy")
    standalone(args, output, name, value, compilers, nm)
    check_scalar_global_emission(args, ir, name)
    return ir, config, output


def check_object_keys(args, node, reference, compilers, nm):
    """Identity-only object keys: the fresh empty-object argument programs,
    their typed Node/interpreter observations, and the controls that forge
    their evidence (driver_object_keys.py)."""
    positives = object_argument_sources()
    check_object_argument_observations(args, node, reference)
    saved = {
        name: check_positive(args, node, reference, compilers, nm, name, spec)
        for name, spec in positives.items()
    }
    check_object_argument_controls(args, saved)
    print(f"object keys: {len(positives)} native programs and their controls")


def setup():
    """The command line every group shares, and the two oracles plus the two
    compilers and the nm control they all need. Each group is its own lit
    test (global-maps*.test), so a failure in one no longer hides the rest
    and lit -j parallelises across them; within a group the programs run one
    after another, which is what lit's own pool is for."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--translate", required=True)
    parser.add_argument("--opt", required=True)
    parser.add_argument("--node", required=True)
    parser.add_argument("--reference", required=True)
    parser.add_argument("--work", type=Path, required=True)
    args = parser.parse_args()
    args.work.mkdir(parents=True, exist_ok=True)
    compilers = find_compilers()
    nm = shutil.which("nm") or shutil.which("llvm-nm")
    if (
        not all(compilers)
        or not nm
        or not owned.VM.search(host.run([nm, "-C", str(args.reference)]).stdout)
    ):
        raise RuntimeError("need both host compilers and a working VM-symbol control")
    return args, args.node, args.reference, compilers, nm


def positive_cases():
    mutated = SOURCE.replace("return state.size;", "state.set('x', 1); return state.size;")
    growing = SOURCE.replace("return state.size;", "state.set(state.size, 1); return state.size;")
    positives = {
        "ordinary": (SOURCE, "host", 0),
        "already_resolved": (SOURCE, "host", 0),
        "legacy_store_marker": (SOURCE, "host", 0),
        "legacy_field_marker": (SOURCE, "host", 0),
        "repeated": (SOURCE + "\ntrace = host.slot.get();", "host", 0),
        "ordinary_window": (SOURCE.replace("host", "window"), "window", 0),
        "mutate_map": (mutated, "host", 1),
        "boolean_result": (
            SOURCE.replace("return state.size;", "state.set('x', 1); return state.has('x');"),
            "host",
            True,
        ),
        "growing": (growing, "host", 1),
        "growing_repeated": (growing + "\ntrace = host.slot.get();" * 2, "host", 3),
        "primitive_actions": (
            SOURCE.replace(
                "return state.size;",
                "state.set('x', 1); state.has('x'); state.get('x'); "
                "state.delete('missing'); return state.size;",
            ),
            "host",
            1,
        ),
        "replace_delete": (
            SOURCE.replace(
                "return state.size;",
                "state.set('x', 1); state.set('x', 2); state.delete('x'); return state.size;",
            ),
            "host",
            0,
        ),
        "fluent": (
            SOURCE.replace(
                "return state.size;", "state.set('x', 1).set('y', 2); return state.size;"
            ),
            "host",
            2,
        ),
        "has_result": (
            SOURCE.replace(
                "return state.size;",
                "state.set(false, 1); state.set(state.has(false), 2); return state.size;",
            ),
            "host",
            2,
        ),
        "delete_result": (
            SOURCE.replace(
                "return state.size;",
                "state.set(false, 0); state.set(true, 1); state.set(state.delete(true), 2); "
                "return state.size;",
            ),
            "host",
            2,
        ),
        "seeded_local_key": (
            SOURCE.replace(
                "return state.size;",
                "state.set(1, 2); state.set(1, 3); state.set(state.get(1), 4); "
                "state.delete(3); return state.size;",
            ),
            "host",
            1,
        ),
        "shared": (SHARED, "host", 1),
        "shared_growing": (
            SHARED.replace("state.set('x', 1)", "state.set(state.size, 1)"),
            "host",
            1,
        ),
        "shared_repeated": (
            SHARED.replace("state.set('x', 1)", "state.set(state.size, 1)")
            + "\nhost.slot.set(); trace = host.slot.get();",
            "host",
            2,
        ),
        "shared_early_read": (
            SHARED.replace("host.slot.set();", "host.slot.get(); host.slot.set();"),
            "host",
            1,
        ),
        "shared_three": (
            SHARED.replace(
                "get() { return state.size; },",
                "size() { return state.size; }, get() { return state.size; },",
            )
            + "\ntrace = host.slot.size();",
            "host",
            1,
        ),
        **parameter_sources(),
        **result_sources(),
        **seeded_result_sources(),
        **key_fact_sources(),
        **joined_result_sources(),
        **size_result_sources(),
        **payload_result_sources(),
        **mixed_result_sources(),
        **saved_read_sources(),
        **saved_join_sources(),
        **guarded_saved_sources(),
        **shortcircuit_sources(),
        **nullable_result_sources(),
        **nullable_key_sources(),
        **nullable_payload_sources(),
        **nullable_host_result_sources(),
        **nullable_nested_result_sources(),
        **leaf_object_sources(),
        **leaf_absence_sources(),
        **primitive_absence_sources(),
        **leaf_clear_sources(),
        **{
            name: row
            for name, row in leaf_readback_sources().items()
            if name not in LEAF_READBACK_UNOWNED
        },
        **numeric_entry_sources(),
        **scalar_global_sources(),
        **constant_global_sources(),
        **string_field_sources(),
        **zero_size_sources(),
        **one_size_sources(),
        **delete_size_sources(),
        **join_size_sources(),
        **mutation_size_sources(),
        # Keep the original refusal source byte-for-byte. Its method-local
        # empty payload now has the same independently proved leaf owner.
        "object_payload": (refusal_sources()["object_payload"], "host", 1),
    }
    return positives


def report(positives, shared_refusals, rollback):
    print(
        f"native captured Map ownership: {len(positives)} complete programs (4/4, 5/5, 6/6); "
        "Node/interpreter/GCC/Clang explicit+deduced and Map/table/callable lifetime pass; "
        f"{len(refusal_sources()) - 1} source refusals and contract/rerun/budget controls pass; "
        f"one exact native Undefined observation and {len(shared_refusals)} shared-method refusals; "
        "the historical five-call Boolean result retains its ctnative::js_boolean_t() callable and exact Boolean output; "
        f"{len(parameter_refusals())} argument refusals preserve current call operands; "
        "typed parameterized setters 5/5 with changing source and saved-callable keys; "
        f"{len(result_sources())} live result programs preserve call order and operands; "
        f"{len(result_refusals())} result-proof refusals and missing-return carrier refusal; "
        f"{len(seeded_result_sources())} seeded result programs and growing lifetime pass; "
        f"{len(key_fact_sources())} per-key result programs; "
        f"{len(joined_result_sources())} type-joined result programs; "
        f"{len(size_result_sources())} bounded-size programs and "
        f"{len(size_result_refusals())} size-key refusals with discriminating observations; "
        f"{len(payload_result_sources())} Bool/String payload programs and saved-string lifetime; "
        f"{len(payload_result_refusals()) - 1} missing-payload carrier refusal distinguishes false; "
        f"{len(mixed_result_sources())} closed mixed Map programs and saved-string lifetime; "
        f"{len(mixed_result_refusals())} mixed deleted-result refusals preserve calls; "
        f"{len(saved_read_sources())} saved-read/write programs in both optimization modes; "
        f"{len(saved_read_refusals())} saved-read missing/deleted refusals and saved-string lifetime; "
        f"{len(saved_join_sources())} conditional saved-value programs in both modes; "
        f"{len(saved_join_refusals())} conditional missing/deleted/mixed-tag refusals; "
        "both future getter flags and independent owning strings survive final Map release; "
        f"{len(guarded_saved_sources())} live has-guarded scalar programs in both modes; "
        f"{len(guarded_saved_refusals())} absent/stale/wrong-guard/payload refusals; "
        "guarded future reads own both selected Strings after final Map release; "
        f"{len(shortcircuit_sources())} short-circuit scalar programs in both modes; "
        f"{len(shortcircuit_refusals())} short-circuit guard/effect/tag refusals; "
        "the original nullable short-circuit result retains complete host ownership but refuses "
        "its optional Bool/String intermediate with prepared calls intact; "
        "present empty/false/zero select fallback, future Strings survive final Map release; "
        f"{len(nullable_result_sources())} nullable result programs preserve String/null/undefined; "
        "future nullable getter and owning strings survive reentry and final Map release; "
        f"{len(nullable_result_refusals())} unknown/mixed/object/effect nullable refusals; "
        f"{len(nullable_key_sources())} nullable Map-key programs preserve all four key identities; "
        f"{len(nullable_key_refusals())} mixed snapshot refusals and fresh/stale key forgeries; "
        "saved owning keys survive caller mutation, deletion, reentry and final Map release; "
        f"{len(nullable_payload_sources())} nullable payload write/readback programs and "
        f"{len(nullable_payload_refusals())} missing/object/snapshot refusals; "
        "stored String/null/undefined/empty tags and saved payload ownership survive "
        "overwrite/delete, caller mutation, reentry and final Map release; "
        "deleted nullable reads return Undefined independently of their former payload tag; "
        "mixed nullable reads retain the exact 14/19-call witnesses and their finite payloads; "
        f"{len(mixed_nullable_payload_refusals())} mixed missing/deleted/aliasing-result refusals "
        "retain host ownership and prepared calls under fresh/stale scalar/nullable read forgeries; "
        "restoring each exact live read restores 6/6 native in both modes; "
        f"{len(nullable_host_result_sources())} acyclic nullable host-result programs retain "
        "the exact 15-call chain, live conditional writes and owning saved results; "
        f"{len(nullable_host_result_refusals())} independent unknown/missing/deleted/aliasing "
        "host-result refusals and exact repairs pass both modes and fresh/stale proof controls; "
        f"{len(nullable_nested_result_sources())} same-method result programs retain the exact "
        "15-call nested chain, later nullable/String actuals and saved owning nested results; "
        "the unchanged historical foreign-empty-Map source now preserves Undefined results "
        "through nested/future calls and owning lifetimes; "
        f"{len(nullable_nested_result_refusals()) - 2} unknown/unseeded/later-actual "
        "host refusals and exact repairs pass both modes and fresh/stale proof controls; "
        "the historical leaf-writing sibling retains complete ownership and a separate "
        "Object/String carrier refusal with nested prepared operands intact; "
        f"{len(leaf_object_sources())} method-local leaf programs and the historical object payload "
        "preserve runtime allocation, Map writes, fixed scalar fields and numeric public signatures; "
        "future distinct objects and saved size/set/erase callables pass overwrite/deletion, reentry "
        "and final Map/object lifetime checks; "
        f"{len(leaf_object_refusals()) - 4 - len(STRING_FIELD_PROMOTED)} object graph/field/result/read ownership refusals "
        "restore exact gated sources and reject fresh/stale forged leaf reports; "
        "the historical later object key retains complete ownership, all eight calls and a separate "
        "mixed String/object key carrier refusal in both modes; "
        f"{len(leaf_readback_sources()) - len(LEAF_READBACK_UNOWNED)} local leaf readback programs "
        "preserve definite object origins, strict identities and fixed scalar field reads; "
        "saved aliases observe later field writes across replacement/deletion and saved numeric "
        "callables pass future-argument, reentry and final-owner sanitizer lifetime checks; "
        f"{len(leaf_readback_refusals().keys() - LEAF_ABSENCE_PROMOTED_REFUSALS - NUMERIC_ENTRY_PROMOTED - HISTORICAL_STRING_FIELD_CARRIERS)} "
        "unknown/missing/export/field refusals restore exact sources; "
        f"{len(LEAF_FIELD_RESULTS)} exact raw field results remove only independently proved absence; "
        "saved raw numeric results and callable lifetimes pass future-argument and field mutations; "
        f"{len(LEAF_COMPARISON_REPAIRS)} exact comparison-only fresh allocations retain distinct "
        "identities, field writes and saved-callable lifetimes with their exact saved-object repairs; "
        f"{len(leaf_absence_sources())} absence and historical delete programs preserve exact "
        "raw/prepared calls, definite Undefined and saved-object repairs; "
        "nonidentical two-arm joins survive source preparation and both future flags; "
        f"{len(leaf_absence_refusals()) - len(LEAF_CLEAR_PROMOTED)} possible-alias and conditional "
        "absence refusals reject fresh/stale forgeries and restore exact admitted sources; "
        "saved Undefined across reseed and branch deletion pass final Map/leaf lifetime checks; "
        f"{len(leaf_clear_sources())} exact clear programs retain fresh arbitrary-key absence, "
        "saved object/Undefined reads, aliases, reseeding and surviving nonidentical branches; "
        f"{len(leaf_clear_refusals())} possible-alias, one-arm and evaluated-argument clear refusals "
        "retain every source operation under fresh/stale reports and exact admitted repairs; "
        "three clear lifetime families retain saved callables over 128 future calls, release "
        "both Maps after reentry and preserve an observed leaf until its final owner releases; "
        f"{len(numeric_entry_sources())} numeric entry programs retain evaluated arithmetic and call order; "
        f"{len(numeric_entry_refusals())} non-Number/future-input refusals retain their exact repairs; "
        f"{len(NUMERIC_ENTRY_SAVED_GLOBALS)} historical saved-global sources and "
        f"{len(scalar_global_sources())} new scalar programs retain live global stores, loads and arithmetic; "
        f"{len(scalar_global_refusals())} source-order/write/future-family refusals and "
        f"{len(SCALAR_GLOBAL_CARRIERS)} scalar Map identity refusals retain exact repairs; "
        f"{len(SCALAR_GLOBAL_INITIALIZED)} original alias/direct observations retain definite stored-value types; "
        f"{len(constant_global_cases())} constant-global probes/candidate edits preserve exact scalar observations; "
        f"{len(CONSTANT_GLOBAL_UNOWNED)} unowned/{len(CONSTANT_GLOBAL_CARRIERS)} complete-owner cases (including native Undefined output), "
        "constant-only alias/arithmetic edges and mixed saved-Map lifetimes pass; "
        "Boolean aliases/results retain true/false output with independent Number/Boolean tags; "
        "14 wrong-tag/null/missing-store observation mutations reach the exact termination check; "
        f"{len(zero_size_sources())} exact zero-size programs preserve all thirteen historical sources, "
        "live reads/calls, SameValueZero and saved facts through growth; "
        f"{len(zero_size_cases()) - len(zero_size_sources())} unproved size/branch refusals keep calls "
        "under stale/fresh forgeries and exact repairs; one saved-zero lifetime runs 128 future "
        "calls/both flags through owner/table release, reentry and final Map/leaf destruction; "
        f"{len(one_size_sources())} exact finite-size programs preserve all eight continuation sources; "
        "saved one/two, aliases, repeated keys and both structural arms keep runtime reads/calls; "
        f"{len(one_size_cases()) - len(one_size_sources())} cardinality cases (including native optional-field output) retain "
        "fresh/stale forgeries, reruns and exact admitted repairs; a saved-one lifetime runs "
        "128 future calls/both flags, owner/table release, reentry and final Map/leaf destruction; "
        f"{len(delete_size_sources())} delete-size programs preserve all ten continuation sources; "
        "current object fields, aliases, structural arms and immutable sizes retain runtime operations; "
        f"{len(delete_size_cases()) - len(delete_size_sources())} independent deletion refusals retain "
        "fresh/stale forgeries and exact repairs; a saved deletion lifetime runs 128 future calls, "
        "both flags, owner/table release, reentry and final Map/leaf destruction; "
        f"{len(join_size_sources())} equal-cardinality join programs preserve twelve continuation sources; "
        "disjoint deleting/writing arms retain real saved-size and object-field reads; "
        f"{len(join_size_cases()) - len(join_size_sources())} unequal-size, missing-common-key and "
        "post-join mutation refusals retain exact repairs and fresh/stale evidence checks; "
        "one saved join lifetime runs 128 future calls/both flags through final Map/leaf release; "
        f"{len(mutation_size_sources())} known after-join mutation programs preserve twelve continuation sources; "
        "definite insertion, overwrite and deletion keep real size/Map/field operations; "
        f"{len(mutation_size_cases()) - len(mutation_size_sources())} uncertain mutation refusals retain "
        "exact repairs, fresh/stale forgeries and future key/flag observations; "
        "one saved-two mutation lifetime runs 128 future calls through final Map/leaf release; "
        f"{len(object_argument_sources())} fresh empty-object argument programs preserve live Map calls; "
        f"{len(object_argument_cases()) - len(object_argument_sources())} argument/effect/carrier refusals remain; "
        "saved object-key getters distinguish same/alias/distinct keys over 128 future rounds, "
        "release borrowed arguments; saved siblings retain keys across caller release, overwrite, delete, "
        "clear, reentry and final Map/key release; "
        f"{len(string_field_sources())} owning String-field programs preserve exact tags, bytes and live accesses; "
        "six field refusal/repair families and six emitted field-tag controls remain independent; "
        "two historical String-field sources retain complete ownership and exact equality/mixed-Map refusals; "
        "saved String callables and snapshots survive 128 future calls, both flags and final Map/leaf release; "
        f"{len(NUMERIC_ENTRY_LIFETIMES)} numeric lifetime families retain 128 future results across both branches, reentry, "
        "final Map release and independent leaf release; "
        f"{len(leaf_field_result_refusals())} complete-schema field results "
        "execute exact native output with complete ownership and independent schema proofs; "
        f"{len(PRIMITIVE_ABSENCE_CARRIERS)} exact absent-result carrier refusals preserve complete owners, "
        "concrete diagnostics and prepared producer/consumer/capture operands; "
        "the unchanged ten-call empty-String deletion source and exact repair preserve nullable "
        "key signatures and future Undefined/empty/distinct-key observations; "
        f"{len(seeded_result_refusals().keys() - PRIMITIVE_ABSENCE_CARRIERS)} seeded proof and "
        f"{len(seeded_carrier_refusals())} seeded carrier refusals; "
        f"{len(rollback)} speculative rollback cutoffs"
    )
