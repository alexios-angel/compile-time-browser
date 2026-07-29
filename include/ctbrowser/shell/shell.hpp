#pragma once
// The shell: the engine assembled into something that loads a page.
//
//   input    events described without SDL, so the whole browser is drivable
//            headlessly
//   assets   name -> bytes, consulted before the filesystem
//   images   BMP decoding into the display list's bitmap, plus a hook the app
//            layer fills in from SDL3_image
//   net      HTTP over header-only Boost.Asio, which is what backs fetch()
//   forms    form-control state, kept off the DOM node on purpose
//   canvas   the 2D drawing context and where its pixels live
//   bindings the web platform, bound to the VM - script holds HANDLES, and
//            a mutation is a callback the browser interprets
//   browser  the assembly - parse, style, layout, paint, raster, composite -
//            and, more importantly, what each kind of change lets a frame SKIP
//
// The SDL3 window and event loop are in ctbrowser.app, which is optional: the
// browser here needs no display, which is what lets tests render whole pages
// and compare them byte for byte.

#include <ctbrowser/shell/assets.hpp>
#include <ctbrowser/shell/bindings.hpp>
#include <ctbrowser/shell/browser.hpp>
#include <ctbrowser/shell/canvas.hpp>
#include <ctbrowser/shell/embedded_fonts.hpp>
#include <ctbrowser/shell/forms.hpp>
#include <ctbrowser/shell/images.hpp>
#include <ctbrowser/shell/input.hpp>
#include <ctbrowser/shell/metrics.hpp>
#include <ctbrowser/shell/net.hpp>
