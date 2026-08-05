#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <ctbrowser/core/core.hpp>
#include <ctbrowser/dom/dom.hpp>
#include <ctbrowser/layout/layout.hpp>
#include <ctbrowser/paint/paint.hpp>
#include <ctbrowser/raster/raster.hpp>
#include <ctbrowser/script/script.hpp>

#include <ctbrowser/core/algorithms.hpp>
#include <ctbrowser/shell/assets.hpp>
#include <ctbrowser/shell/canvas.hpp>
#include <ctbrowser/shell/forms.hpp>
#include <ctbrowser/shell/images.hpp>
#include <ctbrowser/shell/input.hpp>
#include <ctbrowser/shell/net.hpp>
#include <ctbrowser/shell/webgl.hpp>

// The web platform, bound to the the engine VM.
//
// the previous engine had ~179 hand-written set_method calls against a tree-walking interpreter
// that held raw `node *`. Two things changed and both matter:
//
//   * SCRIPT HOLDS HANDLES, NOT POINTERS. An element wrapper carries a
//     node_id, and every native resolves it against the live document. A stale
//     reference is a failed lookup that returns undefined, not a use-after-free.
//     the previous engine was only safe because the document owned every node forever and
//     nothing was concurrent.
//   * MUTATION IS A CALLBACK. A native that changes the document calls
//     on_mutation, and the browser decides what that invalidates. Bindings do
//     not know about layout, and layout does not know script exists.
//
// The surface is deliberately the part of the previous engine's that real pages use, and the
// gaps are named rather than stubbed: a `getContext` that returns an object
// with no drawing on it is worse than one that is absent, because a page
// checking for canvas support gets the wrong answer.

namespace ctbrowser::shell {

using ctbrowser::script::context;
using ctbrowser::script::value;

// Argument coercion. the previous engine open-coded these at 150 call sites; the plan's answer
// was an IDL generator, and this is the part of it that is actually load
// bearing - the codegen would emit exactly these.
[[nodiscard]] inline std::string arg_string(context & cx, std::span<value> args, std::size_t i) {
    return i < args.size() ? cx.to_string(args[i]) : std::string{};
}
[[nodiscard]] inline double arg_number(std::span<value> args, std::size_t i) {
    return i < args.size() ? context::to_number(args[i]) : 0.0;
}
[[nodiscard]] inline value arg(std::span<value> args, std::size_t i) {
    return i < args.size() ? args[i] : value::undefined();
}

// Where an element wrapper keeps its handle. A property rather than a side
// table, so a wrapper is self-describing and two wrappers for the same element
// resolve to the same node.
inline constexpr std::string_view handle_property = "__node";

// Where a Path2D keeps the verbs it recorded. A Path2D is a RECORDING, not a
// drawing: it is built once and replayed into a canvas by fill(path) or
// stroke(path), possibly under a different transform than the one in force
// when it was built. Keeping the verbs in an ordinary script array means the
// GC traces them with no new heap kind, and a page can be shown what it built.
inline constexpr std::string_view path_commands_property = "__cmds";

class dom_bindings {
public:
    // `on_mutation` is how the browser learns it has to re-run the pipeline.
    // Taking it as a callback rather than a browser reference keeps this
    // testable on its own and keeps the dependency pointing one way.
    dom_bindings(document & doc, atom_table & atoms, canvas_store & canvases, form_store & forms,
                 std::function<void()> on_mutation, std::function<void(node_id)> on_focus)
        : doc_(&doc), atoms_(&atoms), canvases_(&canvases), forms_(&forms),
          on_mutation_(std::move(on_mutation)), on_focus_(std::move(on_focus)) {}

    // Where loadImage() and fetch() look. Both are owned by the browser, which
    // hands them over before scripts run; without them the page still runs and
    // every load simply fails.
    void observe_resources(asset_registry & assets, image_store & images);
    // Whether fetch() may open a socket when the registry misses. Off makes a
    // run hermetic, which is what a test wants and what CTBROWSER_NETWORK=0
    // selects.
    void allow_network(bool allowed) { network_allowed_ = allowed; }

