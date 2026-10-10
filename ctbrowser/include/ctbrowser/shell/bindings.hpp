//===- bindings.hpp - Script bindings for one document ----------*- C++ -*-===//
//
// dom_bindings, the bridge between one document and the script engine. Its
// members are implemented across lib/Shell/bindings/, one directory or file
// per area (events, frames, stylesheets, custom elements, ...); the comments
// below name the file where it is not obvious. The plain data records it keeps
// are in bindings/*_types.hpp.
//
//===----------------------------------------------------------------------===//
#pragma once

#include "bindings/document_types.hpp"
#include <ctbrowser/shell/timer_queue.hpp>

namespace ctbrowser::shell {

namespace detail {
struct control_helpers;
}

/// The script bindings for one document: wraps its nodes as JS objects,
/// installs the Web APIs (DOM, CSSOM, events, timers, fetch, frames), and
/// takes the events and state the browser feeds in. Detailed contracts:
/// docs/reference/header-contracts/shell-bindings-*.md
///
/// One instance is the page's (the primary); documents the page creates and
/// frames' documents get their own instances that point back at it.
class dom_bindings {
public:
    /// \p on_mutation is how the browser learns it has to re-run the pipeline.
    /// Taking it as a callback rather than a browser reference keeps this
    /// testable on its own and keeps the dependency pointing one way.
    /// \p on_focus is called when script moves focus.
    dom_bindings(document & doc, atom_table & atoms, canvas_store & canvases, form_store & forms,
                 std::function<void()> on_mutation, std::function<void(node_id)> on_focus)
        : doc_(&doc), atoms_(&atoms), canvases_(&canvases), forms_(&forms),
          on_mutation_(std::move(on_mutation)), on_focus_(std::move(on_focus)) {}

    ~dom_bindings();

    /// Where resources are loaded from and images are decoded into.
    void observe_resources(asset_registry & assets, image_store & images);

    /// Whether fetch() may open a socket when the registry misses. Off makes a
    /// run hermetic, which is what a test wants and what CTBROWSER_NETWORK=0
    /// selects.
    void allow_network(bool allowed) { network_allowed_ = allowed; }

    /// Whether script called `location.reload()`. Navigation is state rather
    /// than an action: the reload tears down this context and the program still
    /// running inside it, so the call records the request and the browser
    /// drains it between ticks.
    [[nodiscard]] bool reload_requested() const noexcept { return reload_requested_; }
    /// Records the page's new address: the document's URL properties and the
    /// `:target` element follow it.
    void observe_location(std::string href, std::string hash);

    /// Tells the bindings which element has focus, for document.activeElement.
    void observe_focus(node_id id);
    /// What `alert()` calls with its message.
    void set_alert_hook(std::function<void(const std::string &)> hook);

    /// `element.click()` dispatches a click event and, if nothing called
    /// preventDefault, performs the element's default action. The default
    /// action belongs to the browser (following a link, toggling a checkbox,
    /// submitting a form), so it comes in as this hook for the same reason
    /// on_mutation does: the dependency points one way.
    void set_activate_hook(std::function<void(node_id)> hook) { on_activate_ = std::move(hook); }

    /// Links wrappers made before the DOM interfaces existed. Returns whether
    /// script wrote to a form control since the last call.
    bool refresh_wrappers();

    /// Layout results, so offsetWidth and friends can answer. Set by the
    /// browser after each layout; null until the first one, and the natives
    /// return 0 then rather than pretending.
    void observe_layout(const layout::fragment * fragments) { fragments_ = fragments; }

    /// The cascade's output, for getComputedStyle. getComputedStyle needs three
    /// sources (this, observe_boxes and observe_layout) because
    /// `style::computed_style` holds only the declarations that matched, as
    /// text, with no inheritance and no initial values. So a keyword comes from
    /// the style map, a resolved length from the box tree, and a used size from
    /// the fragment; see lib/Page/computed_style/.
    void observe_styles(const style::style_map * styles) { styles_ = styles; }

    /// The box tree, for getComputedStyle; see observe_styles.
    void observe_boxes(const layout::box_node * boxes) { boxes_ = boxes; }
    /// The trees and viewport getComputedStyle and the client rects are read
    /// from, as last observed: what ctbrowser/page answers its queries over.
    [[nodiscard]] page::page_view view_of_page() const;
    /// The viewport size in CSS pixels, for innerWidth and media queries.
    void observe_viewport(int width, int height);

    using loaded_frame = binding_detail::loaded_frame;

    /// The <iframe>s that have a document loaded, for the browser to lay out.
    [[nodiscard]] std::vector<loaded_frame> loaded_frames() const {
        std::vector<loaded_frame> out;
        for (const frame_entry & entry : frames_) {
            if (entry.bindings != nullptr) { out.push_back({entry.element, entry.bindings}); }
        }
        return out;
    }

    [[nodiscard]] document & owned_document() noexcept { return *doc_; }

    /// Advances the page clock (milliseconds since the page loaded) that
    /// performance.now and the timers read.
    void advance_clock(double ms) { now_ms_ += ms; }

    [[nodiscard]] double now_ms() const noexcept { return now_ms_; }

    /// Makes \p cx's garbage collector mark everything these bindings hold.
    /// Called on the primary only: the realm has one external-roots callback,
    /// so the primary walks every document it made.
    void register_roots(context & cx);

    /// Installs the Web APIs into \p cx's global object.
    void install(context & cx);

    // --- Dispatch, from the browser --------------------------------------

    /// Fires a plain event of \p type at \p target (the window when empty).
    bool dispatch(std::string_view type, node_id target);

    bool dispatch_key(std::string_view type, node_id target, const input_event & input);
    bool dispatch_keypress(node_id target, std::string_view character); // events/input.cpp
    [[nodiscard]] bool form_associated(node_id element) const;          // custom_elements.cpp
    // User activation (HTML 6.4.3), window/activation.cpp: a trusted
    // activation-triggering input event notifies; transient activation lasts a
    // few seconds after it, sticky activation for good; consuming ends the
    // transient part in every window of the tree.
    void notify_activation();
    [[nodiscard]] bool has_transient_activation() const;
    [[nodiscard]] bool has_sticky_activation() const;
    void consume_activation();

    bool dispatch_error(std::string_view message);
    bool dispatch_error(std::string_view message, std::string_view script_src);

    bool dispatch_mouse(std::string_view type, node_id target, const input_event & input);
    bool dispatch_wheel(node_id target, const input_event & input);
    bool dispatch_touch(std::string_view phase, node_id target, float x, float y,
                        bool pointer = true);
    /// While a finger is down, its scrolls' scrollend events wait for the lift
    /// (events/input.cpp).
    void hold_scrollend(bool hold);
    std::optional<std::vector<node_id>> held_scrollend_;
    bool wheel_scroll(node_id from, float dx, float dy); // events/input.cpp
    void select_by_user(node_id anchor, std::size_t anchor_byte, node_id focus,
                        std::size_t focus_byte); // events/input.cpp
    bool dispatch_input(std::string_view type, node_id target,
                        std::optional<std::string_view> input_type,
                        std::optional<std::string_view> data); // events/input.cpp
    /// The computed role, or with \p label the accessible name, of the element
    /// `getter(slot)` returns in the page: testdriver's get_computed_role and
    /// get_computed_label (page/accessibility.cpp).
    std::optional<std::string> computed_accessibility(std::string_view getter, double slot,
                                                      bool label);

    bool dispatch_event(std::string_view type, node_id target, value event);
    /// A slot's assigned nodes and a slottable's slot (shadow_dom.cpp); the
    /// browser's focus navigation walks the flat tree through them.
    [[nodiscard]] std::vector<node_id> assigned_nodes_of(node_id slot) const;
    [[nodiscard]] node_id assigned_slot_of(node_id slottable) const;

    /// `element.click()`: a click event, then the default action.
    bool click(node_id target);
    /// The node at which the `click` whose activation behaviour is running was
    /// dispatched (events/dispatch.cpp); a <label>'s activation reads it.
    node_id activation_event_target;
    /// A label's labeled control, labelable custom elements included (HTML
    /// 4.10.4; element/control_methods/collections_and_labels.cpp).
    [[nodiscard]] node_id labeled_control(node_id label);
    [[nodiscard]] std::optional<bool> control_validity(node_id id); // :valid/:invalid's answer.
    /// Called after any write to the document, the browser's own included (a
    /// <summary>'s activation): queues the mutation observers' records and runs
    /// the attribute change steps (entry.cpp).
    void mutated();
    [[nodiscard]] bool is_connected(node_id target) const;

