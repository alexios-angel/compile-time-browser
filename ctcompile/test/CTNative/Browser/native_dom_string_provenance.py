"""native dom string provenance: continued from native_dom_string_harness."""

from CTNative.Browser.native_dom_string_harness import *


def emitted(args, module, name, *, optional_read=True, uri_call=False, callbacks=0):
    text = module.read_text()
    entries = dom.NATIVE.findall(text)
    if len(entries) != 1 + callbacks or dom.FUNCTION.search(text) or "ctnative.not_native" in text:
        raise RuntimeError(
            f"{name}: expected a complete typed DOM entry and {callbacks} callbacks\n{text}"
        )
    cpp = run([args.translate, "--mlir-to-cpp", str(module)]).stdout
    if (
        dom.VM.search(cpp)
        or re.search(
            r"nullable_scalar|nullable_string|std::variant|shared_ptr|weak_ptr|invoke_callable|\bmain\s*\(",
            cpp,
        )
        # The optional String is get_attribute's return type in the runtime
        # header, so a program shows it either by calling the helper or by
        # carrying std::optional<std::string> itself (a branch over a null).
        or (
            optional_read
            and "ctnative::get_attribute" not in cpp
            and "std::optional<std::string>" not in cpp
        )
        or (not optional_read and "ctnative::has_attribute" not in cpp)
        or (uri_call and re.search(r"\bcatch\s*\(", cpp))
        or (
            uri_call
            and any(
                token not in cpp
                for token in ("ctbrowser::decode_uri_component", ".has_value()", "std::move")
            )
        )
    ):
        raise RuntimeError(
            f"{name}: expected an ordinary optional String using the shared DOM API\n{cpp}"
        )
    return cpp, entries[0]


def completion_provenance_checks(args, ir, contract):
    original = ir.read_text()
    helper = next(
        body
        for body in re.findall(r"^  ctjs\.func [^\n]+\n.*?^  }\n", original, re.M | re.S)
        if "@read$3(" in body.splitlines()[0]
    )
    # An unsigned cast turns i32 -1 into index 4294967295. Like completion
    # code 1, that selects the default arm and returns the same active value.
    # Keep the former refusal mutation as an exact native-body equivalence check.
    before, after = "arith.constant 1 : i32", "arith.constant -1 : i32"
    if helper.count(before) != 1:
        raise RuntimeError("completion unsigned selector anchor changed")
    unsigned = args.work / "completion-unsigned-selector.mlir"
    unsigned.write_text(original.replace(helper, helper.replace(before, after, 1)))
    checked = dict(contract, module_sha256=dom.fingerprint(args.opt, unsigned))
    for provider in ("ctbrowser-dom-v1", "ctbrowser-dom-session-v1"):
        for optimize in (False, True):
            label = f"completion-unsigned-selector-{provider}-{optimize}"
            baseline = dom.lower(
                args, ir, dict(contract, provider=provider), label + "-original", optimize=optimize
            )
            admitted = dom.lower(
                args, unsigned, dict(checked, provider=provider), label, optimize=optimize
            )
            bodies = [
                re.sub(r'ctnative\.provenance = "(?:[^"\\]|\\.)*"', "", path.read_text())
                for path in (baseline, admitted)
            ]
            if bodies[0] != bodies[1]:
                raise RuntimeError(f"{label}: unsigned selector changed the native body")
    variants = {
        "poison-return": ("scf.yield %20, %c1_i32", "scf.yield %0, %c1_i32"),
        "poison-observation": ("ctjs.compare strict_eq %5, %6", "ctjs.compare strict_eq %0, %6"),
        "live-selector": (
            "%10 = arith.index_castui %9#1",
            "%live = arith.extui %8 : i1 to i32\n    %10 = arith.index_castui %live",
        ),
        # Bypass the unsigned cast: a negative index must still refuse.
        "negative-selector": (
            "%10 = arith.index_castui %9#1 : i32 to index",
            "%10 = arith.constant -1 : index",
        ),
        "frame-exit": ("ctjs.frame_exit %1", ""),
        "unvisited-arm": (
            "    default {",
            "    case 99 {\n      %unvisited = ctjs.constant #ctjs.boolean<true>\n"
            "      scf.yield %unvisited : !ctjs.value\n    }\n    default {",
        ),
    }
    for name, (before, after) in variants.items():
        if before not in helper:
            raise RuntimeError(f"completion provenance anchor changed: {name}")
        mutated = args.work / f"completion-{name}.mlir"
        mutated.write_text(original.replace(helper, helper.replace(before, after, 1)))
        checked = dict(contract, module_sha256=dom.fingerprint(args.opt, mutated))
        for provider in ("ctbrowser-dom-v1", "ctbrowser-dom-session-v1"):
            for optimize in (False, True):
                diagnostic = dom.lower(
                    args,
                    mutated,
                    dict(checked, provider=provider),
                    f"completion-{name}-{provider}-{optimize}",
                    optimize=optimize,
                    success=False,
                )
                if "error: native DOM source:" not in diagnostic:
                    raise RuntimeError(f"completion {name}: wrong refusal\n{diagnostic}")
    for budget in (0, 32, 128):
        diagnostic = dom.lower(
            args, ir, contract, f"completion-budget-{budget}", max_steps=budget, success=False
        )
        if "budget exhausted" not in diagnostic:
            raise RuntimeError(f"completion budget {budget}: wrong refusal\n{diagnostic}")
    return 4 * len(variants) + 7


