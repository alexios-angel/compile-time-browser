# A JavaScript lexer of our own

## Why — the number, and how it was arrived at

Callgrind on a **whole page render** (`examples/corpus/p5basic`, loading the p5.js
bundle and drawing a sketch) says the cost of showing a page is JavaScript
front-end work, and after two compiler fixes the single biggest item in the
engine is the lexer this repository does not own:

```
23.7%  ctjs::vp::lex
19.3%  libc memory operations
 6.5%  compiler_impl::collect_captured_names
 3.4%  function_proto::add_name
 2.4%  compiler_impl::mentions_arguments
```

It was 17.5% before; it did not get slower, everything around it got faster.
For context, `context::run_loop` — the entire interpreter, actually executing
the program — was 1.4%. **Showing a page costs far more in reading JavaScript
than in running it.**

Profiling on WSL2 uses callgrind, not `perf`: Microsoft's kernel has no PMU, so
`linux-tools` cannot work. Callgrind is deterministic anyway — instruction
counts rather than sampled time — which suits a repository that byte-compares
goldens and has measured ±10% wall-clock variance.

## Why a new one rather than a patch to ctjs

Two reasons, and the second is the real one.

**`ctjs::vp::lex` is `constexpr`.** It exists to run during constant evaluation,
which is what ctjs is *for*. That forbids `memchr`, runtime-built lookup tables,
and anything resembling SIMD, and it is not a limitation worth arguing with
upstream about — it is the point of the library.

**This engine no longer needs a constexpr lexer.** The compile-time engine was
deleted in 2026-07-27 (`docs/history/v1-retirement.md`); scripts arrive from pages at
run time. Paying constant-evaluation constraints for a runtime-only workload is
the whole cost.

ctjs stays. Its **parser** is good and is not being replaced.

## The seam, which is what makes this tractable

`ctjs::vp::parser` is constructed from a token vector, not from source:

```cpp
// external/compile-time-javascript/include/ctjs/vparse.hpp:1001
std::vector<token> toks = lex(src, &report);
parser ps{toks, a, 0, src};
```

So a new lexer that produces `ctjs::vp::token` **drops in with no parser
change at all**. That is the entire architectural risk, removed. The token is
already lean and does not want redesigning:

```cpp
struct token { tk kind; std::string_view s; };   // 24 bytes, views into source
```

Nothing is allocated per token today. **This is not an allocation problem** —
which matters, because that was the shape of the last two wins and assuming it
again would be lazy.

## What is actually slow — to be CONFIRMED before it is fixed

The strongest candidate, found by reading:

```cpp
// vparse.hpp:67 — called once per identifier
constexpr bool is_keyword(std::string_view w) {
    for (std::string_view k : keywords) { if (k == w) { return true; } }
    return false;
}
```

**Forty string comparisons per identifier**, on a bundle with hundreds of
thousands of them. It is the same shape as the `is_captured` linear scan that
turned out to be 15% of a page render — which is exactly why it must be
*measured* rather than assumed. Stage 0 exists for that.

## STAGE 0 RAN, AND IT CANCELLED STAGES 1-5 (2026-07-31)

The plan said stage 0 could end the plan. It did, and it is worth reading before
anything below is acted on.

**The hypothesis was wrong.** `is_keyword` scanning forty keywords per
identifier looked exactly like the `is_captured` bug. Replacing it with a hash
table measured **38.30 ms -> 39.20 ms** on the p5 bundle: nothing. Forty short
comparisons that short-circuit on length are about what one hash of the same
string costs.

**The real cost was the operator table.** Line-level attribution put ~44% of
`lex` inside `<string_view>` and `<char_traits.h>`, and the punctuator loop
`substr`'d and compared **all 56 operators, longest first** - with `(`, `)`,
`;`, `,`, `.`, `{`, `}` at the END of the table. Every parenthesis in p5.js paid
~45 failed comparisons.

Comparing one byte first fixes it in one line, keeps `constexpr`, and keeps
longest-match semantics:

```
38.30 ms  ->  19.64 ms       1.95x     (landed, ctjs 8b30d84)
38.30 ms  ->  16.53 ms       2.34x     (first-byte bucket table - NOT landed,
                                        needs static storage, costs constexpr)
```

Token streams verified **identical** - kind, offset and lexeme of every token -
across p5.js, sixteen corpus pages and the GLSL fixtures: 18 files, ~450,000
tokens. `p5_ratchet` went 0.88s -> **0.70s**.

**So there is no new lexer, for now.** The plan's own terms were that if the fix
turned out to be a patch to ctjs then that is what should happen, and it was.
Whether a runtime-only lexer is still worth writing is a question for the NEXT
profile, not this one - `lex` has to be re-measured in its new position before
anyone spends a week on it. Everything below stays as the design if that
measurement says yes.

