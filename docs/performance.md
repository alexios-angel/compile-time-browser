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

## Property lookup: a `std::string` built and thrown away on EVERY lookup (2026-08-02)

`object_object::find` was 10.7% of a Phaser frame - two thirds of what the whole
interpreter costs. The cause was one line:

```cpp
[[nodiscard]] value * find(std::string_view name) {
    const auto it = index.find(std::string{name});   // <-- a temporary, per lookup
```

`flat_map<std::string, V>::find` takes the key type, so asking it with a
`string_view` constructs a `std::string` to throw away. Every property read in
the engine paid for it.

The fix is heterogeneous lookup - a transparent hasher and `std::equal_to<>`, so
`find(string_view)` is answered directly. `ctbrowser::string_flat_map` in
`core/containers.hpp`. Both hasher overloads hash *through* `string_view` on
purpose: heterogeneous lookup is only correct when the two key types hash
identically, and `std::hash<std::string>` is not required to agree with
`std::hash<std::string_view>`.

**Measured, callgrind, same build directory, only the source toggled:**

| | `object_object::find` | total |
|---|---|---|
| `tests/bench_script` before | 2.658 G (6.59%) | 40.345 G |
| `tests/bench_script` after | **1.462 G (3.67%)** | **39.839 G (-1.26%)** |
| `phaser_invaders` before | 2.558 G (**10.74%**) | 23.815 G |
| `phaser_invaders` after | **1.350 G (5.78%)** | **23.342 G (-1.99%)** |

**Property lookup got 47% cheaper and a whole Phaser frame got 2% cheaper, by
deleting a temporary.** No library was added - the map was already
`boost::unordered_flat_map`, which is the right container; it was being asked
the wrong way.

**What is left in `find`, for whoever goes further.** It still hashes a string
and compares bytes on every property access. The engine already interns strings
(`core/atom.hpp`), so the real ceiling here is property names as atoms - an
integer compare instead of a hash and a memcmp - which is the change
`value.hpp` already anticipates when it calls the index "the slot a shape /
inline-cache design replaces later". That is a much larger change and it is not
a library question either.


## Canvas blending was per PIXEL, and everything in it was loop-invariant (2026-08-02)

`canvas_context::blend` was **22.9% of a Phaser frame** - the single largest
item in the engine, larger than the whole interpreter. Adding
`write_axis_rect` (9.1%), the `clip` lambda (6.4%) and `fill_axis_rect` (3.2%),
software canvas rasterisation was **41.6%**.

`blend(x, y, c)` is called once per pixel and re-derived, per pixel:

* a `shared_ptr` null check on `pixels_`
* `clipped_out`, which calls `width()` - another `shared_ptr` deref - up to
  twice, then indexes the mask
* `with_alpha(c)`, the global-alpha multiply
* a bounds check inside `bitmap::at` and another inside `bitmap::put`

**None of it varies along a row.** `blend_span(y, x0, x1, c)` hoists all of it:
one null check, one row clamp, the row pointer and the mask pointer computed
once, and the global alpha applied once. `write_span` does the same for the
opaque path and degenerates to `std::fill` when nothing is clipped.

**Measured, callgrind, a real Phaser frame:**

| | instructions |
|---|---|
| before | 22.939 G |
| after | **16.101 G (-29.8%)** |

`blend` and `write_axis_rect` both fall out of the top of the profile entirely.

**THE ARITHMETIC IS UNCHANGED, deliberately** - same `blend_over`, same order,
same rounding. This is a hoist, not a better blend, and the evidence is that all
**13 goldens are byte-identical** on the Windows cross-build. A faster blend that
moved a golden would be a different change needing a different argument.

### Why not the GPU

Asked and answered: the `gpu` subsystem is **composition** - tile textures and
one textured quad - not a 2D rasteriser. Moving canvas fills onto it would be a
real project, and three things stand in the way of even measuring it: the devbox
has no SDL at all, a Linux binary under WSL2 sees only lavapipe (a CPU Vulkan),
and the 13 goldens are byte-compared across two platforms - a GPU rasteriser
will not be bit-identical to the software one. See `docs/platform.md`.

### Where the time is now

| | share |
|---|---|
| `context::run_loop` | 18.4% |
| the `clip` lambda | 9.1% |
| `ctjs::vp::lex` | 8.8% |
| `object_object::find` | 8.4% |
| `std::_Hash_bytes` | 6.8% |

`find` plus `_Hash_bytes` is **15%** - property lookup still hashes a string and
compares bytes on every access. Interning property names as atoms
(`core/atom.hpp` already interns) is the change that removes both, and it is the
next thing worth doing.


## The hash was paid for twice (2026-08-02)

The property-lookup cluster was the top of the profile after the canvas work -
bigger than the interpreter:

| | share |
|---|---|
| `object_object::find` | 8.39% |
| `std::_Hash_bytes` | 6.78% |
| `context::lookup_property` | 3.69% |
| `__memcmp_avx2_movbe` | 3.17% |
| **total** | **22.0%** |

