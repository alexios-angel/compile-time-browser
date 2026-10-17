#!/usr/bin/env python3
"""Check class-initialization lowering against Node and the reference interpreter.

Each fixture in OBSERVATIONS runs under Node and the reference interpreter, is
imported to the ctjs dialect, and must be refused when lowered to EmitC as is.
After --ctnative-specialize-class-initialization, fixtures in POSITIVES must
lower completely and run natively with Node's result; fixtures in
PREPARED_ONLY prepare but native lowering must still refuse them.
"""

import argparse

from CTNative.Lowering.Objects.classes.initialization_inputs import *

from CTNative.harness import native_args, run_parallel, selected_fixtures, without_intrinsic

# OBSERVATIONS maps a fixture name to (Node result, reference-interpreter
# result): the value each engine prints as `a=`, or None if that run must fail
# (see initialization_inputs.py). POSITIVES lists the fixtures that must lower
# fully to native code; PREPARED_ONLY lists the ones that prepare but stay
# refused.
#
# Captured variables that only ever hold one primitive literal keep that
# literal when the class or helper body is copied.
OBSERVATIONS.update(
    {
        "inherited-primitive-captures": (34, 34),
        "primitive-capture-loop": (8, 8),
        "primitive-capture-computed": (7, 7),
    }
)
POSITIVES.update(
    {
        "method-captured",
        "nested-helper-primitive",
        "captured-helper-nested",
        "inherited-primitive-captures",
        "primitive-capture-loop",
    }
)

# Fixtures whose unprepared import now lowers to a complete native program:
# class lowering handles their classes without
# --ctnative-specialize-class-initialization. Each listed mode must lower
# completely and print Node's value; every other fixture and mode keeps its
# refusal boundary. The optimized-only entries need optimization first: it
# removes an uncalled helper, or folds a dead arm (an untaken receiver read or
# an unknown call behind a constant test) that their preparation still
# refuses.
UNTRUSTED_NATIVE = {
    **dict.fromkeys(
        (
            "class-map-record-alias-method-read",
            "class-map-record-alias-method-write",
            "class-map-record-alias-method-overwrite-delete",
            "class-map-record-alias-method-distinct",
            "class-map-record-shared-owner",
            "class-map-record-owner-method-write",
            "class-map-record-reinsert-aliases",
            "class-map-record-direct",
            "class-map-record-overwrite",
            "class-map-record-delete",
            "class-map-record-clear",
            "class-map-record-payloads",
            "class-map-record-alias-write",
            "class-map-record-alias-method",
            "class-map-optional-key-tags",
            "class-map-optional-key-missing",
            "class-map-optional-key-booleans",
            "class-map-optional-key-nan",
            "class-map-optional-key-signed-zero",
            "class-map-direct-string-keys",
            "class-map-distinct-string-keys",
            "class-map-direct",
            "class-map-shared",
            "class-map-distinct",
            "class-map-method-loop",
            "nested-helper-constructor",
            "nested-helper-method",
            "nested-helper-chain",
            "nested-helper-shared",
            "nested-helper-diamond",
            "nested-helper-order",
            "captured-helper-method",
            "captured-helper-constructor",
            "captured-helper-order",
            "local-holder-method",
            "local-holder-arrow",
            "local-holder-branches",
            "local-holder-order",
            "local-holder-receiver",
            "local-helper-arguments",
            "local-helper-branches",
            "local-helper-order",
            "local-helper-values",
            "empty",
            "number",
            "method",
            "method-arguments",
            "method-branches",
            "method-loop",
            "method-dispatch",
            "method-increment-dispatch",
            "method-decrement-dispatch",
            "constructor-branch",
            "method-empty",
            "method-chain",
            "method-chain-empty",
            "method-chain-effects",
            "method-chain-order",
            "method-chain-cycle",
            "method-constructor-call",
            "method-constructor-order",
            "method-constructor-chain",
            "method-constructor-constant",
            "method-captured",
            "primitive-capture-loop",
        ),
        (False, True),
    ),
    **dict.fromkeys(
        (
            "nested-helper-receiver",
            "nested-helper-effect",
            "bootstrap-r",
            "method-branch-ambient",
            "helper-decided-uncalled",
            "helper-decided-nested-uncalled",
            "helper-decided-live-result",
        ),
        (True,),
    ),
    # Static members: when the class is never used as a value, every read of
    # a static member becomes a direct call, `this` in a static member and
    # `this.constructor` in a method name the class itself, and a static
    # member nothing reads is left uncalled (ClosureLifting/ClassStatics.cpp).
    **dict.fromkeys(
        (
            "class-map-mixed-method",
            "method-captured-class-key",
            "method-captured-class-name",
            "receiver-default-dispatch",
            "static-arguments",
            "static-branch",
            "static-call-arguments",
            "static-call-capture",
            "static-call-getter",
            "static-call-this-getter-chain",
            "static-call-this-method",
            "static-caller",
            "static-chain",
            "static-constant",
            "static-forward-chain",
            "static-length",
            "static-methods",
            "static-name",
            "static-order",
            "static-repeated",
        ),
        (False, True),
    ),
    # The same, where a static member nothing reads is refused (a getter
    # cycle, a throwing getter, an ambient call): optimization removes it as
    # unreachable and records the removal, and without optimization it stays,
    # refused, beside the native program (UNTRUSTED_PARTIAL).
    **dict.fromkeys(
        (
            "static-call-ambient",
            "static-call-unused",
            "static-cycle",
            "static-error-coercion",
            "static-error-replaced",
            "static-error-return",
            "static-throw-ambient",
            "static-throw-object",
            "static-throw-unused",
            "static-throw-unused-chain",
            "static-unread-chain",
            "static-unread-nested",
            "static-unread-new-this",
        ),
        (True,),
    ),
    # Inside its own constructor a class's name is the constructor's own
    # closure, so `Shape.SEED` there is a static read through that closure,
    # which the rewrite resolves like any other name of the class.
    **dict.fromkeys(("class-map-mixed-captures",), (False, True)),
    # The same, once optimization folds the computed key `"NA" + "ME"` to a
    # constant.
    **dict.fromkeys(("static-dynamic",), (True,)),
}

# Inherited classes that ClosureLifting/ClassInheritance.cpp flattens per
# concrete class now lower completely without preparation. Each prints Node's
# value; override-ambient's uncalled Base.read goes with its class.
UNTRUSTED_NATIVE.update(
    dict.fromkeys(
        (
            "class-map-record-alias-method-inherited",
            "inherited",
            "inherited-base-instance",
            "inherited-branch-helper",
            "inherited-chain",
            "inherited-dispatch",
            "inherited-explicit",
            "inherited-helper-chain",
            "inherited-helper-constructor",
            "inherited-helper-distinct",
            "inherited-helper-method",
            "inherited-helper-order-values",
            "inherited-helper-siblings",
            "inherited-method",
            "inherited-method-chain",
            "inherited-method-constructor",
            "inherited-method-instances",
            "inherited-method-leaf",
            "inherited-method-override",
            "inherited-nested-helper-distinct",
            "inherited-number-return",
            "inherited-order",
            "inherited-post-super-chain",
            "inherited-post-super-method",
            "inherited-post-super-order",
            "inherited-post-super-override",
            "inherited-super-chain",
            "inherited-super-helper",
            "inherited-super-nearest",
            "inherited-super-receiver",
            "inherited-super-target-cfg",
            "override-ambient",
            "override-constructor",
            "override-different-leaves",
            "override-distinct",
            "override-leaf",
            "override-middle",
            "override-unused-constructor",
        ),
        (False, True),
    )
)
UNTRUSTED_NATIVE.update(
    dict.fromkeys(
        (
            "inherited-helper-decided-leaf-built",
            "inherited-helper-effect",
            "inherited-helper-receiver",
        ),
        (True,),
    )
)
# Unprepared modes that lower to a native program beside functions that stay
# refused, because nothing removes unreachable code without optimization:
# each refused function is a static member nothing reads.
UNTRUSTED_PARTIAL = dict.fromkeys(
    (
        "static-call-ambient",
        "static-call-unused",
        "static-cycle",
        "static-error-coercion",
        "static-error-replaced",
        "static-error-return",
        "static-throw-ambient",
        "static-throw-object",
        "static-throw-unused",
        "static-throw-unused-chain",
        "static-unread-chain",
        "static-unread-nested",
        "static-unread-new-this",
    ),
    (False,),
)

# Inherited classes with static members: flattening copies the statics onto
# each concrete class, and the static-member rewrite then resolves
# `this.constructor.X` per class.
UNTRUSTED_NATIVE.update(
    dict.fromkeys(
        (
            "inherited-getter-constant",
            "inherited-getter-constructor",
            "inherited-getter-dependencies",
            "inherited-getter-fresh-empty",
            "inherited-getter-override",
            "inherited-getter-override-dependency",
            "inherited-getter-shared-receivers",
            "inherited-getter-static-reads",
            "inherited-getter-three-levels",
            "inherited-method-getter",
            "inherited-static-call",
            "inherited-static-getters",
            "inherited-static-getters-base-reads-leaf",
            "inherited-static-getters-component",
            "inherited-static-getters-component-method",
            "inherited-static-getters-shadow",
            "inherited-static-getters-shadow-both",
            "inherited-static-getters-shadow-extended",
            "inherited-static-getters-shadow-method",
            "inherited-static-getters-shadow-method-both",
            "inherited-static-getters-shadow-method-chain",
            "receiver-default-inherited",
        ),
        (False, True),
    )
)
UNTRUSTED_NATIVE.update(dict.fromkeys(("inherited-getter-unused-ambient",), (True,)))

