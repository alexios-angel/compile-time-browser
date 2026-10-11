//===- Events.hpp - Native event listeners ----------------------*- C++ -*-===//
//
// addEventListener for generated native code, the Event a listener receives,
// and the dispatch a native host runs when an event reaches a page element.
// The registrations are kept by ctbrowser's own listener_registry (the
// bookkeeping the VM's bindings use), with the listener's C++ callable as the
// payload: the table owns each listener, and the captures bound into it,
// until the program ends or the host forgets the document.
//
// Only programs that register listeners include this header.
//
//===----------------------------------------------------------------------===//

#pragma once

#ifndef CTNATIVE_DOM
#error "Define CTNATIVE_DOM before including the native event listeners"
#endif

#include "Browser.hpp"

#include <ctbrowser/shell/listener_registry.hpp>

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace ctnative {

struct window_object;

/// The Event a native listener receives (DOM §2.2) while the host dispatches
/// it: a view of the dispatch's own state, so preventDefault and
/// stopPropagation reach the dispatch. It is one pointer and owns nothing, so
/// generated listeners take it by value. It is valid only during the listener
/// call; the native lowering admits a listener's event parameter only as the
/// receiver of the reads and calls below, so no copy outlives that call.
class js_event {
public:
    /// What one dispatch shares with every listener it calls.
    struct state {
        std::string type;
        js_element_t target;
        std::optional<js_element_t> current_target;
        bool cancelable = true;
        bool canceled = false;
        bool stop_propagation = false;
    };

    explicit js_event(state & shared) noexcept : shared_(&shared) {}

    /// Event.type, .target and .currentTarget (DOM §2.2). The target is a page
    /// element, since the host dispatches only at elements. The current target
    /// is the element whose listener runs; a listener on the document or the
    /// window has none here, and the lowering refuses its currentTarget read.
    [[nodiscard]] js_string type() const { return js_string{shared_->type}; }
    [[nodiscard]] js_element_t target() const { return shared_->target; }
    [[nodiscard]] js_element_t currentTarget() const { return shared_->current_target.value(); }
    /// Event.defaultPrevented: whether a listener canceled the event.
    [[nodiscard]] js_boolean_t defaultPrevented() const { return js_boolean_t{shared_->canceled}; }

private:
    friend struct prevent_default_method;
    friend struct stop_propagation_method;
    state * shared_;
};

/// Event.prototype.preventDefault (DOM §2.2): cancels a cancelable event.
struct prevent_default_method {
    undefined_t call(const js_event & event) const {
        if (event.shared_->cancelable) { event.shared_->canceled = true; }
        return {};
    }
};
/// Event.prototype.stopPropagation (DOM §2.2): the dispatch stops after the
/// current element's listeners.
struct stop_propagation_method {
    undefined_t call(const js_event & event) const {
        event.shared_->stop_propagation = true;
        return {};
    }
};
struct event_prototype {
    prevent_default_method preventDefault;
    stop_propagation_method stopPropagation;
};
struct event_constructor {
    event_prototype prototype;
};
inline constexpr event_constructor Event{};

/// The event listeners a native program registered, per document. One table
/// per program (event_listeners::program()), as a program is one realm.
///
/// Each listener is a std::move_only_function the table owns through a
/// unique_ptr, so the callable stays at one address while it runs even if it
/// registers another listener. Nothing removes a listener yet, since
/// removeEventListener is not native: a listener lives until forget() or the
/// end of the program.
class event_listeners {
public:
    /// A listener as the table calls it, with the event being dispatched.
    using listener = std::move_only_function<void(const js_event &)>;

    /// A listener as generated code passes it: one that takes the event, or
    /// one that declares no parameter.
    template <class F> static listener wrap(F && callback) {
        if constexpr (std::invocable<F &, const js_event &>) {
            return listener{std::forward<F>(callback)};
        } else {
            return listener{[run = std::forward<F>(callback)](const js_event &) mutable { run(); }};
        }
    }

    /// The table every native call registers into.
    static event_listeners & program() {
        static event_listeners table;
        return table;
    }

    /// EventTarget.addEventListener(type, callback, capture) (DOM §2.7) on an
    /// element, not once and not passive. Each call adds a new listener, since
    /// every native callback is a fresh function object (the lowering admits
    /// only a closure created for this call), so the duplicate check of step 4
    /// never matches.
    void add(const js_element_t & target, std::string type, listener callback, bool capture) {
        const ctbrowser::element_ref element = target.value();
        add(*element.owner, {ctbrowser::shell::event_target_kind::node, element.id.key()}, target,
            std::move(type), std::move(callback), capture);
    }
    /// The same on `document`'s Document node or its window, which are past
    /// the document element on every element's propagation path.
    void add(const ctbrowser::document & document, ctbrowser::shell::event_target_kind kind,
             std::string type, listener callback, bool capture) {
        add(document, {kind, 0}, std::nullopt, std::move(type), std::move(callback), capture);
    }

    /// Dispatches an event of `type` at `target` (DOM §2.9), as the host does
    /// for a click. The path is the target, its ancestor elements, the
    /// document and the window. Capturing listeners run from the window down
    /// to the target's parent, then at the target its capturing listeners and
    /// then its others, then, when the event bubbles, the others of each step
    /// back up to the window. The dispatch stops after the step in which a
    /// listener called stopPropagation. Each step's listeners are the ones
    /// registered when the step begins, in registration order. A listener
    /// that throws is reported (reported()) and the rest still run, as the VM
    /// reports an exception and continues. Returns how many listeners ran;
    /// `canceled`, when given, is set to whether a listener canceled the
    /// event.
    ///
    /// ponytail: no shadow-tree retargeting and no passive or once listeners,
    /// since none can be registered natively yet; the VM's propagation path in
    /// dom_bindings is the one to lift when they can.
    std::size_t dispatch(const js_element_t & target, std::string_view type, bool bubbles = true,
                         bool cancelable = true, bool * canceled = nullptr) {
        using ctbrowser::shell::event_target_kind;
        const ctbrowser::element_ref element = target.value();
        const auto found = documents_.find(element.owner);
        if (found == documents_.end()) { return 0; }
        per_document & listeners = found->second;
        // The path from the target outwards.
        std::vector<ctbrowser::shell::listener_target> path;
        {
            const auto txn = element.owner->read();
            for (ctbrowser::node_id at = element.id; at; at = txn.parent(at)) {
                path.push_back({event_target_kind::node, at.key()});
            }
        }
        path.push_back({event_target_kind::document, 0});
        path.push_back({event_target_kind::window, 0});
        js_event::state shared{std::string{type}, target, std::nullopt, cancelable};
        const js_event event{shared};
        std::size_t ran = 0;
        // Runs one step's listeners of one phase; false once propagation
        // stopped.
        const auto step = [&](const ctbrowser::shell::listener_target & at, bool capture) {
            for (const std::uint64_t id : listeners.registry.snapshot(at, type, capture)) {
                const auto * entry = listeners.registry.begin(at, type, id, capture);
                if (entry == nullptr) { continue; }
                shared.current_target = entry->payload.current_target;
                // The payload's address survives the listener adding another.
                listener & callback = *entry->payload.callback;
                ++ran;
                try {
                    callback(event);
                } catch (const std::exception & thrown) { reported_.emplace_back(thrown.what()); }
            }
            return !shared.stop_propagation;
        };
        bool going = true;
        for (std::size_t i = path.size(); going && i-- > 1;) { going = step(path[i], true); }
        going = going && step(path[0], true) && step(path[0], false);
        for (std::size_t i = 1; going && bubbles && i < path.size(); ++i) {
            going = step(path[i], false);
        }
        if (canceled != nullptr) { *canceled = shared.canceled; }
        return ran;
    }

    /// Drops every listener registered on `document`, its nodes and its
    /// window. A host calls it before destroying a document with listeners.
    void forget(const ctbrowser::document & document) { documents_.erase(&document); }

    /// The messages of the exceptions listeners threw, oldest first.
    [[nodiscard]] const std::vector<std::string> & reported() const noexcept { return reported_; }

private:
    /// One registration: the element it is on, which is the event's
    /// currentTarget while it runs (none for the document and the window), and
    /// the listener.
    struct registration {
        std::optional<js_element_t> current_target;
        std::unique_ptr<listener> callback;
    };
    struct per_document {
        ctbrowser::shell::listener_registry<registration> registry;
        std::uint64_t next_callback = 0;
    };
    std::unordered_map<const ctbrowser::document *, per_document> documents_;
    std::vector<std::string> reported_;

    void add(const ctbrowser::document & document, ctbrowser::shell::listener_target target,
             std::optional<js_element_t> current_target, std::string type, listener callback,
             bool capture) {
        per_document & listeners = documents_[&document];
        ctbrowser::shell::listener_registry<registration>::entry made;
        made.target = target;
        made.type = std::move(type);
        made.callback_id = ++listeners.next_callback;
        made.capture = capture;
        made.payload = registration{std::move(current_target),
                                    std::make_unique<listener>(std::move(callback))};
        (void)listeners.registry.add(std::move(made));
    }
};

/// EventTarget.prototype.addEventListener (DOM §2.7), as generated code spells
/// it: `ctnative::EventTarget.prototype.addEventListener.call(el, type, f)`,
/// with `ctnative::document` or `ctnative::window` for `el` on those targets,
/// and an optional fourth argument, the capture flag. `f` takes the event or
/// declares no parameter.
struct add_event_listener_method {
    template <class F>
    requires std::invocable<F &, const js_event &> || std::invocable<F &>
    undefined_t call(const js_element_t & target, const js_string & type, F && callback,
                     js_boolean_t capture = js_boolean_t{}) const {
        event_listeners::program().add(target, std::string{type.value()},
                                       event_listeners::wrap(std::forward<F>(callback)),
                                       static_cast<bool>(capture));
        return {};
    }
    template <class F>
    requires std::invocable<F &, const js_event &> || std::invocable<F &>
    undefined_t call(const document_object &, const js_string & type, F && callback,
                     js_boolean_t capture = js_boolean_t{}) const {
        event_listeners::program().add(
            page_document(), ctbrowser::shell::event_target_kind::document,
            std::string{type.value()}, event_listeners::wrap(std::forward<F>(callback)),
            static_cast<bool>(capture));
        return {};
    }
    template <class F>
    requires std::invocable<F &, const js_event &> || std::invocable<F &>
    undefined_t call(const window_object &, const js_string & type, F && callback,
                     js_boolean_t capture = js_boolean_t{}) const {
        event_listeners::program().add(
            page_document(), ctbrowser::shell::event_target_kind::window, std::string{type.value()},
            event_listeners::wrap(std::forward<F>(callback)), static_cast<bool>(capture));
        return {};
    }

private:
    /// The document of the active document scope, through its element.
    static const ctbrowser::document & page_document() {
        return *require_document_element().value().owner;
    }
};

struct event_target_prototype {
    add_event_listener_method addEventListener;
};
struct event_target_constructor {
    event_target_prototype prototype;
};
inline constexpr event_target_constructor EventTarget{};

} // namespace ctnative
