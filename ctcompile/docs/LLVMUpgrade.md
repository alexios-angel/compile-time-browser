# Upgrading LLVM and MLIR

**Upgrading LLVM is its own change. Do not combine it with feature work.**

## The pin

`cmake/LLVMVersion.cmake`, at the monorepo root:

```cmake
set(CTCOMPILE_REQUIRED_LLVM_MAJOR 22)
set(CTCOMPILE_TESTED_LLVM_VERSION "22.1.8")
set(CTCOMPILE_MAX_LLVM_MAJOR 22)
```

`ct_require_llvm_version()` refuses to configure outside that range, with a
message naming both versions and the install it found. That is deliberate:
without it the failure lands inside `mlir-tblgen`, in a diagnostic that names a
TableGen template and never mentions a version.

## Why 22 and not the 20 the plan names

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
