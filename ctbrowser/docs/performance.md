# Where the time actually goes

Everything here is measured on this tree. Numbers without a measurement behind
them are not in this file.

## How to measure, on WSL2

**`perf` cannot work here.** WSL2 runs a custom Microsoft kernel with no PMU, so
the `linux-tools` packages have nothing to read. Use **callgrind**:

```bash
valgrind --tool=callgrind --callgrind-out-file=cg.out ./build/unittests/ctbrowser-test-X
#                                                       ^ or build/test/, or build/benchmarks/
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

## Splitting the compiler cost 1.52%, and one function was all of it (2026-08-09)

`ctbrowser/lib/Script/compile.cpp` was 3,845 lines of one class. Splitting it into eleven
files means every member body crosses a translation-unit boundary, and there is
no LTO in this build - so it was measured before and after rather than hoped
about. `p5basic`, 30 frames, font8x8, under callgrind:

| | instructions | `compiler_impl` share |
|---|---|---|
| one file | 743,467,779 | 31.59% |
| eleven files | 773,796,001 (+4.08%) | 34.49% |
| eleven files, `at()` inline | 754,759,353 (**+1.52%**) | 32.80% |

**`compiler_impl::at()` was 19.0 M of the 30.3 M.** Five lines - the accessor
the whole compiler reads the node pool through. Out of line it appeared in the
profile at 4.18% and 32.4 M instructions; inlined, it does not appear at all,
which is why nobody had noticed it was hot. It is defined in
`ctbrowser/lib/Script/compile/compiler_impl.hpp` with these numbers written beside it.

No other member earned the same treatment. The profile behind `at()` is flat,
and that was checked rather than assumed - the temptation is to pull back the
whole list of one-liners, and the measurement says only one of them mattered.

This also corrects the headline table below. `declare_local` at 15.1% and
`collect_captured_names` at 7.6% are the numbers from BEFORE the memcmp fix
recorded further down this file. The current hot set in the compiler is the
capture analysis: `tour` 6.8%, `mentions_arguments` 3.9%, `child_slots` 2.4%.

## The finding that reorganised the work

Profiling a **whole page render** — `examples/corpus/p5basic`, loading the p5.js bundle
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

* **The lexer** — `docs/history/lexer.md` has the design for a runtime-only
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
| `ctbrowser/benchmarks/bench_script` before | 2.658 G (6.59%) | 40.345 G |
| `ctbrowser/benchmarks/bench_script` after | **1.462 G (3.67%)** | **39.839 G (-1.26%)** |
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


## The clip mask was built a pixel at a time (2026-08-02)

`canvas_context::clip` builds a full-canvas mask, and the lambda it hands to
`for_each_span` ran per PIXEL with every term loop-invariant - `clipped_out`
calling `width()` twice, a third `width()` for the index, three `shared_ptr`
derefs a pixel, and the row offset re-derived each time. **9.4% of a Phaser
frame**, in a lambda.

Hoisted the same way `blend_span` was: 15.601 G -> **15.058 G (-3.5%)**, and the
lambda itself 9.40% -> **6.13%**. All 13 goldens byte-identical.

`for_each_span` takes a `std::function`, which is an indirect call - but it is
called once per SPAN while the body runs per PIXEL, so the call is not the cost
and a `function_ref` or a template would buy little. The body was the cost.

## Allocators: measured three, and the answer is "yes, but" (2026-08-02)

Allocator traffic is **~4.8%** of a Phaser frame: `_int_malloc` 1.32%,
`_int_free` 1.26%, `malloc` 0.97%, `free` 0.62%, `malloc_consolidate` 0.25%,
`operator new` 0.35%. That is the one remaining hot item where a LIBRARY is the
whole answer - a drop-in allocator is a link-line change that cannot alter
results.

Measured by `LD_PRELOAD` before adopting anything, which is the cheap way to
find out:

| allocator | instructions | wall clock (min of 7) |
|---|---|---|
| glibc | 15.058 G | 1021 ms |
| jemalloc | 14.618 G (-2.9%) | **974 ms (-4.6%)** |
| mimalloc | **14.421 G (-4.2%)** | 976 ms (-4.4%) |

**jemalloc and mimalloc are tied on wall clock** - 974 against 976 is noise -
while mimalloc executes about 1.4% fewer instructions.

**So the tie-breaker is not performance, it is the Windows cross-build.**
mimalloc is a Microsoft project with first-class Windows support and builds
under llvm-mingw; jemalloc's mingw support is an afterthought and its Windows
story has been unmaintained for years. This tree ships Windows binaries and
byte-compares 13 goldens on them, so an allocator that does not cross-compile is
not a candidate whatever it scores.

**Not adopted yet, deliberately.** It needs a `tools/mingw/build-mimalloc-mingw.sh`
alongside the three that already build zlib, libpng, libjpeg-turbo and Boost.URL
into the cross sysroot, and that is a build-matrix commitment rather than a code
change. The measurement is here so the decision can be made on numbers.

The cheaper half of the same win needs no dependency: `clip()` allocates a fresh
`width * height` mask on every call, and Phaser calls it every frame. Reusing
that buffer removes a large allocation per frame without adding anything.


## mimalloc adopted, and Windows gained three times what Linux did (2026-08-02)

Allocator traffic was ~4.8% of a Phaser frame and the one hot item a LIBRARY
answered outright. Three were measured by `LD_PRELOAD` before anything was
adopted, which is the cheap way to find out:

| allocator | instructions | wall clock (min of 7) |
|---|---|---|
| glibc | 15.058 G | 1021 ms |
| jemalloc | 14.618 G (-2.9%) | 974 ms (-4.6%) |
| mimalloc | **14.421 G (-4.2%)** | 976 ms (-4.4%) |

**jemalloc and mimalloc tied on wall clock**, so the tie-breaker was the Windows
cross-build: mimalloc is a Microsoft project that cross-compiles under
llvm-mingw, jemalloc's mingw support is an afterthought. This tree ships Windows
binaries and byte-compares thirteen goldens on them.

**Adopted as v3.4.3**, pinned identically on both platforms - brew on Linux
(`tools/Brewfile`), `tools/mingw/build-mimalloc-mingw.sh` into the mingw sysroot.

| | Linux (instructions) | Windows `.exe` (600 frames, min of 7) |
|---|---|---|
| system allocator | 15.058 G | 678 ms |
| mimalloc v3 | **14.433 G (-4.2%)** | **599 ms (-11.7%)** |

**Windows gained nearly three times what Linux did**, which is not a surprise
once stated: it is measuring the distance from the platform's default allocator,
and the Windows CRT's is much further behind than glibc's. It is also the answer
to "why carry a third-party allocator when glibc is fine" - glibc is the good
case.

### Three ways this could have silently done nothing, and what stops each

* **The override may not be linked.** A global `operator new` in a static
  archive is pulled in only to satisfy an undefined symbol; if link order lets
  libstdc++ answer first, the binary uses the system allocator and looks
  identical. `ctbrowser::allocator_name()` ASKS mimalloc whether a fresh
  allocation came from its own regions, and `ctbrowser/unittests/unit/core_basics` asserts it.
* **The wrong major version.** apt ships v2, this tree pins v3, and they are
  different allocators behind the same header name - compile against one and
  link the other and the result is undefined behaviour that starts as a crash
  somewhere unrelated. `allocator_version()` is checked against 300.
* **A stale cross-build.** The builder script clones only if the checkout is
  missing, so bumping the pin would have changed nothing - the same trap the
  clang toolchain sat in for weeks. It is keyed on the tag now.

Half the `delete` forms are the other quiet failure: replacing
`operator new(size_t)` and leaving the sized or aligned `delete` to libstdc++
hands a mimalloc pointer to the system `free`. All eight forms are overridden.


## The prototype chain hashed the same name at every level (2026-08-02)

After mimalloc the property cluster was still the top actionable item:
`object_object::find` 13.09%, `lookup_property` 4.11%, `memcmp` 3.51% - **20.7%**
of a Phaser frame.

**Call counts said what the percentages could not.** 18.78 M calls to `find` for
8.35 M calls to `lookup_property` - **2.25 finds per property access** - at about
101 instructions each. The 2.25 is the prototype chain: `lookup_property` asks
every level for the same name, and each `find` HASHED IT AGAIN. The hash cannot
change between levels.

The fix rides **Boost.Unordered's heterogeneous lookup** - the same machinery
that lets a `string_view` be found in a `std::string`-keyed map. A transparent
hasher may accept more than one key type, so it can accept one that hands back
the hash it was given:

```cpp
struct prehashed_name { std::string_view text; std::size_t hash; };
std::size_t operator()(prehashed_name n) const noexcept { return n.hash; }  // free
```

`lookup_property` computes the hash once and walks the whole chain with it.
There is no lower-level "find with this hash" entry point in the container, and
none is needed.

| | before | after |
|---|---|---|
| `find` + `lookup_property` + `memcmp` | 2.992 G (20.7%) | **2.435 G (17.3%)** |
| a Phaser frame | 14.452 G | **14.053 G (-2.8%)** |

The cluster is **-18.6%**. `find`'s own line drops from 13.09% to 4.12% while
`lookup_property` rises, because the prehashed overload inlines into it - the
third time in this file that reading one line rather than the cluster would have
given the wrong answer.

The equality had to be spelled out beside the hasher: `std::equal_to<>` cannot
compare a `prehashed_name` to a `std::string`, and the comparison is against the
TEXT only - two names are equal when their characters are, never because their
hashes agree.

### What is left, and it is not a library

`memcmp` is still 3.6%: **key comparison**, which no hash function and no
container removes. 28.5 M calls, mostly on names of a few characters, where the
call overhead rivals the compare. The remaining structural fix is what it has
been for three rounds - **property names as atoms**, turning the key into a
`std::uint32_t` so the comparison is an integer one. `core/atom.hpp` already
interns. Boost.Flyweight is the library-shaped version of the same idea and was
considered; it loses here because this tree already has an intern table, and two
implementations of one job is the mistake it keeps citing.


## Five hash maps, measured rather than compared from their READMEs (2026-08-02)

> **SUPERSEDED 2026-08-08 — the four alternates are gone and there is one map.**
> The measurement below still stands and is why this section is kept: it is the
> evidence for what removing them cost. See "One map, and what it cost" at the
> end of this file for the number. `CTBROWSER_STRING_MAP` no longer exists.

`ctbrowser::string_flat_map` is the VM's property index and the hottest map in
the engine. Five open-addressing maps were built into it and run on a real
Phaser frame, because on paper they are all the same shape.

| map | instructions | wall (min of 11) | peak RSS |
|---|---|---|---|
| `boost::unordered_flat_map` | 14.053 G | 1009 ms | 199.7 MB |
| `ankerl::unordered_dense` | 14.360 G | 989 ms | 182.5 MB |
| **`tsl::robin_map`** | **13.995 G** | **973 ms** | **182.4 MB** |
| `absl::flat_hash_map` | 14.593 G | 1005 ms | 199.4 MB |
| `gtl::flat_hash_map` | 14.340 G | 997 ms | 200.5 MB |

**tsl::robin_map: -3.6% wall and -8.7% peak RSS** against the Boost default.
Adopted, vendored header-only at `external/robin-map` with its MIT licence.
`CTBROWSER_STRING_MAP` still selects any of the five, so this is re-measurable
rather than frozen in.

**The instruction count barely moved (-0.4%) while wall clock did, and that
disagreement IS the finding.** Robin-hood probing bounds how far a key can sit
from its ideal slot, so a lookup touches fewer cache lines. Callgrind counts
instructions and cannot see a cache miss - which is why the deterministic metric
this file has leaned on all day is the wrong instrument for THIS question, and
peak RSS moving 8.7% in the same direction is the corroboration.

`google::dense_hash_map` was asked about and rejected without benchmarking, on
evidence rather than reputation: it has **no `is_transparent`** anywhere, so
heterogeneous lookup is impossible - every `find(string_view)` would rebuild a
`std::string` (undoing -47%) and the prehashed chain walk could not exist at all
(-18.6%). It also needs `set_empty_key`/`set_deleted_key` sentinels, and no
string is safe to reserve when any string can be a property name. Last upstream
push 2021. `absl::flat_hash_map` is its living descendant and was measured.

### Windows needed a different method, and the first answer was wrong

The Windows exe is timed by launching it from WSL, and run-to-run drift there is
about **4%** - larger than the effect. A first pass compared 614 ms against an
earlier 589 ms and concluded tsl REGRESSED Windows. Those numbers came from
different sittings.

Rebuilding both maps as two binaries and **interleaving the runs**, ten rounds
each, removes the drift:

| | Windows (interleaved, min of 10) |
|---|---|
| `boost::unordered_flat_map` | 611 ms |
| `tsl::robin_map` | **602 ms (-1.5%)** |

Smaller than Linux's -3.6% but the same direction. **When the difference you are
chasing is near the noise floor, interleave; do not compare two sittings.** That
is the second time in this file that comparing numbers taken at different times
produced a confident wrong answer.


## simdutf for base64: 42x, verified, and NOT adopted yet (2026-08-02)

Tested on request. `simdutf::base64_to_binary` against
`ctbrowser::base64_decode`, on a 256 KB payload the size of the base64 PNGs
Phaser decodes at boot:

| | throughput |
|---|---|
| `ctbrowser::base64_decode` | 1,131 MB/s |
| `simdutf::base64_to_binary` | **47,878 MB/s — 42.3x** |

Byte-identical output. That is the largest single-function margin measured in
this whole exercise.

**It is not a drop-in, and the differential test is why we know.** simdutf's
`base64_default_accept_garbage` looked like our documented leniency - "a
character outside the alphabet is ignored rather than fatal" - and it is not:
it **stops at the first `=`**, where ours reads past it. Over 200,000 malformed
inputs the two disagreed on **4.7%**, first case `"=w%S5"` (ours 2 bytes, theirs
0).

**The shape that works is strict-with-fallback.** simdutf's *default* mode is
the spec's forgiving-base64: whitespace skipped, a character outside the
alphabet a failure. Restricting the comparison to inputs it ACCEPTS:

```
strict accepted and we AGREE:  60856
strict accepted and we DIFFER: 0
```

So: try simdutf strict, and fall back to the existing decoder when it refuses.
Well-formed input - every `data:` URL and every real `atob` - takes the fast
path; malformed input keeps today's behaviour exactly.

**Not adopted, because the honest case is thin.** base64 does not appear in
either corpus profile at all: it is below the 99% threshold on both the Phaser
frame and the p5 load, because each decodes a few hundred KB once at boot. 42x
on 0.2% is 0.2%. Against that, simdutf is a COMPILED library and would need the
mingw sysroot treatment mimalloc got.

It is written down because the argument changes the moment a corpus page embeds
a large `data:` URL, which is ordinary on the real web - and because the
measurement and the equivalence proof are the expensive part, and they are done.

**Our decoder is also more lenient than the specification**, which this file
should say plainly: WHATWG forgiving-base64 FAILS on a character outside the
alphabet, and browsers throw `InvalidCharacterError`. `base64_decode`'s comment
claims to match "the WHATWG `atob` in every browser" and does not. Adopting
simdutf strict *without* the fallback would fix that - a behaviour change worth
making deliberately rather than as a side effect of a performance patch.


## Array.prototype.sort was O(n^2) - and why it cannot be threaded (2026-08-02)

Found by asking whether x86-simd-sort applied. It does not: the engine has
exactly three sorts, the only hot one is the polygon filler's scanline
crossings (**2-20 elements**, where SIMD sorting is far below its crossover),
and `Array.prototype.sort` compares through a page-supplied callback that no
SIMD kernel can vectorise. But looking established that the sort was a stable
INSERTION sort:

| n | before | after | |
|---|---|---|---|
| 250 | 0.5 ms | 0.1 ms | |
| 1000 | 6.7 ms | 0.4 ms | |
| 4000 | **104.5 ms** | **1.8 ms** | **58x** |

Measured against `n^2`, the old numbers tracked it to within a tenth; the new
ones fall away from it. Extrapolating the old curve, ten thousand elements was
most of a second and a page would have looked hung.

**A bottom-up merge sort keeps the property that made insertion sort the
choice.** `std::sort` is undefined behaviour with an inconsistent comparator,
and a comparator written in JavaScript can return anything at all. A merge reads
only inside two index ranges it computed itself, so no answer the comparator
gives can move an index out of them - the safety is STRUCTURAL rather than a
promise the comparator has to keep. It is also stable, which the specification
requires.

**It sorts a snapshot**, which is a robustness fix rather than a speed one: the
old loop indexed the live array while calling out, so
`a.sort(() => { a.length = 0; return 0; })` walked off the end. There are tests
for that now, and for stability - mutation-tested by taking the right side on a
tie, which produces `cdab` where `cbad` is required.

The default (no comparator) path **computes its string keys once**. It was
calling `to_string` inside `stable_sort`'s comparison, so about `2 log n`
conversions per element - a thousand items paid for twenty thousand string
allocations to answer a thousand questions.

### Why this is not multithreaded

Asked, and the answer is correctness rather than effort. **`c.call(comparator,
...)` re-enters the VM**: it pushes onto `frames_`, allocates in `registers_`,
can allocate on the script heap and can trigger a collection. That state is
per-context and single-threaded. Calling a JS comparator from several threads
would not be slow, it would be memory corruption. The specification also lets a
comparator have side effects, so the ORDER of calls is observable and parallel
evaluation changes behaviour.

The default path is different in principle: once the string keys are extracted -
which needs the VM, because `to_string` can call a user `toString` - what
remains is a plain array of `std::string` with no VM involvement, and the
engine has a thread pool. **It is still not worth it**: sorting two thousand
keys is 0.4 ms, and a parallel merge does not pay for its coordination until
tens of thousands of elements, which is far past what pages sort. Recorded so
the idea is dismissed with a number rather than re-proposed.

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
on yours; `ctbrowser/benchmarks/bench_script` is how.

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



## One map, and what it cost (2026-08-08)

`CTBROWSER_STRING_MAP` is gone. `string_flat_map` is `boost::unordered_flat_map`
and nothing else; `external/robin-map` is deleted.

**Three of the five branches could not have compiled.** ankerl, absl and gtl had
no include path anywhere in the build - no `find_package`, no `FetchContent`, no
vendored copy. They were benchmarked once, in a tree that had those headers
somewhere, and had been dead `#elif` arms ever since. Only Boost and the
vendored tsl were real.