# Static-member fixtures the static-member rewrite completes in both modes;
# each prints Node's value.
UNTRUSTED_NATIVE.update(dict.fromkeys(("static-defaults", "static-default-fields"), (False, True)))
# Without optimization, an inherited static getter nothing reads stays
# refused beside the native program.
UNTRUSTED_PARTIAL.update(dict.fromkeys(("inherited-getter-unused-ambient",), (False,)))

# Key-conflict fixtures: the reference interpreter has no `console`, so these
# fixtures report a conflict by throwing rather than by logging it.
CONFLICT_OBSERVATIONS = {
    "class-map-record-nested-conflict-direct": (594414, 594414),
    "class-map-record-nested-conflict-holder": (15927, 15927),
    "class-map-record-nested-conflict-constructor": (59112, 59112),
    "class-map-record-nested-conflict-reached": (2, None),
    "class-map-record-nested-conflict-saved-child-conflict": (2, None),
    "class-map-record-nested-conflict-hidden-observer": (7, 7),
    "class-map-record-nested-conflict-hidden-coercion": (7, 7),
    "class-map-record-nested-conflict-unused-holder-slot": (15927, 15927),
    "class-map-record-nested-conflict-dynamic-key": (7, 7),
    "class-map-record-nested-conflict-returned-owner": (7, 7),
    "class-map-record-nested-conflict-hidden-store": (7, 7),
}
OBSERVATIONS.update(CONFLICT_OBSERVATIONS)
VENDOR_DATA_OBSERVATIONS = {
    "class-map-record-nested-vendor-holder": (15927, 15927),
    "class-map-record-nested-vendor-constructor": (59112, 59112),
    "class-map-record-nested-vendor-live-this": (15927, 15927),
    "class-map-record-nested-vendor-unused-observer": (15927, 15927),
}
OBSERVATIONS.update(VENDOR_DATA_OBSERVATIONS)
POSITIVES.update(
    {"class-map-record-nested-vendor-holder", "class-map-record-nested-vendor-constructor"}
)
OBSERVATIONS.update(
    {
        "class-map-record-nested-object-holder": (15927, 15927),
        "class-map-record-nested-object-distinct": (7553, 7553),
        "class-map-record-nested-object-returned-key": (15927, 15927),
        "class-map-record-nested-object-number-key": (15927, 15927),
        "class-map-record-nested-object-changing-key": (15929, 15929),
        "class-map-record-nested-object-unused-observer": (15927, 15927),
    }
)
POSITIVES.update(
    {
        "class-map-record-nested-constructor-object-key",
        "class-map-record-nested-object-holder",
        "class-map-record-nested-object-distinct",
        "class-map-record-nested-object-changing-key",
        "class-map-record-nested-nullable-object-key",
    }
)

POSITIVES.update(
    {
        "class-map-record-nested-conflict-direct",
        "class-map-record-nested-conflict-holder",
        "class-map-record-nested-conflict-constructor",
    }
)

OWN_FIELDS = {
    "own-fields-length": (2, 2),
    "own-fields-order": (113, 113),
    "own-fields-clear": (11, 11),
    "own-fields-effects": (246, 246),
    "own-fields-boxed-key": (11, 11),
    "own-fields-loop": (11, 11),
    "own-fields-conditional": (1, 1),
    "own-fields-dynamic": (1, 1),
    "own-fields-new-field": (2, 2),
    "own-fields-numeric-key": (7, 7),
    "own-fields-receiver-escape": (1, 1),
    "own-fields-snapshot-escape": (7, 7),
    "own-fields-callback": (7, 7),
    "own-fields-object-replaced": (1, 1),
    "own-fields-helper-replaced": (1, 1),
    "inherited-own-fields-collision": (7, 7),
    "own-fields-unused-ambient": (7, 7),
    "own-fields-replacement-return": (7, 7),
}
OBSERVATIONS.update(OWN_FIELDS)
OBSERVATIONS["bootstrap-base-data"] = (7, 7)
# A constructor branch that reads a static getter which always throws ends
# there, so code after the read in that branch is dead.
THROWING_ARMS = {
    "inherited-own-fields-iterate-throwing-arm" + suffix: (7, 7)
    for suffix in ("", "-subclass", "-static-shadow", "-returning", "-escape-before", "-own-getter")
}
OBSERVATIONS.update(THROWING_ARMS)
PREPARED_ONLY.update(
    {
        "inherited-own-fields-iterate-throwing-arm",
        "inherited-own-fields-iterate-throwing-arm-own-getter",
    }
)
# Own static getters beside inherited ones, and own static calls on `this`.
OBSERVATIONS.update(
    {
        "inherited-static-getters": (7, 7),
        "inherited-static-getters-shadow": (7, 7),
        "inherited-static-getters-base-reads-leaf": (7, 7),
        "inherited-static-getters-component": (7, 7),
        "inherited-static-getters-shadow-both": (7, 7),
        "inherited-static-getters-shadow-method": (7, 7),
        "inherited-static-getters-shadow-extended": (7, 7),
        "inherited-static-getters-shadow-throwing": (7, 7),
        "inherited-static-getters-shadow-method-both": (7, 7),
        "inherited-static-getters-shadow-method-chain": (7, 7),
        "inherited-static-getters-component-method": (7, 7),
        "static-call-this-method": (7, 7),
        "static-call-this-getter-chain": (6, 6),
        "static-call-this-detached": (7, 7),
        "static-call-this-foreign": (7, 7),
        "static-call-this-new-this": (7, 7),
    }
)
# Constructor branches that a constant argument decides, where every `new`
# call site passes a constant of the same truthiness.
OBSERVATIONS.update(
    {
        "own-fields-branch-decided-falsy": (7, 7),
        "own-fields-branch-decided-truthy": (14, 14),
        "own-fields-branch-decided-mixed": (7, 7),
        "inherited-own-fields-branch-decided-extended": (7, 7),
    }
)
POSITIVES.update({"own-fields-branch-decided-falsy", "own-fields-branch-decided-truthy"})
# Mirrors Bootstrap's Config._typeCheckConfig, which calls Object.entries on
# the object returned by a static DefaultType getter (a fresh, empty object).
OWN_ENTRIES = {
    name: (7, 7)
    for name in (
        "own-entries-default-type",
        "inherited-own-entries-default-type",
        "own-entries-default-type-fields",
        "own-entries-default-type-primitive",
        "own-entries-default-type-written",
        "own-entries-default-type-escaped",
        "own-entries-argument",
        "own-entries-default-type-passed",
        "own-entries-default-type-effect",
    )
}
OBSERVATIONS.update(OWN_ENTRIES)
ENTRIES_FRESH = "class Object.entries requires a fresh empty static getter object"
ENTRIES_PATH = "class Object.entries requires a proved method with a closed effect-free path"
POSITIVES.update({"own-entries-default-type", "inherited-own-entries-default-type"})
# Static methods that no reachable code reads are removed before the class analysis.
UNREAD_STATICS = {
    name: (7, 7)
    for name in (
        "static-unread-new-this",
        "static-unread-chain",
        "static-unread-nested",
        "inherited-static-unread-leaf-read",
        "static-unread-dynamic",
        "static-unread-constructor-read",
    )
}
OBSERVATIONS.update(UNREAD_STATICS)
POSITIVES.update({"static-unread-new-this", "static-unread-chain", "static-unread-nested"})
# When a constructor branch is decided at compile time, a pure helper call in
# the branch that is not taken is removed. A helper that no live code calls is
# left out of the class analysis.
DECIDED_HELPERS = {
    name: (7, 7)
    for name in (
        "helper-decided-uncalled",
        "helper-decided-nested-uncalled",
        "inherited-helper-decided-leaf-built",
        "helper-decided-live-result",
        "helper-decided-other-call",
        "helper-decided-holder-slot",
    )
}
OBSERVATIONS.update(DECIDED_HELPERS)
POSITIVES.update(
    {
        "helper-decided-uncalled",
        "helper-decided-nested-uncalled",
        "inherited-helper-decided-leaf-built",
        "helper-decided-holder-slot",
    }
)
# A base class constructed only through a derived class's super() call: its
# own-property snapshot is taken in the derived class's constructor.
DEFERRED = {
    "inherited-own-fields-iterate-deferred-arm": (7, 7),
    "inherited-own-fields-iterate-deferred-loop": (7, 7),
    "inherited-own-fields-iterate-deferred-truthy": (14, 14),
    "inherited-own-fields-iterate-deferred-siblings": (7, 7),
    "inherited-own-fields-iterate-deferred-chain": (7, 7),
    "inherited-own-fields-iterate-deferred-siblings-differ": (7, 7),
    "inherited-own-fields-iterate-deferred-mixed": (7, 7),
    "inherited-own-fields-iterate-deferred-early-snapshot": (7, 7),
    "inherited-own-fields-iterate-deferred-added": (7, 7),
    "inherited-own-fields-iterate-deferred-implicit": (7, 7),
    "inherited-own-fields-iterate-deferred-implicit-arguments": (7, 7),
    "inherited-own-fields-iterate-deferred-helper": (7, 7),
    "own-fields-branch-helper-falsy": (7, 7),
    "own-fields-branch-helper-nested": (7, 7),
    "own-fields-branch-helper-truthy": (14, 14),
    "own-fields-branch-helper-mixed": (7, 7),
    "own-fields-branch-helper-object": (7, 7),
    "own-fields-branch-helper-property": (7, 7),
}
OBSERVATIONS.update(DEFERRED)
OBSERVATIONS["bootstrap-base-button"] = (7, 7)
# The element helpers' only calls sit in constructor branches decided at
# compile time, so those calls are removed, the helpers are erased and the
# derived-class program lowers completely.
POSITIVES.update(
    {"own-fields-branch-helper-nested", "inherited-own-fields-iterate-deferred-helper"}
)
POSITIVES.update(
    {
        "inherited-own-fields-iterate-deferred-arm",
        "inherited-own-fields-iterate-deferred-loop",
        "inherited-own-fields-iterate-deferred-truthy",
        "inherited-own-fields-iterate-deferred-siblings",
        "inherited-own-fields-iterate-deferred-chain",
        "inherited-own-fields-iterate-deferred-added",
        "inherited-own-fields-iterate-deferred-implicit",
        "inherited",
        "inherited-own-fields-implicit",
        "own-fields-branch-helper-falsy",
        "own-fields-branch-helper-truthy",
    }
)
# BaseComponent's (B) own method still reaches Config's (W) throwing NAME
# getter, so this fixture prepares but native lowering still refuses that
# method (checked in main).
PREPARED_ONLY.add("inherited-static-getters-component-method")
POSITIVES.update(
    {
        "inherited-static-getters",
        "inherited-static-getters-shadow",
        "inherited-static-getters-component",
        "inherited-static-getters-shadow-both",
        "inherited-static-getters-shadow-method",
        "inherited-static-getters-shadow-method-both",
        "inherited-static-getters-shadow-method-chain",
        "static-call-this-method",
        "static-call-this-getter-chain",
    }
)
POSITIVES.update(
    {
        "own-fields-length",
        "own-fields-order",
        "own-fields-clear",
        "own-fields-effects",
        "own-fields-boxed-key",
        "own-fields-loop",
        "inherited-own-fields-loop",
    }
)