    /// Runs the callbacks due this tick: finished fetches, image loads, frame
    /// loads, FileReader reads and animation frames. Returns how many ran.
    std::size_t run_due_callbacks();
    /// Runs the due timers, each as a task; with \p queued_tasks_only, only the
    /// tasks the host queued and none of the page's setTimeout/setInterval
    /// (window/callbacks.cpp). Returns how many ran.
    std::size_t run_timers(bool queued_tasks_only);

    /// Loads, replaces or drops frame documents to match the <iframe>s now in
    /// the document.
    void reconcile_frames();
    /// Plans a form submission's navigation of its target frame; it runs at the
    /// start of the next tick. A later submission of the same form replaces it.
    bool navigate_form_target(node_id form, form_navigation navigation);
    /// Runs the navigations navigate_form_target planned.
    void run_planned_navigations();
    /// The embedder's request handler, asked before the asset registry.
    void observe_requests(const request_handler * handler);
    /// Browser hooks for form submission and reset; set by the browser, called
    /// by the form bindings.
    std::function<bool(node_id form, node_id submitter)> submit_from_;
    std::function<void(node_id form)> reset_form_;
    /// Records the `first-paint` and `first-contentful-paint` entries, once.
    void record_first_paint();

    /// Announces a resource the browser loaded for an element (a
    /// `<link rel=stylesheet>`, a `<style>`, a `<script>`) at that element on
    /// the next tick, the way an `<iframe>`'s load is: `load`, or `error` when
    /// the bytes were not found. Queued rather than fired because the page's
    /// own script registers the listener after the element was processed.
    void announce_load(node_id id, bool ok) { frame_loads_.push_back(pending_frame{id, ok, true}); }

    [[nodiscard]] std::size_t pending_timers() const noexcept { return timers_.size(); }
    /// Milliseconds until the next timer is due; 0 while animation frame
    /// callbacks are waiting.
    [[nodiscard]] double next_callback_ms() const;

    [[nodiscard]] std::size_t pending_animation_frames() const noexcept;

    /// The first fault a timer or animation-frame callback raised. Empty when
    /// the page's callbacks are running cleanly.
    [[nodiscard]] const std::string & callback_error() const noexcept { return callback_error_; }
    /// The distinct WebGL calls a context refused to forward, for diagnostics.
    [[nodiscard]] std::vector<std::string> unforwarded_gl_calls() const;

    [[nodiscard]] const std::vector<std::string> & console_output() const noexcept;

private:
    using timer = binding_detail::timer;

    using listen_on = binding_detail::listen_on;

    using path_step = binding_detail::path_step;

    using listener = binding_detail::listener;

    // --- Element wrappers -------------------------------------------------

    [[nodiscard]] value wrap(context & cx, node_id id);

    [[nodiscard]] static node_id unpack(std::uint64_t bits);

    [[nodiscard]] node_id receiver(context & cx);
    [[nodiscard]] static value unwrap_legacy_proxy(value v); // element/wrapper.cpp

    void refresh_element(context & cx, script::object_object & obj, node_id id);

    [[nodiscard]] rect box_of(node_id id) const;

    using located = binding_detail::located;
    [[nodiscard]] located locate(node_id id, bool scrolled = false) const;
    [[nodiscard]] bool is_viewport_element(node_id id, bool scrolling);
    [[nodiscard]] bool potentially_scrollable(node_id body) const;
    /// `offsetParent` (CSSOM View): an empty node_id where the spec says null.
    [[nodiscard]] node_id offset_parent_of(node_id id);

    [[nodiscard]] point viewport_scroll() const;
    void scroll_viewport_to(double x, double y);
    /// The offset of a scroll container a script has scrolled; (0, 0) otherwise.
    [[nodiscard]] point scroll_offset_of(node_id id) const;
    void scroll_element_to(node_id id, double x, double y);
    void set_scroll_position(node_id id, char axis, double v);
    [[nodiscard]] double scroll_position(node_id id, char axis);
    void scroll_into_view(node_id id, std::string_view block, std::string_view inline_,
                          bool nearest_container);
    [[nodiscard]] node_id scrolling_element();
    /// The border box in viewport coordinates: locate(id, true).
    [[nodiscard]] rect client_rect_of(node_id id) const;
    /// The Promise a finished scroll returns, resolved with its ScrollResult.
    [[nodiscard]] static value scroll_settled(context & cx);
    void install_element_scrolling(context & cx);
    void install_element_geometry(context & cx);
    void install_style_accessor(context & cx);
    [[nodiscard]] value make_style_view(context & cx, node_id id);
    /// getClientRects() (CSSOM View): one rect per fragment, in viewport
    /// coordinates and tree order.
    [[nodiscard]] std::vector<rect> client_rects_of(node_id self);

    using box_geometry = page::box_geometry;
    [[nodiscard]] std::vector<box_geometry> client_boxes_of(node_id self,
                                                            std::string_view box) const;
    void install_window_scrolling(context & cx, script::object_object & window);
    void install_document_geometry(context & cx, script::object_object & doc);
    [[nodiscard]] std::vector<node_id> elements_from_point(double x, double y, bool all,
                                                           node_id asker = {});
    [[nodiscard]] std::optional<box_geometry> box_geometry_of(value node, std::string_view box);
    [[nodiscard]] static value make_dom_point(context & cx, double x, double y);
    [[nodiscard]] static value make_dom_rect(context & cx, const rect & r);
    [[nodiscard]] static value make_dom_quad(context & cx, const rect & r);
    [[nodiscard]] static value make_dom_quad(context & cx, const rect & r,
                                             const transform & matrix);
    void install_geometry_interfaces(context & cx);
    /// The computed value of \p property for \p id from the cascade's map, or "".
    [[nodiscard]] std::string_view cascade_value(node_id id, std::string_view property) const;

    void define_operation(context & cx, std::initializer_list<const char *> interfaces,
                          const char * name, unsigned length, script::native_fn fn);
    void install_operations(context & cx);
    void install_attribute_methods(context & cx);
    void install_node_methods(context & cx);
    void install_node_namespaces(context & cx);
    void install_control_methods(context & cx);
    void install_control_focus_canvas(context & cx);
    void install_control_tables(context & cx);
    void install_control_select_options(context & cx);
    void install_control_collections_labels(context & cx);
    void install_control_numbers_validation(context & cx);
    void install_control_form_data(context & cx);
    void install_control_text_selection(context & cx);
    void install_control_pickers(context & cx);
    friend struct detail::control_helpers;

    using operation = binding_detail::operation;
    /// Built lazily for a secondary document, on the first call routed to it.
    std::vector<operation> operations_;
    void note_callback_fault(std::string_view source);
    [[nodiscard]] listener make_listener(context & cx, path_step target, std::span<value> args);
    void add_listener(listener made);
    void reap_spent_listeners();
    void set_inner_html(node_id target, std::string_view markup);
    [[nodiscard]] std::string inner_html(node_id target) const;
    node_id copy_subtree(const read_txn & from, node_id node, node_id parent);
    node_id clone_node(const read_txn & from, node_id source, bool deep,
                       const dom_bindings * owner = nullptr,
                       binding_detail::custom_element_registry * fallback = nullptr);
    bool insert_node(node_id parent, node_id child, node_id before);

    [[nodiscard]] bool pre_insert_valid(context & cx, node_id parent, node_id child, value node_arg,
                                        value ref_arg);
    [[nodiscard]] node_id node_from(context & cx, value v, bool whole_fragment = false);
    [[nodiscard]] node_id convert_nodes(context & cx, std::span<value> args);
    [[nodiscard]] node_id viable_sibling(node_id self, std::span<value> args, bool forward);
    [[nodiscard]] std::string namespace_of(node_id id) const;
    [[nodiscard]] std::string text_content(node_id target) const;
    void define_location_property(context & cx, script::object_object & holder);
    void install_element_views(context & cx, script::object_object & obj, node_id id);
    void install_element_child_views(context & cx, script::object_object & obj, node_id id);