Measured on `phaser_invaders`, same build directory, only the source toggled:

| | instructions | wall (min of 10) | peak RSS |
|---|---|---|---|
| `tsl::robin_map` | 14.115 G | **0.99 s** | 297.0 MB |
| `boost::unordered_flat_map` | 14.199 G (+0.59%) | 1.03 s (+4.0%) | **278.0 MB (-6.4%)** |

**The wall-clock cost reproduced and the RSS finding did NOT - it reversed.**
2026-08-02 measured tsl 8.7% LIGHTER than Boost; this measures it 6.4% HEAVIER.
Same question, same machine class, opposite answer, and the difference is the
binary: ANGLE and SwiftShader are linked into it now, and peak RSS is dominated
by things that have nothing to do with the property index. The 2026-08-02 number
was not wrong, it was measuring a smaller program.

That is the useful lesson and it is one this file keeps re-learning: **a
measurement is about a binary, not about a library.** The -3.6% wall was
reproducible because it is a property of the map; the -8.7% RSS was not because
it never was.

+4% wall is the honest price of dropping four dependencies, and the section
below is where most of it comes back.

### What was NOT the answer, this time

`boost::concurrent_flat_map` was the candidate for `atom_table`, whose
`shared_mutex` guards a read-hot, write-rare intern table - textbook for it. The
profile settled it instead:

```
1,584,965 (0.01%)  ctbrowser::atom_table::text(ctbrowser::atom) const
        -          ctbrowser::atom_table::intern  (below the 99% threshold)
```

**The atom table is 0.01% of a Phaser frame and interning does not appear at
all.** A lock-free version of nothing is nothing, and it is not worth a floor
raise to 1.89. (The floor itself moved 1.80 -> 1.88 on 2026-08-20, for
Boost.Hash2 - see `docs/build.md`. It does not change this verdict: 1.89 is
still above it, and the measurement is what refuses the library.)

Boost.Bloom, as a negative filter on the prototype chain, is refused on the same
grounds: the chain is 2.25 levels and the fix that removes the cost is atoms, not
a filter in front of the same string compare.


## The Babylon profile, which nobody had taken (2026-08-08)

Babylon.js has been a corpus since 2026-08-02 and had never been profiled. It is
the largest bundle in the tree - 12 MB, 181,222 lines - and it is a **completely
different program** from the Phaser frame every number above describes.

`babylon_ratchet`, 9.442 G instructions:

| | share |
|---|---|
| `__memcmp_avx2_movbe` | **28.94%** |
| `compiler_impl::compile_ident` | 10.47% |
| `compiler_impl::resolve_upvalue` (+ recursive) | 13.12% |
| `ctjs::vp::lex` | 8.61% |
| `compiler_impl::emit_write` | 3.12% |
| `compiler_impl::compile_stmt` | 2.90% |
| `context::run_loop` (+ recursive) | **1.58%** |

