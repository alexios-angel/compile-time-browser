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
#include "Handlers.hpp"

#include <ctbrowser/shell/listener_registry.hpp>

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <functional>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

namespace ctnative {

struct window_object;

/// The fields a program writes onto its events beyond the DOM's own (such as
/// Bootstrap's `delegateTarget`): one struct per program, which its generated
/// code defines. Only that code names its members, so the runtime needs no
/// definition.
struct event_extra;

/// An Event (DOM §2.2): the one a native listener receives while the host
/// dispatches it, or one the program made with `new Event`. A handle to the
/// event's state, which every copy names, so preventDefault, stopPropagation
/// and the extra fields reach the dispatch, every later listener and the code
/// that dispatched it; generated code passes it by value. The handle shares
/// its state through std::shared_ptr because emitted code copies values
/// between locals, and each copy must still be the same event. The native
/// lowering admits an event only as the receiver of the reads and calls below
/// and as dispatchEvent's argument, so no copy escapes its function.
class js_event {
public:
    /// An event's state.
    struct state {
        std::string type;
        bool bubbles = true;
        bool cancelable = true;
        /// Set when the event is dispatched (DOM §2.9 step 5).
        std::optional<js_element_t> target;
        std::optional<js_element_t> current_target;
        bool canceled = false;
        bool stop_propagation = false;
        bool dispatching = false;
        /// A std::shared_ptr only because event_extra is incomplete here: its
        /// deleter is bound where generated code creates it. Nothing else
        /// shares it; the event owns its fields.
        std::shared_ptr<event_extra> extra;
    };

    explicit js_event(std::shared_ptr<state> shared) noexcept : shared_(std::move(shared)) {}

    /// Event.type, .target and .currentTarget (DOM §2.2). The target is a page
    /// element, since the host dispatches only at elements. The current target
    /// is the element whose listener runs; a listener on the document or the
    /// window has none here, and the lowering refuses its currentTarget read.
    [[nodiscard]] js_string type() const { return js_string{shared_->type}; }
    [[nodiscard]] js_element_t target() const { return shared_->target.value(); }
    [[nodiscard]] js_element_t currentTarget() const { return shared_->current_target.value(); }
    /// Event.defaultPrevented: whether a listener canceled the event.
    [[nodiscard]] js_boolean_t defaultPrevented() const { return js_boolean_t{shared_->canceled}; }

    /// The program's extra fields of this event, made on the first write.
    /// Const because listeners receive `const js_event &`; the fields belong
    /// to the event's state, not to the handle. A template so that the struct
    /// is complete where it is made (in generated code).
    template <class Extra = event_extra> Extra & extra() const {
        if (!shared_->extra) { shared_->extra = std::make_shared<Extra>(); }
        return *shared_->extra;
    }
    /// The extra fields, or null when nothing wrote one.
    [[nodiscard]] const event_extra * extra_if() const noexcept { return shared_->extra.get(); }

private:
    friend struct prevent_default_method;
    friend struct stop_propagation_method;
    friend class event_listeners;
    std::shared_ptr<state> shared_;
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

/// The EventInit dictionary (DOM §2.2): both flags default to false.
struct event_init {
    js_boolean_t bubbles{};
    js_boolean_t cancelable{};
};

/// The Event constructor: `new Event(type, init)` is
/// `ctnative::Event(type, init)` (DOM §2.2 "new Event(type, eventInitDict)").
struct event_constructor {
    event_prototype prototype;

    js_event operator()(const js_string & type, event_init init = {}) const {
        auto made = std::make_shared<js_event::state>();
        made->type = std::string{type.value()};
        made->bubbles = static_cast<bool>(init.bubbles);
        made->cancelable = static_cast<bool>(init.cancelable);
        return js_event{std::move(made)};
    }
};
inline constexpr event_constructor Event{};

/// The event listeners a native program registered, per document. One table
/// per program (event_listeners::program()), as a program is one realm.
///
/// Each listener is a std::move_only_function the table holds through a
/// shared_ptr: the dispatch holds it too while it runs, so the callable stays
/// at one address and alive even if it registers another listener or removes
/// itself. A listener lives until removeEventListener, forget() or the end of
/// the program.
///
/// A registration's callback id is the closure's callable_identity
/// (Handlers.hpp) when generated code passes an identified_callable, so adding
/// the same function twice is ignored and removeEventListener finds it (DOM
/// §2.7.3 step 4 and §2.7.7). A callback without one gets an id no identity
/// can equal: nothing can name it again.
class event_listeners {
public:
    /// Where a listener is registered: a document, a target in it, and the
    /// element that is currentTarget there (none for the document and the
    /// window).
    struct site {
        const ctbrowser::document * document;
        ctbrowser::shell::listener_target target;
        std::optional<js_element_t> current_target;
    };

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