    /// `getComputedStyle`, on `window` and as a bare global.
    void install_computed_style(context & cx);
    [[nodiscard]] value computed_style_object(context & cx, node_id id);
    [[nodiscard]] value computed_style_object(context & cx, node_id id, atom pseudo);
    [[nodiscard]] std::vector<std::pair<std::string, std::string>> computed_style_entries(
        node_id id);
    [[nodiscard]] std::vector<std::pair<std::string, std::string>> computed_style_entries(
        node_id id, const style::computed_style_ptr & pseudo);

    void install_dom_exception(context & cx);
    void install_abort(context & cx);
    void install_autocomplete(context & cx);
    [[nodiscard]] value make_abort_signal(context & cx);
    [[nodiscard]] bool is_abort_signal(value v) const;
    void signal_abort(context & cx, value signal, value reason);
    void install_media_queries(context & cx);
    [[nodiscard]] value match_media(context & cx, std::string_view text);
    void install_promise_rejections(context & cx);
    void track_promise_rejection(value promise, bool handled);
    /// Called at the end of a microtask checkpoint: queues the task that fires
    /// unhandledrejection for the rejections collected since the last one (see
    /// install_promise_rejections).
    void queue_rejection_notification();
    bool fire_rejection_event(context & c, std::string_view type, value promise, bool cancelable);
    std::vector<value> rejections_to_notify_;   // Until the checkpoint takes them.
    std::vector<value> outstanding_rejections_; // Notified, still unhandled.
    /// A <details> whose `open` came or went: its toggle task, and the state
    /// before the first change that task stands for (handler_attributes.cpp).
    struct details_toggle {
        node_id details;
        std::uint32_t task = 0;
        bool was_open = false;
    };
    std::vector<details_toggle> details_toggles_;
    void queue_details_toggle(node_id details, bool was_open, bool open);
    [[nodiscard]] bool is_media_query_list(value v) const;
    [[nodiscard]] dom_bindings & owner_of_media_query_list(value list);
    [[nodiscard]] bool media_query_matches(std::string_view media);
    [[nodiscard]] value make_dom_exception(context & cx, std::string_view name,
                                           std::string message);
    /// make_dom_exception, thrown: the form a native binding needs.
    void throw_dom_exception(context & cx, std::string_view name, std::string message);

    void install_css_interface(context & cx);

    // --- The BEGIN/END markers below name the file in lib/Shell/bindings/ that
    // implements most of each group; some members live in sibling files.

    void install_dom_interfaces(context & cx);
    void install_rendered_text(context & cx);
    void ensure_dom_interfaces(context & cx);
    [[nodiscard]] value prototype_for_node(const read_txn & txn, node_id id) const;
    [[nodiscard]] value interface_prototype(std::string_view name) const;
    [[nodiscard]] value reflected_get(context & cx, const void * row);
    [[nodiscard]] value reflected_set(context & cx, const void * row, std::span<value> args);

    [[nodiscard]] atom attribute_key(const read_txn & txn, node_id id,
                                     std::string_view qualified) const;
    [[nodiscard]] value attribute_object(context & cx, node_id owner, const attribute & held);
    /// `element.attributes`, refilled in place so the map keeps its identity.
    void refresh_attribute_map(context & cx, script::object_object & map, node_id id);
    void install_dataset(context & cx, script::object_object & obj, node_id id);
    [[nodiscard]] bool validate_and_extract(context & cx, std::string_view where,
                                            const std::string & ns, const std::string & qualified);

    std::vector<value> interface_prototypes_;
    value interface_keeper_;
    bool interfaces_linked_ = false;

    void install_character_data(context & cx);
    void install_hyperlink_utils(context & cx);
    [[nodiscard]] value construct_node_interface(context & cx, std::string_view which,
                                                 std::span<value> args);
    [[nodiscard]] bool nodes_are_equal(const read_txn & txn, node_id left, node_id right) const;
    // END reflection

    // BEGIN mutation observers (bindings/mutation.cpp)
public:
    void record_mutations(const std::vector<document::write_note> & writes);

private:
    using mutation_options = binding_detail::mutation_options;

    using mutation_registration = binding_detail::mutation_registration;

    using mutation_node_state = binding_detail::mutation_node_state;

    void install_mutation_observer(context & cx);
    [[nodiscard]] script::object_object * mutation_observer_at(std::size_t index);
    [[nodiscard]] script::array_object * mutation_records_of(std::size_t index);
    [[nodiscard]] value make_mutation_record(context & cx, std::string_view type, node_id target);
    void queue_mutation_record(std::size_t observer, value record);
    void queue_mutation_delivery();
    void deliver_mutation_records();
    void take_mutation_snapshot();
    void collect_observed(const read_txn & txn, node_id root, bool subtree,
                          std::vector<node_id> & into) const;
    /// The observer's index, or npos when the value is not one of ours.
    [[nodiscard]] std::size_t mutation_observer_index(value v) const;
    void sync_mutation_roots();

    std::vector<value> mutation_observers_;
    std::vector<mutation_registration> mutation_registrations_;
    flat_map<std::uint64_t, mutation_node_state> mutation_snapshot_;
    value mutation_observer_prototype_;
    value mutation_record_prototype_;
    value mutation_trampoline_;
    /// The interface object, whose `retained` list roots the values above.
    script::native_object * mutation_interface_ = nullptr;
    bool mutation_delivery_queued_ = false;
    bool delivering_mutations_ = false;
    // END mutation observers

public:
    void update_css_animations(const read_txn & txn, const style::style_map & before,
                               const style::style_map & after);
    void tick_animations();
    [[nodiscard]] double next_animation_event_ms() const noexcept;
    [[nodiscard]] std::vector<std::pair<std::string, std::string>> animated_values(
        node_id id, float font_size,
        const std::function<std::string_view(std::string_view)> & underlying) const;
    [[nodiscard]] std::uint64_t animation_stamp() const noexcept;

private:
    using animation_keyframe = binding_detail::animation_keyframe;

    using effect_timing = binding_detail::effect_timing;

    using keyframe_effect_record = binding_detail::keyframe_effect_record;

    using animation_kind = binding_detail::animation_kind;

    using effect_phase = binding_detail::effect_phase;

    using animation_record = binding_detail::animation_record;
    static constexpr std::size_t no_record = static_cast<std::size_t>(-1);

    using timing_sample = binding_detail::timing_sample;
    [[nodiscard]] timing_sample sample_timing(const animation_record & a) const noexcept;
    /// `interpolate_text` plus a colour lerp: sRGB, premultiplied (CSS Color 4 §17).
    [[nodiscard]] static std::string interpolate_value(std::string_view property,
                                                       std::string_view from, std::string_view to,
                                                       double progress,
                                                       const style::css::length_context & ctx);
    [[nodiscard]] static bool transitionable(std::string_view property, std::string_view from,
                                             std::string_view to);
    /// Whether record \p a composites before \p b (Web Animations §5.4.2).
    [[nodiscard]] bool composites_before(std::size_t a, std::size_t b) const noexcept;
    void cancel_record(std::size_t index);
    void update_css_transitions(node_id element, std::size_t tree_order,
                                const style::computed_style & before,
                                const style::computed_style & after,
                                const std::vector<std::pair<std::string, std::string>> & current,
                                const std::vector<std::size_t> & records);
    void update_css_animation_list(node_id element, std::size_t tree_order,
                                   const style::computed_style & after,
                                   const std::vector<std::size_t> & records);
    std::unordered_map<std::uint64_t, std::vector<std::size_t>> owned_;
    void fire_animation_event(std::size_t index, std::string_view type, double elapsed_ms);

    void install_animations(context & cx);
    [[nodiscard]] std::size_t animation_index(value v) const;
    [[nodiscard]] std::size_t effect_index(value v) const;
    [[nodiscard]] value make_keyframe_effect(context & cx, node_id target, value keyframes,
                                             value options);
    [[nodiscard]] value make_animation(context & cx, std::size_t effect);
    [[nodiscard]] std::size_t push_effect(context & cx, keyframe_effect_record made);
    /// Read the KeyframeEffect timing and keyframes dictionaries. On invalid
    /// input each throws a TypeError on `cx` and returns false.
    [[nodiscard]] bool read_timing(context & cx, value options, effect_timing & into);
    [[nodiscard]] bool read_keyframes(context & cx, value keyframes,
                                      std::vector<animation_keyframe> & into);
    [[nodiscard]] double animation_current_time(const animation_record & a) const noexcept;
    void set_animation_current_time(animation_record & a, double t);
    [[nodiscard]] std::string_view play_state(const animation_record & a) const noexcept;
    [[nodiscard]] double effect_end_time(const keyframe_effect_record & e) const noexcept;
    void update_finished_state(context & cx, std::size_t index);
    void sync_animation_roots();
    [[nodiscard]] std::vector<std::size_t> animations_on(node_id id, bool subtree) const;

