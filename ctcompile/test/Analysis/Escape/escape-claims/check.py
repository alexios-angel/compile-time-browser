#!/usr/bin/env python3
"""THE ESCAPE ORACLE, CLOSED - Phase 55A checked by Phase 55O.

Three steps over ONE corpus, and steps 1 and 2 never meet until step 3:

  1. the INTERPRETER runs the corpus with the recorder installed and, at every
     frame pop, asks its own collector whether each object the frame allocated
     is still reachable            (ctcompile-test-type-oracle --script)
  2. the COMPILER imports the same corpus, runs EscapeAnalysis, and writes one
     claim per allocation site     (ctcompile-test-escape-claims)
  3. tools/check/escape-oracle.py compares the two.

ZERO SOUNDNESS VIOLATIONS IS THE GATE: a site claimed `confined` that the
collector found reachable after its frame returned is a defect, and the
checker names it. Precision is printed, never gated. The vacuous passes are
guarded: something must have been observed, and - on the fixture, which calls
every one of its confined sites - at least one confined claim must have been
proved SOUND, since an analysis that says `escapes` everywhere is right the way
a stopped clock is. On a corpus under --script an eighth of the bundle runs and
none of its confined sites is in a function that ever runs, so "sound > 0"
would gate on execution coverage rather than the analysis.

The claims emitter's own census lines are cross-checked for completeness
(every Stored claim has a classified first-witness target, every live read a
provenance record, every function a classification); these are coverage and
link-integrity gates, never precision counts or new confinement claims.

--strict (the fixture) adds: partial and pending both zero; the checker's dump
against expected.txt through check-dump.py; the inherited-accessor programs
(inherited-*.js, each in a fresh runtime context) whose child argument must
stay Stored after deletion; and the SHA-256 pins on the fixture functions whose
measured bytecode coordinates the snapshot names - a pinned body that changed
must have its evidence remeasured, not blessed.
"""

import argparse
import hashlib
import re
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent

