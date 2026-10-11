//===- Events.hpp - Native event listeners ----------------------*- C++ -*-===//
//
// addEventListener for generated native code, and the dispatch a native host
// runs when an event reaches a page element. The registrations are kept by
// ctbrowser's own listener_registry (the bookkeeping the VM's bindings use),
// with the listener's C++ callable as the payload: the table owns each
// listener, and the captures bound into it, until the program ends or the
// host forgets the document.
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
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace ctnative {

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
    /// A listener as the table calls it. The native lowering passes only
    /// listeners that declare no parameter, so no Event object is built.
    using listener = std::move_only_function<void()>;

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
    void add(ctbrowser::element_ref target, std::string type, listener callback) {
        per_document & listeners = documents_[target.owner];
        ctbrowser::shell::listener_registry<std::unique_ptr<listener>>::entry made;
        made.target = {ctbrowser::shell::event_target_kind::node, target.id.key()};
        made.type = std::move(type);
        made.callback_id = ++listeners.next_callback;
        made.payload = std::make_unique<listener>(std::move(callback));
        (void)listeners.registry.add(std::move(made));
    }

    /// Dispatches an event of `type` at `target` (DOM §2.9), as the host does
    /// for a click: the target, then, when it bubbles, each ancestor element
    /// up to the document element. Each step's listeners are the ones
    /// registered when the step begins, in registration order. A listener
    /// that throws is reported (reported()) and the rest still run, as the VM
    /// reports an exception and continues. Returns how many listeners ran.
    ///
    /// ponytail: element steps only, with no capture phase, since only
    /// bubbling listeners on elements can be registered natively. The
    /// document and window steps, capture, stopPropagation and shadow-tree
    /// retargeting arrive with the first native registration that needs them;
    /// the VM's propagation path in dom_bindings is the one to lift then.
    std::size_t dispatch(ctbrowser::element_ref target, std::string_view type,
                         bool bubbles = true) {
        const auto found = documents_.find(target.owner);
        if (found == documents_.end()) { return 0; }
        per_document & listeners = found->second;
        std::vector<ctbrowser::node_id> path;
        {
            const auto txn = target.owner->read();
            for (ctbrowser::node_id at = target.id; at; at = txn.parent(at)) {
                path.push_back(at);
                if (!bubbles) { break; }
            }
        }
        std::size_t ran = 0;
        for (const ctbrowser::node_id at : path) {
            const ctbrowser::shell::listener_target step{ctbrowser::shell::event_target_kind::node,
                                                         at.key()};
            for (const std::uint64_t id : listeners.registry.snapshot(step, type, false)) {
                const auto * entry = listeners.registry.begin(step, type, id, false);
                if (entry == nullptr) { continue; }
                // The payload's address survives the listener adding another.
                listener & callback = *entry->payload;
                ++ran;
                try {
                    callback();
                } catch (const std::exception & thrown) { reported_.emplace_back(thrown.what()); }
            }
        }
        return ran;
    }

    /// Drops every listener registered on `document`'s nodes. A host calls it
    /// before destroying a document whose elements have listeners.
    void forget(const ctbrowser::document & document) { documents_.erase(&document); }

    /// The messages of the exceptions listeners threw, oldest first.
    [[nodiscard]] const std::vector<std::string> & reported() const noexcept { return reported_; }

private:
    struct per_document {
        ctbrowser::shell::listener_registry<std::unique_ptr<listener>> registry;
        std::uint64_t next_callback = 0;
    };
    std::unordered_map<const ctbrowser::document *, per_document> documents_;
    std::vector<std::string> reported_;
};

/// EventTarget.prototype.addEventListener (DOM §2.7), as generated code spells
/// it: `ctnative::EventTarget.prototype.addEventListener.call(el, type, f)`.
struct add_event_listener_method {
    template <std::invocable F>
    undefined_t call(const js_element_t & target, const js_string & type, F && callback) const {
        event_listeners::program().add(target.value(), std::string{type.value()},
                                       event_listeners::listener{std::forward<F>(callback)});
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
