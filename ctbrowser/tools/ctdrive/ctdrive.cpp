//===- ctbrowser/tools/ctdrive/ctdrive.cpp - Drive a page over a socket ---===//
//
//   ctdrive page.html --port 7000                 a window, driven from outside
//   ctdrive page.html --port 7000 --size 800 600
//   SDL_VIDEODRIVER=offscreen ctdrive page.html --port 7000    the same, unseen
//
// `ctbrowse` opens a page and hands it to a user. This opens a page and hands
// it to a program: one line of JSON per command over a loopback socket, one
// line of JSON back. It exists so ctbrowser can be driven through the same
// scripted interaction as a real browser and the two compared; see
// tools/check/compare.py, which drives this and Playwright side by side.
//
// The window is real by default. Watching the engine take a click at human
// speed beside Chrome doing the same is the point; SDL_VIDEODRIVER=offscreen
// turns that off when nobody is looking.
//
// Commands run on the event loop's own thread, from `on_frame`. A reader
// thread poking the browser while the main one ticks and draws it would be a
// data race, and this engine's suite runs under TSan. So the socket is polled,
// never blocked on: session::poll() runs whatever is ready and returns.
//
//===----------------------------------------------------------------------===//

#include <ctbrowser.hpp>

#include "options.hpp"
#include "wpt_handlers.hpp"

// Plain sockets, not Asio: the engine does not depend on Asio, and a tool
// pulling it back in would add it to every build that compiles the examples.
// A one-client line protocol needs only listen, accept, read, write and a
// non-blocking flag.
#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif
// Header-only Boost.JSON: this include belongs in exactly one translation unit
// and brings the implementation with it. The cross sysroot's compiled Boost is
// Boost.URL and Boost.ProgramOptions only (tools/mingw/build-boost-mingw.sh), so
// this is how a real parser comes without building a third library for it.
#include <boost/json/src.hpp>

#include <algorithm>
#include <cerrno>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <random>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace {

namespace json = boost::json;
// One socket API, two spellings.
#if defined(_WIN32)
using socket_t = SOCKET;
inline constexpr socket_t invalid_socket = INVALID_SOCKET;
inline void close_socket(socket_t s) {
    ::closesocket(s);
}
inline void set_non_blocking(socket_t s) {
    u_long on = 1;
    // FIONBIO expands to an unsigned long while ioctlsocket takes a signed
    // one, so the cast is required rather than cosmetic under -Wconversion.
    ::ioctlsocket(s, static_cast<long>(FIONBIO), &on);
}
inline bool would_block() {
    return ::WSAGetLastError() == WSAEWOULDBLOCK;
}
using socklen_arg = int;
using iolen_t = int;
/// Binds exclusively, so no other process can bind the same port over this
/// one and take the commands (Windows lets SO_REUSEADDR steal a bound port).
inline bool claim_port_exclusively(socket_t s) {
    const BOOL on = TRUE;
    return ::setsockopt(s, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, reinterpret_cast<const char *>(&on),
                        sizeof(on)) == 0;
}
struct winsock_once {
    winsock_once() {
        WSADATA data;
        ::WSAStartup(MAKEWORD(2, 2), &data);
    }
};
#else
using socket_t = int;
inline constexpr socket_t invalid_socket = -1;
inline void close_socket(socket_t s) {
    ::close(s);
}
inline void set_non_blocking(socket_t s) {
    ::fcntl(s, F_SETFL, ::fcntl(s, F_GETFL, 0) | O_NONBLOCK);
}
inline bool would_block() {
    return errno == EWOULDBLOCK || errno == EAGAIN || errno == EINTR;
}
using socklen_arg = socklen_t;
using iolen_t = std::size_t;
// A bound port is already exclusive here without SO_REUSEADDR.
inline bool claim_port_exclusively(socket_t) {
    return true;
}
struct winsock_once {};
#endif