    // NAVIGATION, as state rather than as an action. `location.reload()` cannot
    // reload the page where it is called: the reload tears down this context and
    // the program still running inside it. So it records the request and the
    // browser drains it between ticks, which is also how the previous engine did it.
    [[nodiscard]] bool reload_requested() const noexcept { return reload_requested_; }
    void clear_reload_request() noexcept { reload_requested_ = false; }
    // What `location` reports. The browser sets it; the page can only read it,
    // because assigning to location.href is a navigation and the engine has none.
    void observe_location(std::string href, std::string hash);

    // Where focus is now, so `document.activeElement` can answer. The focus
    // hook below goes the other way - script setting focus - and left the
    // bindings with no way to READ it.
    void observe_focus(node_id id);
    // Where alert() goes. The browser records them, because a reload replaces
    // these bindings and the alert that caused it must survive that.
    void set_alert_hook(std::function<void(const std::string &)> hook);

    // `element.click()` does two things: it dispatches a click event, and - if
    // nothing called preventDefault - it performs the element's DEFAULT ACTION.
    // The second half belongs to the browser (following a link, toggling a
    // checkbox, submitting a form), so it comes in as a hook for the same reason
    // on_mutation does: the dependency points one way.
    void set_activate_hook(std::function<void(node_id)> hook) { on_activate_ = std::move(hook); }

    // Returns whether a page write reached a control, so the browser knows the
    // paint is stale without having to diff the form store.
    bool refresh_wrappers();

    // Layout results, so offsetWidth and friends can answer. Set by the
    // browser after each layout; null until the first one, and the natives
    // return 0 then rather than pretending.
    void observe_layout(const layout::fragment * fragments) { fragments_ = fragments; }
    void observe_viewport(int width, int height);
    // Milliseconds since the page loaded, for performance.now and the timers.
    void advance_clock(double ms) { now_ms_ += ms; }
    [[nodiscard]] double now_ms() const noexcept { return now_ms_; }

    // Everything this holds that the VM cannot see. Registered with the context
    // so a collection does not free a page's own listeners out from under it.
    void register_roots(context & cx);

    void install(context & cx);

    // --- dispatch, from the browser --------------------------------------

    // Fire `type` at `target` and at every ancestor, like a bubbling DOM event.
    // Returns whether a listener called preventDefault.
    bool dispatch(std::string_view type, node_id target);

    // A KeyboardEvent. `code` is the physical key and `key` is what it means -
    // pages read both, and an event object carrying neither is why a page could
    // register a keydown listener and never learn which key was pressed.
    bool dispatch_key(std::string_view type, node_id target, const input_event & input);

    // A MouseEvent. clientX/clientY are viewport coordinates, which is what
    // MDN's breakout reads to move its paddle.
    bool dispatch_mouse(std::string_view type, node_id target, const input_event & input);
    // A `wheel` event, and whether the page CONSUMED it - a page that calls
    // preventDefault means the document must not scroll as well. See the
    // definition for the sign and the units, both of which are easy to invert.
    bool dispatch_wheel(node_id target, const input_event & input);

    bool dispatch_event(std::string_view type, node_id target, value event);

    // Run the timers that are due, then the animation callbacks. Returns how
    // many ran, so an event loop can tell whether it needs another frame.
    std::size_t run_due_callbacks();

    [[nodiscard]] std::size_t pending_timers() const noexcept { return timers_.size(); }
    // When the next callback is due, in milliseconds from now. Infinity when
    // there is none - which is what lets an idle application block rather than
    // poll. An animation frame is due IMMEDIATELY: a page that asked for one
    // wants the next frame, not a timer's worth of delay.
    [[nodiscard]] double next_callback_ms() const;

    [[nodiscard]] std::size_t pending_animation_frames() const noexcept;
    // The first fault a timer or animation-frame callback raised, and how many
    // there have been. Empty when the page's callbacks are running cleanly.
    [[nodiscard]] const std::string & callback_error() const noexcept { return callback_error_; }
    [[nodiscard]] std::size_t callback_faults() const noexcept { return callback_faults_; }
    // --- the WebGL back end, stage 2 of docs/angle-plan.md -------------------
    //
    // BEFORE THE PAGE RUNS. A context is made when a page asks for one, and its
    // backend cannot change underneath programs already compiled into it - so
    // this decides for contexts made from here on and says nothing about any
    // that exist.
    void prefer_angle(bool on) { angle_preferred_ = on; }
    [[nodiscard]] bool angle_preferred() const noexcept { return angle_preferred_; }
    // Every GL call a page made that the ANGLE path does not forward yet,
    // gathered from all its contexts. EMPTY is the claim a test makes; a
    // backend that silently dropped calls would paint something plausible.
    [[nodiscard]] std::vector<std::string> unforwarded_gl_calls() const;

