# Handoff: continuing ctcompile

> NOTE (Claude, 2026-09-16): `ctcompile-v1` history was **reworded** while Codex
> was stopped - every unpushed commit from `f7966251` (origin) forward now has a
> `ctcompile(<area>): ...` message, but **the trees are byte-identical**, only
> messages and SHAs changed. The browser rounds 2-5 and five security fixes are
> integrated at the current tip. Pre-reword tips are kept as
> `ctcompile-v1-backup-premsg2` / `-premsg`. Full detail is in the
> `SESSION HANDOFF` journal at the end of `../../AGENT-SYNC.md`. Just branch from
> the current `ctcompile-v1` tip - nothing about the native work changed.

Native work continues on `ctcompile-v1` in the `compile-time-browser` monorepo.
The application driver remains incomplete; native compiler development uses
`ctjs-translate` and `ctjs-opt`. Build on the devbox using `tools/remote-build.sh`
under `/tmp/ctbrowser-devbox-build.lock`, then run the local formatter before
committing. There is no CI. Do not build on the small local machine.

## Integrated-runtime recovery complete, 2026-09-17 UTC

Resumed clean `228d80d1` from this handoff and the interrupted **2026-09-16
22:29 AGENT-SYNC** gate. The native source had already landed: `c8b9856d`
(original anchored prefix), `bf872db3` (branch-local confined filter scheduling),
and `c7d1b1a2` (held bounded Neg stride). Claude reworded their old SHAs without
changing the trees. September 7 WIP is already an ancestor; no branch was replayed.

The integrated browser's Annex B fix gives a block function a local binding.
The former duplicate-export fixture therefore has one global publication and now
correctly admits. Its exact original source is preserved as a native/Node/VM positive;
an explicit repeated `var` export retains the refusal, with one-versus-two raw
publication assertions. An isolated before/after probe confirms the scope distinction.
No compiler or browser semantics changed during recovery.

The fresh devbox build passed **817 targets**. The full gate passed **309/310
CTests (1978.86s)**: all **309 non-lit tests**, and **256/257 lit (1720.91s)**;
only that stale expectation failed. After the test-only correction, **3/3 focused
CTests (143.02s)** passed, including the complete Strings driver **1/1 lit (142.57s)**.
Thus every test has passed across the full and corrected focused runs; this is not
a single all-green full invocation. Both sets of **1,792 hashes** match locally and
on the devbox, and only `native_dom_strings.py` differs between them. Logs, failed
and corrected manifests, generated-code probes and reports are preserved under
`/tmp/ctcompile-runtime-resume/`; the corrected workflow exits **0**.

Stable clang-format 23.1.1 passes **890 C++ / 107 Python / 105 web files**. The
required pinned formatter reproduces the same **nine unchanged HEAD files / 26
diagnostics**, matching the predecessor baseline. Independent bounded static reviews
found no actionable prefix/filter or held-Neg proof defect. WPT/test262 corpus
measurements were not rerun.

Fresh full Bootstrap remains **19/574 native / 0 of 47 globals**, without skips or
pruning. Data remains **7/7**, Button **4/86** with **22 agreeing Node/VM lifecycle
observations**; all four reports are byte-identical to the previous frozen gate.
Original M retains **24 nine-register blocks / handler ^bb12**. In all four
provider/policy modes, the original filter, count loop and anchored-prefix loop each
compile **two native functions**. Full H still refuses **DOM helper branch contains
an unproved local identity**; Unicode key normalization and live dataset values each
refuse **DOM property read lacks a proved receiver and supported member**.

**Exact next:** present own `t.dataset[n]` String reads through public
`ctbrowser::dataset_value`, proving the exact element, immutable snapshot member and
unchanged enumeration epoch. Fresh dataset lookup cannot renew a stale key. The
existing iterator completion normalizer already keeps the key and consumer together;
generic String joins must not gain membership. The independent escape continuation
is preserving held signed Number snapshots through unary Plus and a second Neg.
Full H also needs its fresh result-object/valid-element guard, Unicode first-code-unit
normalization, loop-local M calls and dynamic assignment/collision semantics.
Node/VM still disagree for `bsÉtage` and `bsİtem`; this oracle gap is journaled for
Claude. Config/inheritance/defaults, retained callbacks and the application driver
remain open. Older sections below are historical checkpoints.

## Original snapshot iteration and scalar loop completion, 2026-09-16 UTC

Resumed clean **9b02a344**, its HANDOFF and the **19:22:54 AGENT-SYNC** journal:
the unfinished thread was the original filtered `for...of`, with source regressions
preserved at `/tmp/ctcompile-dom-iterator-tests/native_dom_dataset.loop-draft.py`.
Both histories and unmerged branches were checked; September 7 WIP was already landed
and older lens/JSON branches were explicitly superseded. Three agents split source
normalization, regressions and escape review. Root recovered two service-limited
agents' work; the source agent completed independent proof and next-boundary reviews.

**a607afc6** preserves source loop condition/yield tuples and exact register
correspondence through completion dispatch. An inactive slot may disappear only with
a constant predicate, an unused destination and a same-type live state replacement;
all source operations are accounted for. Original Array iterator and open/next/close
identities are explicit host premises. Iterator preparation reuses the complete
dataset/filter prefix proof on a private clone, then reproves the complete entry.
Scalar Number/String/Boolean loop state is supported. Each indexed String read needs
the exact immutable vector, a zero-start/unit-increment index and dominance by that
vector's `index < length` true arm. Vector writes, loop DOM mutations, escaped helpers,
unknown calls and insufficient budgets still refuse without published evidence.

**d9112248** connects those proofs to native String inference and owning `vector.at`
extraction. Original count, ordered-key and saved-snapshot loops execute unchanged;
generated C++ uses ordinary vectors, strings and loops, with no Script/VM/GC symbols.
The driver preserves all previous source witnesses and adds iterator/prototype,
mutation, escape and missing-premise controls. Independent prepared-loop tests cover
incorrect starts, updates, guards, vectors, branch placement, forwarded slots and every
insufficient proof budget. No browser implementation or runtime semantics changed.

Corrected focused build, **3/3 CTests (0.89s) / 2/2 lit (34.66s) PASS**. Dataset:
**15 sources / 45 Node-VM source-double observations / eight GCC-Clang binaries /
228 refusals**, HTML/SVG and lifetime sanitization. The first build's const MLIR
handle API error was fixed; failed logs and hashes are retained. The escape review
found no defect in the landed negative-Sub proof; no escape extension was started.
Historical escape results remain **895 observed sites / 40 sound / zero violations /
40 of 172 precision (23.3%)**, with all **1,123 snapshot rows unchanged**.

