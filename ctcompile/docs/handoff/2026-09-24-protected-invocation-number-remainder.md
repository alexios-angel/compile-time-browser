# Protected invocation projection and computed Number remainder, 2026-09-24 UTC

Resumed clean `30a722ad` from the latest HANDOFF, Current native work, journal and
both areas' commit history. The old `codex-wip-20260907` is already an ancestor;
no unmerged or dirty predecessor patch required landing. Agents independently
worked on source controls, escape proof and review. User interruptions and rate
limits interrupted source/escape work; the parent resumed their frozen checkpoints.

## Landed

`d5e5e122` makes native preparation consume recovered nonthrowing `Element.hasAttribute`
invocations in local handlers without explicit throws. It reuses
`CheckedInvocations` and the existing completion mapper. The original contract
must identify the direct Element parameter, exact member, receiver and one literal
String argument. Every other protected operation still needs a separate effect
proof. The call remains in its source branch and order; only the proved normal
continuation is selected. Its complete result tuple retains inactive failure
padding, while successful saved values retain their original outer SSA. Failure
payload and unwind state never substitute for a successful result.

The no-explicit-throw attempt uses a bounded disposable clone, leaving the original
handler for the existing URI/JSON consumer if this proof does not apply. Exact
Boolean completion tags fold before the existing branch projection; no borrowed
node becomes an escaping C++ exception. Five new sources cover direct return,
saved state, conditional calls, multiple reads and read-then-throw. All eighteen
historical local and both original outer source bodies remain unchanged.

`f1409c98` makes escape analysis retain independent binary64 remainder snapshots through LLVM
APFloat `mod`, with JavaScript/fmod rather than IEEE nearest-quotient semantics.
Only original Number literals or independently proved Number results supply its
inputs. Converted bits never reconstruct arithmetic or property keys. The
existing 64-operation depth, mutation census and Div/Mod work charge remain.
Controls cover fractional and negative divisors, a following addition, saved and
unwritten children, invalid property/operand authority, mutation and depth 64/65.
All 162 historical source function bodies remain unchanged; three are appended.

## Measured focused validation

- Exact `ctcompile_exception_recovery`: **1/1 PASS**, 5.33 s, 5.34 s total.
- `CTNative/Browser/native-dom-caught-node.test`: **1/1 PASS**, 474.55 s,
  357 excluded. Its completed harness covers **368 native executions, 60 refusals
  and 58 Node/VM observations** across the two providers and optimization policies.
- Exact `ctcompile_escape_analysis_arrays`: **1/1 PASS**, 3.92 s, 3.93 s total.
- `Analysis/Escape/escape-claims/table-index-overwrite.test`: **1/1 PASS**,
  0.17 s, 357 excluded. All three new functions are observed once.
- Frozen source **38de654ca93c52a916b27e820f8e4c00751d0c3100c04482067a2333e0a0f7e5**,
  program **f037892d3c412d79**: child function 1/site 4 Stored -> **Confined**;
  key table site 14 Passed -> **Confined**. Two of four total sites are confined,
  zero soundness violations. The original measured baseline was retained without
  replay. Node returns `[0,0,0]`; the VM observes the child once, confined, with
  zero unresolved or unchecked instances.
- The next `** 1` witness below is measured once: four observed sites, zero
  statically confined sites, zero violations. Its child is dynamically confined
  once, with no unresolved or unchecked instances; Node returns `[0,0,0]`.
- `tools/format.sh --check`: **1126 C++, 158 Python, 114 web files PASS**.
  `git diff --check` passes. All **ten final code/test hashes** match the devbox
  and completed independent review. All **184 emitted C++ files** have no
  Script/AOT symbols. The native fixture also checks the absence of an escaping
  source C++ exception.

Locked `tools/remote-build.sh` used explicit affected targets:
`ctcompile-test-exception-recovery`, `ctjs-opt`, `ctjs-translate`, `ctcompile-tool`,
`ctcompile-test-native-reference`, `ctcompile-test-escape-analysis-arrays`,
`ctcompile-test-escape-claims` and `ctcompile-test-type-oracle`.

The first native build passed, then three new controls failed because the draft
mistook normal Invoke arguments for the saved unwind vector and omitted the
CTJS Boolean normal flag. Review corrected both. The next run reached the new
normal tuples but exposed inactive padding through an unfurled false tag; exact
Boolean truth folding fixed that. The final recovery and selected native lit
passed. Existing URI/JSON checks remained passing.

The first arrays run had one outdated work expectation: 32 new independent
remainder snapshots cost 96 rather than 64 units. The input and production were
unchanged; the expectation now includes Mod. All new remainder behavior controls
passed that first run. After arrays passed, the selected table lit exposed three
new functions with zero entries: their call sites were missing from the driver.
The three calls were added before its historical deliberate BigInt error; no
function body changed. Only table lit and the small standalone measurements ran
after that correction. Successful native/arrays checks were not replayed.

Skipped: full CTest/compiler lit, the separate complete custom-iterator fixture,
unaffected native replay, broad corpus/matrices, full Bootstrap, browser
WPT/test262, Windows and sanitizers. No browser/runtime/shared implementation,
local C++ build, push or history rewrite occurred. Linux inspection read eleven
executable identities with sixty access errors; Windows Get-CimInstance returned
344 records. Neither found an actual Claude match, so status remained uncertain
and concurrent-area rules applied.

## Exact next boundaries

Original observing outer iterator getter `1a7fb166` and method `7acf503b` still
need the coordinated protocol consumer for recovered open/next/close completions,
exact call payload/state, normal exhaustion, cleanup and saved-return state.
Every discarded non-call status edge needs its independent effect proof. The
new local consumer does not provide throwing-call or iterator authority. Escaping
borrowed nodes still need an owner that lasts through the exception's lifetime.

Broader protected observers and iterators, unguarded Bootstrap defaults, the
application driver and full native Bootstrap remain unfinished. No full-Bootstrap
admission or coverage gain is claimed.

Escape source **1e33691869b9a209c461a34f1324a3b418c61dc9ce5a7025811695af907a4d79**,
program **a30ea12594e7fddf**, uses
`(((keys[i % 2] - 0.25 + 0.25) % 3) ** 1) | 0`.
The child remains Stored and its key table Passed despite the confined VM
observation. Preserve original Number evidence through this exact power identity;
converted bits cannot supply a general Number or property-key proof. The older
fractional-property Node/VM discrepancy remains separate.

Evidence: `../test-results/2026-09-24-protected-invocation-number-remainder/`,
including frozen sources/baseline, focused logs, generated C++ (no executables),
final hashes, independent review and checksum manifest.
