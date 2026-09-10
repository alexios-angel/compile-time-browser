"""The checks: emitted-C++ censuses, the lifetime harnesses, standalone execution
and the budget bisection.

Split out of native-owned-global-maps.py on 2026-09-08, verbatim.
"""

from .harness_scalar_maps import *
from .harness_objects import *
from .harness_entries import *
from .harness_fields import *

def standalone(args, output, name, value, compilers, nm):
    deduced = args.work / f"{name}.deduced.mlir"
    host.run([args.opt, str(output), "--ctnative-print-deduced", "-o", str(deduced)])
    for mode, ir in (("explicit", output), ("deduced", deduced)):
        cpp = host.run([args.translate, "--mlir-to-cpp", str(ir)]).stdout
        object_argument = name.startswith('object_argument_')
        size_signature = ("std::function<js_num(ctnative::nullable_string)>"
                          if name in nullable_host_result_sources()
                          else "std::function<bool()>" if name == "boolean_result"
                          else "std::function<js_num()>")
        map_action = "ctnative::map_has(" if name == "boolean_result" or object_argument else "ctnative::map_size("
        if (owned.VM.search(cpp) or "std::shared_ptr<ctn_slot>" not in cpp
                or (not object_argument and size_signature not in cpp) or map_action not in cpp
                or not re.search(r"std::shared_ptr<ctnative::method_\w+>\s+slot\s*;", cpp)):
            raise RuntimeError(f"{name}/{mode}: missing standalone Map/table/callable owners\n{cpp}")
        if object_argument:
            check_object_argument_calls(cpp, name, mode)
        if name == "boolean_result":
            entry = re.search(r"\bmain\(\)\s*\{(.*?)^\}", cpp, re.M | re.S)
            getter = re.search(r"\bfn_3\([^\n]*\)\s*\{(.*?)^\}", cpp, re.M | re.S)
            if (not entry or not getter or entry[1].count("ctnative::invoke_callable(") != 1
                    or entry[1].count("ctnative::global_boolean(") != 1
                    or "ctnative::global_number(" in entry[1]
                    or getter[1].count("ctnative::map_set(") != 1
                    or getter[1].count("ctnative::map_has(") != 1):
                raise RuntimeError(f"{name}/{mode}: lost the live Boolean result or its exact observation tag")
        if name in parameter_sources():
            params = {"shared_parameter_number": "js_num", "shared_parameter_bool": "bool",
                      "shared_two_parameters": "std::string, js_num"}.get(name, "std::string")
            if f"std::function<js_num({params})>" not in cpp:
                raise RuntimeError(f"{name}/{mode}: missing typed setter arguments\n{cpp}")
        if name in leaf_object_sources():
            check_leaf_object_calls(cpp, name, mode)
        if name in string_field_sources():
            check_string_field_calls(cpp, name, mode)
        if name in zero_size_sources():
            check_zero_size_calls(cpp, name, mode)
        if name in one_size_sources():
            check_one_size_calls(cpp, name, mode)
        if name in delete_size_sources():
            check_exact_size_calls(cpp, name, mode, delete_size_cases()[name])
        if name in join_size_sources():
            check_exact_size_calls(cpp, name, mode, join_size_cases()[name])
        if name in mutation_size_sources():
            check_exact_size_calls(cpp, name, mode, mutation_size_cases()[name])
        if name in leaf_readback_sources():
            check_leaf_readback_calls(cpp, name, mode)
        if name in leaf_absence_sources() or name in leaf_clear_sources():
            check_leaf_absence_calls(cpp, name, mode)
        if name in numeric_entry_sources() or name in scalar_global_sources() or name in constant_global_sources():
            check_numeric_entry_calls(cpp, name, mode)
        if name in RESULT_SIGNATURES:
            result, params, _ = RESULT_SIGNATURES[name]
            getter_params = "js_num" if name in {
                "result_formal", "result_seeded_formal", "seeded_dynamic_formal"} else ""
            if name in {**saved_join_sources(), **guarded_saved_sources(), **shortcircuit_sources(),
                        **nullable_result_sources(), **nullable_key_sources(),
                        **nullable_payload_sources(), **nullable_host_result_sources(), **nullable_nested_result_sources()}:
                getter_params = "bool"
            if name == "nullable_threeway":
                getter_params = "bool, bool"
            setter_result = "ctnative::nullable_string" if name in NULLABLE_PAYLOAD_READBACKS else "js_num"
            if (f"std::function<{result}({getter_params})>" not in cpp
                    or f"std::function<{setter_result}({params})>" not in cpp):
                raise RuntimeError(f"{name}/{mode}: missing typed producer/consumer signatures\n{cpp}")
            check_result_calls(cpp, name, mode)
        if name in payload_result_sources():
            payload = "std::string" if "string" in name else "bool"
            if f"std::shared_ptr<ctnative::map_storage<{payload}, {payload}>>" not in cpp:
                raise RuntimeError(f"{name}/{mode}: missing homogeneous owning Map carrier\n{cpp}")
        if name in primitive_absence_sources() and (
                "std::shared_ptr<ctnative::map_storage<ctnative::nullable_string, std::string>>" not in cpp):
            raise RuntimeError(f"{name}/{mode}: lost the independent nullable-key/String-payload carrier")
        if name in MIXED_RESULT_TYPES:
            _, alternative = MIXED_RESULT_TYPES[name]
            variant = f"std::variant<bool, {alternative}>"
            key = {"result_seeded_mixed_contents": "double", "result_seeded_join_reseed": "double",
                   "result_seeded_bool_string_contents": "bool"}.get(name, variant)
            spellings = {key, key.replace("double", "js_num")}
            if not any(f"std::shared_ptr<ctnative::map_storage<{k}, {v}>>" in cpp
                       for k in spellings for v in {variant, variant.replace("double", "js_num")}):
                raise RuntimeError(f"{name}/{mode}: missing exact finite key/payload carrier\n{cpp}")
        source = args.work / f"{name}.{mode}.cpp"
        source.write_text(cpp)
        if name == "leaf_object_string_field":
            source = args.work / f"{name}.{mode}.identity.cpp"
            source.write_text(string_field_identity_cpp(cpp))
        if name == "field_string_lifetime":
            source = args.work / f"{name}.{mode}.identity.cpp"
            source.write_text(string_field_lifetime_cpp(cpp))
        if name == "zero_size_saved_lifetime":
            source = args.work / f"{name}.{mode}.identity.cpp"
            source.write_text(zero_size_lifetime_cpp(cpp))
        if name == "size_one_saved_lifetime":
            source = args.work / f"{name}.{mode}.identity.cpp"
            source.write_text(one_size_lifetime_cpp(cpp))
        if name == "size_deleted_saved_lifetime":
            source = args.work / f"{name}.{mode}.identity.cpp"
            source.write_text(delete_size_lifetime_cpp(cpp))
        if name in {"joined_size_saved_lifetime", "joined_mutation_saved_lifetime"}:
            source = args.work / f"{name}.{mode}.identity.cpp"
            source.write_text(zero_size_lifetime_cpp(cpp))
        if name in {'object_argument_exact', 'object_argument_siblings'}:
            source = args.work / f"{name}.{mode}.identity.cpp"
            observer = (retained_key_lifetime_cpp if name == 'object_argument_siblings'
                        else object_argument_lifetime_cpp)
            source.write_text(observer(cpp))
        if name in primitive_absence_sources():
            source = args.work / f"{name}.{mode}.observed.cpp"
            source.write_text(primitive_absence_cpp(cpp))
        if name in leaf_object_sources() and (not name.endswith("_repair")
                                               or name == "leaf_object_identity_repair"):
            source = args.work / f"{name}.{mode}.identity.cpp"
            source.write_text(leaf_object_identity_cpp(cpp, name))
        if name in LEAF_COMPARISON_CASES:
            source = args.work / f"{name}.{mode}.identity.cpp"
            source.write_text(comparison_identity_cpp(cpp, name))
        if name in LEAF_ABSENCE_LIFETIMES:
            source = args.work / f"{name}.{mode}.identity.cpp"
            source.write_text(leaf_absence_lifetime_cpp(cpp, name))
        if name in LEAF_CLEAR_LIFETIMES:
            source = args.work / f"{name}.{mode}.identity.cpp"
            source.write_text(leaf_clear_lifetime_cpp(cpp, name))
        if name in NUMERIC_ENTRY_LIFETIMES:
            source = args.work / f"{name}.{mode}.identity.cpp"
            source.write_text(numeric_entry_lifetime_cpp(cpp, name))
        if name in {**nullable_result_sources(), **nullable_key_sources(), **nullable_payload_sources(),
                    **nullable_host_result_sources(), **nullable_nested_result_sources()}:
            key = "std::string" if name == "nullable_homogeneous_key" else "std::variant<bool, std::string>"
            if name in nullable_key_sources():
                key = "ctnative::nullable_string"
                if name == "nullable_key_identity_normalized":
                    key = "std::string"
                elif name in {"nullable_original_key", "nullable_second_key_use", "nullable_key_mixed"}:
                    key = "std::variant<bool, ctnative::nullable_string>"
            payload = "std::variant<bool, std::string>"
            if name in nullable_payload_sources():
                key = payload = "ctnative::nullable_string"
                if name in {"nullable_payload_mixed", "nullable_mixed_payload_readback"}:
                    key = payload = "std::variant<bool, ctnative::nullable_string>"
                elif name in {"nullable_payload_mixed_readback", "nullable_payload_mixed_identity",
                              "nullable_payload_mixed_saved"}:
                    payload = "std::variant<bool, ctnative::nullable_string>"
            if name in {**nullable_host_result_sources(), **nullable_nested_result_sources()}:
                key = "ctnative::nullable_string"
                payload = "std::variant<bool, ctnative::nullable_string>"
            if f"std::shared_ptr<ctnative::map_storage<{key}, {payload}>>" not in cpp:
                raise RuntimeError(f"{name}/{mode}: nullable signature changed the exact Map schema")
            source = args.work / f"{name}.{mode}.identity.cpp"
            source.write_text(nullable_identity_cpp(cpp, name))
        for index, compiler in enumerate(compilers):
            binary = (args.work / f"{name}.{mode}.{index}").resolve()
            host.run([compiler, *owned.FLAGS, str(source), "-o", str(binary)])
            if owned.VM.search(host.run([nm, "-C", str(binary)]).stdout):
                raise RuntimeError(f"{name}/{mode}: linked a VM symbol")
            traces = 2 if name in {*LEAF_COMPARISON_CASES, *LEAF_ABSENCE_LIFETIMES,
                                  *LEAF_CLEAR_LIFETIMES, *NUMERIC_ENTRY_LIFETIMES,
                                  "field_string_lifetime", "zero_size_saved_lifetime",
                                  "size_one_saved_lifetime", "size_deleted_saved_lifetime",
                                  "joined_size_saved_lifetime", "joined_mutation_saved_lifetime",
                                  "object_argument_exact", "object_argument_siblings"} else 1
            if normalized_scalar_output(host.run([str(binary)]).stdout) != scalar_global_output(name, value) * traces:
                raise RuntimeError(f"{name}/{mode}: standalone result mismatch")
        if name in {"ordinary", "mutate_map", "growing", "result_seeded_growing"}:
            lifetime(args, cpp, name, mode, value, compilers[1])
        if name in {"shared_growing", "shared_parameter"}:
            shared_lifetime(args, cpp, name, mode, compilers[1])
        if name in {"result_seeded_string_saved", "result_seeded_mixed_string_saved",
                    "saved_read_write_string_saved", "saved_join_string_saved",
                    "guarded_saved_string_saved", "shortcircuit_string_saved"}:
            string_payload_lifetime(args, cpp, name, mode, compilers[1])
        if name == "nullable_string_saved":
            nullable_payload_lifetime(args, cpp, name, mode, compilers[1])
        if name == "nullable_key_string_saved":
            nullable_key_lifetime(args, cpp, name, mode, compilers[1])
        if name in {"nullable_payload_saved", "nullable_payload_mixed_saved", "nullable_host_result_saved",
                    "nullable_nested_result_saved"}:
            nullable_stored_payload_lifetime(args, cpp, name, mode, compilers[1])
        if name == "leaf_object_lifetime":
            leaf_object_lifetime(args, cpp, name, mode, compilers[1])
        if name in {"local_field_readback_lifetime_checked", "local_field_readback_lifetime"}:
            leaf_readback_lifetime(args, cpp, name, mode, compilers[1])
        if name in LEAF_COMPARISON_CASES:
            comparison_identity_lifetime(args, cpp, name, mode, compilers[1])
        if name in LEAF_ABSENCE_LIFETIMES:
            leaf_absence_lifetime(args, cpp, name, mode, compilers[1])
        if name in LEAF_CLEAR_LIFETIMES:
            leaf_absence_lifetime(args, cpp, name, mode, compilers[1])
        if name in NUMERIC_ENTRY_LIFETIMES:
            numeric_entry_lifetime(args, cpp, name, mode, compilers[1])
        if name == "field_string_lifetime":
            string_field_lifetime(args, cpp, name, mode, compilers[1])
        if name == "zero_size_saved_lifetime":
            zero_size_lifetime(args, cpp, name, mode, compilers[1])
        if name == "size_one_saved_lifetime":
            one_size_lifetime(args, cpp, name, mode, compilers[1])
        if name == "size_deleted_saved_lifetime":
            delete_size_lifetime(args, cpp, name, mode, compilers[1])
        if name in {"joined_size_saved_lifetime", "joined_mutation_saved_lifetime"}:
            zero_size_lifetime(args, cpp, name, mode, compilers[1])
        if name in {'object_argument_exact', 'object_argument_siblings'}:
            object_argument_lifetime(args, cpp, name, mode, compilers[1])