    std::vector<keyframe_effect_record> effects_;
    std::vector<animation_record> animations_;
    value animation_prototype_;
    value keyframe_effect_prototype_;
    value css_animation_prototype_;  // CSSAnimation: `animationName`.
    value css_transition_prototype_; // CSSTransition: `transitionProperty`.
    value timeline_;
    script::native_object * animation_interface_ = nullptr;
    std::uint64_t animation_generation_ = 0;

public:
    void react_custom_elements();
    void upgrade_created_subtree(node_id root);
    [[nodiscard]] value custom_elements_registry(context & cx);

private:
    using custom_element_registry = binding_detail::custom_element_registry;

    using custom_element_definition = binding_detail::custom_element_definition;

    using custom_element_state = binding_detail::custom_element_state;

    using custom_element_reaction = binding_detail::custom_element_reaction;

    void install_custom_elements(context & cx);

    /// The page's bindings (the primary): this one when primary_ is null.
    /// Custom element definitions live in the primary's vector, whichever
    /// registry took them. A document a page made (createHTMLDocument,
    /// DOMParser) has no browsing context and so no registry (HTML 4.13.3) and
    /// never upgrades a candidate, but an element adopted into it keeps its
    /// definition, and that definition is found here.
    [[nodiscard]] dom_bindings & primary() noexcept {
        return primary_ == nullptr ? *this : *primary_;
    }

    [[nodiscard]] const dom_bindings & primary() const noexcept {
        return primary_ == nullptr ? *this : *primary_;
    }
    [[nodiscard]] custom_element_registry & registry_of(value receiver);
    /// A new registry record on the primary, for \p object.
    custom_element_registry & make_registry(dom_bindings * document, value object);
    [[nodiscard]] value create_html_element(context & cx, const std::string & name,
                                            value options = value::undefined(), bool exact = false,
                                            bool prefixed = false, node_ns ns = node_ns::html);
    [[nodiscard]] value construct_custom_element(context & cx, std::size_t definition,
                                                 custom_element_registry * registry, atom tag);
    /// The document parser's custom element hooks; see
    /// tree_builder::set_custom_element_hooks.
    [[nodiscard]] node_id parser_create_custom_element(
        std::string_view local_name, const std::vector<html::token_attribute> & attributes,
        node_id intended_parent);
    void parser_inserted_custom_element(node_id element);
    std::uint32_t markup_insertion_counter_ = 0; // "Throw-on-dynamic-markup-insertion counter".
    [[nodiscard]] std::size_t custom_definition_for(const read_txn & txn, node_id id,
                                                    bool connected = false) const;
    [[nodiscard]] std::size_t custom_definition_of(context & cx, value receiver);
    // Scoped registries: a node's custom element registry (DOM 4.9); see
    // "which registry a node has" in custom_elements.cpp.
    [[nodiscard]] custom_element_registry * document_registry() const noexcept;
    [[nodiscard]] custom_element_registry * registry_of_node(const read_txn & txn, node_id id,
                                                             bool connected = false) const;
    void set_node_registry(const read_txn & txn, node_id id, custom_element_registry * registry);
    [[nodiscard]] bool registry_option(context & cx, value dictionary,
                                       custom_element_registry *& out, bool & given);
    [[nodiscard]] bool shadow_registry_option(context & cx, node_id host, value init);
    void assign_parsed_registries(const read_txn & txn, node_id at,
                                  custom_element_registry * inherited);
    void adopt_registries(const read_txn & txn, node_id at, custom_element_registry * parent,
                          bool counted);
    [[nodiscard]] bool may_upgrade() const noexcept;
    void install_registry_accessors(context & cx);
    /// Records writes the snapshot diff cannot see (`setAttribute(n, sameValue)`)
    /// from mutated()'s write log (custom_element_reactions.cpp).
    void note_attribute_writes(const std::vector<document::write_note> & writes);
    flat_map<std::uint64_t, std::vector<atom>> written_attributes_;
    /// An element's "is value" (DOM 4.9), fixed at creation; not its `is`
    /// attribute, which a page may change afterwards.
    flat_map<std::uint64_t, std::string> is_values_;
    flat_map<std::uint64_t, custom_element_registry *> node_registries_;
    flat_map<std::uint64_t, custom_element_registry *> shadow_registries_; // By host.
    flat_map<std::uint64_t, bool> inert_roots_;
    custom_element_registry * scoped_document_registry_ = nullptr;
    const custom_element_registry * upgrade_filter_ = nullptr;
    /// HTML's "active custom element constructor map", by the prototype a
    /// receiver carries: the registry, and the document creating the element.
    /// Primary only.
    std::vector<std::tuple<std::uint64_t, custom_element_registry *, dom_bindings *>>
        active_constructors_;
    [[nodiscard]] value construct_html_element(context & cx, value receiver,
                                               std::string_view interface_name);
    [[nodiscard]] static std::string_view interface_name_for_tag(std::string_view tag);
    void walk_custom_elements(const read_txn & txn, node_id start, bool connected, bool upgrade,
                              flat_map<std::uint64_t, bool> & roots_seen);
    void scan_custom_elements();
    /// Keeps `loose_watch_` right for one tracked element's state.
    void watch_loose(std::uint64_t key, const custom_element_state & state);
    void flush_custom_element_reactions(std::size_t from);
    void run_upgrade(context & cx, std::size_t definition, node_id target, value wrapper);
    void report_custom_element_exception(context & cx, value thrown, std::string_view where,
                                         value callback = value::undefined());
    /// The window of a callback's realm (a closure's [[Realm]]), or \p otherwise.
    [[nodiscard]] dom_bindings & global_of(value callback, dom_bindings & otherwise);
    [[nodiscard]] value construct_fenced(context & cx, value constructor, bool & threw,
                                         value & thrown);
    void sync_custom_element_roots();

    /// A document with a browsing context has a registry: the page's, and a
    /// frame's once frames.cpp asked for it.
    [[nodiscard]] bool has_browsing_context() const noexcept { return registry_ != nullptr; }

    std::vector<custom_element_definition> custom_definitions_; // Primary only.
    flat_map<std::uint64_t, custom_element_state> custom_elements_;
    flat_map<std::uint64_t, bool> loose_watch_;
    std::uint32_t scan_generation_ = 0;
    std::size_t sweep_at_ = 64; // When the map is this big, sweep the gone nodes.
    std::vector<custom_element_reaction> custom_reactions_;
    std::vector<std::size_t> reaction_floors_; // The flushes in progress, outermost first.
    std::vector<std::pair<dom_bindings *, std::size_t>> adoptees_;
    value construct_fence_;                    // Primary only.
    value custom_elements_registry_prototype_; // Primary only.
    custom_element_registry * registry_ = nullptr;
    std::vector<std::unique_ptr<custom_element_registry>> registries_;
    flat_map<std::uint64_t, custom_element_registry *> registry_objects_;
    script::native_object * custom_elements_interface_ = nullptr;
    void install_element_internals(context & cx, script::object_object & html_element_proto);
    std::function<value(node_id)> face_submission_value_;
    [[nodiscard]] node_id form_owner_of(const read_txn & txn, node_id id) const;
    [[nodiscard]] bool form_control_disabled(const read_txn & txn, node_id id) const;
    value element_internals_prototype_;
    value custom_state_set_prototype_;
    // END element internals

    // BEGIN style sheets (bindings/stylesheets/)
public:
    using css_declaration = style::css::declaration;

    using css_rule_record = binding_detail::css_rule_record;

    using css_sheet_record = binding_detail::css_sheet_record;

    void install_style_sheets(context & cx);
    void install_sheet_property(context & cx, script::object_object & obj, node_id id);

