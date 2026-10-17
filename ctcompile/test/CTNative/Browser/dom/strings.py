#!/usr/bin/env python3
"""Check native lowering of optional-String DOM reads (getAttribute and friends).

Several cases are copied from Bootstrap's bundle (its F and H helpers; main()
first checks the vendored copy still contains them). Node runs the sources as
the oracle and the VM must agree; then the native C++ for both DOM providers,
built by GCC and Clang, must print the same values. Finally every refusal case,
the mutation checks in string/provenance.py, budget exhaustion and the branch
nesting limit are checked. --regexp-only runs only the regexp cases.
"""

from CTNative.Browser.dom.string.provenance import *


def main():
    vendor = dom.BOOTSTRAP
    if any(source not in vendor.read_text() for source in (BOOTSTRAP_F, BOOTSTRAP_H_WRITES)):
        raise RuntimeError("original Bootstrap F/H source changed")
    args = dom.parse_args(__doc__, oracles=True, switches=("regexp-only",))
    if not args.nm:
        raise RuntimeError("native DOM String gate requires nm")
    check_regexp_matching(args)
    if args.regexp_only:
        return
    number_conversion.check_oracles(args)
    uri.check_oracles(args)
    nullable_uri.check_oracles(args)
    boolean_values = [
        "true" if bit == "1" else "false" for _, bits in BOOLEAN_CASES.values() for bit in bits
    ]
    boolean_source = "".join(source for _, source, _ in BOOLEAN_SOURCES)
    boolean_oracle = boolean_source + BOOLEAN_DOUBLE + BOOLEAN_OBSERVATIONS
    oracle = args.work / "oracle.js"
    oracle.write_text(
        READ
        + SAVED
        + NAMES
        + WIDE
        + CAPTURE_SOURCE
        + ORACLE
        + boolean_oracle
        + "\n"
        + "\n".join(
            f"assert.equal(booleanObservation{i:03}, {value}); console.log(booleanObservation{i:03});"
            for i, value in enumerate(boolean_values)
        )
    )
    expected = run([args.node, str(oracle)]).stdout
    string_expected = "".join(
        ("0:\n" if name == "nested_control" else "null\n") + "0:\n3:610062\n2:c3a9\n"
        for name in ("read", "saved", *CAPTURE_RETURNS)
    )
    if expected != string_expected + "".join(value + "\n" for value in boolean_values):
        raise RuntimeError("source optional String observations were not completed")
    observed = args.work / "vm-oracle.js"
    observed.write_text(
        READ
        + SAVED
        + WIDE
        + CAPTURE_SOURCE
        + """
function makeElement(value) {
  return {
    hasAttribute(name) { return name === 'x' && value !== null; },
    getAttribute(name) { return name === 'DATA-State' || name === 'x' || name === 'data-bs-config' ? value : null; },
    setAttribute(name, text) { value = text; },
    removeAttribute(name) { value = null; }
  };
}
"""
        + "\n".join(
            f"var observation{i * 4 + j} = {entry}(makeElement({value}), makeElement('other'));"
            for i, entry in enumerate(("readAttribute", "savedAttribute", *CAPTURE_RETURNS))
            for j, value in enumerate(("null", "''", r"'a\0b'", r"'\u00e9'"))
        )
    )
    with observed.open("a") as stream:
        stream.write("\nvar nameBytes = readWideName({getAttribute(name) { return name; }});\n")
        stream.write(boolean_oracle)
    reference = run([args.reference, str(observed)]).stdout
    encoded = [f"booleanObservation{i:03}={value}\n" for i, value in enumerate(boolean_values)]
    encoded.append('nameBytes="%ED%A0%80"\n')
    for index, line in enumerate(expected.splitlines()[: 4 * (2 + len(CAPTURE_RETURNS))]):
        value = (
            "null"
            if line == "null"
            else '"' + quote_from_bytes(bytes.fromhex(line.split(":")[1])) + '"'
        )
        encoded.append(f"observation{index}={value}\n")
    if reference != "".join(sorted(encoded, key=lambda line: line.partition("=")[0])):
        raise RuntimeError(f"VM optional String observations disagree with Node: {reference}")
    compilers = args.compilers
    includes, libraries = dom.link_options(args)

    def prepare(row):
        name, source, count = row
        return (
            name,
            *dom.prepare(
                args,
                name,
                source,
                count,
                entry_name=(
                    ("host_refusal" if name == "host_local_function_copy" else "invalid")
                    if name in CAPTURE_RETURNS
                    else name
                    if name in HELPER_CASES or name in HOST_CASES
                    else None
                ),
            ),
        )

    prepared = dom.parallel(prepare, SOURCES + BOOLEAN_SOURCES)
    local_copy = (args.work / "host_local_function_copy.raw.mlir").read_text()
    assert local_copy.count('ctjs.store_global "host_refusal"') == 1, local_copy
    prepared.extend(number_conversion.prepare(args))
    prepared.extend(uri.prepare(args))
    prepared.extend(nullable_uri.prepare(args))
    # Lower every case for each provider, with and without optimization, in
    # both the explicit and deduced layouts; then link all of them into one
    # combined C++ client per configuration and compare its output with Node.
    combined = []
    for optimize in (False, True):

        def lower(task, optimize=optimize):
            label, name, ir, contract, provider = task
            native = dom.lower(
                args, ir, dict(contract, provider=provider), label, optimize=optimize
            )
            deduced = args.work / f"{label}.deduced.mlir"
            run([args.opt, str(native), "--ctnative-print-deduced", "-o", str(deduced)])
            return (name, "session" in provider), {"explicit": native, "deduced": deduced}

        modules = dict(
            dom.parallel(
                lower,
                [
                    (f"{name}-{'session' in provider}-{optimize}", name, ir, contract, provider)
                    for provider in dom.PROVIDERS
                    for name, ir, contract in prepared
                ],
            )
        )
        for layout in ("explicit", "deduced"):
            headers, bodies, runs = set(), [], []

            def emit(row):
                namespace, name, module = row
                return emitted(
                    args,
                    module,
                    namespace,
                    optional_read=name != "helper_branch" and name not in uri.CASES,
                    uri_call=name in uri.CASES or name in nullable_uri.CASES,
                )

            units = dom.parallel(
                emit,
                [
                    (f"{name}_{'session' if owned else 'free'}", name, layouts[layout])
                    for (name, owned), layouts in modules.items()
                ],
            )
            for (name, owned), (cpp, symbol) in zip(modules, units):
                namespace = f"{name}_{'session' if owned else 'free'}"
                # Hoist the includes and the runtime's defines (CTNATIVE_DOM,
                # CTNATIVE_ORDERED_MAPS), which must precede its include, out of
                # each unit before wrapping it in a namespace; sorted() puts
                # `#define` before `#include`. The deduced
                # layout's CTCOMPILE_PIN block stays in its unit, inside its own
                # #ifndef. Generated local helper names need not be globally unique.
                headers.update(re.findall(r"^#(?:include|define CTNATIVE_)[^\n]*", cpp, re.M))
                body = re.sub(r"^#(?:include|define CTNATIVE_)[^\n]*\n?", "", cpp, flags=re.M)
                bodies.append(f"namespace {namespace} {{\n{body}\n}}\n")
                entry = namespace + "::" + symbol
                if name in nullable_uri.CASES:
                    runs.append(nullable_uri.client(name, entry, owned))
                    continue
                if name in uri.CASES:
                    runs.append(uri.client(name, entry, owned))
                    continue
                if name in number_conversion.CASES:
                    runs.append(number_conversion.client(name, entry, owned))
                    continue
                if name in ("names", "wide") or name in BOOLEAN_CASES:
                    setup = (
                        f"{entry}_session session; auto & doc = session.document();"
                        if owned
                        else "atom_table atoms_owner; document doc{atoms_owner};"
                    )
                    call = "session.invoke(element)" if owned else entry + "(element)"
                    if name in BOOLEAN_CASES:
                        runs.append(
                            BOOLEAN_RUN.replace("@SETUP@", setup)
                            .replace("@CALL@", call)
                            .replace("@CHECKS@", BOOLEAN_CHECKS.get(name, ""))
                        )
                        continue
                    key = (
                        r'std::string_view{"a\0b", 3}'
                        if name == "names"
                        else r'std::string_view{"\xed\xa0\x80", 3}'
                    )
                    runs.append(
                        NAMES_RUN.replace("@SETUP@", setup)
                        .replace("@CALL@", call)
                        .replace("@NAME@", key)
                    )
                    continue
                saved = name == "saved"
                if owned:
                    checks = SESSION_SAVED_CHECKS if saved else SESSION_READ_CHECKS
                    target = "session.invoke"
                    client = SESSION_RUN.replace("@OWNER@", entry + "_session")
                else:
                    checks = FREE_SAVED_CHECKS if saved else FREE_READ_CHECKS
                    target = entry
                    client = FREE_RUN
                if name in CAPTURE_RETURNS:
                    checks = "" if owned else "assert(!invoke(foreign, element));"
                    if name == "nested_control" and not owned:
                        checks = 'assert(invoke(foreign, element) == "");'
                    key = "data-bs-config" if name == "helper_regex" else "x"
                    fallback = ", true" if name == "nested_control" else ""
                    client = client.replace(
                        "check_values(invoke, doc, element, @SAVED@)",
                        f'check_values(invoke, doc, element, false, "{key}"{fallback})',
                    )
                call = (
                    f"return {target}(first, second);"
                    if saved
                    else f"(void)second; return {target}(first);"
                )
                runs.append(
                    client.replace("@CHECKS@", checks)
                    .replace("@ENTRY@", entry)
                    .replace("@INVOKE@", call)
                    .replace("@SAVED@", "true" if saved else "false")
                )
            path = args.work / f"combined-{optimize}-{layout}.cpp"
            path.write_text(
                "\n".join(sorted(headers))
                + "\n"
                + "\n".join(bodies)
                + number_conversion.CLIENT
                + nullable_uri.CLIENT
                + CLIENT.replace("@RUNS@", "\n".join(runs))
            )
            combined.append(path)
    # The eight combined programs (two policies, two layouts, two compilers)
    # build and run side by side.
    flags = [*FLAGS, *includes]

    def build(case):
        _, path, index, compiler = case
        binary = path.with_suffix(f".{index}")
        return build_and_run(
            compiler, path, binary, flags=flags, libraries=libraries, nm=args.nm, vm=dom.VM
        )

    builds = [
        (f"{path.stem}.{index}", path, index, compiler)
        for path in combined
        for index, compiler in enumerate(compilers)
    ]
    observations = expected + number_conversion.EXPECTED + uri.EXPECTED + nullable_uri.EXPECTED
    for result in dom.parallel(build, builds):
        if result.stdout != observations * 2:
            raise RuntimeError("native optional String observations disagree with source")

    def refuse(row):
        name, body = row
        ir, contract = dom.prepare(
            args,
            name,
            f"function invalid(element) {{ {body} }}\n",
            1,
            entry_name="invalid" if name in HELPER_REFUSALS else None,
        )
        for owned in (False, True):
            provider = "ctbrowser-dom-session-v1" if owned else "ctbrowser-dom-v1"
            dom.refuse(args, ir, dict(contract, provider=provider), f"{name}-{owned}", "DOM")

    def refuse_host(row):
        name, source = row
        ir, contract = dom.prepare(
            args, name, "{\n" + source + "\n}\n", 1, entry_name="host_refusal"
        )
        if name == "host_duplicate_export":
            raw = (args.work / f"{name}.raw.mlir").read_text()
            assert raw.count('ctjs.store_global "host_refusal"') == 2, raw
        for provider in dom.PROVIDERS:
            dom.refuse(args, ir, dict(contract, provider=provider), f"{name}-{provider}", "DOM")

    dom.parallel(refuse, REFUSALS.items())
    dom.parallel(refuse_host, HOST_REFUSALS.items())
    _, completion_ir, completion_contract = next(
        row for row in prepared if row[0] == "helper_branch_completion_dispatch"
    )
    completion_checks = completion_provenance_checks(args, completion_ir, completion_contract)
    _, branch_ir, branch_contract = next(row for row in prepared if row[0] == "branch_nested")
    dom.exhaust(
        args,
        branch_ir,
        branch_contract,
        "branch",
        (0, 1, 32),
        "branch budget {budget}: wrong refusal",
    )
    _, helper_branch_ir, helper_branch_contract = next(
        row for row in prepared if row[0] == "helper_branch_nested"
    )
    dom.exhaust(
        args,
        helper_branch_ir,
        helper_branch_contract,
        "helper-branch",
        (0, 1, 32),
        "helper branch budget {budget}: wrong refusal",
    )
    # Branch nesting in a DOM entry or helper is limited to 63 levels: depth 63
    # must lower and depth 64 must be refused with a branch-depth reason.
    for depth in (63, 64):
        source = (
            "function branchDepth(element) {"
            + "if (element.hasAttribute('x')) {" * depth
            + "element.setAttribute('marker', 'yes');"
            + "}" * depth
            + "return true;}"
        )
        for kind in ("entry", "helper"):
            branch_source = (
                source
                if kind == "entry"
                else "function branchHelper(element) {" + source + "return branchDepth(element); }"
            )
            ir, contract = dom.prepare(
                args,
                f"{kind}-branch-depth-{depth}",
                branch_source,
                1,
                entry_name="branchDepth" if kind == "entry" else "branchHelper",
            )
            for provider in dom.PROVIDERS:
                for optimize in (False, True):
                    result = dom.lower(
                        args,
                        ir,
                        dict(contract, provider=provider),
                        f"{kind}-branch-depth-{depth}-{provider}-{optimize}",
                        optimize=optimize,
                        success=depth == 63,
                    )
                    expected = (
                        "DOM entry branch depth" if kind == "entry" else "DOM helper branch depth"
                    )
                    if depth == 64 and expected not in result:
                        raise RuntimeError(f"{kind} branch depth: wrong refusal\n{result}")
    _, helper_ir, helper_contract = next(row for row in prepared if row[0] == "helper_nested")
    dom.exhaust(
        args,
        helper_ir,
        helper_contract,
        "helper",
        (0, 1, 32),
        "helper budget {budget}: missing bounded refusal",
    )
    stale = dict(helper_contract, module_sha256="0" * 64)
    if "fingerprint mismatch" not in dom.lower(
        args, helper_ir, stale, "helper-stale", success=False
    ):
        raise RuntimeError("DOM helper preparation accepted a stale source fingerprint")
    _, helper_ir, helper_contract = next(row for row in prepared if row[0] == "helper_read")
    provenance_checks = helper_provenance_refusals(args, helper_ir, helper_contract)
    _, method_ir, method_contract = next(
        row for row in prepared if row[0] == "helper_object_method"
    )
    method_checks = method_provenance_checks(args, method_ir, method_contract)
    _, capture_ir, capture_contract = next(
        row for row in prepared if row[0] == "helper_capture_key"
    )
    _, graph_ir, graph_contract = next(
        row for row in prepared if row[0] == "helper_capture_callable"
    )
    capture_checks = capture_provenance_checks(
        args, capture_ir, capture_contract, graph_ir, graph_contract
    )
    _, forwarded_ir, forwarded_contract = next(
        row for row in prepared if row[0] == "capture_forwarded"
    )
    capture_checks += forwarded_provenance_checks(args, forwarded_ir, forwarded_contract)
    _, host_ir, host_contract = next(row for row in prepared if row[0] == "host_capture_key")
    capture_checks += host_provenance_checks(args, host_ir, host_contract)
    _, factory_ir, factory_contract = next(row for row in prepared if row[0] == "host_factory_key")
    capture_checks += factory_provenance_checks(args, factory_ir, factory_contract)
    _, table_ir, table_contract = next(
        row for row in prepared if row[0] == "host_factory_table_key"
    )
    capture_checks += factory_provenance_checks(args, table_ir, table_contract, table=True)
    _, regexp_ir, regexp_contract = next(
        row for row in prepared if row[0] == "helper_regex_original"
    )
    replacement_checks = regexp_provenance_checks(args, regexp_ir, regexp_contract)
    for prefix, source in (
        ("replacement-mixed", REGEXP_MIXED_SOURCE),
        ("replacement-captured", REGEXP_CAPTURED_SOURCE),
    ):
        ir, contract = dom.prepare(args, prefix, source, 1, entry_name="invalid")
        for provider in dom.PROVIDERS:
            for optimize in (False, True):
                label = f"{prefix}-{provider}-{optimize}"
                native = dom.lower(
                    args,
                    ir,
                    dict(contract, provider=provider),
                    label,
                    optimize=optimize,
                )
                emitted(args, native, label)
        replacement_checks += 4
        replacement_checks += regexp_provenance_checks(args, ir, contract, prefix=prefix)
    numeric_refusals = number_conversion.refusal_checks(args, prepared)
    uri_refusals = uri.refusal_checks(args, prepared)
    nullable_uri_refusals = nullable_uri.refusal_checks(args, prepared)
    print(
        f"native DOM Strings: {9 + 4 * len(CAPTURE_RETURNS) + len(boolean_values) + len(number_conversion.RESULTS) + len(uri.RESULTS) + len(nullable_uri.RESULTS)} Node/VM observations, 8 GCC/Clang binaries, "
        f"both providers/policies/layouts; {(len(REFUSALS) + len(HOST_REFUSALS)) * 4 + numeric_refusals + uri_refusals + nullable_uri_refusals} source refusal checks, "
        f"{provenance_checks} provenance/depth refusal checks, {method_checks} method provenance checks, "
        f"{capture_checks} capture provenance/budget checks; "
        f"{replacement_checks} replacement provenance/budget checks; 22 branch depth/budget checks; "
        f"{completion_checks} completion provenance/budget checks"
    )


if __name__ == "__main__":
    main()