def helper_provenance_refusals(args, ir, contract):
    original = ir.read_text()

    def once(text, old, new):
        if text.count(old) != 1:
            raise RuntimeError(f"helper provenance mutation is not unique: {old}")
        return text.replace(old, new)

    functions = re.findall(r"^  ctjs\.func [^\n]+\n.*?^  }\n", original, re.M | re.S)
    if len(functions) != 3:
        raise RuntimeError("helper provenance controls require the complete read source")
    entry = next(body for body in functions if f"@{contract['entry']}(" in body.splitlines()[0])
    helper = next(body for body in functions if "@read$2(" in body.splitlines()[0])
    closure = "ctjs.create_closure %arg2[2] this %1"
    direct = "ctjs.call_direct @read$2(%4, %5, %2, %arg3, %3)"
    identity = "DOM helper lacks an exact local closure identity"
    call_shape = "DOM helper callable escapes or its call shape is unsupported"
    duplicate = once(helper, "@read$2(", "@duplicate$2(")
    alternative = once(helper, "@read$2(", "@alternative$3(")
    variants = {
        "duplicate-index": (
            once(original, helper, helper + duplicate),
            "DOM helper source function identity is ambiguous",
        ),
        "negative-index": (
            once(original, closure, closure.replace("[2]", "[-1]")),
            identity,
        ),
        "missing-index": (
            once(original, closure, closure.replace("[2]", "[2147483647]")),
            "DOM helper closure target is missing",
        ),
        "foreign-enclosing-closure": (
            once(original, closure, closure.replace("%arg2[", "%arg3[")),
            identity,
        ),
        "foreign-enclosing-this": (
            once(original, closure, closure.replace("this %1", "this %arg3")),
            identity,
        ),
        "forged-proof": (
            once(
                original,
                closure,
                closure.replace("%arg2[", "%arg3[") + " {ctnative.host_proved = true}",
            ),
            identity,
        ),
        "different-direct-target": (
            once(
                once(original, helper, helper + alternative),
                direct,
                direct.replace("@read$2(", "@alternative$3("),
            ),
            call_shape,
        ),
        "direct-receiver": (
            once(original, direct, direct.replace("(%4,", "(%arg3,")),
            call_shape,
        ),
        "direct-new-target": (
            once(original, direct, direct.replace(", %5,", ", %arg3,")),
            call_shape,
        ),
        "skipped-helper": (
            once(
                original,
                helper,
                once(helper, "attributes {", "attributes {ctjs.skipped = true, "),
            ),
            "DOM helper requires complete source functions and an uncaptured wrapper",
        ),
    }
    if closure not in entry or direct not in entry:
        raise RuntimeError("helper provenance controls no longer target the entry call")
    for name, (text, reason) in variants.items():
        mutated = args.work / f"helper-provenance-{name}.mlir"
        mutated.write_text(text)
        # Parsing and fingerprinting must succeed before checking the native
        # refusal: malformed IR is not evidence for a provenance guard.
        checked = dict(contract, module_sha256=dom.fingerprint(args.opt, mutated))
        for owned in (False, True):
            provider = "ctbrowser-dom-session-v1" if owned else "ctbrowser-dom-v1"
            for optimize in (False, True):
                diagnostic = dom.lower(
                    args,
                    mutated,
                    dict(checked, provider=provider),
                    f"helper-provenance-{name}-{owned}-{optimize}",
                    optimize=optimize,
                    success=False,
                )
                if f"error: native DOM source: {reason}" not in diagnostic:
                    raise RuntimeError(f"helper provenance {name}: wrong refusal\n{diagnostic}")
    # Each local helper calls only its own capture-free child. The 64th
    # expansion must hit the depth bound, independently of the work budget.
    body = "return target.getAttribute('x') === null;"
    for depth in reversed(range(64)):
        body = f"function deep{depth}(target) {{ {body} }} return deep{depth}(target);"
    body = once(body, "return deep0(target);", "return deep0(element);")
    deep_ir, deep_contract = dom.prepare(
        args,
        "helper-depth",
        f"function helper_depth(element) {{ {body} }}\n",
        1,
        entry_name="helper_depth",
    )
    diagnostic = dom.lower(args, deep_ir, deep_contract, "helper-depth", success=False)
    if "error: native DOM source: DOM helper call tree is recursive or too deep" not in diagnostic:
        raise RuntimeError(f"helper depth: wrong refusal\n{diagnostic}")
    # Flat sibling helpers capture the previous callable. Their iterative
    # expansion must retain the same depth limit as nested helper bodies.
    graph = "function graph0(target) { return target.getAttribute('x') === null; }\n"
    graph += "".join(
        f"function graph{depth}(target) {{ return graph{depth - 1}(target); }}\n"
        for depth in range(1, 64)
    )
    graph += "return graph63(element);"
    nested = "function inner0(target) { return target.getAttribute('x') === null; }\n"
    nested += "".join(
        f"function inner{depth}(target) {{ return inner{depth - 1}(target); }}\n"
        for depth in range(1, 32)
    )
    mixed = f"function graph0(target) {{ {nested} return inner31(target); }}\n"
    mixed += "".join(
        f"function graph{depth}(target) {{ return graph{depth - 1}(target); }}\n"
        for depth in range(1, 32)
    )
    mixed += "return graph31(element);"
    forwarded = "return element.getAttribute('x') === null;"
    for depth in reversed(range(64)):
        forwarded = f"function forward{depth}() {{ {forwarded} }} return forward{depth}();"
    for label, body in (
        ("capture-graph-depth", graph),
        ("capture-mixed-depth", mixed),
        ("capture-forwarded-depth", forwarded),
    ):
        graph_ir, graph_contract = dom.prepare(
            args,
            label,
            f"function capture_graph_depth(element) {{ {body} }}\n",
            1,
            entry_name="capture_graph_depth",
        )
        diagnostic = dom.lower(
            args, graph_ir, graph_contract, label, max_steps=10000000, success=False
        )
        if (
            "error: native DOM source: DOM helper call tree is recursive or too deep"
            not in diagnostic
        ):
            raise RuntimeError(f"{label}: wrong refusal\n{diagnostic}")
    return len(variants) * 4 + 4