    /// What a `<link>` is to the cascade and the object model; see
    /// ctbrowser/page/author_styles.hpp, which the cascade's walk shares.
    using link_sheet = page::link_sheet;
    [[nodiscard]] static link_sheet link_sheet_state(std::string_view rel, bool disabled_attribute,
                                                     bool explicitly_enabled) {
        return page::link_sheet_state(rel, disabled_attribute, explicitly_enabled);
    }
    [[nodiscard]] bool link_explicitly_enabled(node_id id) const;
    [[nodiscard]] std::string_view preferred_sheet_title();
    [[nodiscard]] static std::string resolve_sheet_href(std::string_view base,
                                                        std::string_view reference) {
        return page::resolve_sheet_href(base, reference);
    }

    /// Where CSSOM edits reach the cascade. insertRule, deleteRule and
    /// replaceSync change the object model only; re-running the cascade is the
    /// browser's business. So the bindings publish the new author style text
    /// through this hook and the browser decides. With no hook installed the
    /// object model is still correct and the render simply does not change.
    /// See bindings/stylesheets/internal.hpp.
    void set_author_styles_hook(std::function<void(std::string)> hook) {
        on_author_styles_ = std::move(hook);
    }
    [[nodiscard]] std::string author_style_text();

private:
    void sync_style_sheets(context & cx);
    void sync_sheet_list(context & cx, node_id from, script::object_object & list,
                         std::vector<std::size_t> * order);
    [[nodiscard]] script::object_object * cssom_internals(context & cx);
    [[nodiscard]] value style_sheet_list(context & cx);
    [[nodiscard]] value sheet_object_of(context & cx, node_id owner);
    [[nodiscard]] value sheet_object_for(context & cx, std::size_t sheet);
    [[nodiscard]] value rule_object_for(context & cx, std::size_t rule);
    [[nodiscard]] value make_sheet_object(context & cx, std::size_t sheet);
    [[nodiscard]] value make_rule_object(context & cx, std::size_t rule);
    [[nodiscard]] std::size_t load_imported_sheet(std::size_t rule, std::string_view href);
    [[nodiscard]] value adopted_sheets_array(context & cx, std::span<value> args);
    /// `shadowRoot.styleSheets`: the tree's own list, held on the root's wrapper.
    [[nodiscard]] value shadow_sheet_list(context & cx, node_id root);
    /// A sheet's `@namespace` declarations, re-derived as `StyleSheetList.item()`
    /// does first.
    [[nodiscard]] std::vector<style::css::namespace_declaration> sheet_namespaces(
        std::size_t sheet) const;
    void resync_sheet_list(context & cx, script::object_object & list);
    [[nodiscard]] value make_rule_list(context & cx, std::span<const std::size_t> rules);
    void refresh_rule_list(context & cx, value list, std::span<const std::size_t> rules);
    [[nodiscard]] value declaration_object(context & cx, std::size_t rule);
    void refresh_declaration_object(context & cx, value declarations);
    void install_stylesheet_prototypes(context & cx);
    void install_stylesheet_collections(context & cx, script::object_object * internals);
    void install_stylesheet_rules(context & cx, script::object_object * internals);
    void install_stylesheet_declarations(context & cx, script::object_object * internals);
    void refresh_cached_rules(context & c, std::span<const std::size_t> rules);
    void declaration_accessor(context & cx, script::object_object * on);
    /// Parses \p css into rule records, replacing whatever the sheet held.
    void parse_sheet_rules(std::size_t sheet, std::string_view css);
    [[nodiscard]] std::size_t parse_one_rule(std::size_t sheet, std::string_view text,
                                             std::string & error);
    /// Called when the CSSOM changed something the cascade would care about.
    void style_sheets_changed();

public:
    /// Counts style_sheets_changed() calls: a CSSOM edit changes what an
    /// element computes to without touching the document, so a cached computed
    /// style compares this beside the document version.
    [[nodiscard]] std::uint64_t style_stamp() const noexcept { return style_generation_; }

    /// Counts the browser's style resolutions (update_css_animations is called
    /// at the end of each). A registered property, a resized viewport or a
    /// media change moves a computed value without a document or CSSOM edit;
    /// this stamp is what tells a cached read that it has.
    [[nodiscard]] std::uint64_t restyle_stamp() const noexcept { return restyle_generation_; }

private:
    std::uint64_t style_generation_ = 0;
    std::uint64_t restyle_generation_ = 0;
    [[nodiscard]] css_sheet_record * receiver_sheet(context & cx);
    [[nodiscard]] css_rule_record * receiver_rule(context & cx);
    [[nodiscard]] std::vector<std::string> * receiver_media(context & cx);
    [[nodiscard]] value media_list_object(context & cx, script::object_object & owner);
    void refresh_media_list(context & cx, value list);
    [[nodiscard]] std::string rule_css_text(const css_rule_record & rule) const;

    std::vector<std::unique_ptr<css_sheet_record>> css_sheets_;
    std::vector<std::unique_ptr<css_rule_record>> css_rule_store_;
    /// Owner node -> sheet index, rebuilt by sync_style_sheets.
    flat_map<std::uint64_t, std::size_t> css_sheet_by_owner_;
    std::vector<std::size_t> css_document_sheets_;
    std::vector<std::uint64_t> enabled_links_;
    std::string css_preferred_title_; // See preferred_sheet_title.
    value cssom_internals_;
    std::function<void(std::string)> on_author_styles_;
    // END style sheets

    // BEGIN selectors (bindings/document/tree_ops.cpp)
public:
    /// The cascade's own engine, so a selector cannot mean one thing in a
    /// stylesheet and another in a script. `query()` runs `style::engine::select`,
    /// the matcher a rule goes through; sharing the browser's engine keeps
    /// `:hover` and the interned atoms the same on both sides.
    ///
    /// Optional: bindings built without a browser (as several unit tests are)
    /// fall back to an engine of their own. Matching needs the atom table and
    /// the traversal state, not the rules, so an engine with no sheets answers
    /// a query just as well.
    void observe_style_engine(style::engine & engine) { selector_engine_ = &engine; }

    [[nodiscard]] node_id find_by_id(const std::string & want);

private:
    [[nodiscard]] style::engine & selector_engine();
    style::engine * selector_engine_ = nullptr;
    std::unique_ptr<style::engine> own_selector_engine_;

    void install_document_as_node(context & cx, script::object_object & doc);
    void install_traversal(context & cx, script::object_object & doc);

    [[nodiscard]] bool is_the_document(value v) const;

    [[nodiscard]] std::string locate_namespace(node_id element, const std::string * prefix);
    /// DOM 4.4, "locate a namespace prefix". Empty means no prefix was found.
    [[nodiscard]] std::string locate_namespace_prefix(node_id element, const std::string & ns);

    void normalize_subtree(node_id root);
    // END selectors

    [[nodiscard]] std::shared_ptr<const paint::bitmap> image_argument(value v);

    [[nodiscard]] value matrix_object(context & cx, const transform & t);

    [[nodiscard]] value canvas_context_object(context & cx, node_id id);
    void install_canvas_2d(context & cx);

    [[nodiscard]] value webgl_context_object(context & cx, node_id id, int version);
    void install_webgl_constants(script::object_object * obj, bool webgl2);
    void install_webgl_methods(context & cx, script::object_object * obj, webgl_context * gl,
                               canvas_context * surface);
    void install_webgl_draw_methods(context & cx, script::object_object * obj, webgl_context * gl,
                                    canvas_context * surface, int width, int height, bool webgl2);

    void resize_webgl_context(node_id id, int width, int height);
    void present_webgl_contexts();

    flat_map<std::uint64_t, std::unique_ptr<webgl_context>> webgl_contexts_;
    flat_map<std::uint64_t, script::object_object *> webgl_objects_;

    static bool apply_canvas_font(canvas_context & canvas, std::string_view font);

    [[nodiscard]] node_id handle_of(value v);

    [[nodiscard]] long long size_attribute(const read_txn & txn, node_id id, std::string_view name,
                                           long long fallback) const;

    [[nodiscard]] std::string text_of(node_id id) const;

    void set_text(node_id id, std::string text);

    // --- Globals ----------------------------------------------------------

    void install_console(context & cx);

    void install_document(context & cx);

    /// `alert` and `location`.
    void install_navigation(context & cx);