def check_nested_helper_root(args, source, manifest, name="nested-helper-root"):
    # Insert a ctjs.root only into the captured wrapper (the one helper that
    # loads an upvalue); constructor frames stay untouched. Ordinary helpers
    # keep their frames when their calls become direct calls.
    functions = re.compile(r"^  ctjs.func\b[^\n]*\n.*?^  }\n", re.M | re.S)
    text = source.read_text()
    helpers = [
        body
        for body in functions.findall(text)
        if "ctjs.load_upvalue " in body and "ctjs.set_property " not in body
    ]
    if len(helpers) != 1:
        raise RuntimeError("nested root control lost its unique captured wrapper")
    helper = helpers[0]
    symbol = re.search(r"ctjs.func\s+@([^\s(]+)", helper)[1]
    rooted, count = re.subn(
        r"(^[ \t]*)ctjs.frame_exit (%\w+)",
        r"\1%helper_root = ctjs.constant #ctjs.undefined\n"
        r"\1ctjs.root %helper_root in \2\n\g<0>",
        helper,
        flags=re.M,
    )
    if count != 1:
        raise RuntimeError("nested root control lost its single helper frame exit")
    path = args.work / f"{name}.mlir"
    path.write_text(text.replace(helper, rooted, 1))
    prepared = prepare(
        args,
        name,
        path,
        dict(manifest, module_sha256=host.fingerprint(args.opt, path)),
        success=True,
    )
    after = prepared.read_text()
    retained = [body for body in functions.findall(after) if f"@{symbol}(" in body.splitlines()[0]]
    if len(retained) != 1 or f"ctjs.call_direct @{symbol}(" not in after:
        raise RuntimeError("nested root control lost its direct helper identity")
    if any(
        retained[0].count(operation) != rooted.count(operation)
        for operation in ("ctjs.frame_enter", "ctjs.frame_exit", "ctjs.root ")
    ):
        raise RuntimeError("nested helper preparation discarded its live frame or root")
    return prepared


def check_borrowed_helper_inputs(args, prepared):
    text = prepared.read_text()
    symbol = re.search(r"ctjs.func private @(fn\$\d+)\(", text)[1]
    call = re.search(rf"(?m)^(\s*)%\w+ = ctjs.call_direct @{re.escape(symbol)}\(([^\n]+)\)$", text)
    if not call:
        raise RuntimeError("borrowed helper lost its private direct call")
    operands = call[2].split(", ")
    operands[3] = operands[0]  # An additional caller passes undefined, not an object.
    extra = f"{call[1]}%borrow_bad = ctjs.call_direct @{symbol}({', '.join(operands)})"
    variants = {
        "module-reference": text.replace(
            "module attributes {", f"module attributes {{test.borrow_ref = @{symbol}, ", 1
        ),
        "public-helper": text.replace(f"ctjs.func private @{symbol}(", f"ctjs.func @{symbol}(", 1),
        "mixed-helper-call": text[: call.end()] + "\n" + extra + text[call.end() :],
    }
    for label, changed in variants.items():
        source = args.work / f"borrow-{label}.mlir"
        source.write_text(changed)
        output = args.work / f"borrow-{label}.native.mlir"
        run([args.opt, str(source), "--ctnative-lower-to-emitc=optimize=false", "-o", str(output)])
        check_refusal(label, output.read_text(), len(FUNCTION.findall(changed)))
    return len(variants)


def check_captured_key_inputs(args, source, manifest):
    text = source.read_text()
    symbol = re.search(
        r"ctjs.func private @(class_map_record_constructor_dynamic_key\$\d+)\(", text
    )[1]
    call = re.search(r"ctjs.call_direct @" + re.escape(symbol) + r"\(([^\n]+)\)", text)
    if not call:
        raise RuntimeError("captured key control lost its original wrapper caller")
    operands = call[1].split(", ")
    wrong = list(operands)
    wrong[2] = operands[0]
    variants = {
        "module-reference": text.replace(
            "module attributes {", f"module attributes {{test.key_ref = @{symbol}, ", 1
        ),
        "public-wrapper": text.replace(f"ctjs.func private @{symbol}(", f"ctjs.func @{symbol}(", 1),
        "wrong-callee": text[: call.start()]
        + f"ctjs.call_direct @{symbol}({', '.join(wrong)})"
        + text[call.end() :],
    }
    # Also reference the helper from an op attribute inside a function: a
    # symbol use that is not a call, distinct from the module-attribute case.
    variants["operation-reference"], count = re.subn(
        r"(ctjs.constant #ctjs.undefined)(\n)", rf"\1 {{test.key_ref = @{symbol}}}\2", text, count=1
    )
    if count != 1 or any(changed == text for changed in variants.values()):
        raise RuntimeError("captured key mutation did not change its source")
    for label, changed in variants.items():
        path = args.work / f"captured-key-{label}.mlir"
        path.write_text(changed)
        prepare(
            args,
            f"captured-key-{label}",
            path,
            dict(manifest, module_sha256=host.fingerprint(args.opt, path)),
            success=False,
        )
    return len(variants)


def check_constructor_publication_inputs(args, source, manifest):
    text = source.read_text()
    bodies = re.findall(r"^  ctjs.func\b[^\n]*\n.*?^  }\n", text, re.M | re.S)
    captured = [body for body in bodies if "ctjs.load_upvalue " in body]
    if len(captured) != 2:
        raise RuntimeError("publication control lost its constructor or registration helper")
    for index, body in enumerate(captured):
        symbol = re.search(r"ctjs.func\s+(?:private\s+)?@([^\s(]+)", body)[1]
        changed = text.replace(
            "module attributes {", f"module attributes {{test.publication_ref = @{symbol}, ", 1
        )
        if changed == text:
            raise RuntimeError("publication symbol control did not change its source")
        name = f"constructor-publication-symbol-{index}"
        path = args.work / f"{name}.mlir"
        path.write_text(changed)
        prepare(
            args,
            name,
            path,
            dict(manifest, module_sha256=host.fingerprint(args.opt, path)),
            success=False,
        )
    return len(captured)


def check_record_map_inputs(args, prepared):
    text = prepared.read_text()
    keys = dict(re.findall(r'(%\w+) = ctjs.constant #ctjs.string<"([^"]*)">', text))
    methods = {
        result: (owner, keys.get(key))
        for result, owner, key in re.findall(r"(%\w+) = ctjs.get_property (%\w+)\[(%\w+)\]", text)
    }
    calls = list(re.finditer(r"(?m)^\s*(%\w+) = ctjs.call (%\w+)\(([^\n]+)\)$", text))
    get = next(call for call in calls if methods.get(call[2], (None, None))[1] == "get")
    put = next(call for call in calls if methods.get(call[2], (None, None))[1] == "set")
    payload = put[3].split(", ")[-1]
    key = put[3].split(", ")[1]
    variants = {
        "binding-replaced": text[: get.end()]
        + f'\n    ctjs.store_global "Map", {key}'
        + text[get.end() :],
        "alias-published": text[: get.end()]
        + f'\n    ctjs.store_global "saved", {get[1]}'
        + text[get.end() :],
        "missing-read": text[: get.start()]
        + '\n    %missing_key = ctjs.constant #ctjs.string<"missing">'
        + get[0].replace(get[3].split(", ")[-1], "%missing_key")
        + text[get.end() :],
        "mixed-payload": text[: put.start()]
        + put[0].replace(", " + payload + ")", ", " + put[3].split(", ")[1] + ")")
        + text[put.end() :],
    }
    forged = (
        text[: get.end()]
        + "\n    %sink_undefined = ctjs.constant #ctjs.undefined"
        + f"\n    %sink_result = ctjs.call_direct @record_alias_sink({get[1]}, "
        "%sink_undefined, %sink_undefined) {ctnative.receiver}" + text[get.end() :]
    )
    end = forged.rfind("}")
    variants["forged-receiver-publication"] = (
        forged[:end] + "  ctjs.func private @record_alias_sink(%self: !ctjs.value, "
        "%new_target: !ctjs.value, %callee: !ctjs.value) -> !ctjs.value "
        "attributes {ctnative.receiver, upvalue_count = 0 : i32} {\n"
        '    ctjs.store_global "saved", %self\n'
        "    %undefined = ctjs.constant #ctjs.undefined\n"
        "    ctjs.return %undefined\n  }\n" + forged[end:]
    )
    for label, changed in variants.items():
        # Forge the record-Map attributes on the construct: a mutated program
        # must be refused even when it carries the marks a successful proof
        # would leave.
        changed = re.sub(
            r"(?m)^(\s*%\w+ = ctjs.construct %\w+\(%\w+\))$",
            r"\1 {ctnative.map_records, ctnative.map_site}",
            changed,
            count=1,
        )
        source = args.work / f"record-map-{label}.mlir"
        source.write_text(changed)
        for optimize in (False, True):
            output = args.work / f"record-map-{label}.{optimize}.native.mlir"
            run(
                [
                    args.opt,
                    str(source),
                    f"--ctnative-lower-to-emitc=optimize={str(optimize).lower()}",
                    "-o",
                    str(output),
                ]
            )
            check_refusal(label, output.read_text(), len(FUNCTION.findall(changed)))
            if label == "binding-replaced" and (
                "standard Map binding is assigned in this program" not in output.read_text()
            ):
                raise RuntimeError(
                    "retained records bypassed the final standard Map identity proof"
                )


