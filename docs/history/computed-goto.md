# Computed gotos in the script VM

**Status: DONE (2026-08-02). Implemented, measured properly, kept behind a flag
that is OFF by default.**

Computed goto measured **+3.0% instructions and +3.3% wall** against the
`switch` on the dispatch-bound benchmark - the only comparison that isolates
dispatch, one build directory, benchmark rebuilt and checksummed each time. It
is retained and switchable (`-DCTBROWSER_COMPUTED_GOTO`, or the CMake option)
because that verdict belongs to the branch predictor, not to this code.

**An earlier run of this experiment was invalid and its conclusion was
withdrawn** - the benchmark target is `EXCLUDE_FROM_ALL` and was never rebuilt,
so the numbers compared two things neither of which was computed goto. The
lesson, which this tree had already written down once: verify that the thing you
changed is the thing you ran, and checksum the binary if you are not sure.

**The result that mattered was not the one being looked for.** The dispatch loop
derived `prog` - the program a frame belongs to - on EVERY instruction, to serve
the single opcode that reads it. Moving it into that handler is -5.6%
instructions on the benchmark and -1.7% on a real Phaser frame, from six lines.
`docs/performance.md` has the table.

What also survives is `tests/bench/bench_script`, which should have existed years ago.

---

## The idea

`context::run_loop` dispatches with a `switch` over 88 opcodes. A computed-goto
interpreter replaces that with a table of label addresses and a `goto *` at the
end of **every** handler:

```c
static void * const table[] = { [op::add] = &&L_add, ... };
goto *table[int(next.code)];
L_add:  reg(a) = ...;  goto *table[int(next.code)];
L_move: reg(a) = reg(b); goto *table[int(next.code)];
```

The win is **not** removing a bounds check. It is branch prediction. One
`switch` is one indirect branch that every opcode shares, so the predictor sees
one site with 88 targets and mispredicts constantly. Replicating the jump into
each handler gives the predictor N sites, each of which learns the *pairs* of
opcodes this bytecode actually emits — `get_prop` is usually followed by
`call_method`, `less` by `jump_if_false`. Published numbers for this are
**10–30% on dispatch-bound interpreters**; the ceiling is discussed below and it
is far lower here.

## Feasibility: settled, and it works on both compilers

Measured on the devbox, not assumed. Under this project's real flags
(`-O2 -pedantic -Wall -Wextra -Werror -Wconversion`) the extensions produce
**three** distinct errors:

```
error: use of GNU address-of-label extension   [-Werror,-Wgnu-label-as-value]
error: use of GNU indirect-goto extension      [-Werror,-Wgnu-label-as-value]
error: array designators are a C99 extension   [-Werror,-Wc99-designator]
```

All three are suppressed **locally**, by pragma, without weakening a single flag
for the rest of the tree:

```c
#if defined(__clang__)
#  pragma clang diagnostic ignored "-Wgnu-label-as-value"
#  pragma clang diagnostic ignored "-Wc99-designator"
#elif defined(__GNUC__)
#  pragma GCC diagnostic ignored "-Wpedantic"
#endif
```

Verified compiling and running correctly with **clang 24.0.0git** (the
`std::embed` fork, which is what the presets build with) *and* **GCC 13**, which
matters because the devbox is a second compiler and has caught four real defects
that clang did not.

Two more facts worth having settled before writing any code:

* **The table is plain `.rodata` with no thread-safe-init guard** — checked in
  the emitted assembly. A label address is not a constant expression, so the
  table cannot be `constexpr` and must be a function-local `static`; the worry
  was that this would emit a guard variable and an atomic load on the hottest
  path in the engine. It does not. (This must be **re-checked on GCC**, and
  re-checked if the table ever stops being `const`.)
* **`-Wswitch` is what keeps the opcode list honest today.** Adding an opcode
  and forgetting a case is currently a compile error. A label table indexed by
  array designators has *no* such check — a missing entry is a null pointer and
  a jump to address zero at run time. See "The safety problem" below; this is
  the part of the design that needs care, not the dispatch.