Complete build and **305/305 CTests (2261.10s) / 256/256 lit (1977.19s) PASS**,
wrapper **0**, with no skips. All **1,774 frozen input hashes** match locally and on
the devbox before documentation edits. Fresh full Bootstrap remains **19/574 native /
0 of 47 globals**, Data **7/7**, and Button **4/86** with **22 Node-VM lifecycle
observations**. Both Bootstrap policy reports and Data/Button reports are byte-identical
to the previous gate; no full-bundle admission gain is claimed. Original M retains
**24 nine-register blocks / handler ^bb12**. The full WPT/test262 corpus measurement
was not rerun. Stable formatting passes **879 C++ / 104 Python / 105 web**; the required
pinned formatter reproduces the unchanged **nine files / 26 diagnostics** baseline.

**Exact next, measured in all four provider/policy modes:** the original count loop
now compiles **two native functions**, as does the filter. Full original
`H.getDataAttributes` now refuses **DOM helper branch contains an unproved local
identity**. Its filter closure is created inside the continuation of `if (!t)`, so
branch-local callable scheduling (or proof of that exact valid-element guard) remains
before full H admission. The isolated original `n.replace(/^bs/, "")` loop and the
following `charAt(0).toLowerCase() + slice(1)` loop both refuse **DOM property read
lacks a proved receiver and supported member**. The smallest independent String step
is exact anchored ASCII-prefix removal through ordinary String operations, with
original String/RegExp/factory identities and complete effect/use proof.

The next-key witness measures six Node/VM cases: ASCII, empty, emoji and supplementary
Deseret agree; `bsÉtage` gives Node `étage` versus VM `Étage`, and `bsİtem` gives Node
`i` + U+0307 + `tem` versus VM `İtem`. Current VM charAt/slice use byte positions and
lowercase is ASCII-only. The original expression selects one UTF-16 code unit in JS;
lowercasing the first full Unicode code point would also mishandle the Deseret case.
Record/coordinate this oracle boundary before claiming general key normalization;
the filter alone does not prove an ASCII suffix. No runtime expectations changed.

Live values can reuse public `ctbrowser::dataset_value`, but indexed-key provenance,
same-element presence and mutation epochs must be proved; missing own properties
have Undefined/prototype semantics. Loop-local M calls and dynamic result assignment
remain separate proofs: collisions preserve assignment order and `__proto__` uses its
inherited setter. Full Config/inheritance/defaults, retained callbacks and the
application driver remain open. The read-only `next-key-review.md` and executable
`next-probe.py` under the evidence directory describe these seams.

Claude's **6edb7421** browser work is still separate from this gate: Annex B/catch
bytecode, new globals and platform changes require fresh differential validation when
integrated. Evidence, source hashes, generated C++, reviews and fresh boundary probes:
`/tmp/ctcompile-iteration-resume/`. The previous sections are historical checkpoints.

## Owning snapshot length and negative Number strides, 2026-09-16 UTC

Continued the clean **26ba2f8e** handoff and its exact original dataset iteration
boundary. Both histories and unmerged branches were checked; September 7 WIP was already
an ancestor and old lens branches were explicitly superseded in the journal. Three
agents split source preparation, regressions and escape analysis. Two hit service
limits; root recovered their drafts. A further read-only review found no proof defect;
root removed a duplicate new oracle before the source freeze.

**0f3fbe91** proves `.length` on an owning original dataset-key or filtered String
snapshot. Evidence identifies each exact property read; existing `vec_length` lowering
returns its Number without a browser/VM lookup. Filter identities, callback confinement,
snapshot immutability and complete-budget proof remain required. The original `for...of`
source has not been rewritten into `.length`. The next-loop count/order/saved-snapshot
regression draft is preserved separately at
`/tmp/ctcompile-dom-iterator-tests/native_dom_dataset.loop-draft.py`.

**e1bbd9e4** proves increasing dynamic Sub latches with bounded original negative Number
strides, including the frontend's single literal negation and an unchanged carried
stride. It reuses the existing exact initialization/final-update proof; commuted
subtraction, String/BigInt/unknown and changing strides remain refused. CFG/SCF tests
retain returned children and cover zero trips/overshoot.

Corrected focused build, **3/3 CTests (0.91s)** and **3/3 lit (27.32s)** pass, including
**12 dataset sources / 33 Node-VM source-double observations / eight GCC-Clang binaries
/ 150 refusals**, HTML/SVG and lifetime sanitization. The first array CTest identified
three old UnknownIndex assertions. The unchanged negative-offset sources now prove an
exact absent index/growing length, so still refuse at MissingElement. All three
assertions were corrected and the array CTest rerun successfully.

New escape oracle: **15 observed sites / six sound / zero violations / six of eight
precision (75%)**. Historical oracle remains **895 sites / 40 sound / zero violations /
40 of 172 precision (23.3%)**. The existing **1,123-row** snapshot still matches. Array
suites cover **567 dense / 174 induction / 123 structured rows**, with **21,928 / 12,506
/ 7,047** conservative budget cutoffs.

Complete build and **305/305 CTests (2262.84s) / 256/256 lit (1983.71s) PASS**, wrapper
**0**, with no skips. All **1,772 frozen input hashes** match locally and on the devbox
before these documentation changes. Fresh full Bootstrap remains **19/574 native / 0 of
47 globals**, Data **7/7**, and Button **4/86**, including all **22 Node-VM lifecycle
observations**. Both Bootstrap policy reports, Data, Button and next-boundary reports
are byte-identical to the prior gate. Original M preserves **24 nine-register blocks /
handler ^bb12**. The full WPT/test262 corpus measurement was not rerun. Evidence lives
in `/tmp/ctcompile-dataset-iteration/`. Stable formatting passes **877 C++ / 104 Python
/ 105 web**; the pinned formatter's **nine files / 26 diagnostics** independently match
unchanged HEAD files. No browser implementation or runtime semantics changed.