def untrusted_change(args, name, optimize, raw, native, functions, expected, reference=None):
    """Lower the unprepared import of `name` and return None when it does what
    UNTRUSTED_NATIVE says (a complete native program printing Node's value, or
    else a named refusal boundary), else one line saying what changed: the
    lowering failed, or how many functions became native and whether the
    complete program ran with Node's result."""
    listed = optimize in UNTRUSTED_NATIVE.get(name, ())
    label = f"{name}/{optimize}/untrusted"
    try:
        run(
            [
                args.opt,
                str(raw),
                "--ctjs-resolve-globals",
                "--ctjs-lift-to-scf",
                f"--ctnative-lower-to-emitc=optimize={str(optimize).lower()}",
                "-o",
                str(native),
            ]
        )
    except RuntimeError as error:
        return f"CHANGED {label}: lowering failed: {str(error).splitlines()[0]}"
    text = native.read_text()
    try:
        check_refusal(label, text, functions)
        return f"CHANGED {label}: listed native but kept its refusal boundary" if listed else None
    except RuntimeError:
        pass
    native_count = len(NATIVE.findall(text))
    refused = len(FUNCTION.findall(text))
    shape = f"{native_count}/{functions} native, {refused} refused"
    if refused or "emitc.func @main(" not in text:
        if refused and "emitc.func @main(" in text and optimize in UNTRUSTED_PARTIAL.get(name, ()):
            return None
        return f"CHANGED {label}: partial ({shape})"
    try:
        check_executable(args, f"{name}.{optimize}.untrusted", native, expected, reference)
        return None if listed else f"CHANGED {label}: complete ({shape}), runs with a={expected}"
    except Exception as error:
        return f"CHANGED {label}: complete ({shape}), run failed: {str(error).splitlines()[0]}"