    /// EventTarget.addEventListener(type, callback, capture) (DOM §2.7.3), not
    /// once and not passive. `identity` is the callback's, when it has one;
    /// an equal registration (same site, type, capture and identity) is
    /// ignored.
    void add(site at, std::string type, listener callback, bool capture,
             std::optional<callable_identity> identity = std::nullopt) {
        per_document & listeners = documents_[at.document];
        ctbrowser::shell::listener_registry<registration>::entry made;
        made.target = at.target;
        made.type = std::move(type);
        made.callback_id = identity ? identity->value : anonymous | ++listeners.next_callback;
        made.capture = capture;
        made.payload = registration{std::move(at.current_target),
                                    std::make_shared<listener>(std::move(callback))};
        (void)listeners.registry.add(std::move(made));
    }

    /// EventTarget.removeEventListener(type, callback, capture) (DOM §2.7.7):
    /// drops the registration with that site, type, capture flag and callback
    /// identity, if there is one. A dispatch in progress skips it from then
    /// on.
    void remove(const site & at, std::string_view type, callable_identity identity, bool capture) {
        const auto found = documents_.find(at.document);
        if (found == documents_.end()) { return; }
        found->second.registry.remove(at.target, type, identity.value, capture);
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
        auto shared = std::make_shared<js_event::state>();
        shared->type = std::string{type};
        shared->bubbles = bubbles;
        shared->cancelable = cancelable;
        const std::size_t ran = dispatch(target, js_event{shared});
        if (canceled != nullptr) { *canceled = shared->canceled; }
        return ran;
    }

