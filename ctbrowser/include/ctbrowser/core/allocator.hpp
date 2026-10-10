//===- allocator.hpp - Which allocator is linked -------------- -*- C++ -*-===//
//
// Reports which allocator this binary actually uses.
//
// The engine routes `operator new`/`delete` through mimalloc
// (lib/Core/allocator.cpp), because allocator traffic is ~4.8% of a Phaser
// frame (docs/performance.md).
//
// Referencing these functions also matters. A global operator new in a static
// archive is pulled in only to satisfy an undefined symbol; if the link order
// lets libstdc++ satisfy it first, the override is silently dropped and the
// binary uses the system allocator. Anything that must have mimalloc references
// this, and unittests/unit/core/basics asserts the answer, so a missing link
// fails a test.
//
//===----------------------------------------------------------------------===//

#pragma once

namespace ctbrowser {

/// "mimalloc" or "system". Determined by asking mimalloc whether a fresh
/// allocation came from its own regions, not by reading a build flag.
[[nodiscard]] const char * allocator_name() noexcept;

/// mimalloc's version as it reports it (3.4.3 is 343).
/// unittests/unit/core/basics checks it against the major version this tree
/// pins: v2 and v3 are different allocators with the same header name, and a
/// machine with both installed can compile against one and link the other,
/// which is undefined behaviour. The Linux copy comes from brew and the Windows
/// one from tools/mingw/build-libs-mingw.sh.
[[nodiscard]] int allocator_version() noexcept;

} // namespace ctbrowser
