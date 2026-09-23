# Guarded cleanup writes and bounded decimal conversions, 2026-09-23

Resumed clean **f3f6f9b2** and source `c90a81f2` from HANDOFF, Current native
work and iteration 121's final journal. No dirty drafts or unmerged
`codex-wip-20260907` remained. Unrelated branches were preserved. Source, raw and
escape agents worked in parallel; user continuations and rate limits interrupted
their turns. Saved candidates, scripts, logs and baselines allowed completion
without restarting the work.

## Native change

**df15eea0** extends the existing protected-helper proof with one resultless
`scf.if` around the second write. Its condition is the truthiness of a tracked
saved DOM read; its then arm contains one `setAttribute` consuming a saved read,
and its else arm is absent or empty. Branch-local reads, nested control flow,
additional effects and result transport remain unproved. Condition uses join the
complete value-use census. Existing recursive cloning retains source order and
wraps the second write in its existing suppression. No cloning, ownership,
runtime or browser interface changed. Private complete DOM/Style reproof and
selector validation precede publication; the original body exception wins.

The unchanged original is SHA-256
`c90a81f2a4dd36032c31ae7767970c4e59ddb305ddcc043d6ff0ea455091dccb`.
Getter `d12b99c1` and order `3924b2db` also execute. Tests cover a false selector
skipping the second write and a true saved selector remaining true after the
first write changes the attribute. The selected source gate accounts for
**48 native executions, 76 refusals and 24 Node/VM observations**. Its final stdout
was lost on interruption. The fresh work directory reached the final refusal's
manifest after every execution and prior check; recovery verified 24 generated
C++ files, 48 built binaries and their order, then checked only the final refusal.
No native binary execution was replayed. The final refusal diagnostic is saved.

Source preflight passed 26 unique checks; local checks passed 13 source syntax
checks and 12 Node observations. All 233 general historical source bodies,
86 metadata rows, 115 saved cases, 266 retained refusal bodies and 115 historical
oracle constructions remain. Raw tests add four admissions, two typed refusals,
ten structural controls and three expansion-budget sources; all 28 historical
MLIR literals remain unchanged. An independent native review found no actionable
issues and is preserved in the evidence.

## Escape change

**f21f0a8d** changes the shared `boundedConvertedNumber` proof, covering its
primitive, CFG, SCF, length and loop callers. At most 32 original bytes reach
public Core whitespace trimming and conversion. An empty trimmed String or one
optional sign followed by decimal digits is admitted only when its converted
signed magnitude is finite, integral and within the existing uint32 bound.
Malformed signs, fractional/exponent/radix forms and longer inputs remain
unproved. Original Strings and property keys keep their identity; String addition
still concatenates. No table-only parser or runtime implementation was added.

Baseline program `1e04ef69b6936cbf` classified all six child sites as stored.
The same six bodies are functions 49–54 of final program `a2cdf8f0097ce760`:
**three confined, three stored**. The latter cover malformed conversion,
unconverted property spelling and table mutation. Historical functions 28 and
40 also become confined. Six Node witnesses check original and instrumented
sources, array/child identity, String values, own keys and read/write sequences.
All 48 prior source bodies, six baseline bodies and 4,819 historical IR-bearing
literal tokens across 12 raw test files are preserved.

The first exact arrays run reported 592 failures from changed historical
expectations. Those inputs were retained and their exact contents/retention
expectations promoted. The next build found one new pointer-versus-literal test
comparison; changing that loop's element to `std::string` fixed the warning.
The following exact arrays run passed. Production stayed unchanged after its
first successful build. The first table lit run passed but its recording showed
zero entries for new functions after an earlier intentional BigInt throw. Only
the new invocation block moved before that throw. Six permanent recording checks
now require one observed child per new function, with three confined and three
escaped, zero unresolved and zero unchecked. The final selected lit passes.
Parent reviewed the escape change; no completed independent escape review is
claimed after the review agent hit a rate limit.

## Focused validation

- `ctcompile_host_contract`: 1/1 PASS, 2.56 s / 2.57 s total.
- `ctcompile_escape_analysis_arrays`: final 1/1 PASS, 2.82 s / 2.83 s total.
- `Analysis/Escape/escape-claims/table-index-overwrite.test`: final 1/1 PASS,
  0.13 s, 356 excluded; all six new child sites are explicitly observed.
- `tools/format.sh --check`: final 1126 C++, 157 Python, 114 web files PASS.
  An earlier run encountered the parallel escape agent's unfinished formatting.
  Scoped checks and `git diff --check` pass. The final `.test` invocation/recording
  change is outside the formatter's extensions and passed its selected lit gate.
- All 17 final code/test SHA-256 hashes match the devbox. The native fixture
  checks standalone and linked output; its 24 generated C++ files contain no
  Script namespace. Native checks were not replayed after independent escape
  expectation changes.

Explicit devbox targets: `ctjs-opt`, `ctjs-translate`,
`ctcompile-test-native-reference`, `ctcompile-test-host-contract`,
`ctcompile-test-escape-analysis-arrays`, `ctcompile-test-escape-claims` and
`ctcompile-test-type-oracle`. Every devbox command held the shared build lock.
The initial escape baseline command needed its correct executable path and
`--script`/`--out` flags before the successful measurement; no baseline code
change occurred. Builds and C++ execution ran only on the devbox.

Initial process inspection covered 18 accessible Linux and 350 Windows processes;
final inspection covered 14 Linux and 352 Windows processes. No actual Claude
executable, CLI or loop matched; 61 initial and 60 final Linux executable-link
permission errors kept status uncertain. Concurrent-area rules applied. No
browser/shared implementation edits, push or history rewrite occurred.

Full CTest/compiler lit, complete iterator fixtures, unaffected native replays,
broad corpus/matrices, full Bootstrap, browser/WPT/test262, Windows and sanitizers
were skipped. These are focused results, not full-suite or full-Bootstrap gains.

Evidence: `../test-results/2026-09-23-guarded-cleanup-decimal-conversions/` relative
to the repo, with `SHA256SUMS`. Working checkpoints are `/tmp/ctcompile-native122`,
`/tmp/ctcompile-tests122`, `/tmp/ctcompile-raw122` and `/tmp/ctcompile-escape122`.

## Next boundary

`unsupported-selector-guards-second-write-read`, SHA-256
`164962d0a51209f754ca933fed97a66cc4eaa12a74bfe831e4e16824314a0af3`, refuses
**DOM protected helper needs an independent inert-body proof** under both policies.
Its guarded second `setAttribute` evaluates `hasAttribute('data-visited')` inside
the branch. Preserve that read's condition/order, the saved selector and original
body exception. Broader cleanup/control flow, nested iterators, unguarded Bootstrap
defaults, the application driver, full native Bootstrap, general powers, wider
numeric conversion grammars and legacy SCF retention remain unfinished.