Exact next (original IR is now local at `/tmp/ctcompile-dataset-filter-next/`): the
count-only filtered `for...of` still refuses **DOM helper completion observes an
inactive value**; full original H refuses **DOM helper completion requires acyclic
structured source**, each in all four provider/policy modes. The loop needs proofs of
iterator-helper identity, original Array iterator behavior, scalar loop state and
inactive completion slots. Its before region contains an index switch; after-region
arguments forward count/index/completion slots. The completion copier currently returns
one function-result value and cannot model that loop terminal tuple merely by accepting
While. Read-only details are in
`/tmp/ctcompile-dataset-iteration/iteration-boundary.md`. Native snapshot length is now
available for its index path; String extraction still needs an exact integral in-bounds
index proof before using existing `vec_at`. Then key normalization, live dataset reads,
M composition and dynamic result writes remain, as do full Config/inheritance, retained
callbacks and the application driver. No full-bundle admission gain is claimed by this
prerequisite.

## Original dataset filter and moved-VM recovery, 2026-09-16 UTC

Resumed the interrupted dataset filter and runtime integration from **17 dirty
ctcompile paths** and the **16:07:28 abandoned-loop AGENT-SYNC entry**. Both commit
histories and pending branches were checked; September 7 work was already landed.
Three delegated recovery tasks hit service limits before editing; root completed
them. A later agent reviewed the next iteration boundary and found no concrete defects
in the filter proof.
Claude's integrated runtime is the differential oracle; no browser source changed.

**32155832** compiles Bootstrap's original
`Object.keys(t.dataset).filter(t => t.startsWith("bs") && !t.startsWith("bsConfig"))`.
The complete proof requires original Object/keys, Array/filter/default species and
String/startsWith identities, a confined capture-free callback, a Boolean result,
and literal ASCII prefixes. The callback becomes an ordinary native function;
`std::copy_if` produces an owning String vector. Source callbacks, effects,
identity uses, budgets and duplicate function indices remain checked. No VM, GC,
Script symbol, copied browser implementation or external dependency was added.

**bf7a56d5** fixes native and host-prefix String concatenation after the VM's
surrogate normalization changed. Both ordinary and optional String operands reuse
public `ctbrowser::join_surrogates`; standalone programs need public Core headers
and link no browser library. The unchanged `split41` witness now agrees with the VM;
new optional-String and provider snapshot-key checks cover both compiler paths.
The MLIR Analysis object target now declares C++23 for that public header.

**4bf39afe** proves the frontend's exact static-getter `__home` assignments before
removing unobservable setup. Wrong/repeated homes, identity observation and missing
proof budgets still refuse. Class checks cover **69 source observations / 152 native
executions / 138 unprepared and 84 preparation refusals**, plus constructed-method
controls. **9659f0bf** updates preserved Button and sloppy-undefined source witnesses:
Node and VM now agree on **22 original Button lifecycle observations**, inherited
statics and the two inheritance isolates. Explicit raw-IR writes to the fixed
undefined binding remain refused; an empty conditional still lacks a global-owner
proof. DOM checks cover **19 entries / 42 refusal controls**.

**56a39f38** preserves delete sources at their new opaque host-call boundary,
including zero-fact controls, while retaining positive provider-object coverage.
Provider suites pass **39 object / 29 diagnostic cases**. **0e0fa2d7** refreshes the
measured escape snapshot: **1,123 rows**, changed bytecode offsets/program hash and
**79 Stored-to-Passed reasons**, with every observation and confinement result
unchanged. Historical oracle: **895 sites / 35 unclaimed / 40 sound / zero violations /
zero partial or pending / 40 of 172 precision (23.3%)**.
Previously landed **618f5775** commuted Add induction is included in this gate:
**15/15 observed sites / 6 of 8 precision / zero violations**. Current array suites
cover **567 dense / 151 induction / 109 structured rows**, with **21,925 / 11,227 /
6,312** conservative budget cutoffs.

Focused build, **3/3 CTests (0.87s) / 13/13 lit (279.05s) PASS**. Dataset tests cover
**11 sources / 29 Node-VM source-double observations / eight GCC-Clang binaries /
146 refusals**, both providers/policies/layouts, HTML/SVG and lifetime sanitization.
Complete build and **305/305 CTests (2238.51s) / 255/255 lit (1954.77s) PASS**,
wrapper **0**; no tests skipped.
All **1,771 frozen input hashes** match local and devbox sources before these docs.
Stable formatter 23.1.1 passes **877 C++ / 104 Python / 105 web**; the required pinned
formatter's **nine files / 26 diagnostics** independently reproduce from unchanged
HEAD files. Evidence: `/tmp/ctcompile-filter-1624/` (full/focused logs and exits,
source manifest, generated C++, measured reports, escape snapshots and next probes).
Earlier failed focused runs are archived; their four failures were repaired before
the green run. The full WPT/test262 corpus measurement was not rerun.

Fresh full Bootstrap remains **19/574 native**, with **0/47 globals resolved**
(the imported global denominator was previously 43), without skips or pruning.
DOM Data remains **7/7** and Button **4/86**. The separate dataset filter compiles
**two native functions** in all four provider/policy modes; original M/H and Config
typeof/spread each compile one. No full-bundle admission gain is claimed.

**Exact next native boundary:** the count-only `for...of` over the original filtered key
snapshot refuses **DOM helper completion observes an inactive value** in all four
provider/policy modes. Full original `H.getDataAttributes` refuses **DOM helper
completion requires acyclic structured source** in all four modes.
Prove the original for-of helper/iterator identities over the owning dense String
vector, preserve snapshot order/lifetime and scalar loop-carried values, then reuse
existing vector length/index helpers and SCF lowering. Current DOM preparation and
entry proof reject loop regions and block arguments; allowing a loop alone does not
prove its open/next/close calls. Then address original `replace(/^bs/, "")`, dynamic
key normalization, live `dataset[n]` reads with Undefined/prototype semantics, M
composition and dynamic result writes (`__proto__` assignment is a setter, unlike
spread). Matching/live F keys, r(e), inherited defaults/initialization, retained
callbacks and the application driver remain open. Original M still has **24
nine-register blocks / handler ^bb12** before preparation.

Earlier entries below are historical checkpoints.

## Dataset key snapshots and dynamic Add induction, 2026-09-16 UTC

Continued **c5682b0f** and the **10:24:07 AGENT-SYNC** next-boundary journal.
The starting tree was clean; both histories and unmerged branches were checked.
Earlier interrupted work was already landed or explicitly superseded. Three agents
were delegated independent work; service limits stopped their drafts before edits.
Root implemented and gated both concerns. Two agents later reviewed the proofs;
one found a dominance flaw in a negative test, corrected before the final gate.