    [[nodiscard]] const std::vector<std::string> & console_output() const noexcept;

private:
    struct timer {
        std::uint32_t id = 0;
        value callback;
        double due_ms = 0;
        double interval_ms = 0;
        bool repeating = false;
        bool cancelled = false;
    };
    struct listener {
        node_id target; // empty = document/window
        std::string type;
        value callback;
        // The AbortSignal this listener was registered with, if any. Aborting
        // it removes every listener that carries it - which is how a library
        // takes down a whole sketch's listeners in one call.
        value abort_signal = value::undefined();
        // `{ once: true }` - fire and remove. Accepted and ignored before, so a
        // listener a page registered to run exactly once ran on every event: a
        // one-shot "did the user interact yet" handler kept firing, and a
        // library counting how often something happened counted wrong.
        bool once = false;
        // `{ capture: true }` - fired on the way DOWN to the target rather than
        // on the way back up. It is the whole reason to pass it: a capturing
        // listener on an ancestor sees the event BEFORE the target does, which
        // is how a page intercepts one.
        bool capture = false;
        // Set when a `once` listener has fired, so the pass that removes them
        // runs after the dispatch rather than mutating the list being walked.
        bool spent = false;
    };

    // --- element wrappers -------------------------------------------------

    // ONE WRAPPER PER ELEMENT, cached. Two reasons, and the second is the one
    // that showed up as a bug: `getElementById('x') === getElementById('x')` is
    // true in a browser and was false here, and - far worse - a wrapper's
    // properties are a SNAPSHOT taken when it was made. A page that does
    //     const name = document.getElementById('name');
    //     ... later ... name.value
    // read whatever `value` was at page load, forever. That is what made the
    // widget gallery report `color: undefined` and never update.
    [[nodiscard]] value wrap(context & cx, node_id id);

    // Bring every live wrapper back in step with the document. Called before a
    // dispatch and before a frame, which are the two moments a page can observe
    // the difference.

    [[nodiscard]] static std::uint64_t pack(node_id id);
    [[nodiscard]] static node_id unpack(std::uint64_t bits);

    // The element a native was called on. Returns an empty handle when the
    // receiver is not a wrapper - which a native must treat as "do nothing"
    // rather than as "the document root".
    [[nodiscard]] node_id receiver(context & cx);

    // Live-ish properties. Refreshed when a wrapper is made and after layout,
    // which is what `element.offsetWidth` actually needs to be useful.
    void refresh_element(context & cx, script::object_object & obj, node_id id);

    // `value` and `checked` USED TO BE SYNCED HERE, as data properties written
    // on whatever tick this next ran. They are accessors now
    // (install_element_views), which is what makes a read LIVE: a page that
    // creates a control and reads it back in the same statement -
    // `createInput('hello').value()`, which is p5's own DOM library - saw the
    // property as it was before the value existed.
    //
    // The sync could not simply be left in place beside them: an own DATA
    // property shadows an accessor, so it won every read and the accessor was
    // dead code. What is left of this function is the control-kind check, which
    // the wrapper still needs.
    void refresh_control(context & cx, script::object_object & obj, const read_txn & txn,
                         node_id id, std::string_view tag_text) {
        (void)cx;
        (void)obj;
        (void)txn;
        (void)id;
        (void)tag_text;
    }

    [[nodiscard]] rect box_of(node_id id) const;

