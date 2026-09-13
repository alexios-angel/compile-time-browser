# WPT — the next round, as briefs

Written 2026-09-13 at the end of a session that was cut short with four
agents still running. **Everything below is measured** (`docs/wpt.md`, the
`7f9211d0` row; `docs/css-conformance.md` §2-wide; `docs/test262.md`, the
`b346dc0b` row) except where it says what an agent's transcript reports.

## 1. Four agent branches to merge FIRST

Round one ran four agents in their own worktrees, cut from `b346dc0b`. The
session ended before their reports; their branches hold their commits, and
their transcripts (`~/.claude/projects/-mnt-c-Users-aange-Downloads-claude-wt-ctbrowser-wpt/2f716b6e-2cd2-4c93-b4ee-942acece2a4e/subagents/workflows/wf_ddb62f45-b03/agent-*.jsonl`)
hold what they measured. Worktrees are under the MAIN checkout's
`.claude/worktrees/wf_ddb62f45-b03-{1,2,3,4}`:

| worktree | agent | branch | commits | state |
|---|---|---|---:|---|
| `-1` | E: test262 `test/language` (compile, early errors, VM, ctjs) | `worktree-wf_ddb62f45-b03-1` | 25 | 4 files dirty (mid-edit: `compile/frames.cpp`, `statements/functions.cpp`, two js unit tests). Its ctjs commits (`b2b5155..69cc3d8`, seven) are saved as branch `agentE-ctjs` in the ctbrowser-wpt worktree's submodule; the gitlink bump must land with the merge |
| `-2` | T: TypedArray / ArrayBuffer / DataView | `worktree-wf_ddb62f45-b03-2` | 5 | clean |
| `-3` | P: Promise / Proxy / Reflect / Symbol / Iterator / Date / DisposableStack | `worktree-wf_ddb62f45-b03-3` | 7 | 1 file dirty |
| `-4` | W: WPT dom/nodes + html/dom (DOM, bindings) | `worktree-wf_ddb62f45-b03-4` | 18 | clean |

Merge each into `ctbrowser-wpt` under `/tmp/ctbrowser-repo-git.lock`
(`/tmp/wpt11/merge-agent.sh <branch> <worktree>` did the checks), gate, then
remove the worktree, branch and its devbox dir `projects/ctbrowser-agent{E,T,P,W}`.
Read the ctjs note in the memory file `agent-worktree-submodule-commits`
before removing `-1`.

## 2. Open at the tip (`d05c82ad`)

- The browser gate at `24b9b9c6` was 536/540: `bootstrap_layout` (six
  baselines want a REGOLDEN - `margin: auto` now reports its used pixels,
  `24.5px`, which is what CSSOM says and Chrome does; read the diff, it must
  be those lines only), `layout_blocks` (a wrong expectation, fixed in
  `d05c82ad`), `cssom_wpt` and `frames` (a frame document's own `<style>`
  is not applied by `browser/nested.cpp`'s layout: the frame lays out at its
  200px box - a div reads `184px` wide, the UA sheet's body margin - but the
  author sheet collected from the frame document does not reach the cascade.
  `d05c82ad` instruments the frames test to print the frame's sheet count;
  gate-5 (`/tmp/wpt11/gate-5.*`) was running it when the session ended).
  `ctcompile-v1` `834bda1e` carries this state.
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