**2dbd73b6** compiles `Object.keys(element.dataset)` into an owning
`std::vector<std::string>` through public `ctbrowser::dataset_entries`. The explicit
`dataset_parameters` subset contracts HTML/SVG inputs, validated before source
effects; original Object/keys identity and receiver are mandatory. Attribute order,
numeric keys, current public DOM namespace/uppercase exclusions, saved snapshots
after mutation and post-document ownership pass. A saved dataset alias cannot
cross a DOM mutation before enumeration. No Script/VM/GC, DOMStringMap implementation or new dependency
is emitted. MathML namespace URIs remain in Shell, outside this contract.

**3d80df96** accepts dynamic Add latches (`i += 1`) under the same exact bounded
Number start, positive stride, final-update and retention proof as static Add.
String/BigInt/unknown operands, zero stride and unsupported updates remain refused.
The new original-source oracle reports **15/15 sites, 6/8 precision, zero violations**;
the historical fixture remains **40/172 precision**, zero violations. Array suites
cover **567 dense / 131 induction / 103 structured rows**, with **21,925 / 10,077 /
5,979** conservative budget cutoffs.

Focused **2/2 proof/runtime CTests (0.33s), 2/2 lit (81.59s) PASS**.
Dataset: **five Node/VM source-double observations, eight GCC/Clang binaries,
160 native observations, lifetime sanitizer and 50 refusals**, both providers,
policies and layouts, HTML and SVG. JSON remains **18 sources / 486 observations /
eight binaries / 260 refusals**. Every insufficient dataset proof budget withholds
all evidence. The source-double uses a DOMStringMap-shaped `ownKeys` Proxy.

**Real-browser witness:** Chromium preserves `[bsZ,10,2,01,"",__proto__]` and
adds `later` when enumerating a saved dataset after mutation. Existing Shell/VM
sorts that prefix to `[2,10,bsZ,01,"",__proto__]` and omits `later`. Both differences
were measured with Playwright and the existing ctdrive, and journaled for Claude.
A further Chromium witness includes namespaced `data-hidden` and `p:data-other`
attributes in HTML/SVG dataset keys. Existing public DOM `dataset_entries` and
`dataset_value` skip namespaced attributes; Shell delegates to that same core.
Native retains this shared platform limitation. Its fixture expectations must move
with a future core fix; namespace-witness.html/log/json records the discrepancy.
No browser/runtime source or expectations changed; full WPT/test262 was not rerun.

The first full run found one stale refusal in `array_borrow.py`: bounded `i += 2`
now passes the same induction proof. **beb5c1db** executes that source with
Node/VM/native observations and copy controls; a String stride remains refused.
The repaired borrow suite passes **80 native executions / 24 copy controls /
38 refusals**, with **1/1 array CTest (0.84s) and 2/2 lit (56.21s)**. Initial
**287/288 CTests (2325.81s), 253/254 lit (2049.19s), wrapper 8** are archived as
`full-failed-1.*`; compiler code was unchanged. The complete suite was rerun.

Complete build, **288/288 CTests (2307.84s), 254/254 lit
(2039.47s) PASS**, wrapper **0**. All **1,725 frozen input hashes**
match local and devbox sources before this documentation update. Stable formatter
23.1.1 passes **832 C++ / 104 Python / 105 web**; required pinned formatter retains
the byte-identical **nine-file / 26-diagnostic** baseline. Evidence:
`/tmp/ctcompile-dataset-keys/` (logs/exits, manifests, measured.json, generated C++,
next source probes and browser-witness.json).

Fresh full Bootstrap stays **19/574 native / 0 of 43 globals**, without skipping or
pruning. DOM Data stays **7/7**; Button **4/86**, with 22 Node observations and its
known VM inheritance failure. Those reports and all **1,123 historical escape rows**
are byte-identical to the prior gate. No full-bundle improvement is claimed.

**Exact next native boundary:** the original
`Object.keys(t.dataset).filter(t => t.startsWith("bs") && !t.startsWith("bsConfig"))`
refuses all four provider/policy modes at **DOM helper callable escapes or its call
shape is unsupported**. Prove the original `Array.prototype.filter` and
`String.prototype.startsWith` identities, default Array species, and callback use
over the owning key snapshot without exporting the callback.
Full `H.getDataAttributes` still first refuses **DOM helper completion requires
acyclic structured source**, even with Object and dataset_parameters supplied.
Then prove original iteration, dynamic key normalization, live dataset value reads
(Undefined/prototype semantics), M composition and dynamic writes (`__proto__`
assignment is a setter, unlike spread). Original M retains **24 nine-register
blocks / handler ^bb12**. Matching/live F keys, r(e), inherited defaults and
initialization, retained callbacks and the application driver remain open.

Earlier entries below are historical checkpoints.

## Guarded Config spreads and recovered escape work, 2026-09-16 UTC

Resumed the interrupted 09:13/09:15 Config-spread and negated-guard drafts,
found as six dirty files plus `negated-guard.test` and the abandoned 09:15:56
AGENT-SYNC loop. Both histories/unmerged branches were checked; September 7 WIP
was already an ancestor. Three agents recovered regressions, checked proof
soundness and surveyed the next independent boundaries. No browser/VM source changed.

**fcd4a0c2** compiles original Config's guarded JSON spread, including two ordered
spreads. Preparation preserves fresh data objects and immutable branch cell reads;
writer locality is checked before source-order comparisons. The complete DOM proof
requires object-tagged JSON (null, arrays or objects), fresh direct targets and all
writes before any copying observation. Members, identity, captures and later/alias
mutation remain refused. Output owns `ctbrowser::json_value`, explicitly constructs
empty objects, and uses standard C++ for own-key order and overwrites. Numeric keys,
duplicate keys, `__proto__`, array indices, fallbacks and post-document lifetime pass.
Core JSON parser behavior, including nested object storage order, is unchanged.

**344b514d** proves equivalent `!(index >= length)` / `!(length <= index)` array
guards only for the existing bounded Number induction. Source order, retained
children, immutable length and complete-budget requirements survive. The new oracle
observes **15/15 sites, 6/8 precision, zero violations**. Array suites cover
**567 dense / 118 induction / 100 structured rows**, with **21,925 / 9,090 / 5,843**
conservative budget cutoffs. The source's negations remain in imported IR.