    void install_element_methods(context & cx, script::object_object & obj);
    void note_callback_fault(std::string_view source);
    // One reading of addEventListener's third argument, shared by the element,
    // document and window registrations - three copies is three chances for
    // `once` to work on one of them and not the others.
    [[nodiscard]] listener make_listener(context & cx, node_id target, std::span<value> args);
    // `innerHTML`. Setting one PARSES: the markup becomes real nodes under the
    // element, replacing whatever was there. It used to be a plain property on
    // the wrapper, so assigning markup stored a string, rendered nothing, and
    // said nothing.
    void set_inner_html(node_id target, std::string_view markup);
    [[nodiscard]] std::string inner_html(node_id target) const;
    // One node and its subtree, copied from another document into this one.
    // The scratch document a fragment is parsed into shares this atom table, so
    // a tag or attribute name needs no remapping.
    node_id copy_subtree(const read_txn & from, node_id node, node_id parent);
    [[nodiscard]] std::string text_content(node_id target) const;
    void write_location_parts(context & cx, script::object_object & loc);
    // `element.style` and `element.classList` - the two views onto an element
    // that are OBJECTS rather than values, so unlike everything in
    // refresh_element they are built once and keep their identity. A page holds
    // on to `el.style` and writes through it later, which a fresh object every
    // sync would silently discard.
    //
    // They take the id directly because their methods are not called with the
    // element as `this`: `el.classList.add(...)` has the CLASS LIST as the
    // receiver, so `receiver(cx)` finds no handle.
    void install_element_views(context & cx, script::object_object & obj, node_id id);

    [[nodiscard]] node_id id_or_nothing(context & c) { return receiver(c); }

    // The 2D context. Its methods close over the canvas node, so the object can
    // be stored and reused - which is what every canvas page does.
    // What a page can pass to drawImage: a loadImage() handle (a number) or an
    // <img> element wrapper. Anything else is nothing to draw.
    [[nodiscard]] std::shared_ptr<const paint::bitmap> image_argument(value v);

    // A DOMMatrix over a transform: the six numbers plus the methods a page
    // composes them with. See the getTransform binding for why six bare numbers
    // was not enough.
    [[nodiscard]] value matrix_object(context & cx, const transform & t);

    [[nodiscard]] value canvas_context_object(context & cx, node_id id);

    // `canvas.getContext('webgl')`. The JavaScript surface is in its own file -
    // seventy-nine methods and a constant table would bury the DOM in this one -
    // and the state machine it drives is in shell/webgl.hpp.
    [[nodiscard]] value webgl_context_object(context & cx, node_id id, int version);

    // SETTING canvas.width RESIZES THE DRAWING BUFFER, and for a WebGL canvas
    // that is not cosmetic: canvas_context::resize REALLOCATES the bitmap, so a
    // context still holding the old pointer is drawing into freed memory. The
    // size disagreement is the visible half - p5 creates its canvas, asks for a
    // context, and only then sets the size, so without this every p5 WEBGL
    // sketch drew into a 300x150 buffer and read back a 20x20 window of nothing.
    void resize_webgl_context(node_id id, int width, int height);
    // Copy every live WebGL context's surface into its canvas bitmap. Called
    // once at the end of a frame, never per draw.
    void present_webgl_contexts();

    // ONE CONTEXT PER CANVAS, kept for the document's life. getContext is
    // idempotent in the spec: a page that calls it twice gets the same object
    // with the same buffers and programs still bound, and a fresh one each time
    // would quietly lose everything it had uploaded.
    flat_map<std::uint64_t, std::unique_ptr<webgl_context>> webgl_contexts_;
    // Whether a NEW WebGL context should go on the ANGLE backend. Stage 2 of
    // docs/angle-plan.md keeps both paths alive, and this is the switch.
    bool angle_preferred_ = false;
    // The JS wrapper for each, so getContext hands back the SAME object - and a
    // GC root, because the page may drop its reference and ask again.
    flat_map<std::uint64_t, script::object_object *> webgl_objects_;

    [[nodiscard]] static float number(std::span<value> args, std::size_t i);

    // "bold 16px sans-serif" -> 16.
    [[nodiscard]] static float font_size_from(std::string_view font);

    // ...and -> family "sans-serif", bold, not italic. The family used to be
    // thrown away, which is how a canvas asking for Arial got the bitmap font's
    // 8-pixel monospaced cell and a HUD laid out from the right edge ran off it.
    //
    // An honest subset of the CSS `font` shorthand: tokens before the <n>px one
    // supply bold/italic, and the first entry of the family list after it is
    // the family. Not handled, and not pretended to be: `font-weight: 700` as a
    // number, `<size>/<line-height>`, and keyword sizes like `medium`.
    static void font_face_from(std::string_view font, std::string & family, bool & bold,
                               bool & italic);

    [[nodiscard]] node_id handle_of(value v);

    [[nodiscard]] std::string text_of(node_id id) const;

    void set_text(node_id id, std::string text);

    void edit_classes(node_id id, const std::string & name, bool add);