/// A character's DOM `code`, for the key event that accompanies typed text.
///
/// SDL delivers a keystroke as two events, the key and then the text it
/// produced, and the engine takes them the same way: a page listening for
/// `keydown` hears the first and a field is filled by the second. Sending only
/// one of them makes a scripted keystroke fire every listener and insert
/// nothing, or fill a field without the page ever knowing.
[[nodiscard]] std::string code_for(char c) {
    if (c >= 'a' && c <= 'z') { return std::string{"Key"} + static_cast<char>(c - 'a' + 'A'); }
    if (c >= 'A' && c <= 'Z') { return std::string{"Key"} + c; }
    if (c >= '0' && c <= '9') { return std::string{"Digit"} + c; }
    if (c == ' ') { return "Space"; }
    if (c == '\n') { return "Enter"; }
    // Everything else is punctuation the engine has no code for. The text
    // event alone still types it.
    return {};
}

/// The control socket and the commands it applies to the live page. The
/// vocabulary lives in one function, run(); tools/check/compare.py speaks
/// exactly this and nothing else.
///
/// A client must prove it started this process. Loopback is not private (every
/// local process, and a page's own fetch() to 127.0.0.1, can connect), `eval`
/// runs script and `shot` writes any path. So the first line of every
/// connection must be the token printed beside the port; anything else closes
/// it before a command is read. std::random_device is getrandom/urandom under
/// glibc and libc++ and rand_s (RtlGenRandom) under MinGW's libstdc++ and MSVC.
class session {
public:
    explicit session(unsigned short port) {
        std::random_device entropy;
        static constexpr char hex[] = "0123456789abcdef";
        for (int i = 0; i < 32; ++i) { token_ += hex[entropy() & 0x0F]; }
        const auto die = [](const char * what) {
            std::fprintf(stderr, "ctdrive: %s failed\n", what);
            std::exit(1);
        };
        listener_ = ::socket(AF_INET, SOCK_STREAM, 0);
        if (listener_ == invalid_socket) { die("socket"); }
        if (!claim_port_exclusively(listener_)) { die("setsockopt"); }
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = ::htonl(INADDR_LOOPBACK);
        address.sin_port = ::htons(port);
        if (::bind(listener_, reinterpret_cast<sockaddr *>(&address), sizeof(address)) != 0) {
            die("bind");
        }
        if (::listen(listener_, 1) != 0) { die("listen"); }
        sockaddr_in bound{};
        auto length = static_cast<socklen_arg>(sizeof(bound));
        if (::getsockname(listener_, reinterpret_cast<sockaddr *>(&bound), &length) != 0) {
            die("getsockname");
        }
        port_ = ::ntohs(bound.sin_port);
        // Non-blocking, which is the whole contract of poll() below: a page
        // with no command pending must run at full speed.
        set_non_blocking(listener_);
    }
    ~session() {
        if (peer_ != invalid_socket) { close_socket(peer_); }
        if (listener_ != invalid_socket) { close_socket(listener_); }
    }
    session(const session &) = delete;
    session & operator=(const session &) = delete;

    [[nodiscard]] unsigned short port() const { return port_; }
    [[nodiscard]] const std::string & token() const { return token_; }

    /// Called every iteration from app_options::on_frame. Runs whatever the
    /// socket has ready and returns; never blocks.
    void poll(ctbrowser::browser & page) {
        page_ = &page;
        if (peer_ == invalid_socket) {
            const socket_t incoming = ::accept(listener_, nullptr, nullptr);
            if (incoming != invalid_socket) {
                peer_ = incoming;
                authenticated_ = false;
                set_non_blocking(peer_);
            }
        }
        if (peer_ != invalid_socket) { drain(); }
        page_ = nullptr;
    }

private:
    /// Reads what is there, then runs every complete line. A partial line stays
    /// in the buffer for the next frame: the client may write a command in more
    /// than one packet, and a fragment must not be treated as a command.
    void drain() {
        char chunk[4096];
        for (;;) {
            const auto got = ::recv(peer_, chunk, static_cast<iolen_t>(sizeof(chunk)), 0);
            if (got > 0) {
                pending_.append(chunk, static_cast<std::size_t>(got));
                continue;
            }
            // Zero means the client closed; negative means nothing ready
            // (EWOULDBLOCK) or a real error, and both end this frame's read.
            if (got == 0) {
                close_socket(peer_);
                peer_ = invalid_socket;
                pending_.clear();
                return;
            }
            break;
        }
        // A peer that has not authenticated gets no more buffer than a token.
        if (!authenticated_ && pending_.size() > 2 * token_.size() + 2 &&
            pending_.find('\n') == std::string::npos) {
            close_socket(peer_);
            peer_ = invalid_socket;
            pending_.clear();
            return;
        }
        for (std::size_t at = pending_.find('\n'); at != std::string::npos;
             at = pending_.find('\n')) {
            const std::string line = pending_.substr(0, at + 1);
            pending_.erase(0, at + 1);
            if (!authenticated_) {
                std::string_view given{line};
                given.remove_suffix(1);
                if (given.ends_with('\r')) { given.remove_suffix(1); }
                if (given != token_) {
                    close_socket(peer_);
                    peer_ = invalid_socket;
                    pending_.clear();
                    return;
                }
                authenticated_ = true;
                continue;
            }
            reply(run(line));
        }
    }

