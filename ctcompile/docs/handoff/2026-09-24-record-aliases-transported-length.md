# Record aliases and transported length guards, 2026-09-24 UTC

Resumed clean `2ab9ba43` from HANDOFF/detail159, Current native work, both areas'
commit history and the synchronization journal. `codex-wip-20260907` was already
an ancestor; no abandoned patch remained. Source tracing, escape investigation
and independent review ran in parallel. The source and escape agents reached
rate limits after preserving useful checkpoints; the parent finished the
escape implementation. Final independent review completed on all six files.

## Landed

**d3ad9a90** forwards the actual successful open value through If/IndexSwitch
results only when every arm yields that exact dominating SSA identity. It keeps
the selection and all source producers/effects. This does not prove the open
result is non-undefined, nor permit choosing a custom branch by shape. Unknown
and poison alternatives prevent forwarding. The scan and each rewritten use
spend the existing budget; a nested identity requiring another scan remains
conservative.

The done-field observer proof now follows exact SCF result slots and invocation
saved-state slots through their unwind arguments and yielded results. Only root
values, truth tests and unused results terminate successfully. It does not erase
transport or producers. A new control publishes a saved done value after the
original invocation and refuses. Record aliases escaping through a selection
also refuse, while selection effects remain present.

Original getter `1a7fb166` and method `7acf503b`, with lifted helper bodies, now
reach `DOM iterator close requires completion-selected record identity` through
both direct recovery and complete preparation. Next and both close(false)
invocations retain their exact payloads, flags and 15 saved registers, allowing
only equivalent open-result substitution. Original recovery still preserves
four calls and 33 checks. Raw helper CFGs retain their earlier getter/identity
body-proof boundaries. Whole-module and contract rollback remain checked.
**No new native JavaScript source admission is claimed.**

**39d2080a** accepts a cached length's array receiver transported through a
preheader block argument. Existing immutable path state supplies its exact
allocation identity. The receiver's block and length-read block must each pass
the existing bounded single-predecessor chain to that allocation. Joins,
repeated/foreign regions and the depth-64 ceiling remain conservative. The
backedge-index check now applies only to actual counted-header arguments;
preheader arguments have their own unrelated argument numbers. Existing current
extent, saved-bound, mutation census, store-receiver and exact replay proofs
remain mandatory. No new state model was added.

Frozen source SHA-256
`ff06849442a8fc74ee10792a136e25eb506d5ee871d9d440ebffa1c1f91429bb`,
program `47d8cf7cab0bbfde`, changes child Stored and holder Passed to Confined:
**2/4 total allocation sites**, all four observed, zero violations, unlimited VM
recording, three frame pops. Node returns `[[0,0,0],0]`. Additional Node controls
produce `[[0,0,0],0,{},[0,0,{}],[{},0,{}]]` for cleared, early-return, saved-child,
changed-bound and replaced-receiver cases. The latter controls retain escapes.

## Exact focused validation

All builds and devbox commands held `/tmp/ctbrowser-devbox-build.lock`.
Explicit remote-build targets were `ctjs-opt`, `ctjs-translate`,
`ctcompile-test-exception-recovery`, `ctcompile-test-host-contract`,
`ctcompile-test-native-reference`, `ctcompile-test-escape-analysis-arrays`,
`ctcompile-test-escape-claims` and `ctcompile-test-type-oracle`.

- Final native `ctcompile_exception_recovery`: **1/1 PASS, 5.33 s**, 5.34 s total.
- Final `ctcompile_escape_analysis_arrays`: **1/1 PASS, 4.87 s**;
  `ctcompile_host_contract`: **1/1 PASS, 2.89 s**; combined 7.76 s.
  Earlier host-contract passed in 2.84 s.
- Generated build-tree lit filter
  `^ctcompile :: Analysis/Escape/escape-claims/cached-transported-length[.]test$`:
  **1/1 PASS, 0.11 s, 361 excluded**. The unchanged frozen program separately
  passed compiler-claims/VM recording before and after the implementation.
- Selected native sources `invocation-return`, `write-boolean-snapshot` and
  `helper-next-record`, retaining every original outer/refusal control: **PASS**.
  Both DOM providers, optimization/printing options, GCC/Clang and Node/VM
  comparisons remain enabled. This is not a whole-fixture/lit pass. The exact
  selector is preserved as `native/native-selection.py` in the evidence.
- `tools/format.sh --check`: **1126 C++, 158 Python, 114 web files PASS**.
  `git diff --check` passes. Six final source/test hashes match the devbox and
  completed clean independent review. Twenty-four generated C++ files contain
  no `ctbrowser::script`, `ctbrowser::aot` or `ctjs::` namespace; the harness's
  binary-symbol checks pass.
- All 34 frozen native artifacts and eight C++ raw source strings are unchanged.
  Of 302 Python string constants, only the intended refusal diagnostic changes.
  Removing the newly added escape unit block reconstructs historical Validation
  byte-for-byte. The new lit fixture contains the exact frozen escape function.

The first diagnostic build succeeded; invoking its test binary without the
required MLIR fixture printed usage. The corrected exact CTest passed and
preserved the original projected IR. The first candidate recovery gate exposed
done-field saved transport and old alias expectations. The second had two
escaped-alias expectations blocked by the earlier done guard. The completed
observer proof and strengthened controls passed the final gate. The temporary
diagnostic print was removed before final validation and commits.

Skipped: full CTest/compiler lit, whole native fixtures, unaffected native replay,
broad corpus/matrices, full Bootstrap, WPT/test262, Windows and sanitizers.
No browser/shared implementation, local C++ build, push or history rewrite occurred.

## Exact next boundary

In the preserved `native/projected.mlir`, the first common record is `%49#4`:
both arms yield the actual open `%38`. That correspondence is now proved.
The remaining close uses are `%59#4` under completion tag zero and `%59#16`
under tag one. Both guards compare `index_castui(%59#26)` with the tag.

Prove the record slot **together with** that tag slot through the same tuple
producer. The path is `%59` through `%102`, or through `%87` and `%126`.
Tag-zero leaves carry the record in slot 4; tag-one leaves carry it in slot 16.
Opposite slots contain poison. The next-failure leaf has tag two and must remain
excluded by these exact guards. Unknown or disagreeing selected leaves refuse.
Forward only the guarded uses; never globally replace the mixed tuple results
or reuse capture recovery's inactive-padding exemption for record identity.

Then thread next/exhaustion and both unsuppressed close failures through retained
Try/result-bearing Invoke state. Keep saved returns, caught-node identity and
every non-call effect. Close mutates the DOM and throws; suppressed cleanup is
not its semantics. Raw helper CFG normalization remains an earlier independent
requirement. The transported-length witness is complete; wider cached origins
need their own execution provenance. Escaping-node exception owners, broader
iterators, unguarded Bootstrap defaults, the application driver and full native
Bootstrap remain unfinished.

Availability inspection read 11 Linux executable identities with 60 permission
failures and 338 Windows process records. No actual Claude identity matched,
but the incomplete Linux scan means **uncertain**; concurrent-agent rules stayed
in force. No broader browser authorization was used. Recent runtime/Shell history
and WPT/test262 documentation were read; historical metrics were not remeasured.

Evidence is at `../test-results/2026-09-24-record-aliases-transported-length`
relative to the monorepo root, with verified `SHA256SUMS`.