def factory_provenance_checks(args, ir, contract, *, table=False):
    prefix = "factory-table" if table else "factory"
    original = ir.read_text()
    creator = "ctjs.create_closure %arg2[1] this %1"
    entry = "ctjs.create_closure %arg2[2] this %2 captures %1"
    call = "ctjs.call_direct @fn$1(%4, %5, %2, %3)"
    returned = "ctjs.return %4" if table else "ctjs.return %3"
    if any(original.count(anchor) != 1 for anchor in (creator, entry, call, returned)):
        raise RuntimeError("factory provenance anchors changed")
    variants = {
        "foreign-creator": original.replace(creator, creator.replace("%arg2", "%arg0")),
        "forged-creator": original.replace(
            creator, creator.replace("%arg2", "%arg0") + " {ctnative.host_proved = true}"
        ),
        "duplicate-creator": original.replace(creator, creator + "\n    %duplicate = " + creator),
        "wrong-callee": original.replace(call, call.replace("@fn$1", "@" + contract["entry"])),
        "receiver": original.replace(call, call.replace("(%4,", "(%3,")),
        "new-target": original.replace(call, call.replace("%4, %5,", "%4, %2,")),
        "repeated-call": original.replace(call, call + "\n    %again = " + call),
        "observed-result": original.replace(
            call, call + "\n    %same = ctjs.compare strict_eq %6, %6"
        ),
        "wrong-return": original.replace(returned, "ctjs.return %arg3"),
        "foreign-entry-creator": original.replace(entry, entry.replace("%arg2", "%arg0")),
        "implicit-capture": original.replace("ctjs.create_cell %arg3", "ctjs.create_cell %arg0"),
    }
    if table:
        selected = "ctjs.get_property %6[%7]"
        slot = "ctjs.set_property %4[%5], %3"
        name = '%7 = ctjs.constant #ctjs.string<"read-attribute">'
        if any(original.count(anchor) != 1 for anchor in (selected, slot, name)):
            raise RuntimeError("factory table provenance anchors changed")
        variants.update(
            {
                "missing-slot": original.replace(name, name.replace("read-attribute", "missing")),
                "prototype-setter": original.replace("read-attribute", "__proto__"),
                "duplicate-slot": original.replace(slot, slot + "\n    " + slot),
                "noncallable-slot": original.replace(slot, slot.replace(", %3", ", %arg3")),
                "early-slot-read": original.replace(
                    slot, "%early = ctjs.get_property %4[%5]\n    " + slot
                ),
                "dynamic-selection": original.replace(
                    selected, selected.replace("[%7]", "[%arg0]")
                ),
                "observed-selection": original.replace(
                    selected, selected + "\n    %same = ctjs.compare strict_eq %8, %8"
                ),
                "repeated-selection": original.replace(
                    selected, selected + "\n    %again = " + selected
                ),
            }
        )
    for name, text in variants.items():
        mutated = args.work / f"{prefix}-provenance-{name}.mlir"
        mutated.write_text(text)
        checked = dict(contract, module_sha256=dom.fingerprint(args.opt, mutated))
        for provider in ("ctbrowser-dom-v1", "ctbrowser-dom-session-v1"):
            for optimize in (False, True):
                diagnostic = dom.lower(
                    args,
                    mutated,
                    dict(checked, provider=provider),
                    f"{prefix}-provenance-{name}-{provider}-{optimize}",
                    optimize=optimize,
                    success=False,
                )
                if "error: native DOM source:" not in diagnostic:
                    raise RuntimeError(f"{prefix} {name}: wrong refusal\n{diagnostic}")
    indirect = args.work / f"{prefix}-indirect.mlir"
    indirect.write_text(original.replace(call, "ctjs.call %2(%4, %3)"))
    checked = dict(contract, module_sha256=dom.fingerprint(args.opt, indirect))
    dom.lower(args, indirect, checked, f"{prefix}-indirect")
    for budget in (0, 1, 128):
        diagnostic = dom.lower(
            args, ir, contract, f"{prefix}-budget-{budget}", max_steps=budget, success=False
        )
        if "budget exhausted" not in diagnostic:
            raise RuntimeError(f"factory initialization: missing budget refusal\n{diagnostic}")
    return len(variants) * 4 + 4


