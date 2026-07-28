module;
#include <ctbrowser/core/core.hpp>
#include <ctbrowser/dom/dom.hpp>
#include <ctbrowser/layout/layout.hpp>
#include <ctbrowser/paint/paint.hpp>
#include <ctbrowser/raster/raster.hpp>
#include <ctbrowser/script/script.hpp>
#include <ctbrowser/style/style.hpp>

export module ctbrowser;

// The whole engine, in one import.
//
// `import ctbrowser;` and link `ctbrowser::ctbrowser` is everything an
// application needs. The subsystem modules stay importable for anyone who wants
// to reach past the shell, but nobody should have to name five of them and five
// link targets to open one page - which is what the reference consumer did
// before this existed.
//
// core, raster and dom are re-exported because they are NOT optional: the
// browser's own signatures mention `scheduler`, `surface`, `rect` and `node_id`,
// so a consumer needs them whether they asked for them or not.

export import ctbrowser.shell;
export import ctbrowser.app;

export namespace ctbrowser {

// The names an application actually uses, unqualified. `shell` and `raster`
// remain reachable for everything else.
using browser = shell::browser;
using browser_options = shell::browser_options;
using input_event = shell::input_event;
using input_kind = shell::input_kind;
using surface = raster::surface;

} // namespace ctbrowser
