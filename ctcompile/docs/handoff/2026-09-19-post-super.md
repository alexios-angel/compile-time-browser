# Post-super calls, scaled overwrites and loop handoff — 2026-09-19 UTC

Resumed clean `aec2f3b7`. The interrupted thread was recorded in AGENT-SYNC at
15:18/15:20 and in HANDOFF/master 00/24: B's ordinary `this._getConfig(t)` after
its own `super()`, followed by Qi's lexical-super lookup. File splits, CMake
ownership and worktree synchronization had completed since that interruption.
Both agents' histories and unmerged branches were reviewed; September 7 WIP
was not unmerged and is recorded as already integrated. No predecessor dirt
needed recovery. Browser branch-only changes remain on their existing branches.

`7c4c4866` preserves a single-use ordinary method call only after super phase 4,
with both lookup and call receiver equal to the initialized receiver and the key
in the proved method table. Calls are cloned in original evaluation order.
The subsequent `fieldsOnly` census records the new operations, avoiding pointers
to the body replaced by `takeBody`. Complete base and shadowed method bodies,
constructor declaration/global-write exclusions, method mutation checks and
budgets remain. Four new positive sources add **32 native executions**; four
negative sources cover foreign receivers, pre-super access, dynamic keys and
method replacement. All original source fixture parts remain unchanged.

`eb8db016` extends the shared array-loop proof to bounded exact Number products,
including commuted operands. The write footprint uses the transformed positive
stride and endpoints; invariant reloads must miss every write. Array growth,
zero/negative/fractional/String factors, saved aliases, remaining aliases and
historical cycles retain their refusals. Added CFG/SCF controls and an 18-function
source oracle. Replay and ownership representation are unchanged.

Three agents supplied escape implementation, dispatch review and constructor
fixtures. The original fixture agent hit a service rate limit without edits;
the completed reviewer took over that bounded task. Root reviewed, gated and
committed the two concerns separately. No browser/Script behavior changed.

## Measured validation

All builds and remote checks held `/tmp/ctbrowser-devbox-build.lock`.

- `tools/remote-build.sh ctjs-opt ctjs-translate ctcompile-test-host-contract
  ctcompile-test-native-reference` passed (initial build and final fixture sync).
- `ctest --test-dir projects/compile-time-browser/build --output-on-failure
  --no-tests=error -R '^ctcompile_host_contract$'`: **1/1, 0.48s**, 0.49s total.
- `~/.lit-venv/bin/lit -sva -j2 projects/compile-time-browser/build/ctcompile/test
  --filter='^ctcompile :: CTNative/Lowering/Objects/class-(initialization|dom)[.]mlir$'`:
  **2/2, 308.94s**. Class initialization: **195 source observations / 524 main
  native executions / 390 unprepared refusals / 225 preparation refusals**.
  Additional controls: 16 constructed-method executions/20 refusals, eight
  original-r executions/four refusals, four prototype-key executions/six
  refusals, and 13 prepared-source refusals. DOM: **632 observations / eight
  combined native executions / 4,910 refusals**.
- `tools/remote-build.sh ctjs-opt ctjs-translate
  ctcompile-test-escape-analysis-arrays ctcompile-test-escape-claims
  ctcompile-test-type-oracle` passed.
- The same exact CTest command selecting `^ctcompile_escape_analysis_arrays$`:
  **1/1, 1.31s**, 1.32s total.
- Lit selecting
  `^ctcompile :: Analysis/Escape/escape-claims/(scaled|quotient|offset)-index-overwrite[.]test$`:
  **3/3, 0.11s**. Scaled: **54 sites / seven sound / 7 of 10 confined precision**;
  quotient: 51/six/6 of 9; offset: 48/seven/7 of 13. All have zero violations,
  partial, pending and unclaimed sites.
- All **nine changed source hashes** matched the tested devbox copy.
- Required `tools/format.sh --check` reproduced **20 baseline diagnostics in six
  HEAD-identical files**. Changed C++ passes the pinned formatter; changed Python
  passes Black with the repository configuration. One combined formatter call
  covering the sibling loop helper selected the wrong common configuration;
  separate checks with the explicit repo configuration passed. Source Node
  syntax/observations, Python syntax and `git diff --check` passed.

Full CTest/compiler lit, broad native/corpus matrices, WPT/test262, sanitizers,
Windows builds and whole Bootstrap were skipped. Focused results are not a
full-suite pass. Evidence: `/tmp/ctcompile-post-super-20260919/`.

## Unattended loop

The user requested the switch while these focused gates were in progress.
`../codex-ctcompile-loop.sh` now uses `codex exec --json --color never` and the
stdlib-only sibling `codex-ctcompile-output.py`. These files live outside this
Git repository. Commands, bounded output, current file diffs against HEAD,
agent activity/messages and errors display immediately as events arrive.
Reasoning and transport bookkeeping remain in the raw log. The normal `.log`
is readable; `.events.jsonl` retains the full stream, including stderr diagnostics.
Rate-limit detection reads the raw log. Pipeline exit status is preserved, logging
failures stop the loop, and completion requires a successful exit and the entire
required final sentence, rather than merely its last line.

`bash -n`, Black, the renderer's runnable `--self-test`, and four mocked loop
scenarios passed: success, misleading final-line completion, failed completion,
and rate-limit retry. The mocks also verify live flushing, hidden reasoning,
raw-log retention, edit diffs and agent completion. No real model call or loop
was launched. The original shell script is backed up in the evidence directory.
The Windows and home paths resolve to the same updated files.

## Exact next native boundary

The preserved 118-result `inherited-dispatch` now reaches Qi's lexical-super
call and refuses with `class receiver escapes or observes a prototype/descriptor`.
Prove immutable method `__home` and the immediate base for
`LoadHome -> GetProto -> GetProperty -> Call`, retaining Qi as the call receiver.
`LoadHome` has no operands: it reads the running closure implicitly. A direct
symbolic call alone is insufficient because the ordinary method lifter rejects
direct receiver calls. A bounded capture-free single-block base-method inline
is a candidate next slice, subject to its own proof and complete source census.

Authentic W/B/Qi still needs captured constructor helpers, ordinary static methods,
receiver-selected getters, complete H/r/s bodies, conditional configuration,
selectors, events and Popper. Own-data provenance, broader escape control
flow/ownership and the application driver remain. No whole-Bootstrap completion
or new browser compliance result is claimed.