def host_provenance_checks(args, ir, contract):
    original = ir.read_text()
    closure = "ctjs.create_closure %arg2[1] this %3 captures %2"
    export = 'ctjs.store_global "host_capture_key", %4'
    cell = "ctjs.create_cell %1"
    if any(original.count(anchor) != 1 for anchor in (closure, export, cell)):
        raise RuntimeError("host initialization provenance anchors changed")
    variants = {
        "duplicate-creator": original.replace(closure, closure + "\n    %duplicate = " + closure),
        "wrong-export": original.replace(export, export.replace('"host_capture_key"', '"other"')),
        "foreign-creator": original.replace(closure, closure.replace("%arg2[", "%arg0[")),
        "forged-creator": original.replace(
            closure,
            closure.replace("%arg2[", "%arg0[") + " {ctnative.host_proved = true}",
        ),
        "wrapper-receiver": original.replace(cell, "ctjs.create_cell %arg0"),
        "entry-slot": original.replace("ctjs.load_upvalue %arg2[0]", "ctjs.load_upvalue %arg2[1]"),
        "capture-count": original.replace("upvalue_count = 1", "upvalue_count = 2"),
        "wrapper-return": original.replace("ctjs.return %5", "ctjs.return %1"),
        "wrapper-arity": original.replace(
            "@_script_$0(%arg0:", "@_script_$0(%extra: !ctjs.value, %arg0:"
        ),
    }
    for name, text in variants.items():
        mutated = args.work / f"host-provenance-{name}.mlir"
        mutated.write_text(text)
        checked = dict(contract, module_sha256=dom.fingerprint(args.opt, mutated))
        for provider in ("ctbrowser-dom-v1", "ctbrowser-dom-session-v1"):
            for optimize in (False, True):
                diagnostic = dom.lower(
                    args,
                    mutated,
                    dict(checked, provider=provider),
                    f"host-provenance-{name}-{provider}-{optimize}",
                    optimize=optimize,
                    success=False,
                )
                if "error: native DOM source:" not in diagnostic:
                    raise RuntimeError(f"host initialization {name}: wrong refusal\n{diagnostic}")
    for budget in (0, 1, 128):
        diagnostic = dom.lower(
            args, ir, contract, f"host-budget-{budget}", max_steps=budget, success=False
        )
        if "budget exhausted" not in diagnostic:
            raise RuntimeError(f"host initialization: missing budget refusal\n{diagnostic}")
    return len(variants) * 4 + 3