    void reply(const json::value & answer) {
        if (peer_ == invalid_socket) { return; }
        const std::string text = json::serialize(answer) + "\n";
        // Write every byte. The peer socket is non-blocking, so send() may
        // accept only a prefix; anything not looped over is lost and the
        // client, which reads until '\n', blocks or parses half a JSON object.
        // It bites when the client is slow to read (a 4 MB reply came back as
        // 2,588,672 bytes), not merely when the reply is large;
        // tools/check/ctdrive-reply.py reproduces both cases.
        std::size_t sent = 0;
        while (sent < text.size()) {
            const auto took =
                ::send(peer_, text.data() + sent, static_cast<iolen_t>(text.size() - sent), 0);
            if (took > 0) {
                sent += static_cast<std::size_t>(took);
                continue;
            }
            // A full send buffer is not an error: the client is reading, it
            // just has not caught up. Spin rather than drop: the client is
            // synchronously waiting on this exact reply, so there is nothing
            // else for this thread to usefully do and no deadlock to reach.
            if (took < 0 && would_block()) { continue; }
            close_socket(peer_);
            peer_ = invalid_socket;
            pending_.clear();
            return;
        }
    }

    [[nodiscard]] static json::value ok() {
        return json::value{{"ok", true}};
    }
    [[nodiscard]] static json::value fail(std::string_view why) {
        return json::value{{"ok", false}, {"error", std::string{why}}};
    }

    [[nodiscard]] static float number(const json::object & o, std::string_view key,
                                      float fallback = 0) {
        const json::value * v = o.if_contains(key);
        return v != nullptr && v->is_number() ? static_cast<float>(v->to_number<double>())
                                              : fallback;
    }
    [[nodiscard]] static bool flag(const json::object & o, std::string_view key) {
        const json::value * v = o.if_contains(key);
        return v != nullptr && v->is_bool() && v->as_bool();
    }
    [[nodiscard]] static std::string text_of(const json::object & o, std::string_view key) {
        const json::value * v = o.if_contains(key);
        return v != nullptr && v->is_string() ? std::string{v->as_string()} : std::string{};
    }

