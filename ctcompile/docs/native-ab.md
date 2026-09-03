# The native tier's four numbers — part 24 Phase 63 Step 4

**Measured on the devbox on 2026-09-02/03, at `cdb7741` plus this branch.**
Regenerate with

```bash
flock /tmp/ctbrowser-devbox-build.lock \
  ssh devbox 'cd projects/<your dir> && python3 tools/check/native-ab.py \
                --build build --out ctcompile/docs/native-ab.md'
```

**Take the lock, and write the output somewhere only you write.** Several
agents build on this box at once; one of them has already read another's test
output out of a fixed `/tmp` path. The script prints a provenance line into
the report naming the host and the build tree it measured, so a table can be
checked rather than trusted — the one below names
`/home/ubuntu/projects/ctbrowser-measure/build`.

Part 24 Phase 63 Step 4 asks for four numbers per program — **emitted bytes,
C++ compile seconds, stripped binary bytes, run time** — with
`--ctnative-print-deduced` on and off. `tools/check/native-ab.py` produces the
table below; this page is what the table means.

The programs are the four native pipelines wired in
`ctcompile/test/CMakeLists.txt`: `numeric` (`native-pipeline-fixture.js`),
`functions` (`native-fixture.js`), `structs` (`native-struct-fixture.js`) and
`arrays` (`native-array-fixture.js`). Every compile uses the flags the Phase 63
Step 7 gate uses — `-std=c++23 -O2 -pedantic -Wall -Wextra -Werror -Wconversion
-ffp-contract=off` — on **both** g++ 13.3.0 and clang++ 18.1.3, because Step 7
requires both toolchains and a compile-time number from one of them is half an
answer.

`plain` is the module the pipeline emits with every type spelled. `deduced` is
the same module through `--ctnative-print-deduced`: `auto` plus a
`static_assert` pin per deduced declaration. `deduced -DCTCOMPILE_NO_TYPE_PINS`
is the **same file** compiled with the pins preprocessed away; its emitted-byte
figure is the synthetic count of what the file would be if the pins were never
printed, computed by the same rule `check-print-deduced.cmake` uses.

---

## Taken twice, an hour apart, and that is part of the method

The box is shared with five other agents building concurrently, so the whole
run was taken **twice** under the build lock, about an hour apart; the second
is stamped 2026-09-03 01:17 UTC by the provenance line below, and the first
was not stamped, which is why the script now stamps every run.
Every deterministic column came back **identical**: emitted bytes, stripped
binary bytes and instruction counts agree to the byte and the instruction
across both runs. Every timed column moved. The table below is the second run
(its provenance line names the build tree it measured); run 1's medians appear
beside it wherever the difference changes what may be concluded.

If a number in this page is deterministic it was confirmed twice. If it is a
time it comes with a spread, and the between-run drift is quoted too, because
seven repetitions inside one run understate how much a shared box moves
between them.

---

## What the numbers say

**1. `auto` saves under 1% of the source, and the pins cost 33% to 117% of it.**

`auto saves` is `plain − (deduced without pins)`, as a share of `plain`.
`pins cost` is `deduced − (deduced without pins)`, as a share of the pinless
file — the like-for-like base, since it is the same file twice. Identical in
both runs.

| program | plain B | deduced B | deduced without pins B | `auto` saves | pins cost |
|---|---:|---:|---:|---:|---:|
| numeric | 6,137 | 13,210 | 6,083 | 54 B (0.88%) | +7,127 B (**+117%**) |
| functions | 5,834 | 10,503 | 5,772 | 62 B (1.06%) | +4,731 B (**+82%**) |
| structs | 6,019 | 8,949 | 5,977 | 42 B (0.70%) | +2,972 B (**+50%**) |
| arrays | 2,888 | 3,812 | 2,876 | 12 B (0.42%) | +936 B (**+33%**) |

This is the measurement part 24 §3.2 predicted — "over `{bool, i32, f64}`
deduction saves nothing worth having" — and the earlier 62½-E figures said the
same. It is now four programs rather than three, and the pins' share is a
range rather than "roughly double": it falls as the program's non-declaration
content grows, because a pin is a fixed cost per deduced declaration.

**2. The pins are free in the object code, and their compile-time cost cannot
be separated from the noise of a shared box.**

Same stripped binary to the byte and same `Ir` in `main` to the instruction,
in every cell of both runs:

| program | toolchain | stripped B (all three spellings) | Ir in `main` (all three) |
|---|---|---:|---:|
| numeric | g++ | 14,472 | 33,169 |
| numeric | clang++ | 14,528 | 33,186 |
| functions | g++ | 14,472 | 198,881 |
| functions | clang++ | 14,528 | 290,483 |
| structs | g++ | 14,472 | 14,716 |
| structs | clang++ | 14,520 | 13,880 |
| arrays | g++ | 14,568 | 10,195 |
| arrays | clang++ | 14,632 | 11,102 |

Compile time, pins on against pins off, in both runs (median seconds):

| program | toolchain | run 1 ON | run 1 OFF | run 2 ON | run 2 OFF |
|---|---|---|---|---|---|
| numeric | g++ | 0.118 | 0.118 | 0.125 | 0.124 |
| numeric | clang++ | 0.147 | 0.146 | 0.154 | 0.144 |
| functions | g++ | 0.132 | 0.139 | 0.131 | 0.129 |
| functions | clang++ | 0.150 | 0.162 | 0.149 | 0.148 |
| structs | g++ | 0.114 | 0.118 | 0.120 | 0.117 |
| arrays | g++ | 0.204 | 0.202 | 0.191 | 0.192 |

The sign of the difference is not stable: across those twelve cells, removing
the pins made the compile *slower* in four, faster in seven and made no
difference in one. The largest single gap is 0.010 s — numeric, clang++, run
2, about 6% — and the **same comparison in run 1 gave 0.001 s**, while that
cell's pins-on median moved 0.007 s between the two runs all by itself. A
difference that is 0.001 s one hour and 0.010 s the next, with the sign
unstable elsewhere, cannot be attributed to the pins at this scale; it is a
measurement of the box. **Part 24's Step 4 worry was that
"template instantiation is compile time this project pays on every build with
no CI to hide it". At this tier it is not measurable.** That conclusion is
scoped to a tier with no specialisation in it: when 62½-B's option 1 lands the
pins move from one `static_assert` per declaration to one per *instantiation*,
and this table must be re-run rather than believed.

**3. `plain` and `deduced` produce byte-identical object code.** Every
`Ir in main` and every stripped size above is one number for all three
spellings of the same program, on both toolchains, in both runs.
`check-print-deduced.cmake` already required the two files to "differ only in
spelling"; this is that requirement confirmed at the other end of the
compiler, by an instruction count.

**4. Compile time is dominated by what the file includes, not by what it says.**
`arrays` has the *smallest* source of the four (2,888 B) and the *longest*
compile (0.19 s g++ / 0.21 s clang++, against 0.12–0.13 s / 0.15 s for the
rest). The difference is `#include <vector>`. For a fixture of this size the
compile is a measurement of the standard library headers, and the
emitted-byte column is not a proxy for it.

**5. Run time is below the noise floor. Here is the floor, and here is why the
delta is not a result.**

An empty `int main() { return 0; }`, built and linked with the same flags on
the same box in the same run:

| | floor g++ | floor clang++ |
|---|---|---|
| run 1 | 0.33 ms [0.31–0.44] | 0.72 ms [0.69–0.84] |
| run 2 | 0.31 ms [0.29–0.37] | 0.76 ms [0.71–0.87] |

The three scalar fixtures, and their excess over that run's own floor:

| program (g++) | run 1 median | run 1 − floor | run 2 median | run 2 − floor |
|---|---|---|---|---|
| numeric | 0.41 ms | 0.08 | 0.43 ms | 0.12 |
| functions | 0.41 ms | 0.08 | 0.43 ms | 0.12 |
| structs | 0.34 ms | 0.01 | 0.37 ms | 0.06 |

**The excess is not reproducible.** The programs did not change between the
two runs and their deltas moved by 50% to 500%. Each delta is also the same
size as, or smaller than, the floor's own min-to-max spread in the run it came
from (0.13 ms and 0.08 ms). So the wall clock here is measuring `fork`,
`execve` and the dynamic loader; quoting those milliseconds as a per-program
result would be quoting the process launcher, which is worse than reporting no
number at all.

The number that *is* about the program is the callgrind instruction count
inside `main`, taken under `--collect-atstart=no --toggle-collect=main`. It is
deterministic — identical across both runs, immune to the other agents on the
box — and the floor's own count is **3** instructions under g++ and **2**
under clang++, which is what says the toggle is doing what it claims:

| program | Ir in `main`, g++ | Ir in `main`, clang++ |
|---|---:|---:|
| numeric | 33,169 | 33,186 |
| functions | 198,881 | 290,483 |
| structs | 14,716 | 13,880 |
| arrays | 10,195 | 11,102 |