def capture_provenance_checks(args, ir, contract, graph_ir, graph_contract):
    original = ir.read_text()
    closure = "ctjs.create_closure %arg2[2] this %3 captures %2"
    load = "ctjs.load_upvalue %arg2[0]"
    store = "    ctjs.cell_set %2, %5\n"
    if any(original.count(anchor) != 1 for anchor in (closure, load, store)):
        raise RuntimeError("captured helper provenance anchors changed")
    cell = "DOM helper capture cell is mutable or escapes"
    slot = "DOM helper upvalue lacks an exact capture slot"
    variants = {
        "foreign-load": (original.replace(load, load.replace("%arg2", "%arg3")), slot),
        "forged-load-proof": (
            original.replace(
                load, load.replace("%arg2", "%arg3") + " {ctnative.host_proved = true}"
            ),
            slot,
        ),
        "negative-slot": (original.replace(load, load.replace("[0]", "[-1]")), slot),
        "missing-slot": (original.replace(load, load.replace("[0]", "[1]")), slot),
        "capture-count": (
            original.replace("upvalue_count = 1", "upvalue_count = 2"),
            "DOM helper capture target has not been completely expanded",
        ),
        "non-cell": (
            original.replace(closure, closure.replace("captures %2", "captures %arg3")),
            "DOM helper capture lacks a proved local cell",
        ),
        "forwarded-slot": (
            original.replace(
                closure,
                closure.replace("captures %2", "captures %3")
                + " {enclosing_indices = array<i32: 0>}",
            ),
            "DOM helper forwarded capture lacks an exact enclosing slot",
        ),
        "duplicate-closure": (
            original.replace(closure, closure + "\n    %duplicate = " + closure),
            "DOM helper requires an immutable local leaf capture",
        ),
        "second-write": (original.replace(store, store + store), cell),
        "late-write": (
            original.replace(store, "").replace(
                "    %9 = ctjs.constant", store + "    %9 = ctjs.constant"
            ),
            "DOM helper capture assignment does not precede its call",
        ),
        "early-read": (
            original.replace(store, "    %early = ctjs.cell_get %2\n" + store),
            "DOM helper capture assignment does not precede its read",
        ),
    }
    for name, (text, reason) in variants.items():
        mutated = args.work / f"capture-provenance-{name}.mlir"
        mutated.write_text(text)
        checked = dict(contract, module_sha256=dom.fingerprint(args.opt, mutated))
        for provider in ("ctbrowser-dom-v1", "ctbrowser-dom-session-v1"):
            for optimize in (False, True):
                diagnostic = dom.lower(
                    args,
                    mutated,
                    dict(checked, provider=provider),
                    f"capture-provenance-{name}-{provider}-{optimize}",
                    optimize=optimize,
                    success=False,
                )
                if f"error: native DOM source: {reason}" not in diagnostic:
                    raise RuntimeError(f"capture provenance {name}: wrong refusal\n{diagnostic}")
    diagnostic = dom.lower(args, ir, contract, "capture-budget", max_steps=128, success=False)
    if "budget exhausted" not in diagnostic:
        raise RuntimeError(f"capture query: missing work-budget refusal\n{diagnostic}")
    graph = graph_ir.read_text()
    cell = "    %3 = ctjs.create_cell %2\n"
    store = "    ctjs.cell_set %3, %5\n"
    after_call = "    %11 = ctjs.constant #ctjs.null\n"
    if any(graph.count(anchor) != 1 for anchor in (cell, store, after_call)):
        raise RuntimeError("captured callable storage anchors changed")
    initial = graph.replace(cell, "").replace(store, "    %3 = ctjs.create_cell %5\n")
    late = graph.replace(store, "").replace(after_call, store + after_call)
    for name, text, reason in (
        ("initial-cell", initial, None),
        ("uninitialized-call", late, "DOM helper capture assignment does not precede its call"),
    ):
        mutated = args.work / f"capture-storage-{name}.mlir"
        mutated.write_text(text)
        checked = dict(graph_contract, module_sha256=dom.fingerprint(args.opt, mutated))
        for provider in ("ctbrowser-dom-v1", "ctbrowser-dom-session-v1"):
            for optimize in (False, True):
                label = f"capture-storage-{name}-{provider}-{optimize}"
                result = dom.lower(
                    args,
                    mutated,
                    dict(checked, provider=provider),
                    label,
                    optimize=optimize,
                    success=reason is None,
                )
                if reason is None:
                    emitted(args, result, label)
                elif f"error: native DOM source: {reason}" not in result:
                    raise RuntimeError(f"capture storage {name}: wrong refusal\n{result}")
    return len(variants) * 4 + 9