    value make_location(context & cx);

    [[nodiscard]] script::object_object * document_object();
    [[nodiscard]] script::object_object * window_object();

    void install_window(context & cx);
    /// The proxy a realm's scripts see as this document's window: over
    /// \p target, with misses read from \p realm (window.cpp).
    [[nodiscard]] value make_window_view(context & cx, value target, script::realm_record & realm);
    /// `addEventListener` and `removeEventListener` on `window` and as bare
    /// globals of the current realm, for this document's window.
    void install_window_listeners(context & cx, script::object_object & window);
    /// `postMessage` on `window`, delivering to this document's window
    /// (window/messaging.cpp).
    void install_post_message(context & cx, script::object_object & window);
    /// A frame document's own browsing context (frames.cpp): its window, its
    /// realm, the scripts its parse left, and the two events a load ends in.
    void install_frame_realm(context & cx, dom_bindings & owner, node_id element);
    /// Answers the parser's "does a definition disable shadow here" (entry.cpp).
    void watch_shadow_disabled();
    std::shared_ptr<int> lifetime_ = std::make_shared<int>(0); // Expires with these bindings.
    /// The `on<event>` accessors of a window over this document's handlers
    /// (events/handler_attributes.cpp).
    void install_window_handlers(context & cx, script::object_object & window);
    void install_handler_accessor(context & cx, std::string_view attribute,
                                  std::span<script::object_object *> where);
    void run_frame_scripts(context & cx);
    void finish_frame_load(context & cx);
    bool frame_load_finished_ = false;
    /// Enters this document's realm until the result goes out of scope;
    /// nothing for a document without one of its own. Taken wherever the
    /// bindings run script, or read a window handler, on the document's behalf.
    [[nodiscard]] std::optional<context::realm_scope> enter_realm(context & cx);
    /// The bindings whose window is \p window: the page's or a frame's, at any
    /// depth, and the page's for anything else. "Report the exception" and
    /// `reportError` fire at one particular global (frames.cpp).
    [[nodiscard]] dom_bindings & bindings_of(value window);
    /// As bindings_of, but null when \p window is no longer a window of this
    /// page (a removed frame's; frames.cpp).
    [[nodiscard]] dom_bindings * find_bindings_of(value window);
    [[nodiscard]] std::vector<node_id> child_navigables(); // frames.cpp
    /// The bindings that made the node wrapper \p wrapper (the page's or a
    /// frame's, at any depth), or null for anything else. A member installed on
    /// a shared interface prototype captures the bindings that installed it
    /// (the page's), and must route a frame's node to the frame's bindings
    /// through this (frames.cpp).
    [[nodiscard]] dom_bindings * bindings_owning(value wrapper);
    /// A subresource URL as the asset registry names it (frames.cpp).
    [[nodiscard]] std::string subresource_name(std::string_view src) const;

    // --- Images and fetch --------------------------------------------------

    void install_resources(context & cx);

    [[nodiscard]] script::object_object * install_performance(context & cx);

    using pending_fetch = binding_detail::pending_fetch;
    std::vector<pending_fetch> fetches_;

    using pending_image = binding_detail::pending_image;
    std::vector<pending_image> image_loads_;

    using pending_frame = binding_detail::pending_frame;
    std::vector<pending_frame> frame_loads_;

    using frame_entry = binding_detail::frame_entry;
    std::vector<frame_entry> frames_;
    bool frames_dirty_ = true;
    bool reconciling_ = false; // reconcile_frames is running on this document.
    std::vector<planned_navigation> planned_navigations_; // Primary only.
    /// A frame document's <iframe>: the bindings it is in and the element
    /// (set by load_frame).
    dom_bindings * frame_owner_ = nullptr;
    node_id frame_element_;
    [[nodiscard]] std::pair<dom_bindings *, node_id> choose_navigable(std::string_view target);
    void navigate_frame(dom_bindings & navigable, node_id element, form_navigation navigation);
    void install_window_open(context & cx, script::object_object & window);
    void install_history(context & cx, script::object_object & window);
    void install_dom_parser(context & cx);
    const request_handler * request_handler_ = nullptr; // Primary only.
    [[nodiscard]] std::optional<resource_reply> ask_request_handler(resource_request request);
    /// Returns the bindings over the frame's document (null when no wrapper).
    dom_bindings * load_frame(context & cx, node_id id, const std::string & src,
                              const resource_request * request = nullptr);
    void settle_frame(context & cx, const pending_frame & waiting);
    bool connect_blank_frame(node_id id, const std::string & source);
    void forget_frame_load(node_id id);
    dom_bindings * start_frame(node_id id, const std::string & source);
    [[nodiscard]] bool may_nest(const std::string & source) const;
    [[nodiscard]] std::string frame_source(const read_txn & txn, node_id id) const;
    [[nodiscard]] std::string fallback_base_url();
    void connect_inserted_frames(const std::vector<document::write_note> & writes);
    [[nodiscard]] static std::string_view mime_for_path(std::string_view path);

    using read_kind = binding_detail::read_kind;

    using pending_read = binding_detail::pending_read;
    std::vector<pending_read> reads_;
    void settle_read(context & cx, const pending_read & waiting);

    void settle_image(context & cx, const pending_image & waiting);

    /// Queues an image load. \p promise is undefined unless decode() asked
    /// for it.
    void begin_image_load(value target, node_id id, std::string url, value promise);

    void install_image_views(context & cx, script::object_object & obj, node_id id);

    void settle_fetch(context & cx, const pending_fetch & waiting);

    [[nodiscard]] value fetch_now(context & cx, const std::string & url);

    using loaded_resource = binding_detail::loaded_resource;
    [[nodiscard]] loaded_resource load_resource(const std::string & url);
    void install_xhr(context & cx);

    [[nodiscard]] static value make_rejection(context & cx, const std::string & message);

    [[nodiscard]] value make_response(context & cx, const std::string & url, int status,
                                      const std::string & content_type,
                                      std::vector<std::byte> body);

    void install_timers(context & cx);
    void install_image_bitmap(context & cx); // window/image_bitmap.cpp
    /// User activation (HTML 6.4.3; window/activation.cpp).
    void install_user_activation(context & cx, script::object_object & navigator);
    void each_window_in_tree(const std::function<void(dom_bindings &)> & visit);
    double last_activation_ = std::numeric_limits<double>::infinity();

    [[nodiscard]] std::uint32_t add_timer(value callback, double delay_ms, bool repeating,
                                          timer_source source = timer_source::host);

    // --- Events -----------------------------------------------------------

    [[nodiscard]] value make_event(context & cx, std::string_view type, node_id target);
    [[nodiscard]] value make_event_object(context & cx, std::string_view type, bool bubbles,
                                          bool cancelable);
    [[nodiscard]] bool default_passive_value(std::string_view type, const path_step & target);
    bool dispatch_error_value(std::string_view message, value error,
                              std::string_view script_src = {});
    bool error_reporting_ = false; // HTML 8.1.3.3 "in error reporting mode".
    [[nodiscard]] value make_mouse_event(context & cx, std::string_view type, node_id target,
                                         const input_event & input, bool pointer);
    [[nodiscard]] bool has_activation_behavior(const read_txn & txn, node_id node) const;
    void run_activation_behavior(context & cx, node_id target);
    void install_event_interfaces(context & cx);
    bool dispatch_to(value event, path_step at);
    [[nodiscard]] std::vector<path_step> propagation_path(path_step at,
                                                          bool composed = false) const;
    [[nodiscard]] value object_of_step(context & cx, path_step step);
    [[nodiscard]] path_step step_of(value self);

    [[nodiscard]] static bool prevented(value event);

    void fire_at(path_step step, std::string_view type, value event, bool capturing);

    bool fire_handler_property(value target, std::string_view type, value event,
                               value * thrown = nullptr);
    [[nodiscard]] value value_of_wrapper(node_id id) const;

    // --- Lookups ----------------------------------------------------------

    [[nodiscard]] node_id find_by_tag(std::string_view tag);
    /// Every element with this tag, in document order; "*" means all of them.
    [[nodiscard]] std::vector<node_id> all_by_tag(std::string_view tag);
    [[nodiscard]] std::vector<node_id> all_by_class(node_id root,
                                                    const std::vector<std::string> & tokens);
    [[nodiscard]] std::vector<node_id> all_by_name(std::string_view name);