`std::_Hash_bytes` is libstdc++'s MurmurHash2, walked a BYTE AT A TIME, and it
was reached through the `string_hash` added an hour earlier - which used
`std::hash<std::string_view>` because that was the house pattern.

**boost::unordered applies an EXTRA mixing step to any hash not declared
avalanching**, because open addressing needs the low bits to be as good as the
high ones - and `std::hash` is not declared. So every lookup paid for a weak
byte-wise hash and then paid again to fix it up.

`boost::hash` is a stronger mix over word-sized chunks *and* carries the
guarantee. Using it, and declaring `using is_avalanching = void;`, removes both
costs at once:

| | before | after |
|---|---|---|
| `std::_Hash_bytes` | 1.091 G (6.78%) | **0.037 G (0.24%)** |
| `find` + hash together | 2.441 G | **1.929 G (-21%)** |
| a Phaser frame | 16.101 G | **15.601 G (-3.1%)** |

The hash is now inlined into `find` rather than called out to, which is why
`find`'s own number goes *up* while the pair goes down - a reminder to read the
cluster and not the line.

All 13 goldens byte-identical. Nothing observable depends on hash order: the
only iterations over these maps are GC marking and a size sum, and property
ORDER comes from the `props` vector, not the index.

### The library question, answered

This was worth asking of a library and the answer was already in the tree.
`flat_map` has been `boost::unordered_flat_map` for a long time - the right
container. What was wrong was how it was being *asked*: the wrong key type (a
temporary per lookup, fixed above) and the wrong hash (weak, then re-mixed).
**Two library-shaped bugs, no new library.**

A faster hash still - wyhash or xxh3, both single-header and in awesome-cpp -
would now buy almost nothing: hashing is 0.24%. What is left in the cluster is
the probe and `memcmp` at 3.26%, which is **key comparison**, and no hash
function removes that.

**The next step here is structural, not a dependency: property names as atoms.**
`core/atom.hpp` already interns strings, so a name becomes a `std::uint32_t` and
lookup becomes an integer hash and an integer compare - `_Hash_bytes` and
`memcmp` both go to zero, and `value.hpp` already anticipates it by calling the
index "the slot a shape / inline-cache design replaces later". That is a large
change and it is the honest next one.

## Computed-goto dispatch: measured properly, and the surprise was elsewhere (2026-08-02)

A first attempt at this reported computed goto 5.8% slower. **That measurement
was invalid** - `ctbrowser_bench` targets are `EXCLUDE_FROM_ALL`, so
`tools/remote-build.sh` never rebuilt the benchmark, and the comparison was
accidentally *original switch vs restructured switch* across two differently
configured build directories. It is redone here: one build directory, only the
source toggled, the benchmark rebuilt explicitly and its **md5 checked** every
time.

| config | instructions | wall (min of 7) |
|---|---|---|
| **A** the loop as it was | 39.839 G | 277.3 ms |
| **B** restructured, `switch` dispatch | **37.628 G** (-5.6%) | **263.7 ms** (-4.9%) |
| **C** restructured, computed goto | 38.772 G | 272.6 ms |
| **D** original + ONE line moved | **37.612 G** (-5.6%) | 265.4 ms |

Two findings, and the second is worth more than the first.

**Computed goto loses, on this hardware.** C against B is the only pair that
isolates dispatch: **+3.0% instructions and +3.3% wall.** The technique needs to
keep the frame in registers and jump handler to handler; this VM cannot, because
12 handlers push, pop or unwind `frames_` - a `std::vector` that can reallocate
and dangle a cached pointer - so every dispatch has to re-derive, and that
re-derivation is duplicated into all 88 handlers. Caching only in the "pure"
handlers does not rescue it either: `to_number` calls `valueOf`/`toString`
through `call()`, so `add`, `sub` and every comparison can re-enter the VM.

**It is kept anyway, behind `-DCTBROWSER_COMPUTED_GOTO` (CMake option of the
same name, off by default).** The result is a property of the branch predictor
rather than of this code - the devbox is Zen-class with a modern indirect
predictor, and the technique still wins on some microarchitectures. Measure it
on yours; `tests/bench_script` is how.

**The real win was one line, and it had nothing to do with dispatch.** D is the
whole of the restructuring's benefit reproduced by a six-line change: the loop
derived

```cpp
const program & prog = frame.closure != nullptr && frame.closure->owner != nullptr
                           ? *frame.closure->owner : *program_;
```

at the top of **every instruction**, to serve the ONE opcode that reads it
(`op::closure`). Moving it into that handler is **-5.6% instructions** across
the benchmark and **-1.7%** across a real Phaser frame. B and D measure the
same, so the 900-line restructure buys nothing the six-line change does not.

Combined with the property-lookup fix above, a Phaser frame went from
**23.815 G to 22.936 G instructions - 3.7% - for two small changes**, neither of
which needed a library and neither of which was the thing being looked for.

