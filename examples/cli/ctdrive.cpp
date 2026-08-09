// ctdrive - open an HTML file and take orders, live.
//
//   ctdrive page.html --port 7000                 a window, driven from outside
//   ctdrive page.html --port 7000 --size 800 600
//   SDL_VIDEODRIVER=offscreen ctdrive page.html --port 7000    the same, unseen
//
// `ctbrowse` opens a page and hands it to a user. This opens a page and hands
// it to a PROGRAM: one line of JSON per command over a loopback socket, one
// line of JSON back. It exists so ctbrowser can be driven through the same
// scripted interaction as a real browser and the two compared - see
// tools/check/compare.py, which drives this and Playwright side by side.
//
// The window is REAL by default rather than an afterthought. Watching the
// engine take a click at human speed beside Chrome doing the same is the point;
// SDL_VIDEODRIVER=offscreen is how you turn that off when nobody is looking.
//
// Commands run on the LOOP'S OWN THREAD, from `on_frame`. The alternative - a
// reader thread poking the browser while the main one ticks and draws it - is a
// data race, and this engine's suite runs under TSan. So the socket is polled,
// never blocked on: `io.poll()` runs whatever is ready and returns.

#include <ctbrowser.hpp>

// PLAIN SOCKETS. This used Asio, and the engine's dependency on it is gone -
// a tool in the same repository reintroducing it would put the header back on
// every build that compiles the examples. What a one-client line protocol
// needs is listen, accept, read, write, and a non-blocking flag.
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
// Header-only Boost.JSON: this include belongs in exactly ONE translation unit
// and brings the implementation with it. Compiled Boost is not available here -
// a cross build gets an isolated include directory and links Boost::headers and
// nothing else - so this is the only way to have a real parser.
#include <boost/json/src.hpp>

#include <cstdio>
#include <cstdlib>
#include <string>
#include <string_view>

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
using socklen_arg = int;
using iolen_t = int;
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
using socklen_arg = socklen_t;
using iolen_t = std::size_t;
struct winsock_once {};
#endif

// A character's DOM `code`, for the key event that accompanies typed text.
//
// SDL delivers a keystroke as TWO events - the key, then the text it produced -
// and the engine takes them the same way: a page listening for `keydown` hears
// the first and a field is filled by the second. Sending only one of them is
// the classic way a scripted keystroke fires every listener and inserts
// nothing, or fills a field without the page ever knowing.
[[nodiscard]] std::string code_for(char c) {
    if (c >= 'a' && c <= 'z') { return std::string{"Key"} + static_cast<char>(c - 'a' + 'A'); }
    if (c >= 'A' && c <= 'Z') { return std::string{"Key"} + c; }
    if (c >= '0' && c <= '9') { return std::string{"Digit"} + c; }
    if (c == ' ') { return "Space"; }
    if (c == '\n') { return "Enter"; }
    // Everything else is punctuation the engine has no code for. The text
    // event alone still types it, which is the honest half of the answer.
    return {};
}

// The commands, applied to the live page. One function so the vocabulary is in
// one place - tools/check/compare.py speaks exactly this and nothing else.
class session {
public:
    explicit session(unsigned short port) {
        listener_ = ::socket(AF_INET, SOCK_STREAM, 0);
        int reuse = 1;
        ::setsockopt(listener_, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char *>(&reuse),
                     sizeof(reuse));
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = ::htonl(INADDR_LOOPBACK);
        address.sin_port = ::htons(port);
        ::bind(listener_, reinterpret_cast<sockaddr *>(&address), sizeof(address));
        ::listen(listener_, 1);
        sockaddr_in bound{};
        auto length = static_cast<socklen_arg>(sizeof(bound));
        ::getsockname(listener_, reinterpret_cast<sockaddr *>(&bound), &length);
        port_ = ::ntohs(bound.sin_port);
        // NON-BLOCKING, which is the whole contract of poll() below: a page
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

    // Called every iteration from app_options::on_frame. Runs whatever the
    // socket has ready and returns - never blocks.
    void poll(ctbrowser::browser & page) {
        page_ = &page;
        if (peer_ == invalid_socket) {
            const socket_t incoming = ::accept(listener_, nullptr, nullptr);
            if (incoming != invalid_socket) {
                peer_ = incoming;
                set_non_blocking(peer_);
            }
        }
        if (peer_ != invalid_socket) { drain(); }
        page_ = nullptr;
    }

private:
    // READ WHAT IS THERE, then run every COMPLETE line. A partial line stays in
    // the buffer for the next frame - the client may write a command in more
    // than one packet, and treating a fragment as a command is how a driver
    // starts answering questions nobody asked.
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
        for (std::size_t at = pending_.find('\n'); at != std::string::npos;
             at = pending_.find('\n')) {
            const std::string line = pending_.substr(0, at + 1);
            pending_.erase(0, at + 1);
            reply(run(line));
        }
    }