# Every String/BigInt producer, mixed comparison and promoted historical body,
# pinned independently; retention never authorizes a comparison value. The
# five exception bodies are VM-allocated errors (signed shift, Div and Mod zero
# divisors, exact negative and VM-capped Pow exponents) whose bytecode
# coordinate is measured separately from every literal site's compiler claim.
PINS = {
    "objectFrameBigIntShiftEarly": "ad0d55f50570016466a7d91649e25508e777398c8d4e4255592acd1472fcb9eb",
    "objectFrameBigIntDivModDivEarly": "6c68903dc496b7644562597929ae68c21e34728e9b17c4229aac42eafab23f48",
    "objectFrameBigIntDivModModEarly": "cd95b19bbca5f3678ae266a056844bf2abf70802499fbf9841992f697e231b3d",
    "objectFrameBigIntPowNegativeEarly": "bcd270c0edaec69b4a00bf1b8b615094ee15efb18bc1ed2056ad503edc9d2258",
    "objectFrameBigIntPowCapEarly": "c7bb794736cdd22418caaafe2200e405ea6c174e8d0210f0f2883280e060fad1",
    "denseLengthChanged": "1e78b3b9807649f47a64219b19fd901c7fd3e02c92bf5045945edf6a6ee1d1e6",
    "denseLengthIndexed": "4c3c2e02a61b47826cf710db4c6c8db697ce51f68c2d2c613add016962377c1c",
    "denseIndexSaved": "8079e162d26f322849875baadb33c1bdb2eb96ae0e71d4556efa36bfa10b7ffc",
    "denseIndexLoaded": "b39f30b6bd61526fc624ac8e8c2b154d92b88aa5abf284dd753b060ecb8ade3a",
    "denseIndexPaths": "7e6967e5788ecfb93e86c39a23da9066084147836a9c24a0a714f5186a18c83a",
    "denseIndexStringOffset": "c0df7840d662042de1d6d7303ae4a24e896a4ba988979b080312c96181c0faeb",
    "denseIndexChain": "36147bc7c02a94f30077e3af3b4ab41bb117d3080951eb704997afc250c48332",
    "radixBigIntReleased": "97e1f7e347f64d37d20671f08e5b81be7294557464528edfeb8beb67e974d7f7",
    "radixBigIntSaved": "e7c7753b23f82e5cd24eeaf0e213c166ba1c0202f53180a274702480c1ea4370",
    "radixBigIntLoaded": "5edea408be7cd912c643ad285bb4e92049fc0008535399436b8c7a4aba18926b",
    "radixBigIntString": "72166baa7515a0c1586913b240e15b19d0995c8047d1b2d6fcf9036ce8a36172",
    "radixBigIntComputed": "3c98ea19db55e09fd116befca52ad159521d7c46e5a6cfc18a44935392d95051",
    "decimalBigIntReleased": "914279663a7bc5e6287ce35cda705f0bb77ca7d22cca1ce55a43ab1f48c225be",
    "decimalBigIntSaved": "50e976dcfe248e24f2b0df3b9ea14ee72ba93de3c50a975f7b8d268adcba1aab",
    "decimalBigIntSecond": "b49d40863cd7a70a06c461a01191075846abcda76d5dcf89a57d9edec8b67b30",
    "decimalBigIntLoaded": "a16025cf0989ac816c72e1c9dc4c2ddec4dadaa389cd3534b73ebecd643e7d6a",
    "decimalBigIntNegative": "0d9838a9606e12f668a56a97673d3381c6ca83d22a893337b9158a4ccc9afd78",
    "canonicalStringReleased": "24e22951addc7eb5216762324681c87e1b3769a7f9c420b78fd3a2713d109286",
    "canonicalStringSaved": "4733c444cdf812643b140317e78ab082e667f1ea798f1225e4766eb5e455a5c2",
    "canonicalStringLookalike": "251bc9aa4ed08015532045c04cfefcf8ecf58dd9d2380fc68bf73ce3100a3234",
    "canonicalStringLoaded": "8ee79dc39e35973d6a745ad00f533cfefc1292131c8ec8c8fdc98b0c09fe2230",
    "primitiveMixedAddEarly": "fe25b7a99e32b3ce5c6509779c227c72a03925414a060ef3382735f353f2f038",
    "primitiveMixedAddRetained": "89c4002320aa174921bc9cc64b5315fc6368a3c037574414abd4bfabecf52e46",
    "primitiveMixedAddOpaque": "a41e97b69d6dc2cdb44682b9dd376f577983a1d79d0208736199646d7689017f",
    "primitiveMixedStaticEarly": "7e83a0d3796792b7b773a61337bf8b6a1b6de47c6a67946ed2dab1a3caad69d6",
    "primitiveMixedStaticRetained": "65b3a4defa77686a34062fd0afddfde4ec0c93bb1169d5b41ea93e453283fe5c",
    "primitiveMixedStaticOpaque": "551d5fddaa481986e811432530bad60f2f5f8947a510fb103c9f003ad913ae22",
    "primitiveUShrEarly": "afb11ce0d3a043dda6e6a150c33c2850de81f77269f8a7e519b6da8b4e291063",
    "primitiveUShrRetained": "1138c74617b2497738b48d1cb980fa5cfa56aa2b9ffa100d0e318989014ff0bf",
    "primitiveUShrOpaque": "c4ebfb2bb1e31be64214d53da3ffa89b10d362d084471981a6a2491f8f305aae",
    "primitiveMixedModOpaque": "a3ed23568ed66f6f1975ede1e7175a8fadf0454fb32ca79273a4785833f6cecd",
    "primitiveMixedModRetained": "7b9399eabda22f9067c5b178bf2ef6051c32604e98d77a407d9e57eadc4f6ec7",
    "primitiveMixedModEarly": "3819c8237d30be90232ab71288c553a5662f66231f9d0939c1b20212bb752cf7",
    "primitiveMixedDivEarly": "e39cf16b5597d88cea3d89ec61baa8807a0bfa34616c7ddec72e905a09ed7c57",
    "primitiveMixedDivRetained": "5b6048cca0395f437c99bbdfcc51634700149658b24d104ac094ff51512d093f",
    "primitiveMixedDivOpaque": "429af3d520aaf67bdb16f78260a32ab61f354c75750aaf75b0731e3f41dadb16",
    "primitiveMixedMulEarly": "dae55b8bbc2882f08286ab4b44166491ef05a57a60748ce7a9e235e055fea62c",
    "primitiveMixedMulRetained": "0c2db9da890fa70d87e1a6cb3c872b5fbefedb4135b78a65cc9c5284b3d17933",
    "primitiveMixedMulOpaque": "2dc20f97857966611bfc4f0e58dbe72b6c92adfaab04cacca7c69ae10dc8b6a7",
    "primitiveMixedSubEarly": "29a6d5a4a64bfebbd6df2c925df3cd2d7e31695a2e3374c4ebf27a158d21b937",
    "primitiveMixedSubRetained": "e9239bbdcdef56973b2600ef1e1fdb3af3633ed68648712bfb1cbc8cc17477df",
    "primitiveMixedSubOpaque": "5e671465270d8463afca298605711d61a7226ddf17cdfbb73b1020551abffb81",
    "primitivePlusEarly": "0fb234080a5880d0e70dc249dcf1869f84a109a7abb8e37351352d9b0351f245",
    "primitivePlusRetained": "6664a92269834150087224c4baf22d3a9ecba6f48a382c3a50749a16da8110b8",
    "primitivePlusOpaque": "d1eb05647230b75b09975d3c6cfc4c9208517ba4ffd5c0f2a35f8f2696486e5a",
    "objectFrameStringBigIntSaved": "1a2e7c0a1d804774b0b15821ead2780151793b5918012672851be9e951d0a643",
    "objectFrameStringBigIntPaths": "2f6a573189564b54d00efe2fb1afea57eab74ccfc50a17ce380a644acf5dec93",
    "objectFrameStringBigIntTemplate": "8152fe7cabe9273f204b3633509dc864cd963802cbc94ac488c13ee5f7f1e01d",
    "objectFrameStringBigIntOpaqueAdd": "988ba7f5c919a7180ef0a6e46ca1c1fcc82d07815a4262982b888134b960795d",
    "objectFrameStringBigIntOpaqueTemplate": "2aa2a84199c6d7fed02a84a21a7fc2aba8b8ee685244e1679a6fcd582678d2d3",
    "objectFrameStringBigIntObject": "3d812b5dd00d72d0b950e2c4441d4065d97f70b37c47094175ad7b269ffb47fe",
    "objectFrameStringBigIntMixed": "c3691179849e139b98c55136d5687fccc8a4b1730864643891a54975668df002",
    "objectFrameStringBigIntRetained": "ed73af10f5a8c04372701a3e4f9f0782fcde5146f8135fad8f6cb3a1ea1aabc9",
    "objectFrameBigIntMixedSaved": "1fdd344a6e0e72da4279670fa28bbc2bf01d227176cbf67eea1a406437a922fc",
    "objectFrameBigIntMixedPrimitives": "42b39c5caaff02c34a30b8ea0cd6232790e9468b2fe26f6ef356f323c58dd3f9",
    "objectFrameBigIntMixedPaths": "98a4e6d61422325a3cd0483c7db32381f998b71185345927ee39957798491e75",
    "objectFrameBigIntMixedNumbers": "689caa156e121e5e5b010b43b76e2d6accc9ea50326c0ccc2f1d7d07c0abbc6c",
    "objectFrameBigIntMixedOpaque": "679980f4705d49587e08cf035757f00ac40a967ce47102f184526ca2beb07765",
    "objectFrameBigIntMixedObject": "fe967af83bf43781bca1ed9acf1167af97af95e365420fe49ed0035ec4b7a6f2",
    "objectFrameBigIntMixedRetained": "e54918b880a33bf0b1b8590756abacf2ff97b5f728a63e59b5aac5f98781c919",
    "objectFrameBigIntMixedStrings": "867a839a1d93289d794c717586421bffe4df5e940476b24273d6b8e5be2ad084",
    "objectFrameLooseEqualityBigInt": "9b12443c29cecfadbd38c6510e4e19c52c1b7dd1175244627f79064b13bd533e",
    "objectFrameBigIntEqualityMixed": "a25b4876302f65466ca10f08322c8d569cbdf3ef52599089f8541d61258d77e9",
    "objectFrameBigIntRelationalMixed": "4cef5bd507efbf6846fd5a51603f3837b6653fa1e00fd15905d8f2f06a9cc2ed",
    "objectFrameBigIntUnaryMixed": "d8d998eb9b7942aede14b87d3ced8bac62defc5a6c995e3e5e7923c01c210356",
    "objectFrameBigIntBinaryMixed": "4475cd2c7b523fa002af90325ad4e93b54a869aac109e6a892af6771b528296f",
    "objectFrameBigIntStaticMixed": "05624d5cfeeecca9f3c778f70e896bfa6f7a4afb987495ce40ccbf013fba12c0",
    "objectFrameBigIntShiftMixed": "b2cf97831f8a1418cb99a90ddb673f9b1bd2c31bb6c4a8cec78546b04021e552",
    "objectFrameBigIntDivModMixed": "c57d6600f4b0265c68ae63fdd7144b7000414e09ab05d6eff0237ccd97841744",
    "objectFrameBigIntPowMixed": "62f6b50fc5fb2588c6e0d4bb98e051e5737cb5cba28df1a93cc36cc427277054",
}