## Stages (deferred - see above)

### 0. Confirm the attribution inside `lex`
Callgrind attributes to functions, and `is_keyword` is small enough to be
inlined into `lex` — so its cost is currently hidden inside that 23.7%. Get a
line-level or call-count attribution (`--dump-instr=yes`, or a counter build)
**before writing anything.** If the cost is character scanning rather than
keyword lookup, the design below changes.

This stage can also end the plan: if the answer is "replace 40 comparisons with
a hash", that is a patch to ctjs and not a new lexer, and the honest outcome is
to say so.

### 1. `include/ctbrowser/script/lexer.hpp`, `src/script/lexer.cpp`
Runtime-only, producing `ctjs::vp::token`. Techniques the constexpr version
cannot use:

* **Keyword lookup in O(1)** — bucket by length first (JS keywords are 2-10
  characters), then compare within the bucket, or a small perfect hash. Either
  way, not forty comparisons.
* **A 256-entry character-class table**, built once: identifier-start,
  identifier-part, digit, whitespace, punctuation-start. One indexed load
  replaces a chain of range comparisons per character.
* **`memchr` for the long skips** — line comments to newline, block comments to
  `*/`, string bodies to the closing quote. These are the runs where a
  byte-at-a-time loop loses most.
* **One `reserve` from a source-size estimate.** Tokens average ~4-5 bytes of
  source in real code; reserving `src.size() / 4` removes the growth
  reallocations without a second pass.

### 2. The JavaScript-specific hard parts
These are where a lexer is subtle rather than fast, and where ctjs's existing
code is the reference to learn from rather than to beat:

* **Regex versus division.** `/` is either an operator or the start of a
  literal, decided by what came before. Get this wrong and `a / b / c` becomes
  a regex containing ` b `.
* **Template literals** — `tmpl_head` / `tmpl_mid` / `tmpl_tail` with nested
  `${}` needing a brace-depth stack, and a nested template inside a
  substitution.
* **Newline tracking for ASI.** The parser needs to know a newline came before
  a token. Whatever ctjs's token carries for this, the new lexer must carry
  identically — see stage 3.
* **Unicode identifiers.** p5.js has Greek identifiers (`const π = Math.PI`),
  which is why the page files are UTF-8. Bytes >127 are identifier characters;
  that behaviour is carried over exactly, not "improved".

### 3. Differential testing, which is the whole safety argument
`tests/lexer_basics.cpp`: lex a corpus with **both** lexers and require the
token streams to be **identical** — same count, same kinds, same lexeme
offsets and lengths. Not "equivalent"; identical.

The corpus is the one that matters: **the p5.js bundle itself**, plus every
page in `examples/pages/`. Somebody else's 7,000 lines is the only input worth
trusting a lexer against, which is the same reasoning that made p5's own
shaders the GLSL parser's corpus.

This is cheap, decisive, and mirrors `tests/js/vm_basics`, which already
differentially tests the VM against ctjs's interpreter.

### 4. Switch over, behind a measurement
Replace the `lex` call at the one seam. Then:

* the two ratchets must stay 12/12 and the p5 API probe at 179/169;
* **all ten goldens must stay byte-identical** — a lexer cannot change pixels,
  so any movement means something unintended happened;
* re-run the same callgrind workload and report the before/after honestly,
  including if it is disappointing.

### 5. Only if the numbers ask for it
A token kind narrower than `string_view` for punctuation, or lexing on the
scheduler while the previous script compiles. **Not planned** — listed so it is
clear they were considered and deferred, not forgotten.

## What this does NOT do

* **Not a new parser.** ctjs's parser stays and is not the bottleneck.
* **Not a new token type.** Matching `ctjs::vp::token` is what removes the risk.
* **Not constexpr.** That capability is deliberately given up, and it is ctjs's
  reason for existing rather than this engine's.
* **Not a fork of ctjs.** If stage 0 says the fix is a hash table for keywords,
  the right answer is a patch upstream and no new file here.

## Risks

* **Stage 0 says the lexer is not the problem.** Then this plan stops, and that
  is a good outcome rather than a wasted one.
* **A subtle divergence the differential test does not cover** — regex-versus-
  division on a construct p5 does not contain. Mitigated by the corpus being
  real code plus targeted cases, not by hoping.
* **Two lexers in the tree during the transition.** The differential test needs
  both; once switched over, ctjs's is still reachable through the submodule and
  costs nothing.
* Being right about the cause and wrong about the size of the win. The
  measurement in stage 4 is reported either way.