Focused **4/4 proof/runtime CTests (3.91s) / 4/4 lit (80.02s) PASS**. JSON covers
**18 sources / 486 Node-VM observations / eight GCC-Clang binaries / 260 refusals**,
both providers/policies/layouts and lifetime sanitization. Complete build and
**288/288 CTests (2305.41s) / 252/252 lit (2029.51s) PASS**,
wrapper **0**; all **1,721 frozen input hashes** match local and devbox sources
before docs. Stable formatter 23.1.1 passes **831 C++ / 103 Python / 105 web**;
required pinned formatter retains its unchanged **nine-file / 26-diagnostic** baseline.
Evidence: `/tmp/ctcompile-spread-resume/` (`measured.json`, full/focused logs and exits,
manifests, generated C++, source probes). Initial mixed-upload and emitter/preparation
failures are archived and superseded by the final frozen gate.

Full Bootstrap remains **19/574 native / 0 of 43 globals** without skips/pruning;
DOM Data **7/7**, Button **4/86** with 22 Node observations and its known VM
inheritance failure. These reports and all **1,123 historical escape rows** are
byte-identical to the previous integrated gate; precision remains **40/172**, zero
violations. No full-bundle gain is claimed. H, Config typeof and Config spread each
compile in all four modes; original M retains **24 nine-register blocks / handler
^bb12** before preparation.

**Exact next native boundary:** original `H.getDataAttributes(e)` refuses in all
four provider/policy combinations at `DOM helper completion requires acyclic
structured source`. Its loop, `Object.keys(t.dataset).filter(...)`, dynamic key
normalization and per-key `M(t.dataset[n])` reads remain intact. Public
`dom/dataset.hpp` already supplies owning `dataset_entries` and `dataset_value`;
no browser extraction is needed for those reads. Prove the original iteration and
Object identity, namespace eligibility, key snapshot versus live reads, missing-key
semantics and dynamic writes. Its `e["__proto__"] = value` assignment has setter
semantics, unlike spread's own-data definition. Full Config additionally needs
`r(e)`, inherited defaults/initialization, retained callbacks and the driver.
Matching/live F keys remain refused. The independent plan25 continuation is bounded
Number `i += 1` induction: the shared latch recognizer still requires
`BinaryStaticOp`, although dynamic Add already uses `boundedNumberSum`.

Earlier entries below are historical checkpoints.

## Config JSON tags and reversed array guards, 2026-09-16 UTC

Continued the exact Config `typeof` boundary recorded in **cb002682**, HANDOFF,
plan00 and the 07:09:54 AGENT-SYNC journal. The starting tree was clean; both
commit histories and unmerged branches confirmed the interrupted September 7
and JSON-chain work were already resolved. Three agents split native regressions,
proof review and an independent escape-analysis increment. Root recovered two
service-limited test drafts, reviewed them and ran the gates. No browser/VM source
was changed by this work.

**c127ba96** compiles `"object" == typeof H.getDataAttribute(element, "config")`
and the String tag itself. The complete DOM proof authorizes the observation;
generic JsonType and JSON member access remain refused. Emission reads the owning
`json_value.data` with standard `holds_alternative`, without a variant copy at
the observation. Null, arrays and objects all report `"object"`. The original M/H
source, nullable guard, lookup order and both failure snapshots remain intact.
Two initial emitter gate failures are archived; the final form reuses ordinary
deferred member emission before the conditional expression, with no printer change.

**42a2bd80** accepts the equivalent strict array guard `length > index` through
the shared CFG/SCF induction proof. It preserves source operand order, Number,
start/stride, array stability, retained children and complete-budget requirements.
Fifteen CFG/SCF cases and one original-source oracle were added. The new oracle
observes **9/9 sites, 3/5 precision and zero violations**; it keeps the returned
child escaping and the inclusive guard conservative. Array suites now cover
**567 dense / 102 induction / 90 structured rows**, with **21,925 / 8,211 / 5,414**
conservative budget cutoffs respectively.

Focused **3/3 proof CTests (3.88s) / 3/3 lit (55.27s) PASS**. JSON covers **15 sources /
342 Node-VM observations / 8 GCC-Clang binaries / 172 refusals**, both
providers/policies/layouts and post-document lifetime sanitization.

Claude integrated **28878c6c / 0b1e0911** during the first full gate, followed by
the **4c4f8e7b** documentation update. The audit centralizes plain C++ helpers in
`ctcompile/CTNative/Runtime/ctnative.hpp`, removes source-name provenance and
requires combined drivers to hoist `CTNATIVE_` defines with includes. JSON `typeof`
now keys on its carrier; the removed flag controlled include selection.
Independent integration review found no Script/VM/AOT dependency or ownership
change. The pre-audit build and 288/288 CTests (253 lit) passed, but the final
source check detected the concurrent merge and the wrapper exited **1**. Its
results in `/tmp/ctcompile-config-typeof/` are not a gate for the current tip.

Integrated build and **288/288 CTests (2252.04s) / 251/251 lit (1973.33s) PASS**, wrapper
exit **0**. All **1,720 frozen source/submodule hashes** match the devbox and integrated
**4c4f8e7b**; documentation changed afterward.
Stable formatter 23.1.1 passes **831 C++ / 103 Python / 105 web**. The required
pinned formatter retains the existing **nine-file / 26-diagnostic** baseline.
Evidence: `/tmp/ctcompile-config-integrated/` (`gate.log`, `gate.exit`, manifests,
`full-last-test.log`, `native-json.cpp`, `next-measured.json`, `measured.json`).

Fresh full Bootstrap remains **19/574 native / 0 of 43 globals**, both policies,
with no skips or pruning. DOM Data remains **7/7**, Button **4/86**, with 22 Node
observations and the unchanged VM inheritance failure. Their measured reports and
all **1,123 escape rows** are identical to `/tmp/ctcompile-m-gate/`; historical
escape precision remains **40/172**, with zero violations. No full-bundle gain
is claimed.