**Nearly a third of the run was `memcmp`, and the interpreter was 1.6%.** This is
the 2026-07 finding again - reading JavaScript costs more than running it - but
an order of magnitude sharper, because the bundle is three times p5's size.

Its callers named the bug outright:

| caller of `memcmp` | share | calls |
|---|---|---|
| `compile_ident` | 8.68% | **35,224,883** |
| `resolve_upvalue` | 6.18% | |
| `emit_write` | 3.29% | |
| `predeclare_locals`'s lambda | 3.23% | |
| `compile_stmt` | 3.16% | 12,731,131 |
| `resolve_upvalue` (recursive) | 2.47% | |

**35.2 million string comparisons out of one function.** `find_local_entry`
scanned the frame's whole `locals` vector backwards comparing names, and
`resolve_upvalue` did the same to `upvalue_names` at every level of the frame
chain. Both run once per identifier MENTION, so a function is quadratic in its
own size - and Babylon's functions are big enough for that to be a third of the
program.

**This is the third time this exact shape has been found here** - `is_captured`
linear-scanned a vector, `Array.prototype.sort` was insertion sort, and now scope
resolution. The lesson is not "look for linear scans"; it is that a linear scan
only shows up when an input gets big enough, so **a new corpus is a profiling
instrument**, and the cheapest way to find the next one of these is to run
something larger than what you have.