## The measurement that should decide this, and why it probably says no

`docs/performance.md` already profiled a **whole page render** (`p5basic`:
loading the p5.js bundle and drawing a sketch):

```
17.5%  ctjs::vp::lex
15.1%  compiler_impl::declare_local
 7.6%  compiler_impl::collect_captured_names
 1.4%  context::run_loop        <-- the entire interpreter
```

**The entire interpreter is 1.4%.** Dispatch is some fraction of that. Amdahl
puts the ceiling on a page load at well under half a percent, and a 25% faster
dispatch would move it by roughly **0.1%** — unmeasurable against this machine's
±10% run-to-run variance.

**That profile is a page LOAD, and it is the wrong workload for this question.**
Showing a page costs an order of magnitude more in *reading* JavaScript than in
running it. But the corpora now in this tree do not stop at load:

* **Phaser Space Invaders** runs a game loop — sprites, physics, collision — for
  as long as the game is up. `examples/pages/phaser-invaders.html`.
* **Babylon** renders a scene per frame.
* **p5's `draw()`** runs every frame for ever.

Those are execution-dominated, and they are what a computed-goto interpreter is
for. **Nobody has profiled one.** That is stage 0, and the number it produces is
the whole decision.

## Stages

### 0 — a VM benchmark, and a profile of an execution-heavy workload

**There is no script benchmark at all.** `tests/` has `bench_gpu`,
`bench_interaction`, `bench_layout`, `bench_raster`, `bench_reads` and
`bench_style` — and nothing for the VM, which is why the only number anyone has
for `run_loop` came from a page-load profile where it was noise.

1. `tests/bench/bench_script.cpp`, registered with `ctbrowser_bench` like the other
   six. It should run **bytecode-heavy loops that do not allocate** — integer
   arithmetic, property access on a fixed shape, method calls, a tight
   `for` — so the number is dispatch and not the garbage collector. Report
   instructions retired via callgrind (deterministic) as well as wall time,
   because ±10% variance is what forced min-of-seven elsewhere in this tree.
2. **Callgrind a frame-driven workload** — `phaser-invaders` for N frames — and
   report `run_loop`'s share of it. This is the number that decides the rest.

**Decision gate, written before the answer is known so it cannot be moved
afterwards:**

| `run_loop` share of an execution-heavy frame | verdict |
|---|---|
| under 5% | **stop.** Record the number in `docs/performance.md` and delete this plan. |
| 5–15% | implement, and expect ~1–3% end to end. Marginal; the safety cost below may outweigh it. |
| over 15% | implement. |

Stage 0 cancelling stages 1–3 is a **success**, and this tree has that outcome
twice already: the lexer plan and the Phaser plan both cancelled their own later
stages after measuring. It costs a day and buys a permanent benchmark either
way.

### 1 — the macro layer, with the switch still in place

Three macros, and the switch build must stay byte-for-byte equivalent:

```c
VM_DISPATCH_BEGIN(in.code)      // `switch (x) {`      | `goto *table[x];`
VM_CASE(load_const)             // `case op::load_const:` | `L_load_const:`
VM_NEXT                         // `break;`            | re-dispatch
VM_DISPATCH_END                 // `}`                 | (nothing)
```

Land this with `VM_USE_COMPUTED_GOTO` **undefined**, so the compiled output is
identical and the diff is reviewable as pure mechanical churn across 88 cases.
The goldens and all three ratchets must not move.

**`break` cannot be replaced blindly.** A `break` that ends a case must become
`VM_NEXT`; a `break` inside a `for`/`while`/`switch` *within* a handler must
stay a `break`. There are 88 cases and several contain loops. This is the one
step where a sed makes a silent wrong answer, so it is done by hand and the
switch build is the oracle.

### 2 — turn it on, and confront the prologue

