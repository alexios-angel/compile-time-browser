# Where the time actually goes

Everything here is measured on this tree. Numbers without a measurement behind
them are not in this file.

## How to measure, on WSL2

**`perf` cannot work here.** WSL2 runs a custom Microsoft kernel with no PMU, so
the `linux-tools` packages have nothing to read. Use **callgrind**:

```bash
valgrind --tool=callgrind --callgrind-out-file=cg.out ./build/src/tests/ctbrowser-test-X
callgrind_annotate cg.out | head -30
callgrind_annotate --auto=yes cg.out          # line-level, needs -g (tests have it)
```

~50x slower to run. Worth it, and not only because it is the option that works:
**it is deterministic**. Instruction counts, not sampled time.

**That is not a nicety on this machine.** Wall clock here varies ±10% run to run.
One change below looked like a 10% win by wall clock (0.70s -> 0.63s) and was
0.3% by instruction count. Another looked flat and was 3% worse. A repository
that byte-compares goldens should measure the same way.

Alternatives, all confirmed available and none needing kernel support:
`gperftools` (SIGPROF sampling), **Tracy** (frame profiler, works on mingw),
clang's `-fxray-instrument`, `-pg`/gprof. And the Windows cross-build means a
native profiler on the `.exe` is always an option — which is already how GPU work
is measured (`docs/platform.md`).

## The finding that reorganised the work

Profiling a **whole page render** — `examples/p5basic`, loading the p5.js bundle
and drawing a sketch — rather than one subsystem:

```
17.5%  ctjs::vp::lex
15.1%  compiler_impl::declare_local
 7.6%  compiler_impl::collect_captured_names
 1.4%  context::run_loop        <-- the entire interpreter
```

**Showing a page costs an order of magnitude more in READING JavaScript than in
running it.** HTML parsing does not appear at all — its own test is dominated by
process startup. Layout, style and raster are nowhere near the top twenty-five.

That is not where anyone would guess, which is the argument for profiling the
whole engine rather than the subsystem you happen to be holding.

## What landed

`p5_ratchet` compiles the whole p5.js bundle; it is the honest end-to-end number.

| change | effect |
|---|---|
| `is_captured` linear scan -> hash set | page render **-16.4%** instructions |
| `kids()` vector-per-node -> `std::span` | page render **-12.0%** more |
| ctjs lexer: check the first byte before comparing 56 operators | `lex` **1.95x** |
| `is_captured` -> Euler tour, no set per function | **-6.65%**, RSS **-8%** |

```
p5_ratchet   1.31s -> 1.06 -> 0.88 -> 0.70 -> ~0.65s      about -50%
page render  1,841,290,916 -> 1,354,079,256 instructions  -26.5%
```

Every one was the same shape: **work that did not need doing at all.**

* `is_captured` linear-scanned a `std::vector<std::string>` once per local
  declaration, against a vector `collect_captured_names` filled with
  undeduplicated repeats — quadratic on large functions.
* `kids()` returned a `std::vector` by value for children **already contiguous**
  in the pool: a malloc and a free per node visit, computing nothing.
* The lexer's punctuator loop `substr`'d and compared all 56 operators
  longest-first, with `(`, `)`, `;`, `,`, `.` — the ones real code is full of —
  at the **end** of the table.

## What did NOT work, which is the more useful half

Three hypotheses were confidently wrong. They are here so nobody spends the
afternoon again.

**Keyword lookup was not the lexer's cost.** `is_keyword` scans forty keywords
per identifier and looks exactly like the `is_captured` bug. Replacing it with a
hash table: **38.30 ms -> 39.20 ms**, ie. nothing. Forty short comparisons that
short-circuit on length cost about what one hash of the same string costs. The
operator table was the real cost, and only line-level attribution found it.

**Memoising `collect_captured_names` won 0.3%.** Node visits fell 42% (16.5M ->
9.6M) and instructions did not move. The traversal was never the cost — the cost
is the set data, and unioning each function's memoised names into its ancestors
moves as many strings as re-walking did. **The quadratic moved rather than went
away**, which is why an asymptotic argument is not evidence on its own.

**Inserting into a set during that walk was 3.1% WORSE** than building a
duplicate-filled vector and deduplicating once. Hashing every occurrence costs
more than appending it. It did save 12 MB of RSS, which was not worth 3% CPU.

The third attempt — an Euler tour, materialising no set at all — is what worked.
Two failures were the price of understanding why.