### The fix, and what it bought

A per-frame name index beside `locals`: `string_flat_map<small_vector<uint32_t,
2>>` from name to the positions carrying it, innermost last. A vector per name
because names SHADOW - two `let x` in sibling scopes are two entries, and popping
the inner one must uncover the outer rather than erase the name. `upvalue_names`
gets the simpler kind, since it only grows within a frame.

It is safe because `locals` changes size in exactly two places, and both now go
through `add_local` / `shrink_locals`. A bare `push_back` would leave a name the
index cannot find - which is a local that silently reads as a global, the quiet
kind of wrong this compiler has produced before.

Landed together with the `clip()` fix below:

| | before | after |
|---|---|---|
| `babylon_ratchet` | 9.442 G | **4.431 G (-53.1%)** |
| `phaser_invaders` | 14.199 G | **13.428 G (-5.4%)** |
| the whole `ctest` suite | 21.3 s | **16.3 s** |

**Babylon compiles in half the instructions.** Phaser gains much less, which is
the point: its bundle is smaller and its frame is dominated by running rather
than reading, so the same fix is worth 5% there and 53% here. One number would
have told either lie on its own.


## The clip lambda still had a branch in it (2026-08-08)

`canvas_context::clip`'s per-pixel lambda was hoisted on 2026-08-02, from 9.4% to
6.13%. It was **6.55% of a Phaser frame** again here - the second largest single
item after the interpreter - and the hoist had left one thing behind:

