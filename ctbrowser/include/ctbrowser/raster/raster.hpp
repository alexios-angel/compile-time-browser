#pragma once
// Raster and composition: display list in, pixels out.
//
//   surface     pixels as span + explicit stride (libstdc++ 13 has no mdspan)
//   tile        the unit of raster work, in CONTENT space so a scroll does not
//               invalidate it
//   draw        display list -> pixels
//   software    the tile store and compositor the browser owns; headless and
//               byte-for-byte reproducible
//   compositor  draw() for a frame
//   svg         vector graphics -> a bitmap AT THE SIZE ASKED FOR, through
//               plutosvg; optional, and the only third-party rasteriser the
//               engine calls that is not SDL

#include <ctbrowser/raster/backend/compositor.hpp>
#include <ctbrowser/raster/backend/software.hpp>
#include <ctbrowser/raster/draw.hpp>
#include <ctbrowser/raster/gl.hpp>
#include <ctbrowser/raster/surface.hpp>
#include <ctbrowser/raster/svg.hpp>
#include <ctbrowser/raster/text/ttf.hpp>
#include <ctbrowser/raster/tile.hpp>
