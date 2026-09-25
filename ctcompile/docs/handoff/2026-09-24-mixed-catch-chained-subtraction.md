# Mixed local catches and chained subtraction, 2026-09-24 UTC

Resumed clean `8d83a00c` from the latest handoff, Current native work, journal and
both areas' commit history. Prior code was committed; `codex-wip-20260907` was
already an ancestor. Source, escape and independent investigation agents ran in
parallel. User continuations and rate limits interrupted them; saved checkpoints
were resumed without replaying completed baseline or Node checks.

## Landed

`80db2f07` extends local caught-node normalization from all-throw bodies to mixed
normal/throw completions. Existing recovery supplies the original flag, normal
result, thrown payload and live register state. A bounded projection follows that
exact tuple through each SCF arm. Literal normal leaves keep their normal result;
literal throwing leaves clone the original catch with the corresponding payload
and state. Inactive poison and borrowed payloads do not cross the final result
join. Only inert padding may follow a projected completion branch.

Every original protected arm still needs an independent nonthrowing effect proof.
Logical negation is admitted because it performs truthiness without conversion
hooks. Cloning charges subtree work and all three mapping tables; depth remains
bounded at 64. Escaping references into the old completion are rejected before
its region is erased, and the complete private candidate receives fresh DOM
proof. The emitted program contains ordinary branches and public DOM calls,
with no native exception carrier for borrowed elements.

Five frozen mixed sources now execute: identity `4c021634`, catch read `e8cdab26`,
saved state `29caed75`, explicit normal return `46c5ae36` and reversed throw arm
`d80fef24`. The historical conditional source `cc17bd0c` also executes unchanged.
The five earlier complete local sources and both original outer iterator sources
remain byte-identical. Raw controls cover normal/caught state, reversed/nested
branches, implicit effects, borrowed returns, rethrows and budget rollback.

`63037202` adds a read-time binary64 subtraction snapshot to the existing held
value. Each new Number result uses LLVM APFloat, round-to-nearest ties-to-even,
from original Number literals or prior independently proved subtraction results.
No converted integer bits supply an exact Number. The snapshot has a separate
64-operation depth cap and travels with existing value/container copies. Counted
loop proof requests the exact singleton under subtraction demand while retaining
all receiver, reload and mutation checks. Other arithmetic and property keys keep
their independent authority.

Frozen source SHA-256
`8ecfabd88996593271957b58a8a68eecffd10fe99abe1b3b9b73e91e9276589a`, program
`51d9a9499f905cba`, changes child function 1/site 4 from Stored to Confined.
Its key table also becomes confined: **two of four total sites**, versus zero in
the preserved iteration-141 baseline. The child is observed once, with zero
unresolved/unchecked instances and zero soundness violations. All 150 historical
escape source function bodies remain; three new functions cover the original
chain, a saved child and a mutated key table. Raw checks also distinguish original
fractional property keys, subsequent arithmetic, String/unary/bitwise inputs,
retained children and the 64/65 subtraction boundary.

## Focused validation

- Selected `CTNative/Browser/native-dom-caught-node.test`: **1/1 PASS**, 225.46 s,
  357 excluded. It completed **176 native executions, 44 refusals and 26 distinct
  Node/VM observations**, with both providers, optimization policies,
  explicit/deduced output and GCC/Clang. All **88 generated C++ files** contain
  no Script symbols; existing standalone checks also inspect linked symbols.
- Exact `ctcompile_exception_recovery`: **1/1 PASS**, 5.38 s, 5.39 s total.
  After the final clone-mapping budget correction: **1/1 PASS**, 5.20 s,
  5.21 s total. Re-lowering all 44 modules gives byte-identical output to the
  executed versions; native executions were not replayed.
- Thirteen source preflights: **six admitted, seven refused**. The original
  getter/method preserve their precise observing-catch diagnostic. The initial
  nine-source refusal baseline was retained.
- Exact `ctcompile_escape_analysis_arrays`: **1/1 PASS**, 3.54 s, 3.55 s total.
- Selected `Analysis/Escape/escape-claims/table-index-overwrite.test`:
  **1/1 PASS**, 0.17 s, 357 excluded.
