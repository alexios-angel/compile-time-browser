# WPT — the next round, as briefs

**Updated 2026-09-16, session 12.** Round one is MERGED (§1 below is history
now: `c5682f32` T, `20f17da4` P, `247a7c53` W, `9c70aaa0` E on top of the
audit's `09341902`; measured in `docs/test262.md` `9c70aaa0` and
`docs/wpt.md` 2026-09-16). Round two is RUNNING as four agents in their own
worktrees cut from `9c70aaa0` - A animations, G the value grammar, L layout,
C the cascade - with the briefs of §3 (copies in
`~/Downloads/claude/wt/wpt11-session/round2/`, with `COMMON.md`). Five more
briefs are written and waiting for a free slot, in this order:

| brief | what | why now |
|---|---|---|
| `brief-B.md` | `BigInt64Array`/`BigUint64Array`/`Float16Array` (`element_kind` big_i64/big_u64/f16) | the single largest lever in `built-ins`: ~900 files die on `BigInt64Array is not defined` in the harness |
| `brief-U.md` | the WHATWG URL parser behind `URL`, `URLSearchParams`, `location` (replacing the Boost.URL split in `shell/net/url.cpp`) | `url` 11/34, `URLSearchParams` undefined = 12 HARNESS_ERRORs in html/dom |
| `brief-D.md` | `document.open/write/close`, html/syntax fixtures, the script processing model | html/syntax 22/268 (133 TIMEOUT), webappapis dynamic-markup-insertion |
| `brief-F.md` | forms, Range's mutation algorithms, NodeIterator/TreeWalker | forms 58/647, ranges 16/72 (9,294 failing subtests), traversal 8/18 |
| `brief-S.md` | shadow DOM, custom elements reactions, Selection, DOMParser | shadow-dom 74/345, custom-elements 37/193, selection 8/177 |

**Open in the VM, not briefed** (each a day's work, with a Codex notice
because the native backend follows the bytecode):

- **TDZ for `let`/`const`/`class`**: the engine has none except parameter
  defaults (`compile_ident` emits a static throw there). A textual read
  before the declaration in the same scope can be a static throw the same
  way; a read from a nested closure needs a hole value in the register and a
  check on captured reads - a new op or a new value tag, which is an ABI
  change for ctcompile. ~150 test262 files (`block-scope`, `using`'s TDZ
  family, class heritage).
- **RegExp `\p{...}` property escapes** (469 `property-escapes/generated`
  files) and the `v` flag's `unicodeSets` (85): the first is a data job -
  the UCD tables behind General_Category, Script, Script_Extensions and the
  binary properties, generated into a header by a tool under `tools/gen/`.
- **`Object.prototype.toString` of an arguments object** is `[object Array]`
  (arguments objects are arrays here), `Function.prototype` is not callable,
  `NaN`/`Infinity`/`undefined` are writable globals (the globals table has
  no attributes) - each a handful of files.
- `var x;` at a script's top level writes undefined over an existing value:
  `statements/dispatch.cpp` says why it stays (Codex's
  `bootstrap-host-prefix.py` prover reads the write as the declaration).

**Open in the DOM/CSS half, not briefed:** `scrollTop`/`scrollLeft` and the
scroll event on elements (css/cssom-view 44/239 + 8 dom/events files),
`FontFace` (3 HARNESS_ERRORs), `showModal`, the `NodeList` index read that
allocates (250 million of them in `NodeList-static-length-getter-tampered-*`,
now collected but slow).

## 1. (DONE 2026-09-16) The four round-one branches — merged as c5682f32, 20f17da4, 247a7c53, 9c70aaa0

Round one ran four agents in their own worktrees, cut from `b346dc0b`. They
FINISHED minutes after the session's handoff was written: every worktree is
clean, every branch's own gate was 193/193 (the browser preset, MLIR off),
and an adversarial reviewer read each diff whole. Reports and reviews, in
full, are `~/Downloads/claude/wt/wpt11-session/round1-reports.jsonl` (one
JSON line per agent and per review). Worktrees are under the MAIN checkout's
`.claude/worktrees/wf_ddb62f45-b03-{1,2,3,4}`; branch `worktree-wf_ddb62f45-b03-N`.

