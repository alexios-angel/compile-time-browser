# Upgrading LLVM and MLIR

**Upgrading LLVM is its own change. Do not combine it with feature work.**

## The pin

`cmake/LLVMVersion.cmake`, at the monorepo root:

```cmake
set(CTCOMPILE_REQUIRED_LLVM_MAJOR 23)
set(CTCOMPILE_TESTED_LLVM_VERSION "23.1.0")
set(CTCOMPILE_MAX_LLVM_MAJOR 23)
```

`ct_require_llvm_version()` refuses to configure outside that range, with a
message naming both versions and the install it found. That is deliberate:
without it the failure lands inside `mlir-tblgen`, in a diagnostic that names a
TableGen template and never mentions a version.

## 22 → 23, 2026-09-02

The user's call: "the current stable version of LLVM is 23; bump the code and
use that." Done as its own commit, per the rule at the top. What 23 changed
for this tree is recorded in the commit that made the bump; the two mechanical
ones were `mlir::SideEffects::Resource::getName()` becoming `const` (every
escape-route resource in `CTJSEscapeEffects.h`) and the C++ emitter's
declare-variables-at-top check on `emitc.dereference`. On the box the formula
is `brew pin llvm`-pinned so a `brew bundle install` cannot float it again.

What the bump touched, for the next one:

* `usePropertiesForAttributes` is gone from `Dialect` (properties are
  unconditional) - removed from both dialect bases.
* `emitc.apply` is gone (`address_of` / `dereference`) - the fork's printer
  for it removed.
* `mlir::SideEffects::Resource::getName()` is `const` - the escape routes.
* Upstream 23's C++ emitter refuses `emitc.assign` through an
  `emitc.dereference` result under `--declare-variables-at-top`; the boxed
  lowering's one such store (`*out = value`) is now `out[0] = value` through
  `emitc.subscript`, which both emitters accept.
* `test/Target/Cpp/upstream/`: `lvalue`, `global`, `common-cpp` refreshed to
  23's files verbatim; `expressions.mlir` is 22's minus its
  two `emitc.apply` functions, with one expectation taken from 23:
  `parentheses_for_low_precedence` now returns through a temporary, because
  23's dialect stopped inlining a single-use expression into a `return` and
  the fork follows the dialect. 23's own file adds expression forms (a
  `subscript` and a `load` inside an expression, among others) the fork does
  not print yet - refreshing the fork to 23's emitter is that feature, tracked
  in part 22, not part of the bump. The other 31 files are still 22's and
  passed unchanged.

## Why 22 and not the 20 the plan names (history)

The master plan pins major 20. Neither the build box nor anything else here can
install 20: apt ships MLIR 18 for Ubuntu 24.04 and brew ships 22.1.8, and the
project's package policy has been brew-first since 2026-08-01. The pin follows
the machine.

The consequence is not free. Every ODS, PDLL and pass-generation construct the
plan spells was written against 20-era syntax, so **each one has to be verified
against 22 before it is relied on** rather than copied. PDLL is the youngest of
the three and moves fastest; treat its syntax as the most version-sensitive
thing in the project.

## What breaks between releases

In rough order of how often it has bitten LLVM downstreams:

- **Rewrite-pattern APIs** — `matchAndRewrite` signatures, `PatternRewriter`
  methods, and how a pattern reports failure.
- **Effect interfaces** — `MemoryEffectsOpInterface` spelling and the ODS
  helpers that attach it.
- **Property storage** — inherent attributes versus properties, and the
  generated accessors that follow from the choice.
- **`TypeDef` constraint behaviour** — parameter parsing, `assemblyFormat`
  defaults and custom directives.
- **Pass declarations** — `Pass`/`PassBase.td` option plumbing and the
  generated constructor names.

## The procedure

1. Install the new toolchain on the devbox (brew, per policy) and note the exact
   version.
2. Move all three variables in `cmake/LLVMVersion.cmake` in one commit, and
   nothing else in that commit.
3. Configure with `-DCTCOMPILE_ENABLE_MLIR=ON` and build. Read the TableGen
   diagnostics first: they name the construct that moved.
4. Regenerate the dialect and pass documentation and diff it. Generated docs are
   build artifacts (Principle 10) — a diff here is a real interface change, not
   noise.
5. Run `ctest` for both projects, and the differential backend comparison once
   Phase 37 exists: a silent codegen change between LLVM releases is exactly
   what that comparison is for.
6. Record what moved in this file, under a dated heading.

## History

Nothing yet — the pin has not moved since it was set on 2026-08-20 for
Phase -1, and MLIR is still behind `CTCOMPILE_ENABLE_MLIR=OFF`.
