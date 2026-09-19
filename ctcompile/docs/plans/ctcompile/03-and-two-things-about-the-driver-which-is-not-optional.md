[Back to ctcompile.md](../ctcompile.md)

### And two things about the driver, which is not optional

A PDL pattern can only run inside a pattern driver, and the greedy driver
arrives with **folding, constant CSE and AGGRESSIVE region simplification on by
default**. All three are switched off explicitly here; leaving them inherited
would run upstream folding over this IR, which has crashed before.

What cannot be switched off is its **dead-code elimination** — every greedy
entry point "performs simple dead-code elimination before attempting to match
any of the provided patterns", and `GreedyRewriteConfig` has no knob for it. An
unused `emitc.variable` is allocate-only, so the driver started erasing one that
this pass used to erase itself, and `prune-dead-stores.mlir` went red on the
*count* while the output IR stayed byte-identical. The pass now attaches a
`RewriterBase::Listener` and asks the driver what it removed. Relatedly, the
driver reports no per-pattern hit count, so the `report` option recovers the
marks it made by diffing the IR.

### What it cost

Measured on the devbox, compiling the same TU both ways with the same command
line and relinking `ctjs-opt` against each:

| | before | after |
|---|---|---|
| `PruneDeadStores.cpp.o` | 58,424 B | 105,400 B |
| that TU's compile | 2.75 s | 3.27 s |
| `ctjs-opt` | 12,270,824 B | 12,289,440 B (+0.15%) |

`mlir-pdll` itself runs in 11 ms. The binary barely moves because the PDL
interpreter was **already there**: `MLIRRewrite`'s interface libraries are
`MLIRIR;MLIRSideEffectInterfaces;LLVM;MLIRPDLDialect;MLIRPDLInterpDialect;MLIRPDLToPDLInterp;MLIRRewritePDL`,
and `PatternApplicator.cpp.o` has undefined references to `PDLByteCode`
unconditionally. Anything here that already ran a pattern driver already paid
for PDL.

### The boundary

* **PDLL** — rewrites whose whole match is operation names, operands and
  results, *including this project's own operations*: EmitC and SCF cleanups
  after the lowering, the `canonicalize.mlir` patterns the dialect does not have
  yet, and any type-preserving `ctjs` rewrite that needs no lattice. A `ctjs`
  **kind** is available too, through a native constraint — see the correction
  below; only the attribute *literal* is not.
* **An op INTERFACE** — the per-operation `if`-chains in `replace()` and
  `admission::op()`. This is the policy's own prescribed remedy and it is the
  right one, but PDLL is not the mechanism: a `CTNativeLowerable` interface with
  `carrierOf`/`emit` methods keeps the dispatch out of the pass exactly the way
  `CTJS_RuntimeCallOpInterface` already does for the boxed tier, and unlike PDL
  it can be given the solver and the side tables.
* **C++, under the policy's own carve-outs** — `retype()` (in-place types), the
  prune fixpoint (use lists), the importer, and **`admission` in full**.

`admission` deserves the plainest possible statement. Its output is not a
boolean: it is a refusal *string* naming the first thing that failed, and the
tier is defined by that diagnostic — a `ctnative.not_native` reading
*"field `class` is a C++ keyword or a macro of <cmath>/<cstdio>, so the
generated struct would not compile"* is the product. PDLL has no `otherwise`,
no way to say WHICH predicate reports when a match fails, and no way to reach
the lattice the predicates read. (Ordering between whole patterns IS
expressible - a pattern's benefit defaults to the number of matched operations
and can be set explicitly - so the missing thing is the diagnostic, not the
precedence.) **A PDLL `admission` would replace a named refusal with a silent
non-match**, which is a regression in precisely the property this project exists
to protect. It stays C++, and the interface above is how its per-operation
switch stops being a switch.

## PDLL over our own dialect, and the guard that makes it safe

The section above concluded that `mlir-pdll` "has no way to load an out-of-tree
dialect" and therefore that no pattern may touch a `ctjs` operation. The first
clause is true and the second does not follow. Measured on the devbox against
the pinned LLVM 23.1.0 on 2026-09-02, there are **three** separate facts where
the earlier spike saw one.

### 1. Matching our operations already worked

PDLL learns operations from **ODS**, not from a registered dialect. With
`ctcompile/include` on the include path,

