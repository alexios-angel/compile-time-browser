#pragma once
// Private to lib/App/app/. NOT installed and in no file set:
// include/ctbrowser/app/app.hpp declares the application API whole, and
// this exists only so its implementation can be more than one file - it was
// 1,034 lines in one until 2026-09-08. The includes are app.cpp's, so every
// file here sees exactly what that one saw - which includes SDL, under the
// same guard; test/lint/api_surface lists these files for that reason.

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

// The window, the event loop and the only place SDL is read. See the note in
// app.hpp about why SDL's headers are not in the header.

namespace ctbrowser::detail {

using ctbrowser::shell::browser;
using ctbrowser::shell::input_event;

// What a host has to do. Two implementations - one that opens a window and one
// that does not - chosen at RUNTIME, which is why an application never has to
// care whether SDL3 was there when the engine was built.
class host {
public:
    virtual ~host() = default;
    [[nodiscard]] virtual bool start(const app_options &) = 0;
    // Drain input into the browser. False means the user asked to quit.
    [[nodiscard]] virtual bool pump(browser &, bool & changed) = 0;
    virtual void present(browser &) = 0;
    // Block until an event arrives or the timeout expires. The headless host
    // has no events, so it sleeps - which is the same thing from the loop's
    // point of view and keeps a headless run from spinning either.
    virtual void wait_for_event(std::int32_t timeout_ms) {
        std::this_thread::sleep_for(std::chrono::milliseconds{timeout_ms});
    }
    [[nodiscard]] virtual void * native_window() { return nullptr; }
};

// The host for this run: a window when SDL3 is there and one is wanted, the
// headless one otherwise. Defined in hosts.cpp.
[[nodiscard]] std::unique_ptr<host> make_host(const app_options & options);

} // namespace ctbrowser::detail
