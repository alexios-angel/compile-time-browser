//===- ctbrowser.hpp - The engine in one include -------------- -*- C++ -*-===//
//
// The whole engine, in one include.
//
// `#include <ctbrowser.hpp>` and linking `ctbrowser::ctbrowser` is everything
// an application needs. The subsystem headers stay includable for code that
// reaches past the shell.
//
// core, raster and dom are always included because the browser's own signatures
// mention `scheduler`, `surface`, `rect` and `node_id`.
//
//===----------------------------------------------------------------------===//

#pragma once

#include <ctbrowser/app/app.hpp>
#include <ctbrowser/core/core.hpp>
#include <ctbrowser/dom/dom.hpp>
#include <ctbrowser/layout/layout.hpp>
#include <ctbrowser/paint/paint.hpp>
#include <ctbrowser/raster/raster.hpp>
#include <ctbrowser/script/script.hpp>
#include <ctbrowser/shell/shell.hpp>
#include <ctbrowser/style/style.hpp>

namespace ctbrowser {

// The names an application actually uses, unqualified. `shell` and `raster`
// remain reachable for everything else.
using browser = shell::browser;
using browser_options = shell::browser_options;
using input_event = shell::input_event;
using input_kind = shell::input_kind;
using surface = raster::surface;

} // namespace ctbrowser