INHERITED = {
    "getter": ["readInherited pc1 escapes:passed"],
    "setter": ["writeInherited pc2 escapes:stored", "writeInherited pc4 escapes:passed"],
    "iteration": ["iterateInherited pc1 escapes:passed"],
}


def run(command):
    return subprocess.run(command, capture_output=True, text=True)


def numbers(pattern, text, what):
    found = re.search(pattern, text)
    if not found:
        sys.exit(f"the claims emitter did not report {what}:\n{text}")
    return [int(n) for n in found.groups()]


def check_inherited_accessors(args):
    """Each prototype mutation gets a fresh runtime context. Rows are joined by
    the program/function/pc coordinate, not by allocation order: the child
    argument must stay Stored after deletion, and receiver sinks alone do not
    prevent an unsound contents proof from clearing that claim."""
    for case, expected in INHERITED.items():
        source = HERE / f"inherited-{case}.js"
        rec = args.work / f"escape-inherited-{case}.rec"
        claims = args.work / f"escape-inherited-{case}.claims"
        recorded = run(
            [args.oracle, "--script", str(source), "--out", str(rec), "--escape-budget", "0"]
        )
        if recorded.returncode != 0:
            sys.exit(f"inherited {case} recording failed:\n{recorded.stdout}{recorded.stderr}")
        claimed = run([args.claims, "--script", str(source), "--out", str(claims)])
        if claimed.returncode != 0:
            sys.exit(f"inherited {case} claims failed:\n{claimed.stdout}{claimed.stderr}")
        checked = run(
            [sys.executable, args.script, "--recording", str(rec), "--claims", str(claims)]
            + ["--name", f"inherited {case}", "--max-report", "0", "--expect-violations", "0"]
        )
        print(checked.stdout + checked.stderr)
        if checked.returncode != 0:
            sys.exit(f"inherited {case} oracle disagrees with the compiler")
        claim_text = claims.read_text()
        rows, function, program, index = [], "", "", ""
        for line in rec.read_text().splitlines():
            if m := re.match(r"^program ([0-9a-f]+) ", line):
                program = m.group(1)
            elif m := re.match(r"^fn ([0-9]+) .* name ([^ ]+)$", line):
                index, function = m.group(1), m.group(2)
            elif re.match(r"^(read|write|iterate)Inherited$", function) and re.match(
                r"^site [0-9]+ kind obj ", line
            ):
                m = re.match(
                    r"^site ([0-9]+) kind obj made 1 confined 0 escaped 1 unresolved 0 "
                    r"unchecked 0 routes globals:1$",
                    line,
                )
                if not m:
                    sys.exit(f"inherited {case} did not retain its allocation: {line}")
                pc = m.group(1)
                claim = re.search(rf"escape {program} {index} {pc} obj ([^\n]+)", claim_text)
                if not claim:
                    sys.exit(f"inherited {case}: missing compiler claim at pc {pc}")
                rows.append(f"{function} pc{pc} {claim.group(1)}")
        if sorted(rows) != expected:
            sys.exit(f"inherited {case} claims mismatch:\nexpected: {expected}\nobserved: {rows}")