    [[nodiscard]] node_id first_html_element(std::string_view local);
    [[nodiscard]] std::vector<node_id> all_html_elements(std::string_view local);
    [[nodiscard]] node_id title_element();
    [[nodiscard]] node_id body_element();

    [[nodiscard]] value make_html_document(context & cx, const std::string * title);
    [[nodiscard]] value make_xml_document(context & cx, std::string_view ns,
                                          std::string_view qualified_name,
                                          bool as_xml_document = true);
    /// Gives a script-made document its maker's URL (HTML 8.6.2, DOMParser).
    void take_url_of(context & cx, const dom_bindings & maker);
    [[nodiscard]] dom_bindings * owner_of(value v);
    [[nodiscard]] bool is_a_document(value v) const;
    void mark_roots(const script::context::root_visitor & mark) const;
    void adopt_interfaces_of(const dom_bindings & primary);
    void install_tree_accessors(context & cx, script::object_object & doc);
    [[nodiscard]] std::vector<node_id> named_document_items(std::string_view name);
    [[nodiscard]] std::vector<std::string> document_property_names();
    [[nodiscard]] value make_document_proxy(context & cx, value target);
    [[nodiscard]] value make_live_collection(context & cx,
                                             std::function<std::vector<node_id>()> members,
                                             std::string_view interface_name = "HTMLCollection");
    [[nodiscard]] std::vector<node_id> query(std::string_view selector, node_id within = node_id{},
                                             bool * invalid = nullptr, bool first_only = false);

    document * doc_;
    atom_table * atoms_;
    canvas_store * canvases_;
    form_store * forms_;
    std::function<void()> on_mutation_;
    bool settling_types_ = false; // mutated() is settling <input> types (see it).
    std::function<void(node_id)> on_focus_;
    std::function<void(const std::string &)> on_alert_;
    std::function<void(node_id)> on_activate_;
    flat_map<std::uint64_t, std::string> namespaces_;
    flat_map<std::uint64_t, script::object_object *> wrappers_;
    flat_map<std::uint64_t, std::vector<node_id>> manual_slots_;
    flat_map<std::uint64_t, std::vector<node_id>> slot_assignments_;
    std::vector<node_id> signal_slots_;
    std::vector<node_id> slot_roots_dirty_; // slot.assign() named these trees.
    void signal_slot_changes(const std::vector<document::write_note> & writes);
    void fire_signalled_slots();
    flat_map<std::uint64_t, std::pair<std::string, std::string>> nonce_slots_;
    bool wrote_to_control_ = false;
    std::size_t dispatch_depth_ = 0;
    /// What the browser last reported has focus, for document.activeElement.
    node_id focused_;
    std::string location_href_;
    std::string location_hash_;
    /// The element the fragment names (`:target`); see observe_location.
    node_id target_element_;
    value dom_exception_prototype_;
    value css_interface_;
    value location_;
    value document_;
    value document_target_;
    value window_;
    /// The proxy this document's scripts call `window`, which is
    /// `currentTarget` at the window step. The page's is its global object; a
    /// frame's is its own realm's.
    value window_view_;
    /// The realm this document's scripts run in. Null for the page, which uses
    /// the context's main realm, and for a document without a browsing
    /// context; a frame that runs its own scripts has one of its own.
    script::realm_record * realm_ = nullptr;
    std::vector<std::unique_ptr<document>> owned_documents_;
    std::vector<std::unique_ptr<dom_bindings>> secondary_documents_;
    bool secondary_ = false;
    /// The top-level page's bindings (the primary); null when this is the
    /// page. Members marked "Primary only" are used only on that instance.
    dom_bindings * primary_ = nullptr;
    std::string content_type_;
    /// `document.cookie`, in insertion order so reading it back is stable.
    std::vector<std::pair<std::string, std::string>> cookies_;
    std::uint32_t next_object_url_ = 0;
    std::vector<std::pair<std::string, std::string>> object_url_types_;
    value blob_prototype_;
    [[nodiscard]] value make_blob(context & cx, value bytes, std::string_view type);

    value event_prototype_;
    value custom_event_prototype_;
    value message_event_prototype_; // What postMessage fires.
    value mouse_event_prototype_;
    value pointer_event_prototype_;
    value event_target_prototype_;
    /// AbortSignal.prototype, marked as a root like the other prototypes.
    value abort_signal_prototype_;
    value media_query_list_prototype_;
    value canvas2d_prototype_;
    value webgl_prototype_;
    value webgl2_prototype_;
    std::string callback_error_;
    bool reload_requested_ = false;
    asset_registry * assets_ = nullptr;
    image_store * images_ = nullptr;
    bool network_allowed_ = true;
    context * cx_ = nullptr;
    const layout::fragment * fragments_ = nullptr;
    const style::style_map * styles_ = nullptr;
    const layout::box_node * boxes_ = nullptr;
    int viewport_width_ = 0;
    int viewport_height_ = 0;
    // The scroll state; see set_viewport_scroll_hooks.
    flat_map<std::uint64_t, point> element_scrolls_;
    point viewport_scroll_;
    std::function<point()> viewport_scroll_get_;
    std::function<void(point)> viewport_scroll_set_;
    std::vector<node_id> pending_scroll_targets_;
    bool scroll_events_queued_ = false;
    std::vector<value> pending_media_changes_;
    bool media_changes_queued_ = false;
    double now_ms_ = 1;

    using performance_entry = binding_detail::performance_entry;
    std::vector<performance_entry> performance_entries_;

    listener_registry<binding_detail::listener_payload> listeners_;
    /// Payloads follow timer_schedule_ in registration order. A cancelled entry
    /// stays until the current run_timers pass ends, so its callback stays
    /// rooted while that pass runs.
    std::vector<timer> timers_;
    timer_queue timer_schedule_;

    using animation_frame_callback = binding_detail::animation_frame_callback;
    std::vector<animation_frame_callback> animation_callbacks_;
    std::vector<std::uint32_t> cancelled_frames_; // Cancelled during this frame's run.
    std::vector<std::string> console_;
    std::uint32_t next_timer_id_ = 0;

    value listener_fence_;
    value listener_invoke_;
    void install_listener_fence(context & cx, script::native_object & keeper);
    void compile_listener_fence(context & cx);
    /// The native that keeps both alive: the EventTarget constructor.
    script::native_object * fence_keeper_ = nullptr;
    [[nodiscard]] bool invoke_listener(context & cx, value callback, value receiver, value args,
                                       value & thrown, value & returned);

    void install_event_handler_attributes(context & cx);
    [[nodiscard]] value event_handler_get(context & cx, value self, const std::string & name);
    void event_handler_set(context & cx, value self, const std::string & name, value given);
    void activate_event_handler(context & cx, path_step at, std::string_view type);
    void deactivate_event_handler(path_step at, std::string_view type);
    [[nodiscard]] bool has_handler_listener(path_step at, std::string_view type) const;
    void settle_attribute_writes(const std::vector<document::write_note> & writes);
    void settle_form_writes(const std::vector<document::write_note> & writes); // control_methods/
    [[nodiscard]] value compile_handler_attribute(context & cx, value self,
                                                  const std::string & name);

    // --- Shadow DOM adapters over the owning document ---
    using shadow_tree = document::shadow_tree;

    [[nodiscard]] node_id shadow_root_of(node_id host) const;
    [[nodiscard]] const shadow_tree * shadow_tree_of(node_id root) const;
    [[nodiscard]] value attach_shadow(context & cx, node_id host, std::span<value> args);
    void install_shadow_root_members(context & cx, script::object_object & obj, node_id root);
    void install_xml_serializer(context & cx);
    [[nodiscard]] std::string serialize_xml(node_id node, std::string_view inherited) const;
    void install_shadow_dom(context & cx);
    /// `innerHTML` plus the declarative shadow roots, and the copy that carries
    /// them across.
    void set_html_unsafe(node_id target, std::string_view markup, bool run_scripts = false);
    void copy_shadow_trees(const document & src, const read_txn & from, node_id source,
                           node_id made);
    [[nodiscard]] node_id root_of_tree(const read_txn & txn, node_id from, bool composed) const;

