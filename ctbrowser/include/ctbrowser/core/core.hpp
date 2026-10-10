//===- core.hpp - Core subsystem aggregate -------------------- -*- C++ -*-===//
//
// The foundation every other subsystem sits on. Nothing here knows what a
// document, a style or a pixel is.
//
//   allocator  which allocator the binary links (mimalloc or the system's)
//   atom       interned strings, so name comparison is an integer compare
//   containers the third-party containers, aliased in one place
//   depth      depth_guard, the RAII counter for depth-limited recursion
//   geometry   points, rects, sides, colors
//   handle     generation-tagged references, so a stale reference fails a
//              lookup instead of resolving to a recycled object
//   scheduler  thread pool for raster tiles
//   slab       slot-stable storage behind those handles
//
// This header includes all of them. Include a single header directly when that
// is all you need; none depends on being included as a set. algorithms, json,
// number, number_format, symbol and uri are not included here.
//
//===----------------------------------------------------------------------===//

#pragma once

#include <ctbrowser/core/allocator.hpp>
#include <ctbrowser/core/atom.hpp>
#include <ctbrowser/core/containers.hpp>
#include <ctbrowser/core/depth.hpp>
#include <ctbrowser/core/geometry.hpp>
#include <ctbrowser/core/handle.hpp>
#include <ctbrowser/core/scheduler.hpp>
#include <ctbrowser/core/slab.hpp>