- The unchanged standalone escape source's final oracle passes. The recorded
  baseline was retained rather than rerun. One next-boundary source was measured
  separately; this was not a corpus replay.
- Saved source-agent checks: five initial syntax checks/ten Node observations,
  then reversed/nested checks in `added-oracles.json`. Escape agent checked three
  new Node witnesses and the extracted 153-function source syntax. The next
  standalone escape source also passes syntax and Node `[0,0,0]`.
- `tools/format.sh --check`: **1126 C++, 158 Python and 114 web files PASS**;
  `git diff --check` passes. All **eight final code/test hashes** match the devbox.
- Independent outer-catch investigation completed. Final independent reviews
  were interrupted by rate limits; the parent reviewed the final eight files.

Builds used locked `tools/remote-build.sh` with explicit targets: `ctjs-opt`,
`ctjs-translate`, `ctcompile-test-native-reference`,
`ctcompile-test-exception-recovery`, `ctcompile-test-escape-analysis-arrays`,
`ctcompile-test-escape-claims` and `ctcompile-test-type-oracle`.
The first native build was interrupted before checks. The first recovery run
reported two stale assertions because the unchanged historical conditional now
admits; only its expected result changed. An intermediate formatter saw an
unformatted escape line while that agent was editing; final formatting passes.
Editing a temporary gate script while it was being read interrupted escape tests
after its successful build; a separate immutable script ran those tests once.

Skipped: full CTest/compiler lit, complete iterator fixture, unaffected native
replays, broad corpus/matrices, full Bootstrap, browser WPT/test262, Windows and
sanitizers. No browser/runtime/shared implementation changed, no local C++ build,
push or history rewrite occurred. Linux inspection read 12 process identities
but encountered 61 permission errors; Windows Get-CimInstance returned 346
records. Neither found Claude identities. Availability was uncertain, so
concurrent-area rules applied throughout.

## Exact next boundaries

Original outer iterator getter source
`1a7fb1661163b19674ee92084bc52ded3f96417279039131df31282ca6d813b4` and method
`7acf503b5734264de1d4d390cfb239a63273cd9052c078c66fd60166bdc2c6d3` still refuse:
**DOM iterator observing catch requires call/check payload and state proof**.
Their four original open/next/close calls and every removed status edge need
actual payload, state and independent effect correspondence. Preserve cleanup
writes, node identity, saved completions and exhaustion. Uncaught node throws
still require an owner that outlives the exception; do not widen the lifetime
contract or the unobserved suppression proof to admit them.

Nested local source
`fcd90a50f5195b265f880d93973dc1b973b38e6cf59e20204881a6455cf96f3c` remains an
exact protected-effect refusal. Its two DOM reads precede the try, and the
frozen Node outcomes distinguish outer normal, inner normal and caught state.
Its full recovered effect/dispatch graph needs inspection before relaxing that
proof. The measured mixed admissions do not establish general nested support.

Escape source
`b3eae66b1ddb93268438369071605c7631db8dfc52dbb170a977cb6c7c7abfc9`, program
`43ee068c775dc0cc`, uses `(keys[i % 2] - 0.25 + 0.25) | 0`. The child remains
Stored; Node returns `[0,0,0]`, and the VM observes it confined once with zero
unresolved or unchecked instances. Addition needs independent computed Number
evidence. Converted bits remain insufficient. The earlier fractional-property
Node/VM discrepancy is separate.

Protected observers, broader/nested iterators, unguarded Bootstrap defaults, the
application driver and full native Bootstrap remain unfinished. No full-Bootstrap
admission or coverage gain is claimed.

Evidence: `../test-results/2026-09-24-mixed-catch-chained-subtraction/`, with
SHA256SUMS. Checkpoints: `/tmp/ctcompile-native142`, `/tmp/ctcompile-tests142`,
`/tmp/ctcompile-escape142` and `/tmp/ctcompile-review142`.