    /// Dispatches `event` at `target`, as dispatchEvent does for an event the
    /// program made (DOM §2.9): the same path and phases, with the event's own
    /// bubbles and cancelable flags.
    /// Afterwards the event keeps its target and canceled flag, and its
    /// stop-propagation flag is cleared. Throws for an event already being
    /// dispatched, as DOM §2.7 dispatchEvent step 1 does.
    std::size_t dispatch(const js_element_t & target, const js_event & event) {
        using ctbrowser::shell::event_target_kind;
        js_event::state & shared = *event.shared_;
        if (shared.dispatching) {
            throw std::invalid_argument("InvalidStateError: the event is already being dispatched");
        }
        const ctbrowser::element_ref element = target.value();
        shared.target = target;
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
        const std::string type = shared.type;
        shared.dispatching = true;
        std::size_t ran = 0;
        // Runs one step's listeners of one phase; false once propagation
        // stopped.
        const auto step = [&](const ctbrowser::shell::listener_target & at, bool capture) {
            for (const std::uint64_t id : listeners.registry.snapshot(at, type, capture)) {
                const auto * entry = listeners.registry.begin(at, type, id, capture);
                if (entry == nullptr) { continue; }
                shared.current_target = entry->payload.current_target;
                // Held for the call: the listener may add another, which moves
                // the registry's rows, or remove itself, which drops its row.
                const std::shared_ptr<listener> callback = entry->payload.callback;
                ++ran;
                try {
                    (*callback)(event);
                } catch (const std::exception & thrown) { reported_.emplace_back(thrown.what()); }
            }
            return !shared.stop_propagation;
        };
        bool going = true;
        for (std::size_t i = path.size(); going && i-- > 1;) { going = step(path[i], true); }
        going = going && step(path[0], true) && step(path[0], false);
        for (std::size_t i = 1; going && shared.bubbles && i < path.size(); ++i) {
            going = step(path[i], false);
        }
        shared.dispatching = false;
        shared.stop_propagation = false;
        shared.current_target.reset();
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
        std::shared_ptr<listener> callback;
    };
    struct per_document {
        ctbrowser::shell::listener_registry<registration> registry;
        std::uint64_t next_callback = 0;
    };
    /// The top bit marks a callback with no identity; identities count up
    /// from 1 and never reach it.
    static constexpr std::uint64_t anonymous = std::uint64_t{1} << 63;
    std::unordered_map<const ctbrowser::document *, per_document> documents_;
    std::vector<std::string> reported_;
};

/// The listener `callback` that the closure creation `identity` made, for an
/// addEventListener whose function is also passed to removeEventListener.
template <class F>
identified_callable<std::decay_t<F>> identified_listener(callable_identity identity,
                                                         F && callback) {
    return {identity, std::forward<F>(callback)};
}

/// The identity of a function declaration evaluated once, such as a top-level
/// `function onClick() {}`: every value of `onClick` is that one function
/// object, so every use of this names the same identity.
template <auto Function> callable_identity declared_identity() {
    static const callable_identity identity = new_callable_identity();
    return identity;
}

/// Where generated code's EventTarget calls register: an element, or
/// ctnative::document or ctnative::window.
struct listener_sites {
    static event_listeners::site of(const js_element_t & target) {
        const ctbrowser::element_ref element = target.value();
        return {
            element.owner, {ctbrowser::shell::event_target_kind::node, element.id.key()}, target};
    }
    static event_listeners::site of(const document_object &) {
        return {page_document(), {ctbrowser::shell::event_target_kind::document, 0}, std::nullopt};
    }
    static event_listeners::site of(const window_object &) {
        return {page_document(), {ctbrowser::shell::event_target_kind::window, 0}, std::nullopt};
    }

private:
    /// The document of the active document scope, through its element.
    static const ctbrowser::document * page_document() {
        return require_document_element().value().owner;
    }
};

template <class T>
concept event_target_value = requires(const T & target) { listener_sites::of(target); };

template <class F> inline constexpr bool is_identified_v = false;
template <class F> inline constexpr bool is_identified_v<identified_callable<F>> = true;

/// EventTarget.prototype.addEventListener (DOM §2.7.3), as generated code
/// spells it: `ctnative::EventTarget.prototype.addEventListener.call(el, type,
/// f)`, with `ctnative::document` or `ctnative::window` for `el` on those
/// targets, and an optional fourth argument, the capture flag. `f` takes the
/// event or declares no parameter; an identified_callable registers under its
/// identity.
struct add_event_listener_method {
    template <event_target_value T, class F>
    requires std::invocable<F &, const js_event &> || std::invocable<F &>
    undefined_t call(const T & target, const js_string & type, F && callback,
                     js_boolean_t capture = js_boolean_t{}) const {
        std::optional<callable_identity> identity;
        if constexpr (is_identified_v<std::remove_cvref_t<F>>) { identity = callback.identity; }
        event_listeners::program().add(listener_sites::of(target), std::string{type.value()},
                                       event_listeners::wrap(std::forward<F>(callback)),
                                       static_cast<bool>(capture), identity);
        return {};
    }
};

/// EventTarget.prototype.removeEventListener (DOM §2.7.7), as generated code
/// spells it: `ctnative::EventTarget.prototype.removeEventListener.call(el,
/// type, identity)`, with the identity of the closure creation that made the
/// function, and an optional capture flag.
struct remove_event_listener_method {
    template <event_target_value T>
    undefined_t call(const T & target, const js_string & type, callable_identity identity,
                     js_boolean_t capture = js_boolean_t{}) const {
        event_listeners::program().remove(listener_sites::of(target), type.value(), identity,
                                          static_cast<bool>(capture));
        return {};
    }
};

/// EventTarget.prototype.dispatchEvent (DOM §2.7) on an element, as generated
/// code spells it: `ctnative::EventTarget.prototype.dispatchEvent.call(el, e)`
/// for an event the program made. Returns false when a listener canceled it.
struct dispatch_event_method {
    js_boolean_t call(const js_element_t & target, const js_event & event) const {
        (void)event_listeners::program().dispatch(target, event);
        return js_boolean_t{!static_cast<bool>(event.defaultPrevented())};
    }
};

struct event_target_prototype {
    add_event_listener_method addEventListener;
    remove_event_listener_method removeEventListener;
    dispatch_event_method dispatchEvent;
};
struct event_target_constructor {
    event_target_prototype prototype;
};
inline constexpr event_target_constructor EventTarget{};

} // namespace ctnative