    [[nodiscard]] static std::vector<std::string_view> split(std::string_view text);

    void mutated();

    // --- globals ----------------------------------------------------------

    void install_console(context & cx);

    void install_document(context & cx);

    // `alert` and `location`, the last two globals the previous engine had and the engine did not.
    // MDN's breakout calls both the moment the game ends - alert("GAME OVER")
    // then document.location.reload() - so a page could win or lose and then
    // die on an undefined identifier.
    void install_navigation(context & cx);

    value make_location(context & cx);

    [[nodiscard]] script::object_object * document_object();
    [[nodiscard]] script::object_object * window_object();

    void install_window(context & cx);

    // --- images and fetch --------------------------------------------------

    void install_resources(context & cx);

    // A FETCH THAT HAS NOT HAPPENED YET.
    //
    // fetch() used to do the work and hand back an already-settled promise,
    // which was the only option while `await` could not suspend: a pending one
    // would have evaluated to undefined and the rest of the function would have
    // run with it. `await` suspends now, so a fetch can be what it is - work
    // that finishes on a later turn.
    //
    // That is not pedantry. A page's `await fetch(url)` used to return before
    // any other timer or listener could run, so nothing a real page does to stay
    // responsive while loading could be observed at all - and an AbortController
    // had nothing to abort, because the request was over before the object
    // existed.
    struct pending_fetch {
        value promise;
        std::string url;
        value signal; // the AbortSignal it was given, if any
    };
    std::vector<pending_fetch> fetches_;

    // AN IMAGE LOAD THAT HAS NOT HAPPENED YET, for the same reason a fetch is
    // one: `img.src = url` returns immediately and the page hears about it
    // through `onload` on a later turn. Firing synchronously from the setter
    // would work for the way p5 writes it - handlers assigned before src - and
    // break `img.src = url; img.onload = f`, which fires nothing at all.
    struct pending_image {
        value target; // the <img> wrapper whose src was assigned
        node_id id;
        std::string url;
        value promise; // decode()'s promise; undefined for a plain src assignment
    };
    std::vector<pending_image> image_loads_;

    // A FileReader's read, which finishes on a LATER TURN for the same reason an
    // image load does: a page assigns `onload` after calling readAsText, so a
    // reader that delivered synchronously would fire before the handler existed.
    enum class read_kind : std::uint8_t {
        text,
        data_url,
        array_buffer,
        binary_string
    };
    struct pending_read {
        value reader;
        value blob;
        read_kind kind;
    };
    std::vector<pending_read> reads_;
    void settle_read(context & cx, const pending_read & waiting);

    // Resolve the bytes, set `complete`, and announce it - `onload` and any
    // `load` listener, or the error pair.
    void settle_image(context & cx, const pending_image & waiting);

    // Queue one. `promise` is undefined unless decode() asked for it.
    void begin_image_load(value target, node_id id, std::string url, value promise);

    // The loading surface an <img> has beyond a plain element: src, the size
    // that falls back to the decoded pixels, complete, decode().
    void install_image_views(context & cx, script::object_object & obj, node_id id);

    // Do the work for one queued fetch and settle its promise. Called from the
    // event loop, not from fetch().
    void settle_fetch(context & cx, const pending_fetch & waiting);

    [[nodiscard]] value fetch_now(context & cx, const std::string & url);

    [[nodiscard]] static value make_rejection(context & cx, const std::string & message);

