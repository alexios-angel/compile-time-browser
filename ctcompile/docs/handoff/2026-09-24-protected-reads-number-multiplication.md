# Protected DOM reads and computed Number multiplication, 2026-09-24 UTC

Resumed clean `ebda7b23` from the latest handoff, Current native work, journal and
both areas' commit history. The old `codex-wip-20260907` is already an ancestor.
There was no unfinished working-tree patch. Three agents investigated escape,
source tests and native review; rate limits interrupted them. The source agent
completed its checkpoint and harness, the parent completed production and gates,
and a final independent reviewer checked all twelve final code/test files.

## Landed

`8a4dd9b8` proves a narrow protected DOM observer in local catches. The original
fingerprinted entry contract supplies Element parameter indices; local helpers
receive none. A protected method lookup must use the original function entry
argument and the literal name `hasAttribute`. Its call must keep that exact
receiver/callee and one literal String argument. These operations cannot throw a
source exception or reenter. They remain in their original branch order; existing
recovery and completion projection preserve the caught node and saved state.
Complete DOM reproof still precedes publication of the private candidate.

The original `protected-read`, `mixed-protected-normal-read` and
`nested-protected-normal-read` sources execute unchanged. A new conditional source
reads inside the handler and carries saved state into the catch. Raw controls
also cover a lookalike object method that throws, a numeric argument, a fallible
URI call and a missing Element binding. Existing rethrow, escaping borrowed result,
zero/finite budget and outer iterator controls remain. No borrowed C++ exception
carrier, widened owner contract or runtime dependency is added.

`5717a629` reuses the private APFloat helper for independent Number multiplication
snapshots, alongside Add/Sub. Only original Number literals or independent
arithmetic snapshots supply binary64 operands. Every intermediate uses
round-to-nearest, ties-to-even. Public Core supplies ToUint32; converted bits never
reconstruct a Number or property key. The early arithmetic-demand path lets a
loaded product feed the next operation. The 64-operation limit and complete
mutation/reload census remain; newly proved bits consume the existing work budget.

Tests retain all 156 historical source function bodies and add three witnesses.
Raw checks cover a nonidentity product (`0.9/1.9` multiplied by two writes slots
one/three), a following addition, saved children, unknown operands, direct property
refusal, mutations before/after reads, and the 64/65 boundary. Three historical
Number Mul-one expectations are promoted without changing their input programs.

## Measured focused validation

- Exact `ctcompile_exception_recovery`: **1/1 PASS**, 4.90 s, 4.91 s total.
- Native caught-node fixture: **288 executions, 40 refusals, 48 Node/VM
  observations**, both providers and optimization policies, explicit/deduced C++,
  GCC/Clang. The initial selected lit run lost its local SSH connection on user
  continuation; its remote harness completed all executions, then stopped at the
  new forged-method source because `prepare` needed an explicit entry name for a
  source containing a nested function. The harness was fixed; a current-timestamp
  census verified all 288 binaries and 144 source files, then only the remaining
  40 refusals and four outer Node/VM observations ran. The later refusal-source
  creation proves the preceding sequential execution/symbol checks completed.
  **No final whole native-lit pass is claimed.**
- Exact `ctcompile_escape_analysis_arrays`: **1/1 PASS**, 3.82 s, 3.83 s total.
- `Analysis/Escape/escape-claims/table-index-overwrite.test`: **1/1 PASS**,
  0.17 s, 357 excluded.
- Unchanged source
  `1d605beafb439db9d8144e1672a7cc1b9c354126f4beec42b168f843970de354`, program
  `ccf117847c4cd802`: child function 1/site 4 Stored -> **Confined**; key table
  Passed -> **Confined**, **two of four total sites**, zero violations. The
  previous identical-source baseline was preserved without replay. The VM observes
  the child once, confined, with zero unresolved/unchecked instances; Node returns
  `[0,0,0]`.
- Four native source preflights: **four admissions**. Local source preparation
  records four syntax/ten Node observations for protected reads and six
  syntax/twelve Node observations for original/candidate outer catches. Candidate
  outer observations are source checkpoints, not native admissions.
- `tools/format.sh --check`: **1126 C++, 158 Python, 114 web files PASS**;
  `git diff --check` passes. All **twelve final code/test hashes** match the devbox
  and independent review. All **144 generated C++ files** contain no Script/AOT
  symbols; the execution harness also checks linked symbols.
- Independent final review: **no blocking findings** in all five native and seven
  escape files. The reviewer did not run or replay the parent's gates.

All builds used locked `tools/remote-build.sh` and explicit affected targets:
`ctjs-opt`, `ctjs-translate`, `ctcompile-test-native-reference`,
`ctcompile-test-exception-recovery`, `ctcompile-test-escape-analysis-arrays`,
`ctcompile-test-escape-claims`, and `ctcompile-test-type-oracle`.
The first arrays run found three test issues: a new duplicate SSA name, an
incorrect expected refusal category, and the old 64-unit expectation where Mul's
independent facts now cost 96. The second retained only the refusal-category
mismatch; the existing default `UnsupportedControlFlow` is correct because the
unknown operand blocks the loop bound proof. Test construction/expectations were
corrected, and the nonidentity product regression was strengthened. Production
logic did not change during these failed arrays runs. No completed native
execution or recovery gate was replayed.

Skipped: full CTest/compiler lit, the complete custom-iterator fixture, unaffected
native replay, broad corpus/matrices, full Bootstrap, browser WPT/test262, Windows
and sanitizers. No browser/runtime/shared implementation changed, no local C++
build, push or history rewrite occurred. Linux inspection read twelve executable
identities and encountered sixty access errors; Windows Get-CimInstance returned
343 records. Neither found an actual Claude identity. Availability remained
uncertain and concurrent-area rules applied throughout.

## Exact next boundaries

Outer iterator getter
`1a7fb1661163b19674ee92084bc52ded3f96417279039131df31282ca6d813b4`
and method
`7acf503b5734264de1d4d390cfb239a63273cd9052c078c66fd60166bdc2c6d3`
remain refused: **DOM iterator observing catch requires call/check payload and
state proof**. The original graph has 28 caught predecessors, open/next/two close
calls and non-call checks. Preserve each call's failure payload and full pre-call
state, every removed status edge's independent effect proof, cleanup writes,
saved return completion and exhaustion. The getter/method source checkpoints
also include caught reads and saved-state variants. Do not widen unobserved
suppression or a synchronous document owner to admit escaping borrowed nodes.

Escape source
`57db40f7d3dfebab29134056222b05b4b00b2e0f0cd35cdd753641d86cb963aa`, program
`73139a70dad89e41`, uses `((keys[i % 2] - 0.25 + 0.25) / 1) | 0`.
It still reports the child Stored and the key table Passed; zero of four total
sites are confined. Node returns `[0,0,0]`, and the VM observes its child confined
once with no unresolved/unchecked instances and zero soundness violations.
Division needs independent computed Number evidence. The older fractional-property
Node/VM discrepancy remains separate.

Broader protected observers, nested iterators, unguarded Bootstrap defaults,
the application driver and full native Bootstrap remain. These focused results
establish no full-Bootstrap admission or coverage gain.

Evidence: `../test-results/2026-09-24-protected-reads-number-multiplication/`, with
source checkpoints, original baselines, focused logs, generated sources, final
reviews/hashes and a checksum manifest. No executable artifacts are retained.