    /// Parses one command line and applies it to the page; returns the reply.
    [[nodiscard]] json::value run(const std::string & line) {
        if (page_ == nullptr) { return fail("no page"); }
        ctbrowser::browser & page = *page_;

        boost::system::error_code ec;
        const json::value parsed = json::parse(line, ec);
        if (ec || !parsed.is_object()) { return fail("not a JSON object"); }
        const json::object & o = parsed.as_object();
        const std::string cmd = text_of(o, "cmd");

        using ev = ctbrowser::input_event;
        const float x = number(o, "x");
        const float y = number(o, "y");
        const auto button = static_cast<std::uint8_t>(number(o, "button"));

        if (cmd == "move") {
            (void)page.handle(ev::mouse_move_to(x, y));
            return ok();
        }
        if (cmd == "down") {
            (void)page.handle(ev::mouse_down_at(x, y, button));
            return ok();
        }
        if (cmd == "up") {
            (void)page.handle(ev::mouse_up_at(x, y, button));
            return ok();
        }
        if (cmd == "click") {
            // A move first, so hover state and anything tracking the pointer
            // sees the press arrive where a real one would have come from.
            (void)page.handle(ev::mouse_move_to(x, y));
            (void)page.handle(ev::mouse_down_at(x, y, button));
            (void)page.handle(ev::mouse_up_at(x, y, button));
            return ok();
        }
        if (cmd == "key") {
            const std::string code = text_of(o, "code");
            if (code.empty()) { return fail("key needs a code"); }
            const bool shift = flag(o, "shift");
            const bool ctrl = flag(o, "ctrl");
            const bool handled = page.handle(ev::key_press(code, shift, ctrl));
            (void)page.handle(ev::key_release(code, shift, ctrl));
            return json::value{{"ok", true}, {"handled", handled}};
        }
        // A key held across other input: an action sequence's keyDown, then
        // its keyUp ticks later (tools/wpt/testdriver-vendor.js).
        if (cmd == "keydown" || cmd == "keyup") {
            const std::string code = text_of(o, "code");
            if (code.empty()) { return fail(cmd + " needs a code"); }
            const bool shift = flag(o, "shift");
            const bool ctrl = flag(o, "ctrl");
            const bool handled = page.handle(cmd == "keydown" ? ev::key_press(code, shift, ctrl)
                                                              : ev::key_release(code, shift, ctrl));
            return json::value{{"ok", true}, {"handled", handled}};
        }
        if (cmd == "type") {
            const std::string text = text_of(o, "text");
            for (const char c : text) {
                if (const std::string code = code_for(c); !code.empty()) {
                    (void)page.handle(ev::key_press(code));
                    (void)page.handle(ev::key_release(code));
                }
                (void)page.handle(ev::typed(std::string{c}));
            }
            return ok();
        }
        if (cmd == "wheel") {
            const float notches = number(o, "notches");
            // At a point it scrolls what is under it; with no point it is the
            // page's own scroll. Two different verbs, and the engine tells
            // them apart by whether a position came with the event.
            ev wheel =
                o.if_contains("x") != nullptr ? ev::wheel_at(x, y, notches) : ev::wheel_by(notches);
            // Sideways notches, positive to the right.
            wheel.wheel_x = number(o, "notches_x");
            (void)page.handle(wheel);
            return ok();
        }
        // A finger: {"cmd":"touch","phase":"start"|"move"|"end","x":..,"y":..}.
        if (cmd == "touch") {
            const std::string phase = text_of(o, "phase");
            if (phase != "start" && phase != "move" && phase != "end") {
                return fail("touch needs a phase of start, move or end");
            }
            (void)page.touch(phase, x, y);
            return ok();
        }
        // The accessibility tree's answer for testdriver's get_computed_role and
        // get_computed_label: {"cmd":"accessibility","what":"role"|"label",
        // "slot":N}. The element is what `__ctdrive_asked(N)` returns in the
        // page, and the answer goes back through `__ctdrive_answer(N, text)`;
        // a reply here only says whether the step ran, and the promise the
        // test awaits is settled by the page (tools/wpt/testdriver-vendor.js).
        if (cmd == "accessibility") {
            const std::string what = text_of(o, "what");
            const double slot = static_cast<double>(number(o, "slot"));
            const std::optional<std::string> answer =
                page.bindings().computed_accessibility("__ctdrive_asked", slot, what == "label");
            if (!answer) { return fail("accessibility: no element in slot"); }
            const bool ran = page.run_script("window.__ctdrive_answer(" +
                                             std::to_string(static_cast<long long>(slot)) + ", " +
                                             json::serialize(json::string{*answer}) + ")");
            return ran ? ok() : fail(page.script_error());
        }
        if (cmd == "resize") {
            (void)page.handle(ev::resized(static_cast<int>(x), static_cast<int>(y)));
            return ok();
        }
        if (cmd == "eval") {
            // Whatever the script logged comes back, because that is the only
            // way to get a value out: run_script answers whether it ran, not
            // what it produced. Playwright's evaluate() returns the expression
            // directly, so without this the two sides could not be asked the
            // same question; `console.log(document.activeElement.id)` is how
            // you compare focus between engines.
            const std::size_t before = page.bindings().console_output().size();
            const bool ran = page.run_script(text_of(o, "script"));
            json::array logged;
            const auto & console = page.bindings().console_output();
            for (std::size_t i = before; i < console.size(); ++i) {
                logged.push_back(json::string{console[i]});
            }
            return json::value{
                {"ok", ran}, {"error", page.script_error()}, {"console", std::move(logged)}};
        }
        if (cmd == "shot") {
            const std::string path = text_of(o, "path");
            if (path.empty()) { return fail("shot needs a path"); }
            const ctbrowser::raster::surface & image = page.read_pixels();
            // PNG when the name says so (what a browser or an AI can look at),
            // and PPM otherwise (the shape a byte-compared golden keeps). The
            // encoder is the engine's own, the one toDataURL uses.
            bool written = false;
            if (std::filesystem::path{path}.extension() == ".png") {
                ctbrowser::paint::bitmap bits{image.width(), image.height()};
                std::ranges::copy(image.pixels(), bits.pixels.begin());
                const std::vector<std::byte> png = ctbrowser::shell::encode_png(bits);
                std::ofstream out{path, std::ios::binary};
                out.write(reinterpret_cast<const char *>(png.data()),
                          static_cast<std::streamsize>(png.size()));
                written = !png.empty() && out.good();
            } else {
                written = ctbrowser::write_ppm(path, image);
            }
            if (!written) { return fail("cannot write " + path); }
            return json::value{
                {"ok", true}, {"path", path}, {"width", image.width()}, {"height", image.height()}};
        }
        if (cmd == "info") {
            return json::value{{"ok", true},
                               {"title", page.title()},
                               {"scroll_y", page.scroll_y()},
                               {"script_error", page.script_error()},
                               // A <link rel=stylesheet> that did not resolve. Reported here so
                               // tools/check/css-parity.py can call a bad href what it is, a
                               // harness failure, instead of reporting the thousands of property
                               // differences that an accidentally unstyled page produces.
                               {"style_error", page.style_error()},
                               {"alerts", static_cast<std::int64_t>(page.alerts().size())}};
        }
        if (cmd == "quit") {
            std::exit(0); // The loop has no other way out from in here.
        }
        return fail("unknown command: " + cmd);
    }