```cpp
if (previous != nullptr && previous[at] == 0) { continue; }
```

`previous` is the clip being intersected with. It is read ONCE, before
`for_each_span`, and is the same pointer for every span and every pixel of the
call. The test was being asked per pixel, and a branch the compiler cannot prove
invariant is a branch it cannot vectorise around - so the loop stayed scalar,
byte at a time.

Split at the SPAN, where the answer changes never: with no clip in place the row
is `std::fill_n`, which is a memset; with one it is a byte-wise AND that no
longer re-reads the pointer. The arithmetic is unchanged - deliberately, as with
`blend_span` - and it is the same finding a third time: **the body of the
per-pixel loop was the cost, not the thing around it.**

The `make_shared<vector>(w * h, 0)` at the top of `clip()` is still there and is
still an allocation per call. It measures ~0.65% and it is NOT the easy fix it
looks like: `clip_` is a `shared_ptr<const vector>` that `save()` copies into the
state stack, so the buffer is genuinely shared and reusing it in place would
corrupt a saved clip. It needs a `use_count()` check to be correct, which is a
different change from this one.

### Where the time is now

`phaser_invaders`, 13.428 G:

| | share |
|---|---|
| `context::run_loop` | ~22% |
| `context::lookup_property` + `memcmp` | ~14% |
| `ctjs::vp::lex` | ~10% |
| `canvas_context::blend_span` | ~7% |