| wt | agent | tip | commits | measured (devbox, same instrument) | review |
|---|---|---|---:|---|---|
| `-1` | **E** test262 `test/language`: compiler, early errors, VM, ctjs | `c8c281b1`, ctjs `69cc3d8` (gitlink bumped; the seven ctjs commits are also branch `agentE-ctjs` in the ctbrowser-wpt worktree's submodule) | 28 | **19,829 -> 21,290 of 23,726** (+1,476, 15 lost - each an accidental pass before: decorators now a parse error, tagged-template TCO files, `using` TDZ, `delete arguments.length`, two `for (var x of ..)` files whose fix is `/tmp/agentE/var-noinit.patch` = `wpt11-session/var-noinit.patch`, unmeasured) | merge-with-fixes: two MAJORS - `declaring_` set around the whole top-level declarator loop makes every initialiser expression a declaration's write (`dispatch.cpp`, `destructuring.cpp:53`); Annex B.3.3 regressed - a function declared in a nested block of a sloppy function is block-local only (`statements/functions.cpp`). Minors: param-scope names swapped back too early, mixed sync/async `using` in one block, tagged-template cache key, uncapped prototype walks in `lookup.cpp` |
| `-2` | **T** TypedArray / ArrayBuffer / DataView / Uint8Array codecs | `e6f8a94f` | 8 | TypedArray **1 -> 745** of 1,446, TypedArrayConstructors **74 -> 275**, ArrayBuffer **24 -> 190**, DataView **0 -> 438**, Uint8Array **4 -> 64**, Array 2,774 -> 2,787; 0 lost | merge-with-fixes: one MAJOR - `ensure_store` (the `buffer` getter, `subarray`) converts an owning typed array into a view in place (`typed_arrays/constructors.cpp`); minors: constructor-made views on a strong per-buffer list (a leak), a private %ArrayIteratorPrototype%, DataView ctor steps 11-14. Touched `Shell/bindings/resources.cpp` (2 rooting lines, needed) |
| `-3` | **P** Promise / Proxy / Reflect / Symbol / Iterator / Date / DisposableStack | `b4250867` | 9 | Promise **257 -> 703** of 731, Iterator **13 -> 603** of 653, Proxy 146 -> 169, Reflect 115 -> 150, Symbol 48 -> 75, DisposableStack **0 -> 91**, AsyncDisposableStack **0 -> 101**, SuppressedError 0 -> 20, Date 580 -> 583, AsyncFromSync 12 -> 18; WeakRef/FinalizationRegistry measured 28/28 and 46/46 but SHIPPED OFF (`install_weak_refs` commented out) behind Codex's ND-2 pin in `ctcompile/test/Analysis/Escape/Cycle.cpp:530-537`; 0 lost | merge-with-fixes: one MAJOR - **the branch alone turns the gate red**: `unit/image_basics` SEGFAULTs unless `wpt11-session/dispatch-rooting.patch` (10 lines in `bindings/events/dispatch.cpp` `invoke_listener`: root callback/receiver/args across the fence's first compile) is applied - it is a pre-existing GC hole the extra allocations expose; apply it with the merge. Minors: `Reflect.set` through a proxy answers `!throw_pending()`, `set_prototype_of` on a proxy answers false where 10.5.2 says TypeError, `install_iterator`/`install_disposable` called from the tail of `install_promise` instead of `builtins.cpp` |
| `-4` | **W** WPT dom/nodes + html/dom + dom/events | `c9acd0fc` | 19 | dom/nodes **252 -> 262** of 309 (12,061 -> 12,075 subtests), html/dom 149 -> 150, dom/events 81 -> 81 (677 subtests); 0 lost; `document.characterSet` from `<meta charset>` with the Encoding Standard label table is DONE and unit-tested but the 636 subtests stay FAIL because the runner has no server for `encoding.py` | merge-with-fixes: one MAJOR - `compile_handler_attribute` wraps every inline handler in `with (document) { with (form) { with (this) {...}}}` (`events/dispatch.cpp`), the HTML scope chain done as `with` statements: review it against 8.1.6.1's "element's event handler scope" before it meets the corpus. Minors: `tHead` setter's exception type, `xml.cpp` entity splicing O(n^2), `run_inserted_scripts` walks the whole document per inserted script, its `expectations.txt` was regenerated over `b346dc0b`'s (take the root's `aff857e0` file and re-measure instead), MathML-as-foreign-content reverted (`3a4bf5b7`) because `ctcompile/lib/HTML/DocumentComparator.cpp` needs `case node_ns::mathml` - Codex's file |

