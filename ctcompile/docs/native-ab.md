# The native tier's four numbers — part 24 Phase 63 Step 4

**Measured on the devbox on 2026-09-02, at `cdb7741` plus this branch.**
Regenerate with

```bash
flock /tmp/ctbrowser-devbox-build.lock \
  ssh devbox 'cd projects/<dir> && python3 tools/check/native-ab.py --build build \
                --out ctcompile/docs/native-ab.md'
```

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

## What the numbers say

**1. `auto` saves under 1% of the source, and the pins cost 44% to 116% of it.**

`auto saves` is `plain − (deduced without pins)`, as a share of `plain`.
`pins cost` is `deduced − (deduced without pins)`, as a share of the pinless
file — the like-for-like base, since it is the same file twice.

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

**2. The pins are free everywhere except the source file.** Same stripped
binary to the byte, same `Ir` in `main` to the instruction, same compile time
inside the measurement's own spread:

| program | toolchain | compile s, pins ON | compile s, pins OFF | stripped B, both | Ir in `main`, both |
|---|---|---|---|---:|---:|
| numeric | g++ | 0.118 [0.118-0.119] | 0.118 [0.117-0.119] | 14,472 | 33,169 |
| numeric | clang++ | 0.147 [0.145-0.149] | 0.146 [0.146-0.150] | 14,528 | 33,186 |
| functions | g++ | 0.132 [0.128-0.133] | 0.139 [0.130-0.144] | 14,472 | 198,881 |
| structs | g++ | 0.114 [0.112-0.115] | 0.118 [0.115-0.124] | 14,472 | 14,716 |
| arrays | g++ | 0.204 [0.197-0.216] | 0.202 [0.197-0.207] | 14,568 | 10,195 |

The plan's Step 4 worry was that "template instantiation is compile time this
project pays on every build with no CI to hide it". At this scale it is not:
the pins are `static_assert(std::is_same_v<...>)` over already-known types, and
they cost nothing a seven-repetition median can see. **That conclusion is
scoped to the tier as it exists.** The moment 62½-B's specialisation lands, the
pins move from a `static_assert` per declaration to a `static_assert` per
*instantiation*, and this table has to be re-run rather than believed.

**3. `plain` and `deduced` produce byte-identical object code.** Every
`Ir in main` cell is equal across the three spellings of the same program, and
so is every stripped size. `check-print-deduced.cmake` already required the two
files to "differ only in spelling"; this is that requirement confirmed at the
other end of the compiler, on two toolchains, by an instruction count.

**4. Compile time is dominated by what the file includes, not by what it says.**
`arrays` has the *smallest* source of the four (2,888 B) and the *longest*
compile (0.20 s g++ / 0.22 s clang++, against 0.12–0.13 s / 0.14–0.15 s for the
rest). The difference is `#include <vector>`. For a fixture of this size the
compile is a measurement of the standard library headers, and the emitted-byte
column is not a proxy for it.

**5. Run time is below the noise floor, and here is the floor.**

An empty `int main() { return 0; }`, compiled and linked with the same flags on
the same box in the same hour, runs in **0.33 ms [0.31–0.44]** under g++ and
**0.72 ms [0.69–0.84]** under clang++. The three scalar fixtures run in
0.34–0.41 ms (g++) and 0.73–0.76 ms (clang++); their contribution over the
floor — 0.01 to 0.08 ms — is **smaller than the floor's own spread of
0.13 ms**, so the wall clock here is measuring `fork`, `execve` and the
dynamic loader. Reporting those milliseconds as a result would be reporting
the process launcher. (`arrays` is 0.71 ms under g++, and that 0.38 ms is
dynamic linking rather than the program — see below.)

The number that *is* about the program is the callgrind instruction count
inside `main`, taken under `--collect-atstart=no --toggle-collect=main`:

| program | Ir in `main`, g++ | Ir in `main`, clang++ | wall ms above the floor, g++ |
|---|---:|---:|---:|
| numeric | 33,169 | 33,186 | 0.08 (floor spread 0.13) |
| functions | 198,881 | 290,483 | 0.08 (floor spread 0.13) |
| structs | 14,716 | 13,880 | 0.01 (floor spread 0.13) |
| arrays | 10,195 | 11,102 | 0.38 — **but see below** |

Callgrind is deterministic, so it is immune to the five other agents building
on this box; the floor's own count is 3 instructions (g++) and 2 (clang++),
which is what says the toggle is doing what it claims.

`functions` is the one place the two toolchains disagree about the *program*:
198,881 instructions under g++ against 290,483 under clang++, a 46% difference
on identical C++ at identical flags. The fixture is `fib`-style recursion, so
this is a codegen difference and not a library one — worth knowing before any
claim that "the native tier is N times faster" is made from one compiler.

`arrays`' 0.38 ms above the floor is **not** its body: 10,195 instructions
cannot cost that. It is dynamic linking. g++ drops libstdc++ from an empty
translation unit under `--as-needed` and keeps it for one that says
`std::vector`, so the arrays binary's *total* is 1.86 M instructions against
the empty binary's 124 K. Under clang++, which links libstdc++ either way, the
floor is already 1.85 M and `arrays` is unremarkable. **The first version of
this measurement subtracted the empty binary's total from each program's total
and reported `arrays`' body as 1.73 M instructions against `structs`' 16 K.**
That number was a fact about `--as-needed` wearing a program's name, and the
`--toggle-collect=main` column exists because of it.

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