    // The Response object.
    //
    // The BODY methods hand back settled promises: the bytes are already in
    // hand by the time a Response exists, so there is nothing to wait for. It is
    // the fetch itself that is asynchronous, which is the part a page can
    // observe.
    //
    // The surface is what a real caller reads rather than what is easy to
    // provide. p5.js's own `request()` helper branches on `res.ok` and then
    // calls one of json/text/arrayBuffer/blob/bytes, and reads `res.headers` -
    // so `headers` being a bare content-type string meant `headers.get(...)`
    // threw on a library doing the ordinary thing.
    [[nodiscard]] value make_response(context & cx, const std::string & url, int status,
                                      const std::string & content_type,
                                      std::vector<std::byte> body) {
        auto * response = static_cast<script::object_object *>(cx.make_object().as_heap());
        response->set("url", cx.string(url));
        response->set("status", value::number(status));
        response->set("ok", value::boolean(status >= 200 && status < 300));
        response->set("statusText", cx.string(status == 200   ? "OK"
                                              : status == 404 ? "Not Found"
                                                              : ""));
        response->set("type", cx.string("basic"));
        // `headers` IS AN OBJECT with get() and has(), not a string. A page does
        // `res.headers.get('content-type')`, and the only header this engine
        // knows is the content type - so it answers that one and reports every
        // other as absent rather than pretending.
        {
            auto * headers = static_cast<script::object_object *>(cx.make_object().as_heap());
            headers->set("__contentType", cx.string(content_type));
            const auto header_method = [&](std::string name, script::native_fn fn) {
                headers->set(
                    name, value::object(cx.allocate<script::native_object>(name, std::move(fn))));
            };
            const auto is_content_type = [](std::string_view wanted) {
                return ascii_iequals(wanted, "content-type");
            };
            header_method("get", [content_type, is_content_type](context & c, std::span<value> a) {
                if (!is_content_type(arg_string(c, a, 0)) || content_type.empty()) {
                    return value::null();
                }
                return c.string(content_type);
            });
            header_method("has", [content_type, is_content_type](context & c, std::span<value> a) {
                return value::boolean(is_content_type(arg_string(c, a, 0)) &&
                                      !content_type.empty());
            });
            response->set("headers", value::object(headers));
        }

        const std::string text{reinterpret_cast<const char *>(body.data()), body.size()};
        const auto method = [&](std::string name, script::native_fn fn) {
            response->set(name,
                          value::object(cx.allocate<script::native_object>(name, std::move(fn))));
        };
        method("text", [text](context & c, std::span<value>) {
            return c.make_promise(c.string(text), false);
        });
        method("json", [text](context & c, std::span<value>) {
            // Through the standard library's JSON.parse, so one parser decides
            // what JSON means here.
            const value parser = c.global("JSON");
            if (parser.is_object()) {
                if (value * parse =
                        static_cast<script::object_object *>(parser.as_heap())->find("parse")) {
                    const value text_value = c.string(text);
                    const value args[1] = {text_value};
                    return c.make_promise(c.call(*parse, args), false);
                }
            }
            return c.make_promise(value::undefined(), false);
        });
        // The bytes, three ways a caller may ask for them. `bytes()` is the
        // newest and p5 prefers it when present; `arrayBuffer()` is what
        // everything else uses, and `blob()` is what an object URL is made from.
        const auto byte_array = [](context & c, const std::vector<std::byte> & bytes) {
            const value out = c.make_array();
            auto * items = static_cast<script::array_object *>(out.as_heap());
            items->elements = script::element_kind::u8;
            items->items.reserve(bytes.size());
            for (const std::byte b : bytes) {
                items->items.push_back(value::number(static_cast<double>(std::to_integer<int>(b))));
            }
            return out;
        };
        method("bytes", [body, byte_array](context & c, std::span<value>) {
            return c.make_promise(byte_array(c, body), false);
        });
        method("arrayBuffer", [body, byte_array](context & c, std::span<value>) {
            // The shape install_typed_arrays recognises: an object carrying
            // `__bytes`, so `new Uint8Array(buffer)` is a view over THIS
            // storage rather than a copy of it.
            auto * buffer = static_cast<script::object_object *>(c.make_object().as_heap());
            buffer->set("byteLength", value::number(static_cast<double>(body.size())));
            buffer->set("length", value::number(static_cast<double>(body.size())));
            buffer->set("__bytes", byte_array(c, body));
            return c.make_promise(value::object(buffer), false);
        });
        method("blob", [body, content_type, byte_array](context & c, std::span<value>) {
            // A minimal Blob: its size, its type and its bytes. Enough for a
            // page that hands one to URL.createObjectURL, which is the only
            // thing anything here does with one.
            auto * blob = static_cast<script::object_object *>(c.make_object().as_heap());
            blob->set("size", value::number(static_cast<double>(body.size())));
            blob->set("type", c.string(content_type));
            blob->set("__bytes", byte_array(c, body));
            return c.make_promise(value::object(blob), false);
        });
        return cx.make_promise(value::object(response), false);
    }

    void install_timers(context & cx);

    [[nodiscard]] std::uint32_t add_timer(value callback, double delay_ms, bool repeating);

    // --- events -----------------------------------------------------------