```
#include "ctcompile/CTJS/IR/CTJSOps.td"
Pattern P { let root = op<ctjs.binary>; replace root with root; }
```

compiles, exits 0, and emits `operation "ctjs.binary"`. The include is load
bearing for *shape*: `op<ctjs.unary>(a: Value, b: Value)` is a hard error —
*"invalid number of operand groups for `ctjs.unary`; expected 1, but got 2"* —
quoting the record in `CTJSOps.td`.

`lib/CTNative/Lowering/CMakeLists.txt` now passes `${PROJECT_SOURCE_DIR}/include`
and `${PROJECT_BINARY_DIR}/include` to every `add_mlir_pdll_library` beside
`${MLIR_INCLUDE_DIRS}`, spelled the way `ctcompile_target()` spells them for
C++, so a `.td` and a `.h` are included by the same project path.

### 2. Two things it accepts at exit 0 — one a trap, one a documented feature

**An attribute literal of our dialect is dropped, and this one IS a trap.** As
above: stderr says *"'none' attribute created with unregistered dialect"*, the
exit status is **0**, and the emitted pattern's `kind` is a bare
`%1 = attribute` with no constraint. The reference specifies `attr<"…">` as the
textual form of that attribute, parsed when the pattern is compiled, and
`mlir-pdll --help` has no dialect-registration flag — its only inputs are `-I`
and the ODS it parses. Nothing about the outcome is intended.

**An operation name ODS does not know is accepted BY DESIGN.** The earlier spike
had this backwards, and so did a comment in `PruneDeadStores.pdll` claiming the
ODS include made the name checked. It does not:

```
#include "ctcompile/CTJS/IR/CTJSOps.td"
Pattern P { let root = op<ctjs.binry>; replace root with root; }
```

is **exit 0 with an empty stderr**, and so is the same file with the `#include`
deleted. That is not a hole: the language reference has a section titled
*Unregistered Operations* in which a variable of an unregistered operation is
deliberately supported, with result access falling back to numeric indexing. A
misspelling and an intentionally-unregistered operation are the same thing to
the tool, and it has no way to tell them apart.

**So the guard closes a trap and OPTS OUT OF A FEATURE**, and the second half is
a project decision rather than a bug report. This tree has no unregistered
operation it wants to match and every reason to want a misspelling to fail, so
it declines the freedom — which is also why the guard's own test is written the
way it is, below.

`ctcompile/utils/pdll-strict.sh` refuses both. It is installed as
`MLIR_PDLL_TABLEGEN_EXE` in `ctcompile/CMakeLists.txt` — the variable
`AddMLIR.cmake`'s `_pdll_tablegen` reads — as the two-element list *(guard, real
tool)*, so every `add_mlir_pdll_library` in the tree goes through it and no
target can opt out. It does two things:

1. runs the tool, and **fails the build on any diagnostic even at exit 0**;
2. re-runs it with `--dump-ods` (which prints to *stderr*) and `-x=mlir`, and
   fails if any `operation "…"` the emitted pattern names is absent from the
   ODS the run actually loaded. No argument parsing: `-o`, `-d` and `-x` are
   ordinary `cl::opt` scalars, so the copies appended after `"$@"` win.

`test/PDLL/guard.cmake` (ctest: `ctcompile_pdll_guard`, ~70 ms) runs it
over three fixtures in `test/PDLL/` that nothing compiles, and asserts **both
sides** of each: that the raw `mlir-pdll` accepts the two bad ones, and that the
guard refuses them, naming *"unregistered dialect"* and *"ctjs.binry"*
respectively. The raw-acceptance half is what keeps the gate honest, and it
matters more for the misspelling than for the literal: we are overriding
documented behaviour there, so the day the tool changes — a trap closed, or the
unregistered-operation feature withdrawn — the assertion that the raw tool still
accepts the input is the one that fails first, and it says which. The accepted
fixture must pass and its output must contain `operation "ctjs.binary"`, so a
guard that simply refused everything fails here.

### 3. A native constraint is the substitute, and it is better

`ctcompile/lib/CTNative/Lowering/UnaryPlusIsIdentity.pdll` is the first pattern
in the tree over our own dialect: `+x` on a value admission has proved a number
is `x`.