| program | spelling | toolchain | emitted B | compile s (median [min-max]) | stripped B | run ms (median [min-max]) | Ir in `main` | Ir total |
|---|---|---|---:|---|---:|---|---:|---:|
| numeric | `plain` | g++ | 6,137 | 0.119 [0.117-0.122] | 14,472 | 0.41 [0.39-0.50] | 33,169 | 190,889 |
| numeric | `plain` | clang++ | 6,137 | 0.144 [0.144-0.148] | 14,528 | 0.73 [0.70-0.97] | 33,186 | 1,880,648 |
| numeric | `deduced` | g++ | 13,210 | 0.118 [0.118-0.119] | 14,472 | 0.41 [0.38-0.52] | 33,169 | 190,889 |
| numeric | `deduced` | clang++ | 13,210 | 0.147 [0.145-0.149] | 14,528 | 0.74 [0.71-0.86] | 33,186 | 1,880,648 |
| numeric | `deduced -DCTCOMPILE_NO_TYPE_PINS` | g++ | 6,083 | 0.118 [0.117-0.119] | 14,472 | 0.40 [0.39-0.49] | 33,169 | 190,902 |
| numeric | `deduced -DCTCOMPILE_NO_TYPE_PINS` | clang++ | 6,083 | 0.146 [0.146-0.150] | 14,528 | 0.74 [0.71-0.86] | 33,186 | 1,880,678 |
| functions | `plain` | g++ | 5,834 | 0.129 [0.128-0.130] | 14,472 | 0.41 [0.39-0.51] | 198,881 | 355,434 |
| functions | `plain` | clang++ | 5,834 | 0.148 [0.147-0.153] | 14,528 | 0.76 [0.73-0.88] | 290,483 | 2,137,945 |
| functions | `deduced` | g++ | 10,503 | 0.132 [0.128-0.133] | 14,472 | 0.41 [0.40-0.50] | 198,881 | 355,434 |
| functions | `deduced` | clang++ | 10,503 | 0.150 [0.147-0.151] | 14,528 | 0.75 [0.72-0.90] | 290,483 | 2,137,949 |
| functions | `deduced -DCTCOMPILE_NO_TYPE_PINS` | g++ | 5,772 | 0.139 [0.130-0.144] | 14,472 | 0.42 [0.40-0.67] | 198,881 | 355,464 |
| functions | `deduced -DCTCOMPILE_NO_TYPE_PINS` | clang++ | 5,772 | 0.162 [0.154-0.174] | 14,528 | 0.76 [0.73-0.98] | 290,483 | 2,137,955 |
| structs | `plain` | g++ | 6,019 | 0.123 [0.116-0.130] | 14,472 | 0.35 [0.34-0.54] | 14,716 | 139,718 |
| structs | `plain` | clang++ | 6,019 | 0.144 [0.143-0.152] | 14,520 | 0.73 [0.70-0.92] | 13,880 | 1,860,668 |
| structs | `deduced` | g++ | 8,949 | 0.114 [0.112-0.115] | 14,472 | 0.34 [0.33-0.43] | 14,716 | 139,718 |
| structs | `deduced` | clang++ | 8,949 | 0.152 [0.149-0.163] | 14,520 | 0.74 [0.71-1.56] | 13,880 | 1,860,668 |
| structs | `deduced -DCTCOMPILE_NO_TYPE_PINS` | g++ | 5,977 | 0.118 [0.115-0.124] | 14,472 | 0.36 [0.32-0.48] | 14,716 | 139,731 |
| structs | `deduced -DCTCOMPILE_NO_TYPE_PINS` | clang++ | 5,977 | 0.158 [0.154-0.174] | 14,520 | 0.72 [0.71-0.93] | 13,880 | 1,860,698 |
| arrays | `plain` | g++ | 2,888 | 0.206 [0.200-0.212] | 14,568 | 0.71 [0.69-0.97] | 10,195 | 1,858,187 |
| arrays | `plain` | clang++ | 2,888 | 0.226 [0.221-0.237] | 14,632 | 0.72 [0.69-0.96] | 11,102 | 1,860,275 |
| arrays | `deduced` | g++ | 3,812 | 0.204 [0.197-0.216] | 14,568 | 0.74 [0.70-0.94] | 10,195 | 1,858,201 |
| arrays | `deduced` | clang++ | 3,812 | 0.224 [0.220-0.259] | 14,632 | 0.76 [0.73-1.00] | 11,102 | 1,860,289 |
| arrays | `deduced -DCTCOMPILE_NO_TYPE_PINS` | g++ | 2,876 | 0.202 [0.197-0.207] | 14,568 | 0.73 [0.70-0.86] | 10,195 | 1,858,204 |
| arrays | `deduced -DCTCOMPILE_NO_TYPE_PINS` | clang++ | 2,876 | 0.220 [0.217-0.231] | 14,632 | 0.73 [0.70-0.95] | 11,102 | 1,860,319 |

### The noise floor

`int main() { return 0; }`, same flags, same repetitions, same box, same hour.

| toolchain | version | compile s | stripped B | run ms | Ir in `main` | Ir total |
|---|---|---|---:|---|---:|---:|
| g++ | g++ (Ubuntu 13.3.0-6ubuntu2~24.04.1) 13.3.0 | 0.025 [0.025-0.025] | 14,336 | 0.33 [0.31-0.44] | 3 | 123,761 |
| clang++ | Ubuntu clang version 18.1.3 (1ubuntu1) | 0.059 [0.058-0.060] | 14,440 | 0.72 [0.69-0.84] | 2 | 1,846,499 |

Repetitions: 7 timed compiles and 100 timed runs per cell, after one untimed warm-up of each. `Ir in main` is callgrind under `--collect-atstart=no --toggle-collect=main`, so it counts the program's own work and nothing of the loader, the C++ runtime's static initialisation or libc start-up; `Ir total` is the whole process, which is what the wall clock is mostly measuring.
