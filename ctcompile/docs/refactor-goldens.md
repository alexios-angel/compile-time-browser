# Proving a refactor of the native lowering changed nothing

`lib/CTNative/Lowering/LowerToEmitC.cpp` is 1800 lines with 34 `refuse()` call
sites, two `why…()` helpers that between them name sixteen more reasons, and a
call-graph fixpoint that **overwrites** reasons the admission check already
wrote. It is the file most likely to be restructured next, and it is the file
where "the suite is green" says least.

This page is the answer to one question: *after a change that was supposed to
change nothing, what evidence is there that nothing changed?*

## Why green is not evidence here

The differential gate — `ctcompile_native_unit_*`, `ctcompile_differential`,
the module differential — compares **answers**, for programs that are **still
accepted**.

A function that silently stops being native produces no wrong answer. It
produces *no native code*: the interpreter answers instead, byte for byte
correctly, and every differential passes. The claims floors
(`--min-claimed`) are a partial guard, and they are a **floor** — they catch a
function that stops being claimed only once enough of them do to cross a
number set by hand.

That gap is not hypothetical. On 2026-09-02 two sound closed-world fixes shut
whole-program resolution on all three real corpora while the claimed-function
count never moved: the count measured the floor, and the change had moved the
ceiling. Nothing in the suite noticed. The census below did.

## The two instruments

Both are artefacts the build already writes, and neither is checked in.

| | what it is | where the build writes it | resolution |
|---|---|---|---|
| **Modules** | the whole post-pipeline IR for the four fixtures, eight files (`.pipeline.emitc.mlir` and `.pipeline.deduced.emitc.mlir` for `numeric`, `functions`, `structs`, `arrays`) | `<build>/ctcompile/test/` | every operation, type, symbol and source location the lowering emits |
| **Census** | every refusal reason with its count, over the fixture and the three real corpora (`native-claims-{fixture,bootstrap,p5,phaser}.json`) | `<build>/ctcompile/test/` | ~12,900 real refusals, grouped by reason shape |

The modules see what the compiler **emits**; the census sees what it
**declines**, at a scale no fixture reaches. A change that alters either
without meaning to has altered the compiler.

## The procedure

```bash
# before the change, against a built tree
bash ctcompile/test/native-snapshot.sh save /tmp/before build

#   ...make the change, then rebuild (on the devbox, as always)...

bash ctcompile/test/native-snapshot.sh compare /tmp/before build
```

`build` is optional; it defaults to `$CTCOMPILE_BUILD_DIR`, then to
`<repo>/build`. Both subcommands re-run the four census checks themselves
(about twenty seconds — `p5` and `phaser` are large bundles), because those
JSONs are written by `ctest` rather than by the build, and comparing a stale
one against itself is exactly the quiet nothing this page exists to rule out.

### The rule

> **For a pure refactor, not one byte may change.**

Not "the counts match". Not "the suite is green". `cmp` is silent on all twelve
files, or the change was not a refactor. Each of these moves bytes here and
nothing else in the tree:

* a reworded diagnostic — 18 of the 49 reachable refusal strings are still not
  pinned by any lit test (it was 38 before `test/CTNative/Lowering/refusal-*.mlir`
  landed), so a good many rewordings are still invisible to `check-ctcompile`;
* a **reordered** check, where two refusals are reachable for one program and
  the order the checks run decides which one you get (see
  `test/CTNative/Lowering/refusal-equality-order.mlir` for a worked pair);
* a function that quietly stopped being claimed, which no differential can see;
* a fixpoint step that replaced a precise reason with `calls \`X\`, which is not
  native`.

If a change was *meant* to move these, that is fine and normal — say which
files moved and why in the commit that moves them. What is not fine is a
refactor that moved them and did not notice.

## Why a procedure and not a checked-in golden

Three reasons, in order of how hard they are to work around.

1. **The artefacts are not portable.** The modules are written with
   `--mlir-print-debuginfo`, so every operation carries a `FileLineColLoc`
   naming its fixture by the **absolute path** the build passed to
   `ctjs-translate`; the census JSON records `"corpus": "<absolute path>"`. A
   checked-in copy would differ on every machine, and every checkout would have
   to run a scrubber — one more thing that can silently change what is being
   compared.
2. **A golden of derived output has no safe regeneration rule.** The honest
   instruction is "regenerate when the change is meant to move it", and the
   failure mode of every such golden is the same: the diff is large, nobody
   reads it, and it is regenerated *because it changed*. A before/after
   procedure has no regenerate step to abuse — the baseline is taken from the
   tree **as it stands before the edit**, so it cannot be blessed after the
   fact without re-running the old code.
3. **The build already writes all twelve, every time.** A snapshot taken out of
   the same build tree is a true before/after with no third party in between,
   which is precisely what a golden checked in months ago is not.

The cost is honest and stated: this is opt-in. Nothing runs it for you, and a
refactor landed without it has no evidence behind "nothing changed". The one
part that *is* in `ctest` is the instrument's own gate.

## The instrument's own gate

`ctcompile_native_snapshot_selftest` runs `native-snapshot.sh selftest` and
proves both teeth, because a comparison nobody has watched fail is not a
comparison:

1. **the pipeline is byte-deterministic** — re-running it over an unchanged
   tree reproduces `numeric.pipeline.emitc.mlir` exactly. Without this,
   `compare` would report differences that mean nothing, and the first person
   to meet one would learn to ignore it;
2. **the comparison bites** — one byte appended to a saved copy is caught.

## Related

* `tools/check/native-claims.py` — what writes the census, and the two negative
  proofs (`floor_bites`, `silent_drop_caught`) that keep its own teeth.
* `test/CTNative/Lowering/refusal-*.mlir` — the lit tests that pin individual
  diagnostic strings. They are the fine-grained instrument; this page is the
  coarse one that needs no one to have predicted the string in advance.
