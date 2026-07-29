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

#include <ctbrowser/shell/assets.hpp>
#include <ctbrowser/shell/canvas.hpp>
#include <ctbrowser/shell/forms.hpp>
#include <ctbrowser/shell/images.hpp>
#include <ctbrowser/shell/input.hpp>
#include <ctbrowser/shell/net.hpp>

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

    // `value` and `checked` on a form control, in BOTH directions. The VM has
    // no property accessors, so a live property is a sync rather than a getter:
    // what the page wrote wins (it wrote it after we last set it), otherwise
    // the control's own state does. Without the write-back `input.value = ""`
    // would set a property nothing reads and the field would not clear.
    void refresh_control(context & cx, script::object_object & obj, const read_txn & txn,
                         node_id id, std::string_view tag_text) {
        if (forms_ == nullptr) { return; }
        const std::string_view type = txn.attribute_value(id, atoms_->intern("type"));
        const control_kind kind = control_kind_of(tag_text, type);
        if (kind == control_kind::none) { return; }
        control_state & control = forms_->state_of(txn, *atoms_, id);
        auto & mirror = mirrors_[pack(id)];

        if (const value * written = obj.find("value");
            written != nullptr && cx.to_string(*written) != mirror.value) {
            form_store::set_value(control, cx.to_string(*written));
            wrote_to_control_ = true;
        }
        mirror.value = control.value;
        obj.set("value", cx.string(control.value));

        if (kind == control_kind::checkbox || kind == control_kind::radio) {
            if (const value * written = obj.find("checked");
                written != nullptr && context::truthy(*written) != mirror.checked) {
                control.checked = context::truthy(*written);
                wrote_to_control_ = true;
            }
            mirror.checked = control.checked;
            obj.set("checked", value::boolean(control.checked));
        }
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

    [[nodiscard]] value canvas_context_object(context & cx, node_id id);

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

    [[nodiscard]] value fetch_now(context & cx, const std::string & url);

    [[nodiscard]] static value make_rejection(context & cx, const std::string & message);

    // The Response object: settled promises all the way down, matching what the
    // VM's await can unwrap.
    [[nodiscard]] value make_response(context & cx, const std::string & url, int status,
                                      const std::string & content_type,
                                      std::vector<std::byte> body) {
        auto * response = static_cast<script::object_object *>(cx.make_object().as_heap());
        response->set("url", cx.string(url));
        response->set("status", value::number(status));
        response->set("ok", value::boolean(status >= 200 && status < 300));
        response->set("headers", cx.string(content_type));

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
        method("bytes", [body = std::move(body)](context & c, std::span<value>) {
            const value out = c.make_array();
            auto * items = static_cast<script::array_object *>(out.as_heap());
            items->items.reserve(body.size());
            for (const std::byte b : body) {
                items->items.push_back(value::number(static_cast<double>(std::to_integer<int>(b))));
            }
            return c.make_promise(out, false);
        });
        return cx.make_promise(value::object(response), false);
    }

    void install_timers(context & cx);

    [[nodiscard]] std::uint32_t add_timer(value callback, double delay_ms, bool repeating);

    // --- events -----------------------------------------------------------

    [[nodiscard]] value make_event(context & cx, std::string_view type, node_id target);

    [[nodiscard]] static bool prevented(value event);

    void fire_at(node_id target, std::string_view type, value event, bool capturing);
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