def forwarded_provenance_checks(args, ir, contract):
    original = ir.read_text()
    closure = "ctjs.create_closure %arg2[3] this %2 captures %1"
    indices = "enclosing_indices = array<i32: 0>"
    if any(original.count(anchor) != 1 for anchor in (closure, indices)):
        raise RuntimeError("forwarded capture provenance anchors changed")
    slot = "DOM helper forwarded capture lacks an exact enclosing slot"
    variants = {
        "missing-parent-slot": (original.replace(indices, indices.replace("0>", "1>")), slot),
        "forged-parent-slot": (
            original.replace(
                indices, indices.replace("0>", "1>") + ", ctnative.host_proved = true"
            ),
            slot,
        ),
        "local-placeholder": (
            original.replace(indices, indices.replace("0>", "-1>")),
            "DOM helper capture lacks a proved local cell",
        ),
        "foreign-parent": (
            original.replace(closure, closure.replace("%arg2[", "%arg0[")),
            "DOM helper lacks an exact local closure identity",
        ),
        "duplicate-creator": (
            original.replace(
                closure + " {" + indices + "}",
                closure + " {" + indices + "}\n    %duplicate = " + closure + " {" + indices + "}",
            ),
            "DOM helper forwarded capture identity is ambiguous",
        ),
    }
    for name, (text, reason) in variants.items():
        mutated = args.work / f"forwarded-provenance-{name}.mlir"
        mutated.write_text(text)
        checked = dict(contract, module_sha256=dom.fingerprint(args.opt, mutated))
        for provider in ("ctbrowser-dom-v1", "ctbrowser-dom-session-v1"):
            for optimize in (False, True):
                diagnostic = dom.lower(
                    args,
                    mutated,
                    dict(checked, provider=provider),
                    f"forwarded-provenance-{name}-{provider}-{optimize}",
                    optimize=optimize,
                    success=False,
                )
                if f"error: native DOM source: {reason}" not in diagnostic:
                    raise RuntimeError(f"forwarded provenance {name}: wrong refusal\n{diagnostic}")
    diagnostic = dom.lower(args, ir, contract, "forwarded-budget", max_steps=128, success=False)
    if "budget exhausted" not in diagnostic:
        raise RuntimeError(f"forwarded capture: missing work-budget refusal\n{diagnostic}")
    return len(variants) * 4 + 1


def method_provenance_checks(args, ir, contract):
    original = ir.read_text()
    call = "ctjs.call %6(%1, %arg3, %7)"
    direct = "ctjs.call_direct @fn$2(%1, %2, %6, %arg3, %7)"
    helper = re.search(r"^  ctjs\.func @fn\$2[^\n]+\n.*?^  }\n", original, re.M | re.S)
    if original.count(call) != 1 or not helper:
        raise RuntimeError("object helper provenance anchors changed")
    exact = original.replace(call, direct)
    call_shape = "DOM helper callable escapes or its call shape is unsupported"
    alternative = helper[0].replace("@fn$2(", "@alternative$3(")
    variants = {
        "direct": (exact, None),
        "direct-target": (
            exact.replace(helper[0], helper[0] + alternative).replace(
                direct, direct.replace("@fn$2(", "@alternative$3(")
            ),
            call_shape,
        ),
        "direct-new-target": (
            exact.replace(direct, direct.replace(", %2,", ", %arg3,")),
            call_shape,
        ),
        "foreign-receiver": (original.replace(call, call.replace("(%1,", "(%arg3,")), call_shape),
        "foreign-callee": (
            original.replace(call, call.replace("%6(", "%arg3(")),
            "DOM helper object escapes or observes its identity",
        ),
        "observed-receiver": (
            original.replace(
                helper[0], helper[0].replace("ctjs.get_property %arg3[", "ctjs.get_property %arg0[")
            ),
            "DOM helper observes an implicit argument",
        ),
    }
    for name, (text, reason) in variants.items():
        mutated = args.work / f"method-provenance-{name}.mlir"
        mutated.write_text(text)
        checked = dict(contract, module_sha256=dom.fingerprint(args.opt, mutated))
        for provider in ("ctbrowser-dom-v1", "ctbrowser-dom-session-v1"):
            for optimize in (False, True):
                label = f"method-provenance-{name}-{provider}-{optimize}"
                result = dom.lower(
                    args,
                    mutated,
                    dict(checked, provider=provider),
                    label,
                    optimize=optimize,
                    success=reason is None,
                )
                if reason is None:
                    cpp, _ = emitted(args, result, label)
                    if "ctjs.call" in cpp or "script::" in cpp:
                        raise RuntimeError(f"{label}: direct method retained dynamic dispatch")
                elif f"error: native DOM source: {reason}" not in result:
                    raise RuntimeError(f"{label}: wrong refusal\n{result}")
    return len(variants) * 4