```
Constraint IsUnaryPlus(op: Op) [{
  return ::ctcompile::ctnative::pdll::isUnaryPlus(rewriter, op);
}];

Pattern UnaryPlusIsIdentity {
  let root = op<ctjs.unary>(operand: Value);
  IsUnaryPlus(root);
  replace root with operand;
}
```

Unlike the literal, `UnaryKind::Pluss` here is a C++ compile error.

**What a native body can and cannot do, corrected against the reference.** A
native constraint takes any number of matched entities — `Attr`, `Op`, `Type`,
`TypeRange`, `Value`, `ValueRange` — and a *named* `Op<dialect.name>` parameter
is translated to the CONCRETE C++ op class once the ODS is included. That is why
the constraint above is declared `Constraint IsUnaryPlus(op: Op<ctjs.unary>)`:
the generated wrapper is
`IsUnaryPlusPDLFn(::mlir::PatternRewriter &, ::ctcompile::ctjs::UnaryOp)`, the
framework type-checks the argument before calling
(`ProcessDerivedPDLValue::verifyAsArg` is a `TypeSwitch` that fails the
constraint on a mismatch), and the body needs no `dyn_cast` and no null check.
Native **rewrites** go further still: they may return values, several at once,
and build operations.

Two limits are real, and both are narrower than "a plain function pointer that
captures nothing". **Constraints cannot return values at all** — the reference
marks it a TODO — so a constraint can never compute a carrier, only accept or
reject. And **every argument comes from the match**, which is the next section.

`test/CTNative/Lowering/Scalars/unary-plus.mlir` asserts that the pattern fires *and*
that `ctjs.unary neg` in the next function is untouched — which is exactly what
a dropped kind constraint would break.

Both halves were falsified on the devbox rather than argued:

* **The test has teeth.** Weakening `isUnaryPlus` to
  `return mlir::success(unary != nullptr)` — the mutation a dropped attribute
  literal would produce — and relinking `ctjs-opt` makes `unary_minus` vanish
  from the output entirely (`grep -c unary_minus` → 0) and `FileCheck` exit 1.
* **The guard stops a real build, not just its own test.** Rewriting the
  pattern as
  `op<ctjs.unary>(operand: Value) {kind = attr<"#ctjs.unary_kind<plus>">}`
  and running `ninja CTNativeUnaryPlusPDLLIncGen` gives
  `FAILED: … UnaryPlusIsIdentity.h.inc` — with `mlir-pdll`'s own
  *"'none' attribute created with unregistered dialect"* above the guard's
  refusal, and the guard's message stating that the tool had exited 0.

(One process note, since it cost a suite run here: `tools/remote-build.sh`'s
rsync preserves the local mtime, so a source edited *on the box* and then
re-synced can be OLDER than the object built from the edit, and ninja skips the
rebuild. Four native-pipeline gates failed against a binary still carrying the
mutation. `touch` the file on the box after any hand edit there.)

### Why that arm and no other in `replace()`

Every other arm builds an EmitC operation over `f64`/`i1`, and that is only
correct **after** `retype()` has rewritten each value's type from the
`DataFlowSolver` lattice. Two things follow:

* **every argument of a native body comes from the match**, so nothing
  pass-local reaches one: not the `DataFlowSolver` lattice, not `shapes`,
  `accessKey`, `keyConstants` or `names`. A global or a `thread_local` is the
  only route, and it is worse than the switch it would replace; and
* after `retype()` a `ctjs.unary` has an `f64` operand where its ODS declares
  `CTJS_ValueType`, so **the function no longer verifies** and is no place to
  point a pattern driver at all.

`UnaryKind::Plus` is the exception because it is type-preserving at the `ctjs`
level — a `!ctjs.value` result replaced by a `!ctjs.value` operand — so it runs
*before* `retype()` on IR that still verifies, and needs no type from anywhere.
Its soundness comes from **where it runs**, not from what it matches: `+x` is
`ToNumber` in general, and the identity only on a proved number, which
`admission::op()` establishes before `lowering::lower()` is ever called. That is
the same guarantee the C++ arm had.

The driver is `applyOpPatternsGreedily` with the worklist seeded by name and
`GreedyRewriteStrictness::ExistingOps`, not `applyPatternsGreedily`: every
greedy entry point does dead-code elimination first, no option turns it off, and
this pass has `eraseIfUnused()` as a fatal invariant. And because PDL reports
nothing on a non-match, `replace()` keeps a `UnaryKind::Plus` arm that calls
`report_fatal_error` naming the `.pdll` — a pattern that silently stopped firing
would otherwise reach `default:` and blame admission.