**And `libc` memory operations are no longer 19.3%.** That entry has been stale
since mimalloc landed; measured now it is `free` 0.61%, `_mi_theap_malloc_zero`
0.50%, `malloc` 0.18%, `operator new` 0.18% - **about 1.6% in total.** The
`std::pmr` arena this file has recommended for layout twice is not justified by
anything currently measurable, and that recommendation is withdrawn until a
profile puts it back.

The honest next change is unchanged and is now the biggest thing left:
**property names as atoms.** `lookup_property` plus `memcmp` is ~14% of a Phaser
frame, all of it hashing a string and comparing bytes for a name the engine
already interns elsewhere.


## Three Boost-shaped candidates, and only one of them was real (2026-08-08)

Asked of the tree directly: what is left that a Boost container answers? The
answer turned out to be "almost nothing", and the two negative results are worth
more than the positive one.

### `globals_` and `inline_cache_` still built a temporary - and it measured NEUTRAL

Both were `flat_map<std::string, V>` asked with a `string_view`, which is exactly
the shape that cost 10.7% in `object_object::find` and paid **-47%** to fix. Both
are `string_flat_map` now.

| | |
|---|---|
| `phaser_invaders` before | 13.646 G |
| after | 13.648 G (**+0.016%, noise**) |

**The bug is real and no corpus in this tree exercises it.** `widgets.html` has
ZERO inline `style=` attributes and `phaser-invaders.html` has one, so
`inline_style_of` returns at `if (text.empty())` before it ever reaches the
cache; and Phaser's hot path reaches globals through the opcode, which passes a
`std::string` and so never built the temporary. `context::global()` is the
embedder path, which the corpora do not hammer.

Kept as a strict cleanup with no performance claim attached. The lesson is the
one this file keeps re-learning from the other side: **a bug that looks identical
to a measured one is not therefore worth the same**, and which corpus exercises
the line decides that, not how the line reads.

### Indexing the closure and native property tables was REFUSED on a measurement