**Merge order and what to do at each:** T, then P (+ the dispatch rooting
patch), then W (drop its `expectations.txt`, keep the root's), then E (the
largest; bump the ctjs gitlink to `69cc3d8` - fetch the commits from
`agentE-ctjs` first, see the memory note `agent-worktree-submodule-commits`).
Resolve conflicts in `builtins/internal.hpp` (three agents appended a line),
`Shell/bindings/resources.cpp` (T and W both added the same rooting lines),
`docs/script.md`. Then ONE full gate (MLIR on), a full WPT+test262 re-measure,
and the JOURNAL lines for Codex: every agent reported JS SEMANTICS changes
(class fields compiler-driven, `delete` semantics, `using`, tagged templates,
typed arrays real, Promise two-tick thenables, Proxy invariants, `Symbol`
well-knowns, `hasOwnProperty` via the descriptor trap) - the native backend
reads this VM as its oracle and will see every one as a divergence. Expected
after the merge, if nothing is lost in the merge: test262 roughly **+4,300
files** (language +1,461, built-ins about +2,900) on the 32,295 of `7f9211d0`.

**Things the agents asked of files outside their paths** (all in the reports;
the ones worth doing first): `symbol.cpp` needs `Symbol.dispose`,
`Symbol.asyncDispose` and `Symbol.unscopables` as well-knowns (81 `using`
tests and `remove-unscopable.html` wait on them); `value.hpp` needs
`element_kind` `big_i64`/`big_u64`/`f16` (~800 typed-array files read
`BigInt64Array`); `compile/classes.cpp` must link a derived CONSTRUCTOR's
[[Prototype]] to the parent (15.7.14 step 8.d: `class P extends Promise`,
`class X extends Uint8Array`, ~50 files); `ct262.cpp`'s `$262.detachArrayBuffer`
can be `ArrayBuffer.prototype.transfer` now (294 skipped files); the VM's
proxy `get` trap hands symbol keys as `"@@..."` strings; a sloppy function
called from a native with `this` undefined must bind `globalThis`.

## 2. Open at the tip (`d05c82ad`)