**Exact next native boundary:** adding Config's following spread
`{..."object" == typeof parsed ? parsed : {}}` refuses in all four modes at
`DOM helper branch contains an unproved local identity`. The branch-local empty
object reaches `DOMSource.cpp` before the final `copy_props` proof. Preparation
currently rejects the nested constructor; its later object census also assumes
every top-level constructor is a callable method holder and erases it. Preserve
proved data constructors through both stages, then require complete entry proof
of the spread and ownership. Null contributes no entries, arrays contribute indexed
entries, and the object tag alone grants no member proof. The existing escape
`CopyProps` certificate covers fresh fixed own-data objects, not runtime JSON keys
or enumeration order; generic native lowering has no `CopyPropsOp` case. Reuse
`carrier::json` ownership, explicitly constructing its object alternative for `{}`
(default `json_value{}` is null), and prove key order and overwrite behavior.
Spread is shallow: copying or moving an owning tree also needs a proof that surviving
aliases cannot distinguish it. Preserve numeric/duplicate/`__proto__` keys, array
indices without `length`, fallback inputs and post-document ownership in source tests.
Then compose dataset/config merging through the existing public `dom/dataset.hpp`.
Original M still has **24 nine-register blocks / handler ^bb12** before preparation.
Matching/live F keys, inherited static/object-valued defaults, initialization,
retained callbacks and the application driver remain open. Pending browser/runtime
branches remain Claude-owned and require fresh differential validation when landed.
The entries below are historical checkpoints.

## Original Bootstrap attribute normalization, 2026-09-16 UTC

Resumed **codex-m-finish's four dirty M-prefix files**, identified through the
04:52 AGENT-SYNC journal, the abandoned 04:53:42 loop and **e7c14d27**. Started
at **5e3d3b86**, with the test-registry audit merged. Three agents split tests,
declaration metadata and review; root recovered service-limited drafts and
completed integration. The old September 7 and JSON-chain threads were already
resolved. No other branch or browser/runtime source was changed.

**32032893** compiles the byte-pinned original M and
`H.getDataAttribute(element, "config")`. Number truthiness reuses the existing
NaN/zero-aware conversion. Boolean, Number, null and optional-String alternatives
join the existing owning `ctbrowser::json_value`; optional bytes are copied only
inside the selected present arm. Original lookup order, nullable guards and both
failure snapshots survive. Emitted C++ calls public Core/DOM APIs and uses ordinary
RAII, without Script/VM/GC dependencies. The API is documented in **a1bc655d**.
F's constant-key no-match proof already existed and is reused; do not reimplement
that slice.

**40f1f3ef** independently imports classic-script `program::hoisted_vars` into
fingerprinted module metadata. Shared host validation rejects malformed names and
absent-binding contradictions. Prefix identity proof recognizes a declaration
without an Undefined store, while its value, callability and intrinsic authority
remain unknown. Regressions remove only bare declaration stores to simulate
Claude's pending runtime correction. This supplies the compiler prerequisite for
removing the temporary compatibility restoration in **7ad52ce2**; the runtime
change remains Claude-owned.

Focused **2/2 proof CTests (3.73s) / 4/4 lit cases (33.94s) PASS**. JSON now covers
**12 sources / 219 Node-VM observations / 8 GCC-Clang binaries / 144 refusals**,
both providers/policies/layouts and a post-document lifetime sanitizer. Independent
reviews of the proof and generated C++ found no material issue. Complete
**375-step build / 288/288 CTests (2339.80s) / 252/252 lit
(2056.50s) PASS**, wrapper exit **0**. All **1,742 frozen source/submodule
inputs** match the devbox and
implementation commit **32032893**; documentation changed afterward. Formatting
23.1.1 passes **844 C++ / 105 Python / 106 web**; the required pinned formatter
retains the existing **nine-file / 26-diagnostic** baseline. The audit moved many
CTest registrations into lit, so totals differ from the previous 605/177 registry.
Evidence: `/tmp/ctcompile-m-gate/`, including full logs/exits, manifests,
`native-json.cpp`, `next-measured.json` and the final measured report.

Fresh full Bootstrap remains **19/574 native / 0 of 43 globals**, both policies,
with no skips or pruning. DOM Data remains **7/7**, Button **4/86**, with 22 Node
observations and the unchanged VM inheritance failure. All **1,123 escape rows**
match the current pinned baseline. Reports are unchanged except for Button's test
path after the lit migration. No full-bundle gain is claimed.

**Exact next native boundary:** original `_mergeConfigObj` performs
`"object" == typeof H.getDataAttribute(element, "config")`. The attribute call now
compiles to one native function in all four provider/policy combinations; adding
that JSON-result `typeof` still refuses at **`ctjs.unary`**. Original M's imported
**24 nine-register blocks / handler ^bb12** remain intact before preparation.
Prove that JSON observation next: the object tag includes null and arrays, so it
must not narrow to object members alone. Then come JSON object spreads and
dataset/config merging; public `dom/dataset.hpp` already
provides the dataset core. Matching/live F keys remain refused separately.
Inherited static/object-valued defaults, initialization, retained callbacks and the
application driver remain unfinished. Claude's pending runtime/audit branches
remain his to land and require fresh differential validation when integrated.
The entries below are historical checkpoints.

## Native JSON chain recovered and gated, 2026-09-16 UTC

Resumed **efe8daa8**, identified in the previous handoff and AGENT-SYNC, by
replaying its draft over the audit landings in `codex-json-resume-20260916`.
**53b9f68a** and **0a7c0302** finish that thread; the old `claude-json-chain`
draft is superseded. Three agents split recovery review, native regressions and
the Bootstrap boundary survey. September 7 WIP is already an ancestor.

**53b9f68a** recovers bounded checked-call chains in source order. JSON member
origins follow complete register flow; both failure paths retain the exact saved
input. Zero-call limits, every insufficient budget and failed proofs publish no
partial evidence or source mutation. **0a7c0302** adds explicit original JSON/parse
identity, reuses the existing `JsonType` only behind a complete DOM proof, and
emits public `ctbrowser::parse_json` with owning `ctbrowser::json_value` results.
Success moves from `std::expected`; String failure arms own their bytes. No Script,
VM, collector, generic JSON fallback or second parser is emitted. **47d6e275**
documents the contract in [native DOM entries](native-dom-entry.md).

Focused gates passed **2/2 proof tests (3.92s)** and **3/3 DOM drivers (178.24s)**.
JSON covers **7 sources / 14 Node-VM observations / 8 GCC-Clang binaries / 72
refusals**, both providers, policies and layouts, plus a lifetime sanitizer after
document destruction. DOM Strings now reports **775 Node-VM observations / 8
binaries / 1,060 source refusals**, with its separate provenance/budget checks.
Complete **310-step build / 605/605 CTests (1706.00s) / 177/177 lit (1305.06s)
PASS**; full wrapper exit **0**. All **1,561 frozen input files / 113 submodule
files** match the devbox and implementation commit **0a7c0302**; API and checkpoint
docs were updated afterward. Evidence: `/tmp/ctcompile-json-resume/`, including
`full.log`, `full.exit`, `full-last-test.log`, manifests and measured JSON reports.