`closure_object::find` and `native_object::find` linear-scan a
`vector<pair<string, value>>`, once per property mention. A class compiles to a
`closure_object`, so every static, every `C.prototype` and every Babel
`_inherits` hop goes through one - and Babylon is 181,222 lines of class-heavy
code, so it was the corpus expected to show it.

It does not:

| | `babylon_ratchet`, 46.577 G |
|---|---|
| `closure_object::find` | 64,466,020 (**0.14%**) |
| `native_object::find` | 2,666,456 (**0.01%**) |

0.15% together, 0.04% of a Phaser frame. **And the fix lost anyway.** Folding
both into one table with a `string_flat_map` index built past a threshold:

| | before | after |
|---|---|---|
| the two `find`s | 5.14 M | **9.54 M (merged, ~1.9x)** |
| `phaser_invaders` | 13.646 G | 13.699 G (**+0.39%**) |

Two causes, both instructive. The branch selecting scan-or-index reads the map's
header before the scan, on a path where the scan itself touches one cache line;
and two small bodies the compiler had been inlining into their callers became one
out-of-line call. The comment above `native_object` in `script/vm.hpp` carries
the numbers so this is not re-proposed.

Why the scan is already right: `ensure_prototype` is lazy on the stated grounds
that "a program allocates far more functions than it constructs", so **most
closures hold nothing at all** and the loop exits on an empty vector.

### Numbers: the one that paid, and it is `std` rather than Boost

`context::to_string` was `std::to_string(double)` - `%f` to six decimals - and
`to_number` was `std::stod` inside a `try`/`catch`. See `script/number_format.hpp`
for what was wrong with each; the short version is that `String(1/3)` was
"0.333333", everything below about 1e-7 printed as "0", and all of it read
`LC_NUMERIC` in a repository that byte-compares goldens across two toolchains.

`std::to_chars`/`from_chars`, with the specification's own notation rule on top:

| | before | after |
|---|---|---|
| `context::to_number` | 173.8 M (1.27%) | **11.6 M (0.09%), -93%** |
| the conversion cluster | 306.8 M | **169.8 M, -44.6%** |
| `phaser_invaders` | 13.646 G | **13.525 G (-0.89%)** |
| `babylon_ratchet`: `to_number` | 121.3 M | **5.6 M, -95%** |

Read the CLUSTER: `to_number`'s own line falls 162 M while `to_number_value`
rises 24 M and `run_loop` 14 M, because what is left inlines into them. That is
the fourth time in this file.

**`babylon_ratchet`'s total moved -3.9% and that number is NOT claimed.** The
rest of the delta is LLVM and SwiftShader symbols - ANGLE's shader JIT - and the
cause is real but is not a speedup: Babylon interpolates numbers into GLSL
source, so a constant that used to print as "0" now prints as "1e-7" and a
DIFFERENT shader gets compiled. Two runs of one binary agree to 0.0016%, so it is
not run-to-run noise; it is a change in what work is done. `phaser_invaders`
draws through the 2D canvas, has no such path, and its -0.89% is fully
attributable to the two lines above.

**Boost.Charconv was considered and turned down** - see `docs/build.md`.

### And it exposed a bug in `Math.cbrt`

`ctbrowser/unittests/js/vm_basics` asserted `Math.cbrt(27)` printed "3" and had been passing
against **3.0000000000000004** for as long as it existed, because six-decimal
formatting printed "3" either way. glibc's `cbrt` is up to an ulp out on a
perfect cube; V8 returns the exact root. Corrected for the exact case only - a
Newton step fixes 27 and 216 and makes `cbrt(0.001)` worse - and `vm_basics` now
asks with `===` instead of a string.

**Full-precision printing makes every libm discrepancy visible.** That is a
determinism question this file should flag rather than settle: a page printing
`Math.sin(x)` now shows all 17 digits, and glibc and the mingw CRT are not
obliged to agree on the last one. No golden moved here, and none of them prints
a computed transcendental. A corpus that does would need checking on both
platforms.