def main():
    args = native_args(__doc__, "fixtures")
    vendor = Path(__file__).resolve().parents[6] / "ctbrowser/vendor/bootstrap/bootstrap.bundle.js"
    bootstrap = vendor.read_text()
    defaults = (args.fixtures / "static-defaults.js").read_text()
    for name in ("Default", "DefaultType"):
        body = f"        static get {name}() {{\n            return {{}}\n        }}"
        if body not in bootstrap or body not in defaults:
            raise RuntimeError(f"Bootstrap Config {name} getter source pin changed")
    # In the minified Bootstrap bundle, `W` is Config, `B extends W` is
    # BaseComponent, `U extends B` is Button and `H` is Manipulator.
    # Copy all of W, not just the Default getter this fixture reads: W's other
    # methods (including the throwing NAME getter) must still be analysed
    # separately from the call that the fixture makes.
    start = bootstrap.index("    class W {")
    end = bootstrap.index("    class B extends W {", start)
    (args.fixtures / "bootstrap-config-defaults.js").write_text(
        "function configDefaults() {\n"
        + bootstrap[start:end]
        + "\n    var instance = new W();\n    var result = W.Default;\n"
        "    result.n = 7;\n    return result.n;\n}\nvar a = configDefaults();\n"
    )
    # Copy Bootstrap's helper `r` into its own declaration, so the Config-only
    # fixture above keeps nothing but its own refusal.
    helper = bootstrap.split("        r = ", 1)[1].split(",\n        a = ", 1)[0]
    declaration = "const r = " + helper + ";\n"
    # Copy Config (W) and BaseComponent (B) with their helpers unchanged.
    # Constructing B with a null element takes B's own early exit after
    # super(), so no Manipulator (H) call has to be invented.
    element = bootstrap.split("        a = ", 1)[1].split(",\n        l = ", 1)[0]
    base_end = bootstrap.index("    const z = t => {", end)
    (args.fixtures / "bootstrap-base.js").write_text(
        "function bootstrapBase() {\n"
        + declaration
        + "const a = "
        + element
        + ";\n"
        + bootstrap[start:base_end]
        + "\n    var instance = new B(null, null);\n"
        "    return instance._element === void 0 ? 7 : 9;\n}\nvar a = bootstrapBase();\n"
    )
    # Prepend Bootstrap's shared Map and the full Data object to the base
    # fixture. Only the trailing ',' of the copied declaration becomes ';'.
    data = (
        bootstrap[
            bootstrap.index("    const t = new Map,") : bootstrap.index(
                '        i = "transitionend",'
            )
        ]
        .rstrip()
        .removesuffix(",")
        + ";\n"
    )
    for name in (
        "class-map-record-nested-vendor-holder",
        "class-map-record-nested-vendor-constructor",
        "class-map-record-nested-object-holder",
        "class-map-record-nested-object-distinct",
    ):
        if data not in (args.fixtures / f"{name}.js").read_text():
            raise RuntimeError(f"{name}: complete vendor Data declaration changed")
    (args.fixtures / "bootstrap-base-data.js").write_text(
        (args.fixtures / "bootstrap-base.js")
        .read_text()
        .replace("function bootstrapBase() {\n", "function bootstrapBase() {\n" + data, 1)
    )
    # Button (U) extends BaseComponent (B) and overrides NAME. B's constructor
    # reaches the shared Data map only when U calls super(), so the analysis
    # must take B's own-property snapshot after U's super call is normalized.
    button = bootstrap[
        bootstrap.index("    class U extends B {") : bootstrap.index(
            '    P.on(document, "click.bs.button.data-api"'
        )
    ]
    (args.fixtures / "bootstrap-base-button.js").write_text(
        (args.fixtures / "bootstrap-base-data.js")
        .read_text()
        .replace(
            "    var instance = new B(null, null);",
            button + "    var instance = new U(null, null);",
            1,
        )
    )
    (args.fixtures / "bootstrap-config-r-defaults.js").write_text(
        declaration + (args.fixtures / "bootstrap-config-defaults.js").read_text()
    )
    # Add Bootstrap's Manipulator object (H) and its value-normalization
    # helpers. Their presence alone must not let the class analysis assume DOM
    # access or callable holders. Copy all four H methods unchanged.
    helpers = bootstrap[bootstrap.index("    function M(t) {") : start]
    combined = (args.fixtures / "bootstrap-config-r-defaults.js").read_text()
    (args.fixtures / "bootstrap-config-r-h-defaults.js").write_text(
        combined.replace("var a = configDefaults();", helpers + "\nvar a = configDefaults();")
    )
    (args.fixtures / "bootstrap-r.js").write_text(
        declaration + "function probe() { class Shape { read(t) { return r(t); } } "
        "var instance = new Shape(); return instance.read(null) ? 9 : 7; } var a = probe();\n"
    )
    # CTCOMPILE_FIXTURES selects fixtures by name; the two checks outside the
    # fixture loop are named "@helper-guards" and "@constructed-methods".
    chosen = selected_fixtures([*OBSERVATIONS, "@helper-guards", "@constructed-methods"])
    helper_checked, helper_refused = (
        check_helper_guards(args, declaration) if "@helper-guards" in chosen else (0, 0)
    )
    # Each fixture runs in its own work directory, so the helpers' fixed file
    # names cannot collide, and the fixtures run side by side (run_parallel).
    # Its counts and changes are returned and summed in OBSERVATIONS order, so
    # the summary does not depend on scheduling.
    shared = args

    def check_fixture(name):
        node_expected, reference_expected = OBSERVATIONS[name]
        args = argparse.Namespace(**dict(vars(shared), work=shared.work / name))
        args.work.mkdir(parents=True, exist_ok=True)
        refusals = preparation_refusals = checked = prepared_refusals = 0
        cutoffs = {}
        key_counts = None
        # Whether this fixture's untrusted lowering no longer keeps the refusal
        # boundary, or its interpreter observation changed; every fixture's are
        # reported together at the end.
        untrusted_changes = []
        # The interpreter's whole output for this fixture, which its native
        # programs are compared with.
        vm_output = {}
        source = args.fixtures / f"{name}.js"
        node = run([args.node, "-e", NODE, str(source)], success=node_expected is not None)
        reference = run([args.reference, str(source)], success=reference_expected is not None)
        if node_expected is not None and (node.stdout != f"a={node_expected}\n" or node.stderr):
            raise RuntimeError(f"{name}: Node observation changed\n{node.stdout}{node.stderr}")
        reference_output = f"a={reference_expected}\n"
        if name == "static-global-effect":
            reference_output += "count=3\n"
        if name == "static-error-replaced":
            reference_output = "Error=9\n" + reference_output
        if name == "class-map-record-nested-conditional-dead-escape":
            reference_output += "retained=undefined\n"
        # A native program prints every global the interpreter prints, so it
        # is compared with the interpreter's whole output, not with `a=` alone.
        vm_output[name] = reference.stdout if reference_expected is not None else None
        if reference_expected is not None and reference.stdout != reference_output:
            # Reported with the untrusted changes; the rest of this fixture's
            # checks assume the old observation.
            change = (
                f"CHANGED {name}: interpreter prints {reference.stdout.strip()!r},"
                f" expected {reference_output.strip()!r}"
            )
            untrusted_changes.append(change)
            return (
                refusals,
                preparation_refusals,
                checked,
                prepared_refusals,
                cutoffs,
                untrusted_changes,
                key_counts,
            )
        raw = args.work / f"{name}.raw.mlir"
        imported = run([args.translate, "--ctbrowser-js-to-ctjs", str(source), "-o", str(raw)])
        text = raw.read_text()
        if "ctjs.skipped" in text or "is not compiled:" in imported.stderr:
            raise RuntimeError(f"{name}: importer skipped source functions")
        check_mutable_helper(text)
        functions = len(FUNCTION.findall(text))
        structured = args.work / f"{name}.structured.mlir"
        run(
            [
                args.opt,
                str(raw),
                "--ctjs-resolve-globals",
                "--ctjs-lift-to-scf",
                # Print these fixtures in generic MLIR form. With explicit
                # super guards, grouped scf results (%n#i) can end up in a
                # cf.switch default edge, which cf.switch's custom parser
                # rejects. The generic form keeps every op and operand.
                *(
                    ["--mlir-print-op-generic"]
                    if name.startswith(("inherited-", "override-", "bootstrap-base"))
                    or name.startswith("class-map-inherited")
                    or name.startswith("class-map-record-nested-inherited-")
                    or name
                    in (
                        "class-map-record-alias-method-inherited",
                        "class-map-record-constructor-inherited",
                        "class-map-record-alias-snapshot-inherited",
                        "class-map-record-nested-constructor-inherited",
                    )
                    else []
                ),
                "-o",
                str(structured),
            ]
        )
        manifest = dict(
            host.manifest(args.opt, structured),
            initial_intrinsics=["__ctbrowser_class_defined"],
        )
        if name.startswith("class-map-") or name in (
            "bootstrap-base-data",
            "bootstrap-base-button",
        ):
            manifest["initial_intrinsics"].append("Map")
        if name == "own-fields-boxed-key":
            lifted = structured.read_text()
            calls = set(re.findall(r"(%\w+) = ctjs.call ", lifted))
            scalar_reads = [
                result
                for result, owner in re.findall(
                    r"(%\w+) = ctjs.get_property (%\w+)\[%\w+\]", lifted
                )
                if owner in calls
            ]
            if not any(
                re.search(r"ctjs.cell_set %\w+, " + re.escape(value) + r"\b", lifted)
                for value in scalar_reads
            ):
                raise RuntimeError("own-field scalar snapshot lost its boxed local producer")
        if (
            name in OWN_FIELDS
            or name.startswith("class-map-record-alias-snapshot-")
            or name == "class-map-record-constructor-snapshot-dispose"
            or name == "class-map-inherited-early-snapshot"
            or name.startswith("own-fields-branch-")
            or name.startswith("inherited-own-fields-")
            or name.startswith("bootstrap-base")
            or name in OWN_ENTRIES
        ):
            manifest["initial_intrinsics"].append("Object")
        if (
            name
            in (
                "own-fields-loop",
                "inherited-own-fields-loop",
                "class-map-record-alias-snapshot-dispose",
                "class-map-record-constructor-snapshot-dispose",
            )
            or name.startswith(("inherited-own-fields-iterate-", "bootstrap-base"))
            or name in OWN_ENTRIES
        ):
            manifest["initial_intrinsics"] += [
                "Array",
                "__ctbrowser_for_of_open",
                "__ctbrowser_iter_next",
                "__ctbrowser_iter_close",
            ]
        if name.startswith(
            (
                "inherited",
                "override-",
                "bootstrap-base",
                "class-map-inherited",
                "class-map-record-nested-inherited-",
            )
        ) or name in (
            "class-map-record-alias-method-inherited",
            "class-map-record-alias-snapshot-inherited",
            "class-map-record-constructor-inherited",
            "class-map-record-nested-constructor-inherited",
        ):
            # Declare the class-runtime hooks the importer emits for
            # inheritance. They are mutable globals, so knowing them does not
            # prove what the super chain is.
            manifest["initial_intrinsics"] += [
                "__ctbrowser_class_heritage",
                "__ctbrowser_bind_this",
                "__ctbrowser_init_fields",
                "__ctbrowser_super_get",
            ]
        if name.startswith(
            (
                "static-throw-",
                "static-error-",
                "inherited-own-fields-iterate-throwing-arm",
                "inherited-static-getters-component",
                "inherited-static-getters-component-method",
                "inherited-static-getters-shadow-throwing",
            )
        ) or name in (
            "bootstrap-base",
            "bootstrap-base-data",
            "bootstrap-base-button",
            "bootstrap-config-defaults",
            "bootstrap-config-r-defaults",
            "bootstrap-config-r-h-defaults",
        ):
            manifest["initial_intrinsics"].append("Error")
        if name in (
            "method-increment-dispatch",
            "method-decrement-dispatch",
            "method-counter-ambient",
        ):
            if (
                "ctjs.binary_static add" not in text
                or "ctjs.binary_static add" not in structured.read_text()
            ):
                raise RuntimeError(f"{name}: import lost the static counter operation")
        if name in (
            "method-dispatch",
            "method-increment-dispatch",
            "method-decrement-dispatch",
            "receiver-default-dispatch",
            "global-holder-dispatch",
        ):
            for operation in (
                "scf.index_switch",
                "arith.index_castui",
                "arith.trunci",
                "ub.poison",
            ):
                if operation not in structured.read_text():
                    raise RuntimeError(f"method dispatch no longer exercises {operation}")
        diagnostic = {
            "class-map-record-missing": "class retained Map get requires a present record",
            "class-map-record-read-after-delete": "class retained Map get requires a present record",
            "class-map-record-mixed-scalar": "class retained Map requires one completed constructor family",
            "class-map-record-mixed-constructors": "class retained Map requires one completed constructor family",
            "class-map-record-computed-key": "class retained Map requires literal string keys",
            "class-map-record-alias-constructor": "class retained Map alias requires data field reads",
            "class-map-record-alias-shadow": "class method is observed or shadowed",
            "class-map-record-alias-constructor-write": "class retained Map alias requires data field reads",
            "class-map-record-alias-snapshot-write": "class own-key snapshot field set changes",
            "class-map-record-alias-snapshot-added": "class own-key snapshot field set changes",
            "class-map-record-alias-snapshot-method-added": "class own-key snapshot field set changes",
            "class-map-record-alias-snapshot-deleted": "class receiver escapes or observes a prototype/descriptor",
            "class-map-record-alias-snapshot-constructor-publication": "class own-key snapshot constructor observes its receiver",
            "inherited-own-fields-iterate-throwing-arm-subclass": "class own-key snapshot branches change ordered fields",
            "inherited-own-fields-iterate-throwing-arm-static-shadow": "class own-key snapshot branches change ordered fields",
            "inherited-own-fields-iterate-throwing-arm-returning": "class own-key snapshot branches change ordered fields",
            "inherited-own-fields-iterate-throwing-arm-escape-before": "class own-key snapshot constructor observes its receiver",
            "inherited-static-getters-shadow-extended": "derived class requires receiver-preserving super normalization",
            "own-fields-branch-decided-mixed": "class own-key snapshot branches change ordered fields",
            "inherited-own-fields-branch-decided-extended": "class own-key snapshot branches change ordered fields",
            "inherited-static-getters-shadow-throwing": "derived class requires receiver-preserving super normalization",
            "inherited-static-getters-base-reads-leaf": "static getter receiver lacks an exact local getter dependency",
            "static-call-this-detached": "static method escapes its exact local call",
            "static-call-this-foreign": "static method escapes its exact local call",
            "static-call-this-new-this": "static method receiver requires an exact own getter read",
            "inherited-own-fields-iterate-forward-missing": "class construction helper requires an existing own field",
            "inherited-own-fields-iterate-forward-recursive": "class construction helper proof exceeds its depth bound",
            "inherited-own-fields-iterate-forward-unused-effects": "unknown call, binding or reflective effect",
            "inherited-own-fields-iterate-borrow-missing": "class construction helper requires an existing own field",
            "inherited-own-fields-iterate-borrow-incompatible": "class construction helper requires an existing own field",
            "inherited-own-fields-iterate-method-missing": "class construction method requires an existing own field",
            "inherited-own-fields-iterate-method-add-field": "class construction method requires an existing own field",
            "inherited-own-fields-iterate-method-override-missing": "class construction method requires an existing own field",
            "inherited-own-fields-iterate-method-snapshot": "class construction method observes an own-key snapshot",
            "inherited-own-fields-iterate-method-inherited-snapshot": "class construction method observes an own-key snapshot",
            "inherited-own-fields-iterate-method-super-snapshot": "class construction method observes an own-key snapshot",
            "inherited-own-fields-iterate-method-inherited-early-snapshot": "class construction method observes an own-key snapshot",
            "inherited-own-fields-iterate-method-super-early-snapshot": "class construction method observes an own-key snapshot",
            "inherited-own-fields-iterate-method-recursive": "class construction method proof exceeds its depth bound",
            "inherited-own-fields-iterate-method-alias-escape-distinct": "class construction method receiver escapes its fixed fields",
            "inherited-own-fields-iterate-method-alias-escape": "class construction method receiver escapes its fixed fields",
            "inherited-own-fields-iterate-method-dead-ambient": "unknown call, binding or reflective effect",
            "inherited-own-fields-iterate-method-receiver-argument": "class receiver escapes or observes a prototype/descriptor",
            "inherited-post-super-holder-branches": "unknown call, binding or reflective effect",
            "own-fields-branch-order": "class own-key snapshot branches change ordered fields",
            "inherited-own-fields-branch-missing": "class own-key snapshot branches change ordered fields",
            "inherited-own-fields-branch-early": "super completion index is not proved",
            "own-fields-branch-observed": "class construction method observes an own-key snapshot",
            "own-fields-branch-loop": "class own-key snapshot requires fixed constructor fields",
            "inherited-own-fields-branch-before-super": "super condition is not a proved Boolean",
            "inherited-own-fields-branch-unused-ambient": "unknown call, binding or reflective effect",
            "inherited-own-fields-conditional": "class own-key snapshot branches change ordered fields",
            "inherited-own-fields-added": "inherited own-key snapshot requires the same ordered fields",
            "inherited-own-fields-empty-added": "inherited own-key snapshot requires the same ordered fields",
            "inherited-own-fields-grandchild-added": "inherited own-key snapshot requires the same ordered fields",
            "inherited-own-fields-sibling-added": "inherited own-key snapshot requires the same ordered fields",
            "inherited-own-fields-leaf-collision": "class own-key snapshot requires fixed constructor fields",
            "inherited-own-fields-before-store": "class construction method observes an own-key snapshot",
            "inherited-own-fields-ancestor-write": "class own-key snapshot field set changes",
            "own-fields-conditional": "class own-key snapshot branches change ordered fields",
            "own-fields-dynamic": "class own-key snapshot requires fixed constructor fields",
            "own-fields-new-field": "class own-key snapshot field set changes",
            "own-fields-numeric-key": "class own-key snapshot requires fixed constructor fields",
            "own-fields-snapshot-escape": "class own-key snapshot requires fixed length or index reads",
            "own-fields-unused-ambient": "unknown call, binding or reflective effect",
            "own-fields-replacement-return": "class own-key snapshot requires a primitive constructor return",
            "nested-helper-changing": "class local cell has changing writes",
            "nested-helper-identity": "captured helper escapes its ordinary local call",
            "nested-helper-excess": "captured helper escapes its ordinary local call",
            "nested-helper-receiver": "captured helper observes its implicit receiver or new.target",
            "nested-helper-effect": "unknown call, binding or reflective effect",
            "nested-helper-newtarget": "captured helper observes its implicit receiver or new.target",
            "nested-helper-recursive": "class local cell is observed before initialization",
            "inherited-helper-changing": "class local cell has changing writes",
            "inherited-helper-identity": "captured helper escapes its ordinary local call",
            "inherited-helper-effect": "unknown call, binding or reflective effect",
            "inherited-helper-receiver": "captured helper observes its implicit receiver or new.target",
            "inherited-helper-newtarget": "captured helper observes its implicit receiver or new.target",
            "captured-helper-replaced": "class local cell has changing writes",
            "inherited-helper-ambient": "unknown call, binding or reflective effect",
            "captured-helper-identity": "captured helper escapes its ordinary local call",
            "captured-helper-receiver": "captured helper observes its implicit receiver or new.target",
            "captured-helper-excess": "captured helper escapes its ordinary local call",
            "inherited-super-helper": "super method requires a proved capture-free linear base target",
            "inherited-explicit": "derived class requires receiver-preserving super normalization",
            "override-ambient": "unknown call, binding or reflective effect",
            "override-shadowed-receiver": "class method is observed or shadowed",
            "override-hidden-ancestor": "class method is observed or shadowed",
            "inherited-method-leaf-shadow": "class method is observed or shadowed",
            "inherited-method-base-shadow": "class method is observed or shadowed",
            "inherited-method-ambient": "unknown call, binding or reflective effect",
            "inherited-method-shadow": "class method is observed or shadowed",
            "bootstrap-base": 'unknown call, binding or reflective effect (global "H")',
            "bootstrap-base-data": "unknown call, binding or reflective effect (op ctjs.call)",
            "own-entries-default-type-fields": "static getter body is not a closed expression",
            "own-entries-default-type-primitive": ENTRIES_FRESH,
            "own-entries-default-type-written": ENTRIES_FRESH,
            "own-entries-default-type-escaped": ENTRIES_FRESH,
            "own-entries-argument": ENTRIES_FRESH,
            "own-entries-default-type-passed": ENTRIES_PATH,
            "own-entries-default-type-effect": ENTRIES_PATH,
            "bootstrap-base-button": "unknown call, binding or reflective effect (op ctjs.call)",
            "inherited-static-unread-leaf-read": "static method receiver requires an exact own getter read",
            "static-unread-dynamic": "static method receiver requires an exact own getter read",
            "static-unread-constructor-read": "static method receiver requires an exact own getter read",
            "helper-decided-live-result": 'unknown call, binding or reflective effect (global "ambient")',
            "helper-decided-other-call": 'unknown call, binding or reflective effect (global "ambient")',
            "inherited-own-fields-iterate-deferred-siblings-differ": "inherited own-key snapshot requires the same ordered fields",
            "inherited-own-fields-iterate-deferred-mixed": "class own-key snapshot branches change ordered fields",
            "inherited-own-fields-iterate-deferred-early-snapshot": "class construction method observes an own-key snapshot",
            "inherited-own-fields-iterate-deferred-implicit-arguments": "derived class requires receiver-preserving super normalization",
            "own-fields-branch-helper-mixed": "class own-key snapshot branches change ordered fields",
            "own-fields-branch-helper-object": "class own-key snapshot branches change ordered fields",
            "own-fields-branch-helper-property": "class own-key snapshot branches change ordered fields",
            "class-map-record-nested-nullable-raw-undefined": "class retained Map get requires a present record",
            "class-map-inherited-early-snapshot": "class construction method observes an own-key snapshot",
            "method-counter-ambient": "unknown call, binding or reflective effect",
            "method-dispatch-ambient": "unknown call, binding or reflective effect",
            "method-dispatch-shadow": "class method is observed or shadowed",
            "method-captured-class-ambient": "unknown call, binding or reflective effect",
            "method-throw-ambient": "unknown call, binding or reflective effect",
            "method-throw-object": "unknown call, binding or reflective effect",
            "method-throw-parameter": "unknown call, binding or reflective effect",
            "bootstrap-config-defaults": 'unknown call, binding or reflective effect (global "r")',
            "bootstrap-config-r-defaults": 'unknown call, binding or reflective effect (global "H")',
            "bootstrap-config-r-h-defaults": "unknown call, binding or reflective effect",
            "static-throw-ambient": "static getter body is not a closed expression",
            "static-throw-object": "static getter throw needs a literal or declared Error payload",
            "static-error-return": "declared Error payload escapes its throw",
            "static-error-replaced": "declared intrinsic binding is replaced by source",
            "static-error-coercion": "static getter throw needs a literal or declared Error payload",
            "static-error-method": "unknown call, binding or reflective effect",
            "instance-default-replacement": "primitive constructor return",
        }.get(name, "")
        if name in (
            "class-map-record-nested-conflict-reached",
            "class-map-record-nested-conflict-saved-child-conflict",
        ):
            diagnostic = "class nested Map conflict observer is reachable"
        prepared = prepare(
            args,
            name,
            structured,
            manifest,
            success=name in POSITIVES | PREPARED_ONLY,
            diagnostic=diagnostic,
        )
        preparation_refusals += name not in POSITIVES | PREPARED_ONLY
        if name == "class-map-record-constructor-dynamic-key":
            preparation_refusals += check_captured_key_inputs(args, structured, manifest)
        if name == "class-map-record-direct":
            check_record_map_inputs(args, prepared)
        if name in (
            "class-map-record-nested-direct",
            "class-map-record-nested-conditional-repeat",
            "class-map-record-nested-shortcircuit-repeat",
            "class-map-record-nested-captured-direct",
            "class-map-record-nested-constructor-direct",
            "class-map-record-nested-inherited-direct",
            "class-map-record-nested-cleanup-direct",
            "class-map-record-nested-nullable-direct",
            "class-map-record-nested-conflict-direct",
            "class-map-record-nested-vendor-holder",
            "class-map-record-nested-object-holder",
        ):
            prepare(
                args,
                name + "-stale",
                structured,
                dict(manifest, module_sha256="0" * 64),
                success=False,
            )
            preparation_refusals += 1
        if name == "class-map-record-nested-constructor-direct":
            preparation_refusals += check_constructor_publication_inputs(args, structured, manifest)
        if name.startswith("class-map-record-") and name in POSITIVES:
            # Normalizing super() removes the imported ReferenceError guards;
            # the inherited source still constructs its Map and the derived
            # class's record.
            constructions = (
                2
                if name
                in (
                    "class-map-record-alias-method-inherited",
                    "class-map-record-alias-snapshot-inherited",
                    "class-map-record-constructor-inherited",
                )
                else text.count("ctjs.construct")
            )
            if name.startswith("class-map-record-nested-"):
                # Preparation removes only the outer Map construction; every
                # record that owns a child stays.
                constructions -= 1
            if name in (
                "class-map-record-nested-conditional-repeat",
                "class-map-record-nested-shortcircuit-repeat",
            ):
                # The second registration's false branch never allocates its child.
                constructions -= 1
            # Expanded helper calls may allocate distinct children from one source site.
            constructions = {
                "class-map-record-nested-captured-direct": 3,
                "class-map-record-nested-captured-distinct": 4,
                "class-map-record-nested-captured-holder": 4,
                "class-map-record-nested-captured-shortcircuit": 2,
                "class-map-record-nested-constructor-direct": 6,
                "class-map-record-nested-constructor-holder": 4,
                "class-map-record-nested-constructor-shortcircuit": 3,
                "class-map-record-nested-constructor-inherited": 2,
                "class-map-record-nested-inherited-direct": 6,
                "class-map-record-nested-inherited-holder": 4,
                "class-map-record-nested-inherited-shortcircuit": 3,
                "class-map-record-nested-cleanup-direct": 6,
                "class-map-record-nested-cleanup-holder": 4,
                "class-map-record-nested-cleanup-shortcircuit": 4,
                "class-map-record-nested-nullable-direct": 6,
                "class-map-record-nested-nullable-holder": 4,
                "class-map-record-nested-nullable-constructor": 4,
                "class-map-record-nested-conflict-direct": 6,
                "class-map-record-nested-conflict-holder": 4,
                "class-map-record-nested-conflict-constructor": 4,
                "class-map-record-nested-vendor-holder": 4,
                "class-map-record-nested-vendor-constructor": 4,
                "class-map-record-nested-object-holder": 4,
                "class-map-record-nested-object-distinct": 5,
                "class-map-record-nested-object-changing-key": 4,
            }.get(name, constructions)
            if constructions != prepared.read_text().count("ctjs.construct"):
                raise RuntimeError(f"{name}: preparation discarded a record or Map construction")
        if name.startswith("class-map-") and name in POSITIVES | PREPARED_ONLY:
            prepare(
                args,
                name + "-no-map-identity",
                structured,
                without_intrinsic(manifest, "Map"),
                success=False,
            )
            preparation_refusals += 1
        if name.startswith("class-map-record-alias-snapshot-") and name in POSITIVES:
            prepare(
                args,
                name + "-no-object-identity",
                structured,
                without_intrinsic(manifest, "Object"),
                success=False,
                diagnostic="class own-key snapshot needs declared Object identity",
            )
            preparation_refusals += 1
        if name == "own-fields-loop":
            for identity in (
                "Array",
                "__ctbrowser_for_of_open",
                "__ctbrowser_iter_next",
                "__ctbrowser_iter_close",
            ):
                prepare(
                    args,
                    "loop-without-" + identity,
                    structured,
                    without_intrinsic(manifest, identity),
                    success=False,
                    diagnostic="class own-key loop requires original Array iterator identities",
                )
                preparation_refusals += 1
            original = structured.read_text()
            opened = re.search(
                r'^(\s*)(%\w+) = ctjs.load_global "__ctbrowser_for_of_open"$', original, re.M
            )
            frame = re.findall(r"(%\w+) = ctjs.frame_enter", original[: opened.start()])[-1]
            rooted = args.work / "own-loop-root.mlir"
            rooted.write_text(
                original[: opened.end()]
                + f"\n{opened[1]}ctjs.root {opened[2]} in {frame}"
                + original[opened.end() :]
            )
            prepare(
                args,
                "own-loop-root",
                rooted,
                dict(manifest, module_sha256=host.fingerprint(args.opt, rooted)),
                success=False,
                diagnostic="class own-key loop contains an unproved effect",
            )
            preparation_refusals += 1
        if name == "own-fields-order":
            prepare(
                args,
                "own-fields-no-object",
                structured,
                dict(manifest, initial_intrinsics=["__ctbrowser_class_defined"]),
                success=False,
                diagnostic="class own-key snapshot needs declared Object identity",
            )
            preparation_refusals += 1
        if name == "static-call-capture":
            cutoffs[name] = check_proof_inputs(args, structured, manifest, prepared, name)
            preparation_refusals += 4
            prepared = check_nested_helper_root(args, structured, manifest, "static-method-root")
        if name == "nested-helper-constructor":
            prepared = check_nested_helper_root(args, structured, manifest)
        if name == "class-map-helper-constructor":
            symbol = re.search(r"ctjs.func private @(fn\$\d+)\(", prepared.read_text())[1]
            source = args.work / "map-helper-symbol.mlir"
            source.write_text(
                structured.read_text().replace(
                    "module attributes {", f"module attributes {{test.helper_ref = @{symbol}, ", 1
                )
            )
            prepare(
                args,
                "map-helper-symbol",
                source,
                dict(manifest, module_sha256=host.fingerprint(args.opt, source)),
                success=False,
                diagnostic="class Map helper has an unproved symbol reference",
            )
            preparation_refusals += 1
            check_nested_helper_root(args, structured, manifest, "map-helper-root")
        if name in ("inherited-explicit", "class-map-record-nested-inherited-direct"):
            preparation_refusals += check_ancestry_inputs(args, structured, manifest)
            if name == "inherited-explicit":
                preparation_refusals += check_super_inputs(args, structured, manifest)
            check_super_roots(
                args,
                structured,
                manifest,
                "super constructor declarations, roots and global writes remain unsupported",
            )
            preparation_refusals += 1
        if name == "inherited-method":
            # Move the derived class's prototype ahead of the base's method
            # definitions: preparation must still insert inherited slots after
            # those definitions.
            text = structured.read_text()
            objects = re.findall(r'^ +%\w+ = "ctjs.create_object"\(\)[^\n]+\n', text, re.M)
            if len(objects) != 2:
                raise RuntimeError("inherited method control lost its two prototypes")
            moved = args.work / "inherited-method-hoisted.mlir"
            moved.write_text(
                text.replace(objects[1], "").replace(objects[0], objects[1] + objects[0])
            )
            prepared = prepare(
                args,
                "inherited-method-hoisted",
                moved,
                dict(manifest, module_sha256=host.fingerprint(args.opt, moved)),
                success=True,
            )
        if name == "bootstrap-r":
            key_counts = check_prototype_keys(args, prepared)
        if name == "global-holder-chain":
            for label, request in (
                ("root", {"roots": [{"binding": "H", "properties": ["read"]}]}),
                ("observation", {"observations": ["a", "H"]}),
            ):
                prepare(
                    args,
                    f"holder-host-{label}",
                    structured,
                    dict(manifest, **request),
                    success=False,
                    diagnostic="global callable holder is requested by the host",
                )
                preparation_refusals += 1
        if name in PREPARED_ONLY | GLOBAL_HOLDERS:
            before, after = structured.read_text(), prepared.read_text()
            if name.startswith("inherited-own-fields-iterate-borrow-") or name in (
                "override-different-leaves",
                "inherited-helper-order",
                "captured-holder-unused",
            ):
                operations = ()
            elif name in (
                "class-map-nested-holder",
                "class-map-helper-numeric-key",
                "captured-holder-receiver-escape",
            ):
                operations = ("ctjs.construct",)
            elif name == "class-map-inherited":
                # Normalizing super() removes the guard Error constructions.
                operations = ("ctjs.load_upvalue",)
            elif name in (
                "inherited-own-fields-iterate-throwing-arm",
                "inherited-own-fields-iterate-throwing-arm-own-getter",
            ):
                # Normalizing super() drops the guard's throw. The throwing
                # getter's throw stays, the dead branch keeps its writes, and
                # dispose clears only `n`.
                operations = ()
                if (
                    after.count("ctjs.throw") != 1
                    or '#ctjs.string<"NAME required">' not in after
                    or '#ctjs.string<"y">' not in after
                    or 'ctjs.load_global "Object"' in after
                ):
                    raise RuntimeError(f"{name}: dead arm or folded snapshot changed")
            elif name == "inherited-static-getters-component-method":
                # Normalizing super() removes the guard throws. Config's NAME
                # getter keeps its throw because BaseComponent's own method can
                # still reach it. Alert's specialized copy returns "alert".
                operations = ()
                if (
                    after.count("ctjs.throw") != 1
                    or '#ctjs.string<"NAME required">' not in after
                    or '#ctjs.string<"alert">' not in after
                ):
                    raise RuntimeError(f"{name}: receiver copy or base throw changed")
            elif name == "class-map-inherited-base-string":
                operations = ()
                if after.count("ctjs.construct") != 2 or "ctjs.load_upvalue" in after:
                    raise RuntimeError("base publication lost its Map or completed leaf owner")
            elif name.startswith("class-map-record-"):
                operations = ("ctjs.construct",)
                if before.count("ctjs.call ") != after.count("ctjs.call ") + 1:
                    raise RuntimeError("record Map preparation changed a source Map call")
            elif name.startswith("class-map-"):
                operations = ("ctjs.construct", "ctjs.load_upvalue")
            elif name in GLOBAL_HOLDERS:
                operations = ()
                for operation in ('ctjs.load_global "H"', 'ctjs.store_global "H"'):
                    if operation not in before or operation in after:
                        raise RuntimeError(f"{name}: preparation did not remove the proved holder")
                converted = before.count("ctjs.call ") - after.count("ctjs.call ") - 1
                if (
                    converted <= 0
                    or after.count("ctjs.call_direct")
                    != before.count("ctjs.call_direct") + converted
                ):
                    raise RuntimeError(f"{name}: holder calls did not become direct helpers")
            else:
                operations = (
                    ("ctjs.call_direct",)
                    if name in ("local-helper-order", "bootstrap-r")
                    else (
                        ("cf.switch", "ctjs.throw")
                        if name.startswith("method-")
                        else ("ctjs.throw",)
                    )
                )
            for operation in operations:
                if not before.count(operation) or before.count(operation) != after.count(operation):
                    raise RuntimeError(
                        f"{name}: preparation changed the original {operation} exits"
                    )
            # Preparation removes the two reads; unused key literals may remain.
            if name == "method-throw-default" and (
                before.count("ctjs.get_property") - after.count("ctjs.get_property") != 2
            ):
                raise RuntimeError("throwing method retained a constructor getter read")
            if name.startswith("static-throw-"):
                getters = set(re.findall(r"ctjs.func @(fn\$\d+)\(", before))
                calls = set(re.findall(r"ctjs.call_direct @(fn\$\d+)\(", after))
                if not getters or calls != getters or "ctjs.define_accessor" in after:
                    raise RuntimeError(
                        f"{name}: throwing getter lost its direct call or retained its accessor"
                    )
        if name == "static-throw-error":
            prepare(
                args,
                "undeclared-error",
                structured,
                dict(manifest, initial_intrinsics=["__ctbrowser_class_defined"]),
                success=False,
                diagnostic="static getter throw needs a literal or declared Error payload",
            )
            preparation_refusals += 1
        if name == "static-chain":
            preparation_refusals += check_getter_parent(args, structured, manifest, prepared)
        if name in (
            "inherited-own-fields-iterate-borrow-captured",
            "inherited-own-fields-iterate-forward-captured",
        ):
            prepared_refusals += check_borrowed_helper_inputs(args, prepared)
        if name == "method-captured-class-name":
            preparation_refusals += check_class_capture_inputs(args, structured, manifest)
        if name == "empty":
            check_overflow_input(args, structured)
            for label, control, options in (
                ("no-authority", dict(manifest, initial_intrinsics=[]), ""),
                ("stale", dict(manifest, module_sha256="0" * 64), ""),
                ("budget", manifest, "max-steps=0"),
            ):
                prepare(args, label, structured, control, success=False, options=options)
                preparation_refusals += 1
        if name in (
            "own-fields-order",
            "own-fields-loop",
            "nested-helper-chain",
            "captured-helper-constructor",
            "captured-holder-sibling",
            "captured-holder-unused-capture",
            "empty",
            "method",
            "method-chain-order",
            "method-loop",
            "method-dispatch",
            "method-increment-dispatch",
            "method-dispatch-throw",
            "receiver-default-dispatch",
            "method-constructor-order",
            "method-captured-class-name",
            "static-chain",
            "static-repeated",
            "static-forward-chain",
            "static-defaults-chain",
            "static-throw-chain",
            "class-map-record-overwrite",
            "class-map-record-constructor-direct",
            "class-map-record-constructor-helper",
            "class-map-record-alias-snapshot-overwrite-delete",
            "class-map-record-constructor-keyed-holder",
            "class-map-record-constructor-multislot-keyed",
            "class-map-record-constructor-dynamic-key",
            "class-map-record-nested-direct",
            "class-map-record-nested-conditional-repeat",
            "class-map-record-nested-shortcircuit-repeat",
            "class-map-record-nested-captured-direct",
            "class-map-record-nested-constructor-direct",
            "class-map-record-nested-inherited-direct",
            "class-map-record-nested-cleanup-direct",
            "class-map-record-nested-nullable-direct",
            "class-map-record-nested-conflict-direct",
            "class-map-record-nested-vendor-holder",
            "class-map-record-nested-object-holder",
            "local-helper-branches",
            "local-holder-arrow",
            "global-holder-chain",
        ):
            cutoffs[name] = check_proof_inputs(args, structured, manifest, prepared, name)
            preparation_refusals += 4
        if name in (
            "class-map-helper-constructor",
            "class-map-record-captured-helper-direct",
            "class-map-record-captured-holder-direct",
            "class-map-record-constructor-holder-direct",
            "class-map-helper-holder",
            "class-map-helper-chain",
            "class-map-helper-shared-callers",
            "class-map-direct",
            "class-map-mixed-method",
            "class-map-method-loop",
            "inherited-own-fields-iterate-forward-direct",
            "inherited-own-fields-iterate-forward-captured",
            "inherited-own-fields-iterate-borrow-direct",
            "inherited-own-fields-iterate-borrow-captured",
            "inherited-own-fields-iterate-borrow-holder",
            "inherited-own-fields-iterate-getter-direct",
            "inherited-own-fields-iterate-getter-chain",
            "inherited-own-fields-iterate-getter-fresh-empty",
            "inherited-own-fields-iterate-helper-captured",
            "inherited-own-fields-iterate-helper-nested",
            "inherited-own-fields-iterate-method-read",
            "inherited-own-fields-iterate-method-nested",
        ):
            cutoffs[name] = check_proof_budget(args, structured, manifest, prepared, name)
            preparation_refusals += 1
        if name in (
            "class-map-inherited-distinct",
            "class-map-inherited-chain",
            "class-map-inherited-mixed-captures",
            "class-map-record-constructor-inherited",
            "class-map-inherited-base-arithmetic",
            "inherited-own-fields-iterate-forward-inherited",
            "inherited-own-fields-iterate-borrow-inherited",
            "inherited-own-fields-iterate-method-nearest",
            "inherited-own-fields-iterate-helper-distinct",
            "inherited-post-super-holder-chain",
            "inherited-post-super-holder-order",
            "inherited-captured-holder-shared",
            "inherited-captured-holder-order",
            "inherited-own-fields-shared",
            "inherited-own-fields-branch-arguments",
            "inherited-branch-helper",
            "inherited-super-order",
            "inherited-helper-distinct",
            "inherited-nested-helper-distinct",
        ):
            check_super_roots(
                args,
                structured,
                manifest,
                (
                    "super constructor declarations, roots and global writes remain unsupported"
                    if name != "inherited-super-order"
                    else "super method target has unsupported control flow, roots or declarations"
                ),
                target="mix$4" if name == "inherited-super-order" else None,
            )
            cutoffs[name] = check_proof_budget(args, structured, manifest, prepared, name)
            preparation_refusals += 2
        for optimize in (False, True):
            native = args.work / f"{name}.{optimize}.untrusted.mlir"
            change = untrusted_change(
                args, name, optimize, raw, native, functions, node_expected, vm_output.get(name)
            )
            if change:
                untrusted_changes.append(change)
                continue
            if name == "helper-global-alias":
                check_mutable_helper(native.read_text())
            refusals += 1
            if name in POSITIVES | PREPARED_ONLY:
                native = args.work / f"{name}.{optimize}.trusted.mlir"
                run(
                    [
                        args.opt,
                        str(prepared),
                        f"--ctnative-lower-to-emitc=optimize={str(optimize).lower()}",
                        "-o",
                        str(native),
                    ]
                )
                if name == "inherited-static-getters-component-method":
                    # Alert and its specialized copy lower to native code.
                    # Only BaseComponent's own method and Config's throwing
                    # NAME getter are refused; no BaseComponent instance
                    # reaches them.
                    native_text = native.read_text()
                    refused = re.findall(r"ctjs.func (?:private )?@(\w+)\$", native_text)
                    # With optimization on, pruning unreachable private
                    # functions may drop both.
                    if (
                        sorted(refused) not in (["fn", "fn"], [] if optimize else None)
                        or len(REFUSAL.findall(native_text)) != len(refused)
                        or "emitc.func @main(" not in native_text
                        or not re.search(r"emitc.func @keyed_\d+\(", native_text)
                    ):
                        raise RuntimeError(f"{name}: receiver copy lost its native boundary")
                elif name in PREPARED_ONLY and not (optimize and name == "bootstrap-r"):
                    native_text = native.read_text()
                    check_refusal(name, native_text, len(FUNCTION.findall(prepared.read_text())))
                    key_type = (
                        "!ctnative.boxed"
                        if name == "class-map-optional-key-object"
                        else "!ctnative.opt"
                    )
                    if (
                        name.startswith("class-map-")
                        and not name.startswith("class-map-record-")
                        and f"!ctnative.map<{key_type}" not in native_text
                    ):
                        raise RuntimeError("class Map lost its key representation refusal")
                    if name == "inherited-helper-order" and (
                        "an object literal passed to a direct call as an argument"
                        not in native_text
                    ):
                        raise RuntimeError("helper mutation lost its argument ownership refusal")
                    if name == "captured-holder-unused" and (
                        "a method field: nothing calls it" not in native_text
                    ):
                        raise RuntimeError("unused holder method lost its invocation refusal")
                    if name == "captured-holder-receiver-escape" and (
                        "a method field: it passes `this` as an argument to another function"
                        not in native_text
                    ):
                        raise RuntimeError("holder receiver lost its argument escape refusal")
                    if len(re.findall(r"^\s*ctjs.throw ", native_text, re.M)) != len(
                        re.findall(r"^\s*ctjs.throw ", prepared.read_text(), re.M)
                    ):
                        raise RuntimeError(f"{name}: native refusal changed the source throw exits")
                    prepared_refusals += 1
                else:
                    checked += check_executable(
                        args, f"{name}.{optimize}", native, node_expected, vm_output.get(name)
                    )
        return (
            refusals,
            preparation_refusals,
            checked,
            prepared_refusals,
            cutoffs,
            untrusted_changes,
            key_counts,
        )

    results = run_parallel([name for name in chosen if name in OBSERVATIONS], check_fixture)
    refusals, preparation_refusals, checked, prepared_refusals = (
        sum(result[index] for result in results) for index in range(4)
    )
    cutoffs = {name: cutoff for result in results for name, cutoff in result[4].items()}
    untrusted_changes = [change for result in results for change in result[5]]
    key_checked, key_refused = next((result[6] for result in results if result[6]), (0, 0))
    if untrusted_changes:
        raise RuntimeError(
            f"{len(untrusted_changes)} class fixture result(s) changed:\n"
            + "\n".join(untrusted_changes)
        )
    plain_checked, plain_refused = (
        check_constructed_methods(args) if "@constructed-methods" in chosen else (0, 0)
    )
    print(
        f"class initialization controls: {len(results)} source observations, "
        f"{checked} native executions, {refusals} unprepared refusals, "
        f"{preparation_refusals} preparation refusals, first complete budgets {cutoffs}"
    )
    print(
        f"constructed method controls: {plain_checked} native executions, {plain_refused} refusals"
    )
    print(f"original r guards: {helper_checked} native executions, {helper_refused} refusals")
    print(f"prototype keys: {key_checked} native executions, {key_refused} refusals")
    print(
        f"prepared source controls: {prepared_refusals} native refusals with original calls/exits"
    )


if __name__ == "__main__":
    main()