    [[nodiscard]] node_id body_or_frameset_of(value self);
    /// Whether \p name is a window-forwarded handler on a body or frameset of a
    /// windowless document.
    [[nodiscard]] bool windowless_forward(value self, std::string_view name);
    void refresh_forwarded_handler(context & cx, node_id element, const std::string & name);
    flat_map<std::string, node_id> forwarded_from_;
    std::vector<node_id> bodies_; // Every wrapped body/frameset, rebuilt when a wrapper is made.
    std::size_t bodies_scanned_at_ = static_cast<std::size_t>(-1);
    [[nodiscard]] dom_bindings & target_owner(value self);

public:
    void set_current_script(node_id script);
    void set_ready_state(std::string_view state);
    bool dispatch_focus(std::string_view type, node_id target, node_id related);
    /// Fires `hashchange` at the window: a HashChangeEvent with both addresses.
    bool dispatch_hash_change(const std::string & old_url, const std::string & new_url);

    /// Time as a script observes it: `performance.now()` and an event's
    /// timeStamp. The engine's clock moves only between ticks, so without this
    /// two readings within a script would be equal and
    /// `while (performance.now() < t)` would never end (WPT
    /// Event-timestamp-safe-resolution.html spins until two events differ).
    /// Each observation adds 0.005 ms (5 us, the resolution browsers coarsen
    /// to) counted from the tick's start, so the value is still a function of
    /// the page's own behaviour and goldens stay deterministic. The first
    /// reading of a tick is the clock itself; an event the engine makes reads
    /// the clock, not this.
    [[nodiscard]] double observed_now() {
        return now_ms_ + 0.005 * static_cast<double>(time_reads_++);
    }
    std::uint64_t time_reads_ = 0;
    void install_frame_accessors(context & cx);
    void install_element_reflection(context & cx);
    void install_double_reflection(context & cx);
    /// `option.label` and `option.value`, which fall back to the option's text.
    void install_option_reflection(context & cx);
    [[nodiscard]] value element_reference_get(context & cx, std::string_view idl,
                                              std::string_view content, bool list);
    void element_reference_set(context & cx, std::string_view idl, std::string_view content,
                               bool list, value given);
    [[nodiscard]] bool element_reference_in_scope(const read_txn & txn, node_id element,
                                                  node_id candidate) const;
    [[nodiscard]] node_id element_reference_by_id(const read_txn & txn, node_id element,
                                                  std::string_view id) const;

    /// The layout flush. A box read from script (offsetX of a dispatched click,
    /// getBoundingClientRect) must see the layout as the script left the
    /// document, which before the first frame is no layout at all. The browser
    /// installs the same flush its getComputedStyle wrapper uses; anything
    /// reading `box_of` calls flush_layout() first. Only what is stale runs.
    void set_layout_hook(std::function<void()> hook) { flush_layout_ = std::move(hook); }
    static void install_iterable_declaration(context & cx, script::object_object & proto,
                                             bool named_only);

    /// Where the viewport's scroll position lives for this document. For the
    /// page it is the browser's. With no hooks set, as for a frame's document,
    /// the bindings keep their own in `viewport_scroll_`.
    void set_viewport_scroll_hooks(std::function<point()> get, std::function<void(point)> set) {
        viewport_scroll_get_ = std::move(get);
        viewport_scroll_set_ = std::move(set);
    }
    void queue_scroll_event(node_id target);
    void fire_scrollend(context & cx, std::span<const node_id> scrolled);
    void report_media_query_changes();

    /// Runs the layout hook. A frame's document has no hook of its own and
    /// flushes through the primary's, whose flush lays out every frame whose
    /// document moved (frames_stale); otherwise a frame's `scrollWidth` read
    /// right after an innerHTML write would answer from no layout at all.
    void flush_layout() {
        if (flush_layout_) {
            flush_layout_();
        } else if (primary_ != nullptr && primary_->flush_layout_) {
            primary_->flush_layout_();
        }
    }
    std::function<void()> flush_layout_;
    std::vector<node_id> unstarted_scripts_;
    void run_inserted_scripts();
    void execute_script_element(context & cx, node_id id, const std::string & source);

public:
    void note_unstarted_script(node_id id) { unstarted_scripts_.push_back(id); }

private:
    void install_range(context & cx);
    [[nodiscard]] value create_range(context & cx);
    void register_live_range(value range);
    void settle_live_ranges(const std::vector<document::write_note> & writes);
    void split_live_ranges(node_id node, node_id made, double offset, node_id parent,
                           double made_index);
    void absorb_live_ranges(node_id current, node_id parent, double index, node_id node,
                            double length);

    using live_boundary = binding_detail::live_boundary;
    void each_live_boundary(const std::function<void(const live_boundary &)> & fn);
    void move_live_boundary(const live_boundary & at, node_id node, double offset);
    std::vector<value> live_ranges_; // Primary only.
    void install_selection(context & cx);
    void place_caret_on_focus(node_id element);   // selection.cpp
    void forget_selection_under(node_id removed); // selection.cpp
    flat_map<std::uint64_t, script::object_object *> adopted_away_;
    bool adopting_ = false; // node_from is cloning for adoption: copy every shadow root.
    flat_map<std::string, value> named_collections_;
    std::vector<node_id> moved_by_mutation_;
    bool moving_ = false;
    node_id move_root_;           // The node moveBefore is moving, while moving_ (tree_ops.cpp).
    bool step_in_shadow_ = false; // The dispatch step fire_at invokes is in a shadow tree.

public:
    [[nodiscard]] std::span<const node_id> moved_by_mutation() const { return moved_by_mutation_; }

private:
    void install_form_owner(context & cx);
    [[nodiscard]] unsigned foreign_document_position(value given);
    void bind_attr_object(context & cx, script::object_object & attr, node_id owner,
                          const attribute & held);
    [[nodiscard]] attribute attribute_of_object(context & cx, value given);
    [[nodiscard]] value clone_attr_object(context & cx, value given);
    [[nodiscard]] std::string outer_html(node_id target) const;
    void set_outer_html(context & cx, node_id target, std::string_view markup);
    [[nodiscard]] std::string serialize_html(node_id target, bool outer,
                                             bool serializable_shadow_roots = false,
                                             const std::vector<node_id> & shadow_roots = {}) const;
    [[nodiscard]] bool validate_and_extract_element(context & cx, std::string_view where,
                                                    const std::string & ns,
                                                    const std::string & qualified);
    flat_map<std::uint64_t, std::vector<std::pair<std::string, script::object_object *>>>
        attr_objects_;
    void forget_attr_object(node_id owner, std::string_view ns, std::string_view local);
    [[nodiscard]] value parse_from_string(context & cx, std::string_view markup,
                                          std::string_view type);
    dom_bindings & adopt_second_document(context & cx, document & fresh);
    /// NamedNodeMap's members, on its prototype; see element/attributes.cpp.
    void install_named_node_map(context & cx);

    using replace_all_note = binding_detail::replace_all_note;
    std::optional<replace_all_note> replace_all_;

public:
    using parser_script = binding_detail::parser_script;
    /// Runs one parser-inserted script for the browser. Returns false when the
    /// page is being replaced, and the parser stops reading.
    using script_runner = std::function<bool(const parser_script &)>;

    void set_script_runner(script_runner run) { run_script_ = std::move(run); }
    /// Parses \p html into the document, running its scripts as the parser
    /// reaches them (bindings/document/write.cpp).
    [[nodiscard]] parse_result parse_document(std::string_view html);

private:
    std::unique_ptr<html::tree_builder> parser_;
    /// Parsers document.open() replaced while one of their calls was still on
    /// the stack (a mutation observer's document.write, at the checkpoint
    /// before a <script> is prepared): aborted, and freed once not busy().
    std::vector<std::unique_ptr<html::tree_builder>> retired_parsers_;
    void replace_parser();
    script_runner run_script_;
    std::vector<parser_script> deferred_scripts_;
    std::vector<parser_script> asap_scripts_;
    /// "Prepare the script element" for one the parser just closed.
    void prepare_parser_script(node_id script);
    void parser_finished(bool initial);
    bool parser_script_created_ = false;
    void install_dynamic_markup(context & cx, script::object_object & doc);
    void document_open(context & cx);
    void document_write(context & cx, std::span<value> args, bool newline);
    void document_close(context & cx);
};

} // namespace ctbrowser::shell
