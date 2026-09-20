# Construction-time getters and left-shift bands — 2026-09-20 UTC

Continued clean `1c424be4` and its recorded construction-time constructor getter
boundary. The September 7 WIP was absent; no dirty interrupted draft remained.
Native fixtures, escape analysis and source review ran in parallel. Service
interruptions stopped the initial agents; root preserved and completed their
drafts and validation. A final independent source review found no soundness issue.

Linux executable/CLI-path checks found no Claude identity among 12 readable
processes, with 56 unread identities. Windows Get-CimInstance checked 345
processes without a Claude identity. Availability remained uncertain; concurrent
restrictions applied. No browser/runtime/shared-file edits or push.

## Landed

- `5b171856`: the own-field census recognizes that the fresh prototype's
  `constructor` backedge exists during construction, before any own field.
  Direct constructor reads and reads in construction-time instance methods
  still pass the existing `fieldsOnly` and exact `staticGetters` proof.
  Original getter bodies, evaluation order, dependency expansion and fresh
  allocations remain intact. Own constructor writes, prototype mutation,
  receiver escape, missing fields, early snapshots and getter effects refuse.
  Existing inherited getter target refusals remain in place.
- `2c7810ed`: left-shift index ranges may occupy one of the existing bounded
  ToInt32 conversion bands. Converted endpoints must remain in signed-i32
  bounds after multiplication, and scaled stride remains bounded. Input or
  output discontinuities and overlapping reloads still refuse. Five CFG and
  three SCF positives plus seven refusal rows cover both bands, masked zero
  counts, reversal, scaled gaps and boundaries. All 30 original source bodies
  are unchanged; source 16 is promoted and ten sources are appended.

Six native positives add **48 executions**: direct getter reads, instance
method reads, nested methods, getter dependencies, argument order and distinct
fresh empty objects. Twelve controls preserve fieldful getter returns, inherited
targets, shadowing, effects, missing fields and snapshots. Original class inputs
01–17 and authentic Bootstrap bodies are unchanged.

Inspected C++ uses stack class records, borrowed receiver pointers, direct method
calls and field assignments. The fresh-empty-object case uses the existing
`shared_ptr<identity_object>` representation for two independent allocations.
There is no Script/VM symbol, collector, closure environment, prototype table or
snapshot iterator container. This work does not change ownership lowering.

## Focused validation

Every devbox build/test command held `/tmp/ctbrowser-devbox-build.lock`. Commands,
logs, source hashes and generated examples: `/tmp/ctcompile-construction-getters-0726/`.

- Built explicit targets `ctjs-opt`, `ctjs-translate`,
  `ctcompile-test-host-contract`, `ctcompile-test-native-reference`,
  `ctcompile-test-escape-analysis-arrays`, `ctcompile-test-escape-claims` and
  `ctcompile-test-type-oracle` with `tools/remote-build.sh`.
- Exact `ctcompile_host_contract`: **1/1**, 0.49s (0.50s total).
- Exact `ctcompile_escape_analysis_arrays`: **1/1**, 1.40s (1.41s total).
- Lit filter `^ctcompile :: Analysis/Escape/escape-claims/(right-shift|composed|left-shift)-index-overwrite[.]test$`:
  **3/3**, 0.14s. The left-shift recording/claims report **120 sites / 21 sound /
  21 of 26 confined precision (80.8%)**, zero violations, partial, pending or
  unclaimed sites. Metrics were read from the existing artifacts without replay.
- Selected class probe: all 18 new getter cases, the 19 previous construction
  method cases, 18 `OWN_FIELDS` cases, six existing getter/inheritance controls
  and original B/Data+B. **63 observations / 168 main native executions /
  126 unprepared and 82 preparation refusals**. GCC/Clang, explicit/deduced
  output and both optimization settings are covered. New complete proof
  budgets: direct getter **1,593**, getter chain **1,924**, fresh empty objects
  **1,826**. Existing budgets: method read **1,622**, nested methods **1,864**,
  nearest override **5,447**, static defaults chain **552**, static chain **387**,
  repeated static reads **417**, field order **763**, clearing loop **1,333**.
  Missing identities, forged inputs, partial budgets and rooted super controls
  pass. Ancillary checks: **16 constructed-method executions / 20 refusals**,
  **eight original-r executions / four refusals**.
- All four native and four escape tested hashes match. Native fixture agent
  checked Node syntax/observations **18/18**; root checked all **40** escape
  sources for syntax, termination and independent retention (**25 confined /
  15 retained**). Original source preservation, changed C++ formatting, Python
  AST/Black and `git diff --check` pass.
- Required `tools/format.sh --check` retains **20 baseline diagnostics in six
  HEAD-identical files**. Changed files pass their formatting checks; no clean
  repository-wide formatting result is claimed.

An initial Bootstrap-only probe failed its stale diagnostic assertion. The
unchanged source was checked directly, the expected diagnostic was updated and
the final selection passed. No whole-class or full-suite pass is claimed.

Skipped: full CTest/compiler lit, whole class-initialization lit, DOM replay,
broad native/corpus matrices, full Bootstrap, WPT/test262, Windows and sanitizers.
The changed census executes only for closed-source own-field snapshots; DOM
continues to refuse that boundary before reaching these changes.

## Next boundary

Both authentic B and Data+B now refuse `class own-key snapshot constructor
observes its receiver`. After the getter reads in `_getConfig` pass the field
census, B's `e.set(this._element, this.constructor.DATA_KEY, this)` passes the
partially constructed receiver as an argument. That needs complete registration
effects and shared Map/stored-receiver ownership and lifetime, not a blanket
exception for calls. Preserve original `e.set`, `e.remove`, `P.off` and config
bodies. Variable field presence across B's guarded early exit is still unproved.

Inherited getter proof remains independently unfinished. The new same-target
Base/Leaf control refuses `inherited receiver getters require per-leaf target
proof`; a leaf-only getter refuses `constructor read lacks an exact local static
getter`, and nearest/distinct overrides refuse super normalization. A minimal
next proof needs effective nearest getter tables per constructor and consistency
of each shared original read's selected target, including transitive getter
dependencies. Preserve lexical captured Base identities. Deduplicate recorded
reads/getters and preserve provenance through constructor/method cloning before
changing those guards; distinct targets need separate receiver/body proofs.

Inherited DOM, static construction, config/selectors/events/Popper, broader
ownership, full Bootstrap and the application driver remain unfinished. Escape
analysis still refuses cross-band shifts, signed output wrapping and unproved
reload overlap.