    [[maybe_unused]] winsock_once winsock_;
    socket_t listener_ = invalid_socket;
    socket_t peer_ = invalid_socket; ///< One client at a time, by design.
    std::string pending_;            ///< Bytes read but not yet a whole line.
    std::string token_;              ///< The first line a connection must send.
    bool authenticated_ = false;     ///< The peer has sent the token.
    unsigned short port_ = 0;
    ctbrowser::browser * page_ = nullptr;
};

} // namespace

int main(int argc, char ** argv) {
    namespace po = boost::program_options;
    std::string path;
    std::string query; // `?file=x` or `#frag`, appended to the page's address.
    unsigned short port = 0;
    std::vector<int> size; // --size W H
    ctbrowser::app_options options;
    options.title = "ctdrive";
    // A solid caret, not a blinking one. Two shots of the same page must not
    // differ by whether the caret happened to be in an on half-period, and the
    // reference browser blinks to its own clock anyway.
    options.caret_blink_ms = 0;
    // A command is not an event, so it does not wake an idle loop. The loop
    // polls the socket once per turn, and with the default quarter-second idle
    // wait each testdriver round trip cost about a second; a test that presses
    // Tab twenty times (shadow-dom/focus-navigation/focus-navigation-slots.html)
    // then timed out in the driver, not the engine. 2 ms keeps polls prompt.
    options.idle_wait_ms = 2;

    po::options_description cli{
        "usage: ctdrive <page.html> --port N [--size W H] [--fixed-dt SECONDS] "
        "[--query ?x=y]\n"
        "\n"
        "Prints `listening on 127.0.0.1:N token T`; a client sends T\n"
        "as its first line, then one JSON object per line:\n"
        "  {\"cmd\":\"click\",\"x\":120,\"y\":200}   move/down/up/click\n"
        "  {\"cmd\":\"key\",\"code\":\"Tab\"}        + shift, ctrl\n"
        "  {\"cmd\":\"type\",\"text\":\"hello\"}\n"
        "  {\"cmd\":\"wheel\",\"notches\":-3}        + x,y to aim it, notches_x\n"
        "  {\"cmd\":\"touch\",\"phase\":\"start\"}   + x,y; move, end\n"
        "  {\"cmd\":\"shot\",\"path\":\"out.ppm\"}      or out.png\n"
        "  {\"cmd\":\"eval\",\"script\":\"...\"}     info, resize, quit"};
    auto add = cli.add_options();
    add("page", po::value(&path)->required(), "the HTML file to open");
    add("port", po::value(&port)->default_value(port),
        "the port to listen on, on 127.0.0.1; 0 lets the OS choose");
    add("size", new ctbrowser_cli::exactly<int>(&size, 2), "the window's width and height");
    // For a replay that has to come out the same every time. A live
    // session wants real time and leaves this alone.
    add("fixed-dt", po::value(&options.fixed_dt), "pretend every frame took exactly SECONDS");
    // The query string that selects a variant: WPT runs `html5lib_write.html?
    // file=adoption01` as one test per `<meta name=variant>`, and the page
    // reads which through `location.search`. A file has no query, so the
    // address gets one here.
    add("query", po::value(&query), "a query string or fragment for the page's address");
    po::positional_options_description positional;
    positional.add("page", 1);
    if (!ctbrowser_cli::parse_options(argc, argv, cli, positional)) { return 2; }
    if (!size.empty()) {
        options.width = size[0];
        options.height = size[1];
    }

    session control{port};
    // The port, on stdout, before anything else: asking for 0 gets one the
    // operating system chose, and the client has no other way to learn it.
    std::printf("ctdrive: listening on 127.0.0.1:%u token %s\n", control.port(),
                control.token().c_str());
    std::fflush(stdout);

    // The page's address is the file's. `run_app_file` loads the page before
    // `on_ready` hands it over, which is too late to give it a URL: its inline
    // scripts have run and every `a.href` they read resolved against nothing.
    // So the loop starts on an empty document and the real one is loaded from
    // on_ready once its `file://` location is set, the same reload-from-a-hook
    // `ctbrowse` uses to follow a link.
    std::ifstream in{path, std::ios::binary};
    if (!in) {
        std::printf("ctdrive: cannot read %s\n", path.c_str());
        return 1;
    }
    std::ostringstream source;
    source << in.rdbuf();
    const std::filesystem::path file{path};
    const std::string absolute =
        std::filesystem::absolute(file).lexically_normal().generic_string();
    const std::string url = (absolute.starts_with('/') ? "file://" : "file:///") + absolute + query;
    const auto kind = ctbrowser::is_xml_extension(file.filename().string())
                          ? ctbrowser::browser::source_kind::xml
                          : ctbrowser::browser::source_kind::html;
    options.asset_path = file.parent_path();
    // Assets are unconfined: this is a developer's driver over in-tree pages,
    // and the corpus reaches `../../vendor/` above its directory.
    // CTBROWSER_CONFINE_ASSETS=1 (read by apply_environment below) confines
    // them again.
    options.confine_assets = false;
    // A document root means a WPT run (tools/wpt/run-wpt.py sets it), and a WPT
    // page talks to wptserve's Python handlers as well as to its files: the
    // ones the corpus's form and frame tests reach are emulated in
    // wpt_handlers.cpp and answer before the registry does.
    ctbrowser::apply_environment(options);
    if (!options.document_root.empty()) {
        options.on_request = ctdrive::wpt_handlers(
            std::filesystem::absolute(options.document_root).lexically_normal().generic_string());
    }
    options.on_ready = [&](ctbrowser::browser & page) {
        page.set_location(url);
        page.load_document(source.str(), kind);
        if (!page.script_error().empty()) {
            std::printf("script error: %s\n", page.script_error().c_str());
            std::fflush(stdout);
        }
        // Report it, because otherwise the failure is silent: a page whose
        // stylesheet did not resolve lays out as if the author wrote none,
        // which looks like a broken engine rather than a broken path.
        if (!page.style_error().empty()) {
            std::printf("style error: %s\n", page.style_error().c_str());
            std::fflush(stdout);
        }
    };
    options.on_frame = [&control](ctbrowser::browser & page) {
        // Not before the first frame. Nothing is laid out until one has run,
        // so a click arriving earlier is mapped through an empty box, lands at
        // offset 0, and types into the front of the value instead of where it
        // was aimed. The client cannot see or avoid this race (it only knows
        // the socket is up), so commands wait here.
        if (page.frames() == 0) { return; }
        control.poll(page);
    };
    return ctbrowser::run_app("", std::move(options));
}
