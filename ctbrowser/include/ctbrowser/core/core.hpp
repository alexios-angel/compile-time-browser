#pragma once

// The foundation every other subsystem sits on. Nothing here knows what a
// document, a style or a pixel is.
//
//   containers the third-party containers, aliased in one place
//   handle     generation-tagged references, so a stale reference FAILS a
//              lookup instead of resolving to a recycled object
//   slab       slot-stable storage behind those handles
//   atom       interned strings, so name comparison is an integer compare
//   scheduler  thread pool for raster tiles
//   geometry   points, rects, sides, colors
//
// This header includes all of them, so `#include <ctbrowser/core/core.hpp>` is
// the one line a subsystem above needs. Include a single header directly when
// that is all you want - nothing here depends on being included as a set.

#include <ctbrowser/core/allocator.hpp>
#include <ctbrowser/core/atom.hpp>
#include <ctbrowser/core/containers.hpp>
#include <ctbrowser/core/geometry.hpp>
#include <ctbrowser/core/handle.hpp>
#include <ctbrowser/core/scheduler.hpp>
#include <ctbrowser/core/slab.hpp>