- The browser gate at `d05c82ad` was 537/540. `bootstrap_layout` is
  regoldened in `bb32f1a4` (fourteen `margin: auto` lines now read the used
  pixels, as CSSOM says and Chrome does) and `layout_blocks` was a wrong
  expectation. Still red, UNVERIFIED after `bb32f1a4`: `cssom_wpt`'s pseudo
  check (its own script error, fixed there) and `frames` - the real bug: a
  frame document's own `<style>` is not applied by `browser/nested.cpp`'s
  layout. The frame lays out at its 200px box (a div reads `184px` wide, the
  UA sheet's body margin) but the author sheet collected from the frame
  document does not reach the cascade; `viewport-units-compute` reads `0px`
  for the same reason. `bb32f1a4`'s frames check prints the frame's
  `<style>` count beside the values, so the next gate says which half.
- `html/dom/reflection-text.html` TIMEOUT since the audit: NOT an infinite
  loop - gdb at 25 s shows the main thread idle in `SDL_WaitEventTimeout`;
  the page never publishes its results (an exception in the harness's async
  flow, most likely). 10,138 subtests.
- `dom/nodes/MutationObserver-characterData`: an HTML `<?pi?>` must be a
  bogus COMMENT (data `?processing data?`), the tree builder makes a
  ProcessingInstruction.

## 3. The round-two briefs

Each is one agent, disjoint paths, own worktree and devbox dir, briefed as
`/tmp/wpt11/brief-{A,G,L,C}.md` were (the standing rules are
`/tmp/wpt11/COMMON.md`; both survive a reboot only if copied).

**A — CSS Animations and CSS Transitions**, on top of the Web Animations
that exist (`bindings/animations.cpp`): `@keyframes` captured by the parser
(it discards the block), `animation-*` making CSSAnimations after every
restyle, `transition-*` comparing before/after computed values, both feeding
the same effect stack, plus colour interpolation. The largest subtest cluster
in the corpus: every `<module>/animation/*.html` file fails the "CSS
Transitions" and "CSS Animations" rows (css-transforms 3,396 failing
subtests, motion 3,120, backgrounds 2,876, masking 2,821, images 2,472, gaps
2,477, filter-effects 2,267).

**G — the CSS value grammar** (`lib/Style/css/properties/**`): 211 of the 415
properties the parsing sweep tests are absent from the table and are expandos
now; `color` fails 5,271 parsing subtests (CSS Color 4/5), `background-image`
2,160 (gradients), then `font`, the grid track lists, `display`'s two-value
syntax, `offset-path`/`clip-path`/`shape-outside` (basic shapes), `filter`,
`content`, `mask`, `box-shadow`, the timing functions.

**L — layout** (`lib/Layout/**`): `calc-size()` (the keywords landed,
`0db27a2f`), `fit-content(<length>)`, an absolutely positioned box inside a
relatively positioned INLINE (its containing block is the inline's
fragments), element scrollbar gutters (`viewport-units-gutter-003/004`), the
css-sizing computed tests.

**C — the cascade** (`style/engine.hpp`, `css/{parser,selector,media}.cpp`,
`bindings/stylesheets/**`): `@layer` and `revert-layer` (css-cascade 25/78),
CSS nesting (1/22), `@container` size and style queries (171
`assert_implements` HARNESS_ERRORs in css-conditional - a style -> layout ->
restyle loop), then selectors (105/210), mediaqueries (5/30), css-syntax
(15/40: `@charset` and byte decoding), css-variables (20/48).

**After W merges, the DOM side splits** by the wide numbers: `document.open/
write/close` (html/webappapis dynamic-markup-insertion, 126 FAIL + 22
TIMEOUT) and the html5lib fixtures that now run as variants (`ctdrive
--query`, `01575260`) - one agent on the tree builder and the parser-driven
document; forms (58/331) + webappapis scripting + dom/ranges (16/63, 9,294
failing subtests) + traversal/lists/collections/abort - one agent;
shadow-dom (74/182) + custom-elements (37/180) + selection (8/86) +
domparsing + encoding + url - one agent.

## 4. The instrument

`tools/check/test262-baseline.sh` runs the whole corpus (48,624 files);
`tools/wpt/fetch-wpt.sh` carries the widened corpus (5,099 runnable, 89 MB);
`tools/wpt/run-wpt.py` runs `<meta name=variant>` tests one per variant.
Measure from the detached worktree `wt/ctbrowser-wpt-measure` pinned at the
SHA under test, in devbox dir `projects/ctbrowser-wpt`, with
`/tmp/wpt11/gate.sh <n> [remote commands]`; the wide run is
`/tmp/wpt11/gate-3.remote`'s first half; tally with `/tmp/wpt11/wtally.py
<dir> [<before-dir>]` and `/tmp/wpt11/t262table.py`. The measured JSON of
every run this session is in `/tmp/m-*` and `/tmp/w-*`.