def regexp_provenance_checks(args, ir, contract, *, prefix="replacement"):
    original = ir.read_text()
    variants = {
        "factory": ('ctjs.load_global "__ctbrowser_regexp"', 'ctjs.load_global "RegExp"'),
        "matching": ('#ctjs.string<"[A-Z]">', '#ctjs.string<"[a-z]">'),
        "flags": ('#ctjs.string<"g">', '#ctjs.string<"gi">'),
        "creator": ("ctjs.create_closure %arg2[3]", "ctjs.create_closure %arg0[3]"),
        "callback-target": ("ctjs.create_closure %arg2[3]", "ctjs.create_closure %arg2[2]"),
        "direct-target": ("ctjs.call_direct @F$2(", "ctjs.call_direct @fn$3("),
        "mixed-arguments": ('#ctjs.string<"config">', '#ctjs.string<"Config">'),
    }
    if prefix == "replacement-mixed":
        variants["second-matching"] = ('#ctjs.string<"toggle">', '#ctjs.string<"Toggle">')
    if prefix == "replacement-captured":
        # F is reached through the proved capture, so the imported call is indirect.
        del variants["direct-target"]
    for name, (before, after) in variants.items():
        if before not in original:
            raise RuntimeError(f"replacement provenance anchor changed: {name}")
        mutated = args.work / f"{prefix}-provenance-{name}.mlir"
        mutated.write_text(original.replace(before, after, 1))
        checked = dict(contract, module_sha256=dom.fingerprint(args.opt, mutated))
        for provider in ("ctbrowser-dom-v1", "ctbrowser-dom-session-v1"):
            for optimize in (False, True):
                diagnostic = dom.lower(
                    args,
                    mutated,
                    dict(checked, provider=provider),
                    f"{prefix}-provenance-{name}-{provider}-{optimize}",
                    optimize=optimize,
                    success=name in ("mixed-arguments", "second-matching"),
                )
                if name in ("mixed-arguments", "second-matching"):
                    emitted(args, diagnostic, f"{prefix}-{name}")
                else:
                    stage = "entry" if name in ("factory", "matching", "flags") else "source"
                    if f"error: native DOM {stage}:" not in diagnostic:
                        raise RuntimeError(f"replacement {name}: wrong refusal\n{diagnostic}")
    for budget in (0, 32, 64):
        diagnostic = dom.lower(
            args, ir, contract, f"{prefix}-budget-{budget}", max_steps=budget, success=False
        )
        if "budget exhausted" not in diagnostic:
            raise RuntimeError(f"replacement budget {budget}: wrong refusal\n{diagnostic}")
    return 4 * len(variants) + 3