def check_pins(corpus):
    source = Path(corpus).read_text()
    for name, pinned in PINS.items():
        body = re.search(rf"function {name}\([^\n]*\) \{{[^\n]*\n(    [^\n]*\n)*\}}", source)
        observed = hashlib.sha256((body.group(0) if body else "").encode()).hexdigest()
        if observed != pinned:
            sys.exit(f"{name}: pinned source changed; preserve or remeasure its evidence")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--oracle", required=True, help="ctcompile-test-type-oracle")
    parser.add_argument("--claims", required=True, help="ctcompile-test-escape-claims")
    parser.add_argument("--script", required=True, help="tools/check/escape-oracle.py")
    parser.add_argument("--corpus", required=True)
    parser.add_argument("--work", type=Path, required=True)
    parser.add_argument("--name", required=True)
    parser.add_argument("--budget", help="checks per function before the recorder stops")
    parser.add_argument("--strict", action="store_true")
    args = parser.parse_args()
    args.work.mkdir(parents=True, exist_ok=True)
    name = args.name

    selftest = run([args.oracle])
    if selftest.returncode != 0:
        sys.exit(
            "the type oracle's self-test failed before the escape claims ran:\n"
            f"{selftest.stdout}{selftest.stderr}"
        )

    rec = args.work / f"escape-claims-{name}.rec"
    claims = args.work / f"escape-claims-{name}.claims"
    budget = ["--escape-budget", args.budget] if args.budget else []
    recorded = run([args.oracle, "--script", args.corpus, "--out", str(rec), *budget])
    print(recorded.stdout + recorded.stderr)
    if recorded.returncode != 0:
        sys.exit(f"recording {name} failed (exit {recorded.returncode})")
    claimed = run([args.claims, "--script", args.corpus, "--out", str(claims)])
    said = claimed.stdout + claimed.stderr
    print(said)
    if claimed.returncode != 0:
        sys.exit(f"claiming {name} failed (exit {claimed.returncode})")

    # A TRACKED SITE OR A SINK OPERAND IN A LIVE BLOCK THE SOLVER NEVER VISITED
    # is an analysis gap hidden behind `escapes` - sound, and asserted at zero.
    sites, operands = numbers(
        r"([0-9]+) unvisited live sites, ([0-9]+) unvisited operands", said, "its unvisited counts"
    )
    if sites or operands:
        sys.exit(
            f"{name}: {sites} unvisited live site(s) and {operands} unvisited operand(s) - "
            "a reachability gap in the analysis"
        )

    # The Stored backlog reports the direct target of each first witness: a
    # complete partition of the unchanged Stored claims, without gating precision.
    targets, *classes = numbers(
        r"stored first-witness targets: ([0-9]+) sites, ([0-9]+) local-confined, ([0-9]+) "
        r"local-escaping, ([0-9]+) local-mixed, ([0-9]+) external-or-mixed, ([0-9]+) primitive, "
        r"([0-9]+) unresolved",
        said,
        "its storage target evidence",
    )
    stored_claims = sum(
        bool(re.match(r"^escape .* escapes:stored$", line))
        for line in claims.read_text().splitlines()
    )
    if targets != sum(classes) or targets != stored_claims:
        sys.exit(
            f"{name}: {targets} storage witnesses, {sum(classes)} classified targets, "
            f"{stored_claims} Stored claims - incomplete storage evidence"
        )

    # The all-write census must cover each first witness, including joins where
    # one write stores several sites. Additional writes and other first reasons
    # are precision evidence, not new confinement claims.
    _, edges, _, _, unresolved_values, _ = numbers(
        r"stored direct-write census: ([0-9]+) writes, ([0-9]+) site edges, ([0-9]+) "
        r"multiple-store sites, ([0-9]+) other-first sites, ([0-9]+) unresolved values, "
        r"([0-9]+) unresolved targets",
        said,
        "its all-write census",
    )
    if edges < stored_claims or unresolved_values:
        sys.exit(
            f"{name}: missing Stored site edges or unresolved stored values in the all-write census"
        )
    witnesses, stored_sites, complete, incomplete, functions = numbers(
        r"stored direct-write coverage: ([0-9]+) first witnesses of ([0-9]+) Stored sites, "
        r"([0-9]+) complete and ([0-9]+) incomplete of ([0-9]+) functions",
        said,
        "all-write completeness",
    )
    if (
        witnesses != stored_claims
        or stored_sites != stored_claims
        or complete + incomplete != functions
    ):
        sys.exit(
            f"{name}: the all-write census did not cover every first Stored witness or classify "
            "every function"
        )

    # Property reads and shared-local-site write links are diagnostic evidence:
    # coverage and link integrity are gated, never a precision count.
    *_, unresolved_bases, invalid_links = numbers(
        r"direct-load candidates: ([0-9]+) links across ([0-9]+) reads, ([0-9]+) stored-site "
        r"edges, ([0-9]+) external-or-mixed bases, ([0-9]+) unresolved bases, ([0-9]+) invalid links",
        said,
        "direct-load candidates",
    )
    if unresolved_bases or invalid_links:
        sys.exit(f"{name}: unresolved bases or invalid write links in the direct-load census")
    records, covered, live_reads, complete, incomplete, functions = numbers(
        r"direct-load coverage: ([0-9]+) records, ([0-9]+) covered of ([0-9]+) live reads, "
        r"([0-9]+) complete and ([0-9]+) incomplete of ([0-9]+) functions",
        said,
        "direct-load coverage",
    )
    if records != live_reads or covered != live_reads or complete + incomplete != functions:
        sys.exit(
            f"{name}: the direct-load census did not cover every live property read or classify "
            "every function"
        )

    # The separately bounded closure must preserve all original candidates and
    # cover every direct read and sink even when its work limit is exhausted.
    *_, invalid_records = numbers(
        r"load-provenance candidates: ([0-9]+) read-site edges, ([0-9]+) stored-site edges, "
        r"([0-9]+) exposure-site edges, ([0-9]+) propagated exposure edges, ([0-9]+) invalid records",
        said,
        "candidate provenance",
    )
    if invalid_records:
        sys.exit(f"{name}: invalid or narrowed candidate provenance")
    reads, live_reads, exposures, sinks, converged, exhausted, functions, complete, incomplete = (
        numbers(
            r"load-provenance coverage: ([0-9]+) reads of ([0-9]+) live reads, ([0-9]+) exposures "
            r"of ([0-9]+) live sinks, ([0-9]+) converged and ([0-9]+) exhausted of ([0-9]+) "
            r"functions, ([0-9]+) complete and ([0-9]+) incomplete inputs",
            said,
            "provenance coverage",
        )
    )
    if (
        reads != live_reads
        or exposures != sinks
        or converged + exhausted != functions
        or complete + incomplete != functions
    ):
        sys.exit(f"{name}: candidate provenance lost live reads, sinks or function classification")

    dump = args.work / f"escape-claims-{name}.txt"
    checked = run(
        [sys.executable, args.script, "--recording", str(rec), "--claims", str(claims)]
        + ["--name", name, "--max-report", "0", "--expect-violations", "0"]
        + (["--dump", str(dump)] if args.strict else [])
    )
    verdict = checked.stdout
    print(verdict + checked.stderr)
    (observed,) = numbers(r"observed sites +([0-9]+)", verdict, "observed sites")
    violations, sound, partial, pending = numbers(
        r"SOUNDNESS violations ([0-9]+) +sound ([0-9]+) +partial ([0-9]+) +pending ([0-9]+)",
        verdict,
        "its verdict tally",
    )
    (unclaimed,) = numbers(r"UNCLAIMED observed sites ([0-9]+)", verdict, "unclaimed sites")
    precision = re.search(r"PRECISION confined ([0-9]+/[0-9]+ = [0-9.]+%)", verdict)
    if not precision:
        sys.exit(f"the checker did not report precision:\n{verdict}")
    reasons = re.search(r"reasons: [^\n]*", verdict)
    if observed <= 0:
        sys.exit(f"{name}: no site was observed - the corpus did not run, so nothing was checked")
    if violations:
        sys.exit(
            f"{name}: {violations} SOUNDNESS violation(s) - a site claimed confined was reachable "
            "after its frame returned; every one is named above"
        )
    if args.strict:
        check_inherited_accessors(args)
        if sound <= 0:
            sys.exit(
                f"{name}: no confined claim was proved sound - the analysis said escapes "
                "everywhere, which is right the way a stopped clock is"
            )
        if partial or pending:
            sys.exit(
                f"{name}: partial {partial}, pending {pending} - the fixture must adjudicate "
                "every confined claim"
            )
        check_pins(args.corpus)
        dumped = run(
            [sys.executable, str(HERE / "check-dump.py"), args.script, str(dump)]
            + [str(HERE / "expected.txt")]
        )
        print(dumped.stdout + dumped.stderr)
        if dumped.returncode != 0:
            sys.exit(f"{name}: escape snapshot/checker controls failed")
    if checked.returncode != 0:
        sys.exit(f"{name}: the checker exited {checked.returncode}")
    print(
        f"escape claims ({name}): {observed} sites observed (unclaimed {unclaimed}), {violations} "
        f"violations, sound {sound}, partial {partial}, pending {pending}, precision "
        f"{precision.group(1)}; {reasons.group(0) if reasons else ''}"
    )


if __name__ == "__main__":
    main()