**a244a2f9** fixes remote sync with `rsync --checksum --no-times`: changed older
worktree contents invalidate Ninja, while identical files keep their timestamps.
Standalone rsync/shell checks pass. The first candidate run mixed stale objects
and is invalid; the fresh compile's const-MLIR-handle test error was fixed before
the green gates. The integrated **09341902** baseline also passed **604/604 CTests
(1503.00s)**. Formatting with 23.1.1 passes **844 C++ / 95 Python / 106 web**;
the required pinned formatter retains the same nine-file/26-diagnostic baseline.

Fresh Bootstrap remains **19/574 native / 0 of 43 globals**, both policies, without
skips or pruning. DOM Data remains **7/7**, Button **4/86**, with 22 Node lifecycle
observations and the unchanged VM inheritance failure. Those reports are identical
to the preceding measurements. All **1,123 escape rows match the current pinned
baseline**; only the already-landed audit program hash differs from the older
pre-format evidence. No browser/runtime or escape-analysis source changed.

**Exact next native boundary:** original `H.getDataAttribute -> M` now reaches the
complete typed DOM proof and refuses at **`ctjs.unary`**, under all four
provider/policy combinations. Original M retains **24 nine-register blocks** and
its handler at **^bb12**. Start with its Number truthiness (`!0`/`!1`), then prove
the full Boolean/Number/null prefix and mixed JSON result ownership, including the
saved optional-String guard. F's original regexp/callback key conversion and H's
attribute-key construction follow. Full dataset/config, initialization/inheritance,
retained callbacks and the application driver remain open; no full-bundle gain
is claimed.

**Independent next compiler task:** Claude's 2026-09-16T03:53 journal records a
temporary restoration of top-level `var x;` writes in **7ad52ce2** to satisfy
`bootstrap-host-prefix.py`'s wrapper proof. Import and prove the existing
`program::hoisted_vars` declaration metadata instead of depending on those writes;
keep runtime semantics as the oracle. The CTJS importer currently carries no such
metadata. Claude's pending runtime/audit branches remain his to land. Earlier
sections below are historical checkpoints.

## Helper/URI composition, the JSON chain draft and the audit, 2026-09-15 UTC

**7a337ee2** composes helper expansion with URI normalization: every
handler-owning function in the fingerprinted DOMSource clone is normalized by
`normalizeDOMURI` (which now takes the function name; the working contract is
re-fingerprinted between functions) and `expandDOMHelpers` then inlines the
structured invoke, treating it as opaque in the helper body check because the
complete DOM entry proof reproves the inlined result. **1392f435** adds three
helper-shaped nullable URI sources (function, saved read, arrow) and three
refusals (payload observed, unguarded nullable read, call inside a branch arm).
Focused gate: `ctcompile_native_dom_strings` PASS 133.48 s; the full gate on
that tip was 605/606 with `ctcompile_native_dom_entry` at its 300 s cap under
`-j8` (223 s in the previous green run) — both DOM driver caps are 900 s now.

The **efe8daa8** JSON draft at this checkpoint has been recovered and gated as
**53b9f68a / 0a7c0302**, described above. Do not resume the old draft again.

**Operator-directed ponytail audit** (this session, all by locked merge, each
branch gated in its own devbox dir): `41d0185a` tools/cmake (mingw builders in
one table, snapshot.sh and its selftest gone, shaderc/gen-shaders/ratchet
shims/CTProject.cmake/LLVMVersion.cmake/GLM deleted, compare.py on Pillow),
`d47a8dd8` Script dedups, `2eae1dc7` Core/DOM/Raster (plain in-place node
payloads, deque slab, one-queue scheduler, abstract ttf backend, GL probes
gone; tsan clean), `11185599` ctcompile lowering (the three PDLL files are
`OpRewritePattern`s, `mlir-pdll` is no longer needed, `withProvedClone`
replaces four host-preparation transactions, `ctjs::functionIndex`/
`isPrimitiveAttr`/`sameValueZero`, `--mode` and `manifest::mode` gone,
`DOMEntryAnalysis` charges its module census before the fingerprint — 604/604),
`4e0b3b77` Style (leading_imports gone, resolve() in engine.cpp, small helper
dedups; the two shorthand expanders were left as two contracts on purpose),
`c3108dc5` Shell (installer helpers, 600 lines out of public headers, WebGL
X-macro, `<canvas width=0>` per spec, ctx.font through the CSS parser, ANGLE
preference deleted). `24eeb654`/`7662b763`/`3c50bc67` format the test
JS/HTML/CSS and repin what that moved (`escape-claims/Initialize.cmake` hashes,
`expected.txt` program row; `Exports/boundary.js` stays byte-exact under
js-beautify ignore markers because 27 pinned hashes derive from it).
The last two audit branches landed on 2026-09-16: **5e3d3b86**
`audit-ctcompile-tests` (24 `cmake -P` checks, the eight Browser drivers and
`native_owned_global_maps` are lit tests; `ctcompile_` CTest registrations
424 → 106, lit 177 → 251, `ctcompile_lit` cap 5400 s; test C++ uses
`ctbrowser/test/support/check.hpp`; escape-claims hash pins live in
`escape-claims/check.py`) and **28878c6c** `audit-ctcompile-emit` (nine
string-literal helper headers → the compiled
`include/ctcompile/CTNative/Runtime/ctnative.hpp` behind `#define
CTNATIVE_ORDERED_MAPS`/`CTNATIVE_DOM`, with `style/engine.hpp` included only by
programs that take a style parameter; every `needs*` flag is gone — **0b1e0911**
removed the `needsDOMJSON` guard c127ba96 had just added; source-name
provenance deleted, locals are `v<N>`, captures `capture_<i>`/`argument_<i>`).
Each passed 288/288 (251 lit) in its own devbox dir; the merged tip compiles
(304 steps) and awaits its combined ctest. The integrated **09341902** baseline has since passed the combined gate above. Sanitizer findings outside the audit, not
fixed: `Script/builtins/collections/keyed.cpp:593` UAF,
`Style/css/calc/units.cpp:36` UAF, `Core/number_format.cpp:194` UB cast.