    void reply(const json::value & answer) {
        if (peer_ == invalid_socket) { return; }
        const std::string text = json::serialize(answer) + "\n";
        // BLOCKING-ish write, deliberately: the reply is one short line and the
        // client is waiting on it. Sending is allowed to fail on a socket that
        // went away, which drops the client rather than the process.
        if (::send(peer_, text.data(), static_cast<iolen_t>(text.size()), 0) < 0) {
            close_socket(peer_);
            peer_ = invalid_socket;
            pending_.clear();
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
            // AT a point scrolls what is under it; with no point it is the
            // page's own scroll. Two different verbs, and the engine tells
            // them apart by whether a position came with the event.
            (void)page.handle(o.if_contains("x") != nullptr ? ev::wheel_at(x, y, notches)
                                                            : ev::wheel_by(notches));
            return ok();
        }
        if (cmd == "resize") {
            (void)page.handle(ev::resized(static_cast<int>(x), static_cast<int>(y)));
            return ok();
        }
        if (cmd == "eval") {
            // Whatever the script LOGGED comes back, because that is the only
            // way to get a value out: run_script answers whether it ran, not
            // what it produced. Playwright's evaluate() returns the expression
            // directly, so without this the two sides could not be asked the
            // same question - `console.log(document.activeElement.id)` is how
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
            const auto image = page.read_pixels();
            if (!image) { return fail("read_pixels failed"); }
            if (!ctbrowser::write_ppm(path, *image)) { return fail("cannot write " + path); }
            return json::value{{"ok", true},
                               {"path", path},
                               {"width", image->width()},
                               {"height", image->height()}};
        }
        if (cmd == "info") {
            return json::value{{"ok", true},
                               {"title", page.title()},
                               {"scroll_y", page.scroll_y()},
                               {"script_error", page.script_error()},
                               {"alerts", static_cast<std::int64_t>(page.alerts().size())}};
        }
        if (cmd == "quit") {
            std::exit(0); // the loop has no other way out from in here
        }
        return fail("unknown command: " + cmd);
    }

    [[maybe_unused]] winsock_once winsock_;
    socket_t listener_ = invalid_socket;
    socket_t peer_ = invalid_socket; // one client at a time, by design
    std::string pending_;            // bytes read but not yet a whole line
    unsigned short port_ = 0;
    ctbrowser::browser * page_ = nullptr;
};

} // namespace

int main(int argc, char ** argv) {
    std::string path;
    unsigned short port = 0;
    ctbrowser::app_options options;
    options.title = "ctdrive";
    // SOLID, not blinking. Two shots of the same page must not differ by
    // whether the caret happened to be in an on half-period, and the reference
    // browser is blinking to its own clock anyway - nothing is learned by
    // asking a reader to reconcile the two.
    options.caret_blink_ms = 0;

    for (int i = 1; i < argc; ++i) {
        const std::string_view arg{argv[i]};
        if (arg == "--size" && i + 2 < argc) {
            options.width = std::atoi(argv[++i]);
            options.height = std::atoi(argv[++i]);
        } else if (arg == "--port" && i + 1 < argc) {
            port = static_cast<unsigned short>(std::atoi(argv[++i]));
        } else if (arg == "--fixed-dt" && i + 1 < argc) {
            // For a replay that has to come out the same every time. A live
            // session wants real time and leaves this alone.
            options.fixed_dt = std::atof(argv[++i]);
        } else if (!arg.starts_with("--")) {
            path = arg;
        }
    }

    if (path.empty()) {
        std::printf("usage: ctdrive <page.html> --port N [--size W H] [--fixed-dt SECONDS]\n"
                    "\n"
                    "Speaks one JSON object per line on 127.0.0.1:N:\n"
                    "  {\"cmd\":\"click\",\"x\":120,\"y\":200}   move/down/up/click\n"
                    "  {\"cmd\":\"key\",\"code\":\"Tab\"}        + shift, ctrl\n"
                    "  {\"cmd\":\"type\",\"text\":\"hello\"}\n"
                    "  {\"cmd\":\"wheel\",\"notches\":-3}        + x,y to aim it\n"
                    "  {\"cmd\":\"shot\",\"path\":\"out.ppm\"}\n"
                    "  {\"cmd\":\"eval\",\"script\":\"...\"}     info, resize, quit\n");
        return 2;
    }

    std::optional<session> control;
    try {
        control.emplace(port);
    } catch (const std::exception & e) {
        std::printf("ctdrive: cannot listen on port %u: %s\n", port, e.what());
        return 1;
    }
    // The port, on stdout, before anything else: asking for 0 gets one the
    // operating system chose, and the client has no other way to learn it.
    std::printf("ctdrive: listening on 127.0.0.1:%u\n", control->port());
    std::fflush(stdout);

    options.on_ready = [](ctbrowser::browser & page) {
        if (!page.script_error().empty()) {
            std::printf("script error: %s\n", page.script_error().c_str());
            std::fflush(stdout);
        }
    };
    options.on_frame = [&control](ctbrowser::browser & page) {
        // NOT BEFORE THE FIRST FRAME. Nothing is laid out until one has run,
        // so a click arriving earlier is mapped through an empty box, lands at
        // offset 0, and types into the front of the value instead of where it
        // was aimed. It is a race the client cannot see and cannot avoid - it
        // only knows the socket is up - so it is refused here.
        if (page.frames() == 0) { return; }
        control->poll(page);
    };
    return ctbrowser::run_app_file(path, std::move(options));
}