`functions` is the one place the two toolchains disagree about the *program*:
198,881 instructions under g++ against 290,483 under clang++, a 46% difference
on identical C++ at identical flags. The fixture is `fib`-style recursion, so
this is a codegen difference and not a library one — worth knowing before any
claim that "the native tier is N times faster" is made from one compiler.

**6. `arrays` runs 0.43 ms above the floor under g++, and that is dynamic
linking, not the program.** 10,195 instructions cannot cost that. g++ drops
libstdc++ from an empty translation unit under `--as-needed` and keeps it for
one that says `std::vector`, so the arrays binary's *total* instruction count
is 1.86 M against the empty binary's 124 K. Under clang++, which links
libstdc++ either way, the floor is already 1.85 M and `arrays` is
unremarkable.

**The first version of this measurement subtracted the empty binary's total
from each program's total and reported `arrays`' body as 1.73 M instructions
against `structs`' 16 K.** That number was a fact about `--as-needed` wearing
a program's name. The `Ir in main` column exists because of it, and the
subtraction it replaced is recorded here so nobody re-derives it.

---

## What is NOT here

**The specialisation cap.** Step 4 asks for every number "for the
specialisation cap at 1 (never specialise) and at 4". **That axis does not
exist.** Phase 62½-B is at option 3 — the join — only: there is no
specialisation, no `dyn<...>`, and no cap option on any pass in
`CTNative/Transforms/Passes.td`. A cap column filled with the same number twice
would be a measurement of nothing, so it is left out and said here instead.
When option 1 lands, `native-ab.py` grows one `--cap` argument and this page
gains a column.

**A corpus.** The four programs above are fixtures. The native backend does not
emit a complete bootstrap, p5 or Phaser compilation unit yet (Phase 63 Steps 2
and 3 put the claimed set at 0.56%–0.78% of those corpora), so there is nothing
corpus-sized to compile. A missing corpus is not a vacuous pass, and this page
says so rather than implying the fixtures are representative.

---

## A correction to the record

The Phase 62½-E table in `24-native-cpp-backend.md` reports, from the printing
gate:

| program | deduced declarations | plain | deduced | deduced without pins |
|---|---|---|---|---|
| numeric | 37 | 4,297 B | 9,093 B | 4,260 B |
| functions | 41 | 4,903 B | 9,806 B | 4,840 B |
| structs | 14 | 2,853 B | 4,756 B | 2,828 B |

**Every one of those numbers is now stale**, and by a lot: at `cdb7741` the
emitted source is 43%, 19% and 111% larger respectively, and `functions` is now
*smaller* than `numeric` where it used to be larger. The ratios the plan drew
its conclusions from survive — `auto` still saves under 1%, the pins still
roughly double the smaller files — but the absolute figures should be replaced
by the table below, and `arrays` added, which that table predates.

---
<!-- generated by tools/check/native-ab.py - do not hand-edit the tables -->

**Provenance.** host `devbox`, build tree `/home/ubuntu/projects/ctbrowser-measure/build`, emitter `/home/ubuntu/projects/ctbrowser-measure/build/ctcompile/tools/ctjs-translate/ctjs-translate`, 2026-09-03 01:17 UTC.