## Saved nullable URI guards and fingerprinting, 2026-09-15 UTC

Resumed the interrupted **6caa728b** full-validation thread, found in this
handoff and AGENT-SYNC. **16b1660d** records its recovered 605 non-lit and
177 lit passes without inventing the disconnected wrapper's missing exit status.
Both histories/unmerged branches were checked; September 7 WIP was already an
ancestor. Three agents reviewed the nullable proof, strict/API quality and repo
complexity/Boost opportunities; root recovered their service-limited drafts,
integrated the changes and ran the gates. No browser/runtime edits.

**b13382ec** separates the complete DOM entry proof from manifest parsing and
fingerprinting. **9517ff21** avoids cloning report-free IR during fingerprinting;
report-bearing input retains clone/clear behavior. Every freshness check remains,
with complete hashing and no cache. Legacy-hash equivalence, nested reports,
source nonmutation and changed-source fingerprints have a regression.

**bb7ba402** compiles the original M guard `if ('string' != typeof t) return t`
on a saved `getAttribute` result before one URI try/catch. The producer remains
`std::optional<std::string>`. A complete branch proof records the exact dominated
String uses, and emission copies its value only inside the selected arm. Null,
empty String, an independent reread, aliases, later DOM mutation and the original
catch snapshot remain distinct. Loose equality is accepted only for two proved
Strings; String-only branch/Invoke results use ordinary owning Strings. No Script,
AOT, generic nullable carrier, handle table or new decoder is emitted.

Focused **3/3 CTests in 131.04s PASS**. DOM Strings now covers **745 Node/VM
observations / eight GCC-Clang binaries / 1,048 source refusals**; the nullable
slice adds **80 observations / 52 refusals**. Both providers, policies and layouts
pass, including result lifetime after document destruction and every incomplete
host-proof budget. A positive-arm source exposed an unreachable importer epilogue
that rejoins a live return; its dead branch is now accepted while every source
operation/effect remains censused. Stable formatting passes **844 C++ / 104 Python /
33 web**; the required pinned formatter's **nine-file / 26-diagnostic** baseline
is byte-identical.

Timing uses unchanged Bootstrap IR, saved baseline/candidate tools, warm-up and
11 alternating pairs on the devbox. Fingerprint command median: **159.20 →
152.11 ms (4.45%)**; instrumented pass: **60.4 → 51.0 ms (15.56%)**. Output and
fingerprints match exactly. Tiny URI lowering measured **8.263 → 8.579 ms**,
so there is no measured general transcompilation speedup. The CLI clears supplied
reports before fingerprinting; its decorated-input timing does not measure the
clone fallback. See [the quality review](native-quality-review-2026-09-15.md).

Complete **310-step build / 606/606 CTests in 1454.14s / 177/177 lit in
962.95s PASS**, with the full wrapper's exit status **0** retained.
All **1,462 source / 113 submodule hashes** match devbox, local and committed
source. Fresh Bootstrap remains **19/574 native / 0 of 43 globals**, both policies,
no skips/prunes; DOM Data **7/7**, Button **4/86**, 22 Node observations and the
existing VM inheritance failure. Button/Data reports and all **1,123 escape rows**
are unchanged. Original M and the isolated nullable URI helper each refuse all
four provider/policy combinations at the helper source-shape check; M retains
**24 nine-slot blocks** and handler **^bb12**. No full-bundle gain is claimed.
Full WPT/test262 were not remeasured; browser/runtime and expectations are unchanged.
Evidence and replay scripts: `/tmp/ctcompile-nullable-uri/`, including durable
`full.log`, `full.exit`, `measured.json` and the isolated `next-probe.py`.

**Exact next boundary:** compose the existing helper expansion and URI recovery
inside the fingerprinted DOMSource transaction. The current driver chooses one
based only on a handler in the selected entry; a handler inside M reaches helper
expansion instead. Preserve the original call-site actual/capture/receiver proof,
handler vectors, checked status edges and all-budget rollback; reprove the complete
result before publication. Then original M needs JSON/parse identity and original
lookup order, sequential URI/JSON failure continuations, and mixed primitive/JSON
ownership. JSON lookup precedes decoding; either failure returns the saved input.
Full H, initialization/inheritance, retained config/callbacks and the native
application driver remain open. Evidence: `/tmp/ctcompile-nullable-uri/`.

## Earlier measurements

This file holds the current checkpoint. When replacing it, move superseded entries
to the dated history below; keep each file under 1,000 lines. Historical claims and
next steps retain their original context and are not current instructions.

| Date | History |
| --- | --- |
| 2026-09-15 | [URI continuations, shared cores and DOM factories](handoff/2026-09-15-01.md) |
| 2026-09-15 | [DOM captures and Number index evidence](handoff/2026-09-15-02.md) |
| 2026-09-14 | [DOM helpers, attributes and class methods](handoff/2026-09-14-01.md) |
| 2026-09-14 | [Original Bootstrap Data, DOM sessions and arrays](handoff/2026-09-14-02.md) |
| 2026-09-13 | [Browser cores, UMD and Data observations](handoff/2026-09-13-01.md) |
| 2026-09-13 | [Recorder callbacks, captured snapshots and array indices](handoff/2026-09-13-02.md) |
| 2026-09-12 | [Child Maps, nullable values and dense arrays](handoff/2026-09-12.md) |
| 2026-09-11 | [Caller-owned Maps, global aliases and accessors](handoff/2026-09-11.md) |
| 2026-09-10 | [Object keys, mixed carriers and Map sizes](handoff/2026-09-10.md) |
| 2026-09-09 | [Saved Map sizes, scalar globals and BigInt operations](handoff/2026-09-09-01.md) |
| 2026-09-09 | [Scalar initialization and arithmetic provenance](handoff/2026-09-09-02.md) |
| 2026-09-08 | [Map absence, object fields and child ownership](handoff/2026-09-08-01.md) |
| 2026-09-08 | [Finite Map results, nullable values and short-circuit reads](handoff/2026-09-08-02.md) |
| 2026-09-08 | [Map writes, payloads, keys and array retention](handoff/2026-09-08-03.md) |
| 2026-09-07 | [Map effects, source calls and exported getters](handoff/2026-09-07-01.md) |
| 2026-09-07 | [Host ownership, protected helpers and native groundwork](handoff/2026-09-07-02.md) |
| earlier | [Compiler bring-up and original AOT decisions](handoff/earlier.md) |