### Precedence is expressible; the first-failing-thing diagnostic is not

The section above this one summarises `admission` as beyond PDLL because the
language has *"no `otherwise`, no way to order which predicate reports first,
and no way to reach the lattice the predicates read"*. **The middle clause is
too strong and is corrected here.** A pattern's benefit defaults to the number
of operations its match section names and can be set explicitly in the pattern's
metadata, so ordering BETWEEN patterns is expressible — "an if-chain encodes an
order that patterns lose" is only half true, and any design decision resting on
the stronger claim should be re-read.

What is genuinely not expressible is the **diagnostic**. A non-match is silent,
there is no `otherwise`, and a native constraint cannot report the failure
either: constraints run only after the structural predicates have matched, so
the case that matters — *this operation is not one we handle* — never reaches
C++. `admission`'s entire output is a refusal string naming the first thing that
failed, so it stays C++ for that reason and for the lattice, not for the
ordering. The same silence is why `replace()` keeps a `UnaryKind::Plus` arm that
`report_fatal_error`s: it is the only way a pattern that stopped firing says so.

### Is a forked `ctjs-pdll` worth it? No.

It would buy exactly one thing: `attr<"#ctjs.binary_kind<add>">` parsing, so a
kind test could be spelled in the pattern instead of in a five-line C++
function. Against that:

* **`mlir-pdll`'s `main` is not a library entry point.** `MLIRPDLLAST`,
  `MLIRPDLLCodeGen` and `MLIRPDLLODS` are exported, but the driver — the option
  parsing, the include search, `--dump-ods`, `--split-input-file`, the depfile,
  `--write-if-changed` — is `mlir-pdll.cpp` and is not installed. A fork is a
  copy of that file, re-vendored at every LLVM bump.
* **PDLL is already the most version-sensitive thing in the tree.**
  `docs/LLVMUpgrade.md` says every ODS, PDLL and pass-generation construct must
  be verified against the new release before it is relied on, and calls PDLL the
  youngest of them. A forked front end makes the project's own tool the thing
  that must be re-verified.
* **The native constraint is not a workaround; it is the safer spelling.** A
  typo in the enumerator is a compile error, a typo in the literal is silence.
  Anything a constraint wants that an attribute cannot say — a use count, a
  parent, a second operand's producer — is already available in it.
* **The remaining gap is unreachable anyway.** The arms that would use a kind
  literal are the ones that need the lattice, and a fork does not give a native
  function a capture.

The verdict holds unless upstream exposes the driver as a library, at which
point the trade is worth re-measuring — and the guard stays either way. Half of
what it refuses has nothing to do with our dialect being out of tree:
unregistered-operation support is a feature of the language, and registering
`ctjs` would not withdraw it.

## The ladder ahead

| phase | what | where |
|---|---|---|
| **0** | Inventories: bytecode, program representation, call paths, GC roots, HTML/CSS startup, and the performance baseline | the bytecode `.def` is the one that sizes the rest |
| 1–6 | Runtime preparation: the AOT ABI, mixed-mode dispatch, GC shadow frames, shared helpers | `ctbrowser/lib/AOT/` — runtime side, no MLIR yet |
| 7–9 | MLIR stood up, the CTJS dialect in ODS, the bytecode importer | register-machine SSA over frame slots, **never over a cell** |
| 10 | Generic CTJS runtime lowering | feeds both backends |
| **10A–10C** | **EmitC — the primary backend**: CTJS to EmitC, C++ emission, translation-unit partitioning over `function_proto`s | every corpus is one UMD bundle, so there is no module split to make |
| 11–12A | The LLVM dialect backend, second | kept as the S0 fallback and the differential oracle |
| 13–14 | Opcode coverage, then async and suspension | only the top frame can suspend today |
| 15–17A | Program metadata, modules, the HTML blueprint, the compiled style program, assets | hold 16B until the CSS engine stops moving |
| 18–20 | The generated launcher, strict AOT-only mode, the validation matrix | the launcher is a fixed library, not generated C++ |

Phases 21–43 are optimization and infrastructure, all optional, and none of them
blocks the core ladder.