def check_regexp_matching(args):
    cases = dict(REGEXP_MATCHING)
    if args.regexp_only:
        cases.update({name: (body, "true") for name, (body, _) in REGEXP_CASES.items()})
    prepared, observations = [], []
    keys = (
        "data-bs-config",
        "data-bs-toggle",
        "data-bs--config",
        "data-bs--toggle",
        "data-bs-direct",
    )
    declarations = []
    for index, (name, (body, expected)) in enumerate(cases.items()):
        source = f"function {name}(element) {{ {body} }}\n"
        declarations.append(source)
        observations.append(
            f"var observation{index:03} = (() => {{ const element = observationElement(null); "
            f"const result = {name}(element); return JSON.stringify([result, "
            + ", ".join(f"element.getAttribute('{key}')" for key in keys)
            + "]); })();"
        )
        ir, contract = dom.prepare(args, name, source, 1, entry_name=name)
        prepared.append((name, ir, contract))
    oracle = args.work / "regexp-oracle.js"
    source = BOOLEAN_DOUBLE + "".join(declarations) + "\n".join(observations)
    oracle.write_text(source)
    reference = run([args.reference, str(oracle)]).stdout
    oracle.write_text(
        source
        + "\n"
        + "\n".join(f"console.log(observation{index:03});" for index in range(len(cases)))
    )
    node = run([args.node, str(oracle)]).stdout
    expected_reference = "".join(
        f'observation{index:03}="{quote_from_bytes(line.encode())}"\n'
        for index, line in enumerate(node.splitlines())
    )
    if reference != expected_reference:
        raise RuntimeError(
            f"original replacement source observations differ: {reference!r}, {node!r}"
        )
    results = [json.loads(line) for line in node.splitlines()]
    if len(results) != len(cases):
        raise RuntimeError("original replacement oracle omitted source observations")
    for (name, (_, expected)), result in zip(cases.items(), results):
        if result[0] != (None if expected == "null" else True):
            raise RuntimeError(f"{name}: original replacement return changed: {result}")
    includes, libraries = dom.link_options(args)
    compilers = find_compilers()
    compilers[1] = args.clang
    for optimize in (False, True):
        headers, bodies, checks = set(), [], []
        for provider in ("ctbrowser-dom-v1", "ctbrowser-dom-session-v1"):
            owned = "session" in provider
            for (name, ir, contract), result in zip(prepared, results):
                label = f"{name}_{owned}_{optimize}"
                native = dom.lower(
                    args, ir, dict(contract, provider=provider), label, optimize=optimize
                )
                cpp, symbol = emitted(args, native, label)
                headers.update(re.findall(r"^#(?:include|define CTNATIVE_)[^\n]*", cpp, re.M))
                body = re.sub(r"^#(?:include|define CTNATIVE_)[^\n]*\n?", "", cpp, flags=re.M)
                bodies.append(f"namespace {label} {{\n{body}\n}}\n")
                setup = (
                    f"{label}::{symbol}_session session; auto & doc = session.document();"
                    if owned
                    else "ctbrowser::atom_table owner; ctbrowser::document doc{owner};"
                )
                call = "session.invoke(element)" if owned else f"{label}::{symbol}(element)"
                check = f'{{ {setup} auto & atoms = doc.atoms(); auto node = doc.create_element(atoms.intern("div")); ctbrowser::element_ref element{{&doc, node}}; '
                expected = "std::nullopt" if result[0] is None else "ctnative::js_boolean_t{true}"
                check += f"assert({call} == {expected});"
                for key, value in zip(keys, result[1:]):
                    if value is None:
                        check += f'assert(!doc.read().has_attribute(node, atoms.intern("{key}")));'
                    else:
                        check += f'assert(doc.read().attribute_value(node, atoms.intern("{key}")) == std::string_view({numbers.cpp_string(value)}, {len(value.encode())}));'
                checks.append(check + "}")
        path = args.work / f"regexp-{optimize}.cpp"
        path.write_text(
            "\n".join(sorted(headers))
            + "\n#include <cassert>\n"
            + "\n".join(bodies)
            + "\nint main() {\n"
            + "\n".join(checks)
            + "\n}\n"
        )
        for index, compiler in enumerate(compilers):
            binary = path.with_suffix(f".{index}")
            run([compiler, *FLAGS, *includes, str(path), *libraries, "-o", str(binary)])
            if dom.VM.search(run([args.nm, "-C", str(binary)]).stdout):
                raise RuntimeError("original replacement output links Script/AOT")
            run([str(binary)])
    refusals = 0
    if args.regexp_only:
        for name, body in REGEXP_REFUSALS.items():
            ir, contract = dom.prepare(
                args, name, f"function invalid(element) {{ {body} }}\n", 1, entry_name="invalid"
            )
            for provider in ("ctbrowser-dom-v1", "ctbrowser-dom-session-v1"):
                diagnostic = dom.lower(
                    args, ir, dict(contract, provider=provider), name + provider, success=False
                )
                if "DOM" not in diagnostic:
                    raise RuntimeError(f"{name}: missing DOM refusal")
                refusals += 1
        _, ir, contract = next(row for row in prepared if row[0] == "helper_regex_original")
        refusals += regexp_provenance_checks(args, ir, contract)
        for prefix, source in (
            ("replacement-mixed", REGEXP_MIXED_SOURCE),
            ("replacement-captured", REGEXP_CAPTURED_SOURCE),
        ):
            ir, contract = dom.prepare(args, prefix, source, 1, entry_name="invalid")
            refusals += regexp_provenance_checks(args, ir, contract, prefix=prefix)
    print(
        f"native DOM replacement: {len(cases)} original Node/VM observations, 4 GCC/Clang executions, {refusals} source/provenance/budget checks"
    )