| program | spelling | toolchain | emitted B | compile s (median [min-max]) | stripped B | run ms (median [min-max]) | Ir in `main` | Ir total |
|---|---|---|---:|---|---:|---|---:|---:|
| numeric | `plain` | g++ | 6,137 | 0.124 [0.123-0.125] | 14,472 | 0.43 [0.41-0.54] | 33,169 | 190,889 |
| numeric | `plain` | clang++ | 6,137 | 0.152 [0.145-0.153] | 14,528 | 0.80 [0.77-0.97] | 33,186 | 1,880,648 |
| numeric | `deduced` | g++ | 13,210 | 0.125 [0.125-0.127] | 14,472 | 0.43 [0.41-0.54] | 33,169 | 190,889 |
| numeric | `deduced` | clang++ | 13,210 | 0.154 [0.151-0.155] | 14,528 | 0.80 [0.76-0.87] | 33,186 | 1,880,648 |
| numeric | `deduced -DCTCOMPILE_NO_TYPE_PINS` | g++ | 6,083 | 0.124 [0.119-0.126] | 14,472 | 0.43 [0.40-0.49] | 33,169 | 190,902 |
| numeric | `deduced -DCTCOMPILE_NO_TYPE_PINS` | clang++ | 6,083 | 0.144 [0.143-0.147] | 14,528 | 0.76 [0.73-0.88] | 33,186 | 1,880,678 |
| functions | `plain` | g++ | 5,834 | 0.133 [0.127-0.156] | 14,472 | 0.44 [0.41-0.53] | 198,881 | 355,434 |
| functions | `plain` | clang++ | 5,834 | 0.148 [0.147-0.156] | 14,528 | 0.76 [0.72-0.88] | 290,483 | 2,137,945 |
| functions | `deduced` | g++ | 10,503 | 0.131 [0.127-0.135] | 14,472 | 0.43 [0.41-0.52] | 198,881 | 355,434 |
| functions | `deduced` | clang++ | 10,503 | 0.149 [0.147-0.153] | 14,528 | 0.76 [0.73-0.89] | 290,483 | 2,137,949 |
| functions | `deduced -DCTCOMPILE_NO_TYPE_PINS` | g++ | 5,772 | 0.129 [0.128-0.132] | 14,472 | 0.41 [0.39-0.51] | 198,881 | 355,464 |
| functions | `deduced -DCTCOMPILE_NO_TYPE_PINS` | clang++ | 5,772 | 0.148 [0.145-0.154] | 14,528 | 0.77 [0.74-0.92] | 290,483 | 2,137,955 |
| structs | `plain` | g++ | 6,019 | 0.121 [0.114-0.121] | 14,472 | 0.37 [0.35-0.45] | 14,716 | 139,718 |
| structs | `plain` | clang++ | 6,019 | 0.152 [0.149-0.163] | 14,520 | 0.78 [0.74-0.95] | 13,880 | 1,860,668 |
| structs | `deduced` | g++ | 8,949 | 0.120 [0.118-0.121] | 14,472 | 0.37 [0.35-0.45] | 14,716 | 139,718 |
| structs | `deduced` | clang++ | 8,949 | 0.151 [0.150-0.154] | 14,520 | 0.78 [0.74-0.92] | 13,880 | 1,860,668 |
| structs | `deduced -DCTCOMPILE_NO_TYPE_PINS` | g++ | 5,977 | 0.117 [0.113-0.119] | 14,472 | 0.35 [0.33-0.46] | 14,716 | 139,731 |
| structs | `deduced -DCTCOMPILE_NO_TYPE_PINS` | clang++ | 5,977 | 0.148 [0.146-0.151] | 14,520 | 0.75 [0.71-0.92] | 13,880 | 1,860,698 |
| arrays | `plain` | g++ | 2,888 | 0.191 [0.190-0.197] | 14,568 | 0.74 [0.70-0.89] | 10,195 | 1,858,187 |
| arrays | `plain` | clang++ | 2,888 | 0.219 [0.215-0.229] | 14,632 | 0.73 [0.71-0.88] | 11,102 | 1,860,275 |
| arrays | `deduced` | g++ | 3,812 | 0.191 [0.190-0.196] | 14,568 | 0.74 [0.70-0.86] | 10,195 | 1,858,201 |
| arrays | `deduced` | clang++ | 3,812 | 0.212 [0.211-0.217] | 14,632 | 0.74 [0.71-1.57] | 11,102 | 1,860,289 |
| arrays | `deduced -DCTCOMPILE_NO_TYPE_PINS` | g++ | 2,876 | 0.192 [0.187-0.193] | 14,568 | 0.74 [0.71-0.83] | 10,195 | 1,858,204 |
| arrays | `deduced -DCTCOMPILE_NO_TYPE_PINS` | clang++ | 2,876 | 0.214 [0.212-0.220] | 14,632 | 0.74 [0.71-0.90] | 11,102 | 1,860,319 |

### The noise floor

`int main() { return 0; }`, same flags, same repetitions, same box, same hour.

| toolchain | version | compile s | stripped B | run ms | Ir in `main` | Ir total |
|---|---|---|---:|---|---:|---:|
| g++ | g++ (Ubuntu 13.3.0-6ubuntu2~24.04.1) 13.3.0 | 0.025 [0.024-0.025] | 14,336 | 0.31 [0.29-0.37] | 3 | 123,761 |
| clang++ | Ubuntu clang version 18.1.3 (1ubuntu1) | 0.058 [0.058-0.058] | 14,440 | 0.76 [0.71-0.87] | 2 | 1,846,499 |

Repetitions: 7 timed compiles and 100 timed runs per cell, after one untimed warm-up of each. `Ir in main` is callgrind under `--collect-atstart=no --toggle-collect=main`, so it counts the program's own work and nothing of the loader, the C++ runtime's static initialisation or libc start-up; `Ir total` is the whole process, which is what the wall clock is mostly measuring.