**This is the real design problem, and it is why the win here will be smaller
than the published 10–30%.** The loop re-derives its context on *every*
iteration:

```c
call_frame & frame = frames_.back();
const program & prog = ...;            // per frame, not per loop
const function_proto & fn = *frame.proto;
if (frame.ip >= fn.code.size()) { break; }
const instruction in = fn.code[frame.ip++];
const std::size_t base = frame.base;
```

A textbook computed-goto interpreter jumps straight from one handler to the
next, skipping exactly this. Here it **cannot**, because `frames_` is a
`std::vector` that handlers push to and pop from: **12 sites** in the loop call
`frames_.push_back`, `frames_.pop_back`, `unwind_to_handler` or `raise`, and any
of them can reallocate the vector and dangle `frame`, or change which function
is executing.

So the handlers split in two:

* **The many that cannot change the frame** — arithmetic, moves, comparisons,
  property access, jumps. These re-dispatch directly, reusing `fn`, `base` and
  `frame`, and get the full benefit.
* **The 12 that can** — `call`, `construct`, `apply`, `ret`, `throw`,
  `push_handler`, `await_value`, `yield_value` and friends. These jump back to
  the prologue and re-derive everything.

That split is correct and is also most of the value: the frame-changing opcodes
are the *rare* ones, and the arithmetic-and-move opcodes are the hot ones. But
it must be enforced, not assumed — putting a frame-changing handler in the fast
group is a use-after-free that will look like a random wrong answer. Enforce it
by construction: `VM_NEXT` for the safe group, `VM_NEXT_RELOAD` for the other,
and a comment on the boundary saying which is which and why.

### 3 — measure, honestly

Same benchmark as stage 0, min-of-seven, plus callgrind instruction counts
(deterministic — no variance to argue about). Report the number whatever it is.

**Non-negotiable, and this is a portability change to the hottest loop in the
engine:**

* All 13 goldens byte-identical **on Linux and on the Windows cross-build**.
* p5 12/12, Phaser 10/10, webgl2 9/10 and 31/31 probes — unmoved.
* asan/ubsan clean, and **tsan**, because the static table is shared state.
* Built and tested on **GCC 13** as well as clang 24, and with
  `VM_USE_COMPUTED_GOTO` forced off, so both dispatch paths stay live.

## The safety problem, which is the real cost

Today, `-Wswitch` makes a missing opcode a **compile error**. That is a genuine
guarantee and it has value beyond this change: `op::yield_value` and
`op::pass_new_target` were both added recently, and the compiler pointed at the
switch each time.

A label table gives that up. The replacements, in order of preference:

1. **Keep the switch as the source of truth for completeness.** Build the label
   table from the same macro list that generates the cases, so one list feeds
   both and a new opcode that is not in it fails the switch build.
2. **A runtime assert** in a debug build that every table entry is non-null,
   plus `static_assert(std::size(table) == std::size_t(op::count_))`. The enum
   has no `count_` today — it would need one, which is a small honest change.
3. A missing entry left as a null pointer is a jump to address 0. That is a
   crash rather than a wrong answer, which is the right direction, but it is a
   crash with no diagnostic and it happens in the field rather than in CI.

**If stage 0 says the win is under 5%, this cost alone settles it.**

## What this is not

* **Not a bytecode redesign.** Superinstructions, register-window changes and
  inline caches are separate questions with much larger ceilings; see
  `docs/script.md`.
* **Not a threading change.** The table is `const` and shared; nothing about
  dispatch becomes stateful.
* **Not tail-call dispatch.** The `[[clang::musttail]]` style — one function per
  opcode, each tail-calling the next — is the other well-known technique and is
  arguably better on modern compilers. It is out of scope here because it would
  restructure all 88 handlers into functions rather than macro-wrapping them in
  place, which is a far larger diff for the same measurement. **If stage 0 says
  dispatch is worth attacking, compare the two before committing to either.**