    [[nodiscard]] value make_event(context & cx, std::string_view type, node_id target);

    [[nodiscard]] static bool prevented(value event);

    void fire_at(node_id target, std::string_view type, value event, bool capturing);

    // `onclick`, `onload` - the handler PROPERTY, run after the listeners.
    void fire_handler_property(value target, std::string_view type, value event);
    [[nodiscard]] value value_of_wrapper(node_id id) const;
    void fire_global(std::string_view type, value event, bool capturing);

    // --- lookups ----------------------------------------------------------

    [[nodiscard]] node_id find_by_id(const std::string & want);

    [[nodiscard]] node_id find_by_tag(std::string_view tag);
    // Every element with this tag, in document order; "*" means all of them.
    [[nodiscard]] std::vector<node_id> all_by_tag(std::string_view tag);
    // Compound selectors only - see the definition.
    [[nodiscard]] std::vector<node_id> query(std::string_view selector, node_id within = node_id{});
    // The document's own live properties - title and activeElement.
    void refresh_document();

    document * doc_;
    atom_table * atoms_;
    canvas_store * canvases_;
    form_store * forms_;
    std::function<void()> on_mutation_;
    std::function<void(node_id)> on_focus_;
    std::function<void(const std::string &)> on_alert_;
    std::function<void(node_id)> on_activate_;
    // What we last wrote into a wrapper, so a differing value means the PAGE
    // wrote it. See refresh_control.
    struct property_mirror {
        std::string value;
        bool checked = false;
    };
    flat_map<std::uint64_t, script::object_object *> wrappers_;
    flat_map<std::uint64_t, property_mirror> mirrors_;
    bool wrote_to_control_ = false;
    // What the browser last told us has focus, for document.activeElement.
    node_id focused_;
    std::string location_href_;
    std::string location_hash_;
    value location_;
    value document_;
    value window_;
    // The first fault a timer or animation frame raised, and how many there
    // were. A page whose draw loop throws every frame has ONE bug, not a
    // thousand, and the first message is the one that names it.
    // `document.cookie`, in insertion order so reading it back is stable.
    std::vector<std::pair<std::string, std::string>> cookies_;
    // Counts the object URLs handed out, so each is distinct. Counted rather
    // than random for the same reason Math.random is seeded: a page that prints
    // one could not otherwise have a golden.
    std::uint32_t next_object_url_ = 0;
    // Blob.prototype, kept so canvas.toBlob's Blob is one too - `x instanceof
    // Blob` has to be true whoever made it.
    value blob_prototype_;

    // THE DOM'S INTERFACE OBJECTS - `window.HTMLCanvasElement` and friends.
    //
    // A browser exposes one per interface, and libraries use them two ways that
    // both have to work: feature detection (`!!window.CanvasRenderingContext2D`)
    // and identity (`el instanceof HTMLCanvasElement`). Defining a bare marker
    // object satisfies the first and makes the second silently FALSE, which is
    // the shape of wrong answer this engine keeps being bitten by - Phaser tests
    // instanceof nine times and p5 four.
    //
    // So each carries a real `prototype`, and the objects that are instances get
    // that prototype linked. See interface_prototype().
    value canvas_element_prototype_;
    value image_element_prototype_;
    value canvas2d_prototype_;
    value webgl_prototype_;
    // A SEPARATE INTERFACE, not a subclass. `WebGL2RenderingContext` does not
    // inherit from `WebGLRenderingContext` in the specification, so a real
    // WebGL 2 context is NOT `instanceof WebGLRenderingContext` - and a page
    // that tests for one to decide which path to take (Phaser does) must get
    // the same answer here as it would in a browser.
    value webgl2_prototype_;
    std::string callback_error_;
    std::size_t callback_faults_ = 0;
    bool reload_requested_ = false;
    asset_registry * assets_ = nullptr;
    image_store * images_ = nullptr;
    bool network_allowed_ = true;
    context * cx_ = nullptr;
    const layout::fragment * fragments_ = nullptr;
    int viewport_width_ = 0;
    int viewport_height_ = 0;
    double now_ms_ = 0;

    std::vector<listener> listeners_;
    std::vector<timer> timers_;
    std::vector<value> animation_callbacks_;
    std::vector<std::string> console_;
    std::uint32_t next_timer_id_ = 0;
};

} // namespace ctbrowser::shell
