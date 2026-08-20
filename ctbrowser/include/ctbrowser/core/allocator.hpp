#pragma once

// Which allocator this binary actually uses.
//
// The engine routes `operator new`/`delete` through mimalloc
// (lib/Core/allocator.cpp), because allocator traffic is ~4.8% of a Phaser
// frame and a drop-in allocator is the one library that answers a hot path
// outright - see docs/performance.md.
//
// THIS DECLARATION IS LOAD-BEARING, and not for its answer. A global operator
// new living in a static archive is pulled in only to satisfy an undefined
// symbol; if the link order lets libstdc++ answer first, the override is
// silently dropped and the binary uses the system allocator while looking
// identical. Anything that must have mimalloc references this, and
// unittests/unit/core_basics asserts the answer - so "we forgot to link it" is a test
// failure rather than a performance mystery six months later.
namespace ctbrowser {

// "mimalloc" or "system". Determined by ASKING mimalloc whether a fresh
// allocation came out of its own regions, not by reading a build flag.
[[nodiscard]] const char * allocator_name() noexcept;

// mimalloc's own version, as it reports it - 3.4.3 is 343. Checked by
// unittests/unit/core_basics against the major version this tree pins.
//
// IT IS A PIN, NOT TRIVIA. The Linux side comes from brew and the Windows side
// from tools/mingw/build-mimalloc-mingw.sh, and v2 and v3 are different allocators
// with the same header name - a box with both installed can compile against one
// and link the other, which is undefined behaviour that starts as a
// hard-to-place crash. Asking the library what it is turns that into a test
// failure.
[[nodiscard]] int allocator_version() noexcept;

} // namespace ctbrowser