**And one from the GLSL evaluator:** deleting the `std::vector` spill from
`components` (never used once in 4.32M evaluations) is a real 1.22x, and was
*not* landed — it silently truncates values over 16 floats, and the packet
refactor makes the question disappear rather than trading correctness for it.

## What is left, ranked and current

```
23.7%  ctjs::vp::lex        (after the 1.95x; still the biggest single item)
19.3%  libc memory operations
 ~5%   collect_captured_names   (Euler tour landed; re-measure before trusting this)
 3.4%  function_proto::add_name
```

* **The lexer** — `docs/lexer-plan.md` has the design for a runtime-only
  replacement, and the header saying stage 0 cancelled it for now. Re-measure
  before spending a week.
* **`libc` memory operations at 19.3%** are not attributed to a caller yet and
  are the largest unexamined item in the engine.
* **The GLSL evaluator**, 0.62 M frag/s, which caps WebGL at 160x160. Separate
  and only matters for WebGL pages: `literal()` re-parses its text with
  `strtof` on **every evaluation** — waste and a `LC_NUMERIC` determinism bug in
  one line — and variable access hashes a `std::string` per read (12.9%).
* **Layout is allocation-bound**, not thread-bound: `bench_layout` tops out at
  1.87x on 21 workers with ~110k malloc/free per pass. `std::pmr` arena first
  (fixes the count), then possibly mimalloc (fixes the cost of each).

## Threading

The runtime cannot; the front end can, and that is where the time is. See
`docs/script.md` — and note the capture index is built read-only *for* that
future.

## Computed-goto dispatch: MEASURED, AND IT LOST (2026-08-02)

Tried, measured, reverted. The implementation is in the history; the number is
here so nobody spends the day again.

`docs/computed-goto-plan.md` set a gate before the answer was known: under 5% of
an execution-heavy workload and the plan deletes itself. The share came in at
the top of the marginal band, so it was worth building.

**What it measured, on the devbox with clang 24:**

| workload | run_loop share | why it was chosen |
|---|---|---|
| `tests/bench_script` | **76.9%** | dispatch-bound on purpose - the best case |
| `tests/phaser_invaders` | **15.0%** | a real frame-driven page |

Then, same binary, both dispatch paths, `tests/bench_script`:

| | instructions (callgrind, deterministic) | wall clock (min of 7) |
|---|---|---|
| `switch` | **38.13 G** | **272.3 ms** |
| computed goto | 40.35 G (**+5.8%**) | 280.5 ms (**+3.0%**) |

**Computed goto executed 5.8% MORE instructions and ran 3% slower**, on the
workload most favourable to it, with six of seven sub-workloads regressing.
Callgrind is deterministic, so this is not variance.

### Why, and it is the interesting part

A `goto *table[op]` at the end of every handler is supposed to win on branch
prediction: 88 dispatch sites instead of one, each learning the opcode PAIRS
this bytecode emits. Two things stopped it.

**The frame cannot be cached across a dispatch, which is what the technique
actually depends on.** The textbook version keeps `frame`, `fn` and `base` live
in registers and jumps straight from handler to handler. Here it cannot: 12
handlers push, pop or unwind `frames_`, which is a `std::vector` - any of them
can reallocate it and leave a cached pointer dangling. So `VM_NEXT` has to
re-derive all of it, and that re-derivation is duplicated into all 88 handlers,
where the `switch` does it once per loop iteration and the compiler keeps it in
registers.

**And the obvious fix is not available.** "Only the pure handlers keep the frame
cached" fails on the opcodes that matter: `context::to_number` calls `valueOf`
and `toString` through `call()` for an object operand, so `add`, `sub`, `less`
and every comparison can re-enter the VM. The safe set reduces to loads, moves
and jumps - not enough to pay for the duplication.

**A modern predictor does not need the help.** The devbox is Zen-class, and
indirect-branch predictors have moved on since the technique was published in
the 1990s; a single well-exercised switch site is predicted about as well as 88.

### What survives

`tests/bench_script` - which did not exist before and should have. Six
benchmarks covered core, style, layout, raster, interaction and the GPU, and
none covered the VM, which is why the only number anyone had for `run_loop`
came from a page-load profile where it was 1.4% and therefore noise. It reports
compile against run, per workload, min of seven.

**The real finding is in the table above and it is not about dispatch.** In a
Phaser frame the interpreter is 15%, while `canvas_context::blend` is 22% and
`object_object::find` is 10.7%. Property lookup by string hash costs two thirds
of what the entire interpreter costs. That is where the next work is.
