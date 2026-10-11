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

/// The Event a native listener receives (DOM §2.2) while the host dispatches
/// it: a view of the dispatch's own state, so preventDefault and
/// stopPropagation reach the dispatch. It is valid only during the listener
/// call; the native lowering admits a listener's event parameter only as the
/// receiver of the reads and calls below, so it never outlives that call.
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

    /// Event.type, .target and .currentTarget (DOM §2.2). The target and the
    /// current target are page elements, since only listeners on elements are
    /// native.
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

    /// EventTarget.addEventListener(type, callback) (DOM §2.7) with no
    /// options: a bubbling-phase listener, not once and not passive. Each call
    /// adds a new listener, since every native callback is a fresh function
    /// object (the lowering admits only a closure created for this call), so
    /// the duplicate check of step 4 never matches.
    void add(const js_element_t & target, std::string type, listener callback) {
        const ctbrowser::element_ref element = target.value();
        per_document & listeners = documents_[element.owner];
        ctbrowser::shell::listener_registry<registration>::entry made;
        made.target = {ctbrowser::shell::event_target_kind::node, element.id.key()};
        made.type = std::move(type);
        made.callback_id = ++listeners.next_callback;
        made.payload = registration{target, std::make_unique<listener>(std::move(callback))};
        (void)listeners.registry.add(std::move(made));
    }

    /// Dispatches an event of `type` at `target` (DOM §2.9), as the host does
    /// for a click: the target, then, when it bubbles, each ancestor element
    /// up to the document element, stopping after the step in which a
    /// listener called stopPropagation. Each step's listeners are the ones
    /// registered when the step begins, in registration order. A listener
    /// that throws is reported (reported()) and the rest still run, as the VM
    /// reports an exception and continues. Returns how many listeners ran;
    /// `canceled`, when given, is set to whether a listener canceled the
    /// event.
    ///
    /// ponytail: element steps only, with no capture phase, since only
    /// bubbling listeners on elements can be registered natively. The
    /// document and window steps, capture, stopPropagation and shadow-tree
    /// retargeting arrive with the first native registration that needs them;
    /// the VM's propagation path in dom_bindings is the one to lift then.
    std::size_t dispatch(const js_element_t & target, std::string_view type, bool bubbles = true,
                         bool cancelable = true, bool * canceled = nullptr) {
        const ctbrowser::element_ref element = target.value();
        const auto found = documents_.find(element.owner);
        if (found == documents_.end()) { return 0; }
        per_document & listeners = found->second;
        std::vector<ctbrowser::node_id> path;
        {
            const auto txn = element.owner->read();
            for (ctbrowser::node_id at = element.id; at; at = txn.parent(at)) {
                path.push_back(at);
                if (!bubbles) { break; }
            }
        }
        js_event::state shared{std::string{type}, target, std::nullopt, cancelable};
        const js_event event{shared};
        std::size_t ran = 0;
        for (const ctbrowser::node_id at : path) {
            const ctbrowser::shell::listener_target step{ctbrowser::shell::event_target_kind::node,
                                                         at.key()};
            for (const std::uint64_t id : listeners.registry.snapshot(step, type, false)) {
                const auto * entry = listeners.registry.begin(step, type, id, false);
                if (entry == nullptr) { continue; }
                shared.current_target = entry->payload.current_target;
                // The payload's address survives the listener adding another.
                listener & callback = *entry->payload.callback;
                ++ran;
                try {
                    callback(event);
                } catch (const std::exception & thrown) { reported_.emplace_back(thrown.what()); }
            }
            if (shared.stop_propagation) { break; }
        }
        if (canceled != nullptr) { *canceled = shared.canceled; }
        return ran;
    }

    /// Drops every listener registered on `document`'s nodes. A host calls it
    /// before destroying a document whose elements have listeners.
    void forget(const ctbrowser::document & document) { documents_.erase(&document); }

    /// The messages of the exceptions listeners threw, oldest first.
    [[nodiscard]] const std::vector<std::string> & reported() const noexcept { return reported_; }

private:
    /// One registration: the element it is on, which is the event's
    /// currentTarget while it runs, and the listener.
    struct registration {
        /// Always set; optional only because the registry default-constructs
        /// its entries.
        std::optional<js_element_t> current_target;
        std::unique_ptr<listener> callback;
    };
    struct per_document {
        ctbrowser::shell::listener_registry<registration> registry;
        std::uint64_t next_callback = 0;
    };
    std::unordered_map<const ctbrowser::document *, per_document> documents_;
    std::vector<std::string> reported_;
};

/// EventTarget.prototype.addEventListener (DOM §2.7), as generated code spells
/// it: `ctnative::EventTarget.prototype.addEventListener.call(el, type, f)`,
/// where `f` takes the event or declares no parameter.
struct add_event_listener_method {
    template <class F>
    requires std::invocable<F &, const js_event &> || std::invocable<F &>
    undefined_t call(const js_element_t & target, const js_string & type, F && callback) const {
        event_listeners::program().add(target, std::string{type.value()},
                                       event_listeners::wrap(std::forward<F>(callback)));
        return {};
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
