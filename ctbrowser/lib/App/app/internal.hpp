//===- ctbrowser/lib/App/app/internal.hpp - App internals -------*- C++ -*-===//
//
// Private to lib/App/app/: not installed and in no file set.
// include/ctbrowser/app/app.hpp declares the whole application API; this
// header gives its implementation files (run.cpp, hosts.cpp) one shared set of
// includes, SDL among them under the CTBROWSER_WITH_SDL3 guard, and the `host`
// interface they share. test/lint/api_surface.cpp lists these files for that
// reason: they are the only engine sources allowed to include SDL.
//
//===----------------------------------------------------------------------===//

#pragma once

#if CTBROWSER_WITH_SDL3
#include <SDL3/SDL.h>
#if CTBROWSER_WITH_IMAGE
#include <SDL3_image/SDL_image.h>
#endif
#endif

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include <ctbrowser/app/app.hpp>

#include <ctbrowser/core/core.hpp>
#include <ctbrowser/paint/paint.hpp>
#include <ctbrowser/raster/raster.hpp>
#include <ctbrowser/script/script.hpp>
#include <ctbrowser/shell/app_bundle.hpp>
#include <ctbrowser/shell/shell.hpp>

namespace ctbrowser::detail {

using ctbrowser::shell::browser;
using ctbrowser::shell::input_event;

/// What a host has to do. There are two implementations, one that opens a
/// window and one that does not, chosen at run time, which is why an
/// application never has to care whether SDL3 was there when the engine was
/// built.
class host {
public:
    virtual ~host() = default;
    /// Opens the window (if any). False means the host could not start.
    [[nodiscard]] virtual bool start(const app_options &) = 0;
    /// Drains input into the browser and sets `changed` when it handled any.
    /// False means the user asked to quit.
    [[nodiscard]] virtual bool pump(browser &, bool & changed) = 0;
    /// Shows the browser's latest frame.
    virtual void present(browser &) = 0;
    /// Blocks until an event arrives or the timeout expires. The headless host
    /// has no events, so it sleeps, which is the same thing from the loop's
    /// point of view and keeps a headless run from spinning.
    virtual void wait_for_event(std::int32_t timeout_ms) {
        std::this_thread::sleep_for(std::chrono::milliseconds{timeout_ms});
    }
    /// The platform window handle (an SDL_Window*), or null when headless.
    [[nodiscard]] virtual void * native_window() { return nullptr; }
};

/// The host for this run: a window when SDL3 is there and one is wanted, the
/// headless one otherwise. Defined in hosts.cpp.
[[nodiscard]] std::unique_ptr<host> make_host(const app_options & options);

} // namespace ctbrowser::detail
