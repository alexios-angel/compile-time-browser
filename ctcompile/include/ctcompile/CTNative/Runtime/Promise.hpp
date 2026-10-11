//===- Promise.hpp - Native promises ----------------------------*- C++ -*-===//
//
// Promise for generated native code: `new Promise(executor)`, the resolve and
// reject functions it hands the executor, `Promise.resolve(v)` and
// `promise.then(onFulfilled)`, with the job ordering of ECMA-262 27.2.
//
// A promise is js_promise<T, E>: T is the type its value was proved to have,
// E the type of the reason it may be rejected with (no_rejection when nothing
// can reject it). There is no promise of an unproved type; ctcompile refuses
// those. The state is shared between the promise, its resolving functions
// and the jobs that will settle or read it, so it is held by a shared_ptr.
// That makes no cycle: a pending promise owns its reactions, each reaction
// owns only the promise it will settle (never its own), and settling moves
// the reactions out into the microtask queue (Microtasks.hpp), which drops
// them once they have run.
//
// Only programs that use promises include this header.
//
//===----------------------------------------------------------------------===//

#pragma once

#include "Microtasks.hpp"
#include "ctnative.hpp"

#include <concepts>
#include <functional>
#include <memory>
#include <optional>
#include <type_traits>
#include <utility>
#include <vector>

namespace ctnative {

/// The reason type of a promise that nothing can reject: its reject function
/// cannot be called, and a rejected reaction cannot happen.
struct no_rejection {};

template <class T, class E = no_rejection> class js_promise;

template <class X> inline constexpr bool is_js_promise = false;
template <class T, class E> inline constexpr bool is_js_promise<js_promise<T, E>> = true;

namespace promise_detail {

/// A promise's internal slots (ECMA-262 27.2.6): [[PromiseState]],
/// [[PromiseResult]] (split into the value and the reason, so each keeps its
/// own type) and the two reaction lists, kept as one list in the order the
/// reactions were added, since each reaction reads the settled state to
/// choose its fulfilled or rejected half.
template <class T, class E> struct state {
    enum class status : unsigned char {
        pending,
        fulfilled,
        rejected
    };
    using reaction = std::move_only_function<void(const state &)>;

    status now = status::pending;
    std::optional<T> value;
    std::optional<E> reason;
    std::vector<reaction> reactions;
};

template <class T, class E> using state_ptr = std::shared_ptr<state<T, E>>;

/// TriggerPromiseReactions (27.2.1.8): one job per reaction, oldest first.
/// The reactions move into the queue, so the promise no longer owns them.
template <class T, class E> void trigger(const state_ptr<T, E> & promise) {
    for (auto & reaction : promise->reactions) {
        microtasks::program().enqueue(
            [promise, run = std::move(reaction)]() mutable { run(*promise); });
    }
    promise->reactions.clear();
}

/// FulfillPromise (27.2.1.4).
template <class T, class E> void fulfill(const state_ptr<T, E> & promise, T value) {
    promise->value.emplace(std::move(value));
    promise->now = state<T, E>::status::fulfilled;
    trigger(promise);
}

/// RejectPromise (27.2.1.7).
template <class T, class E> void reject(const state_ptr<T, E> & promise, E reason) {
    promise->reason.emplace(std::move(reason));
    promise->now = state<T, E>::status::rejected;
    trigger(promise);
}

/// PerformPromiseThen (27.2.5.4.1) steps 9 to 11: a pending promise keeps the
/// reaction until it settles; a settled one queues its job now.
template <class T, class E>
void perform_then(const state_ptr<T, E> & promise, typename state<T, E>::reaction reaction) {
    if (promise->now == state<T, E>::status::pending) {
        promise->reactions.push_back(std::move(reaction));
        return;
    }
    microtasks::program().enqueue(
        [promise, run = std::move(reaction)]() mutable { run(*promise); });
}

/// Settles `target` as `source` settled: the work of a resolving function
/// pair that a promise was resolved with another (27.2.2.2 step 1.a, whose
/// fresh resolving functions are called exactly once).
template <class T, class E> typename state<T, E>::reaction follow(state_ptr<T, E> target) {
    return [target = std::move(target)](const state<T, E> & source) {
        if (source.now == state<T, E>::status::fulfilled) {
            fulfill(target, *source.value);
        } else {
            if constexpr (!std::is_same_v<E, no_rejection>) { reject(target, *source.reason); }
        }
    };
}

} // namespace promise_detail

/// A promise's resolve function (27.2.1.3.2). Copies share the promise and its
/// [[AlreadyResolved]] record with the reject function made beside it, so
/// only the first call of either does anything.
template <class T, class E> class promise_resolve_function {
public:
    promise_resolve_function(promise_detail::state_ptr<T, E> promise,
                             std::shared_ptr<bool> already_resolved)
        : promise_(std::move(promise)), already_resolved_(std::move(already_resolved)) {}

    /// Steps 7 to 12 with a resolution that is not a thenable: fulfills the
    /// promise.
    undefined_t operator()(T resolution) const {
        if (*already_resolved_) { return {}; }
        *already_resolved_ = true;
        promise_detail::fulfill(promise_, std::move(resolution));
        return {};
    }

    /// Steps 13 to 15 with a promise as the resolution: queues
    /// NewPromiseResolveThenableJob (27.2.2.2), whose `then` call adds a
    /// reaction that settles this promise as the resolution settles. That
    /// costs the two jobs the specification orders, so the promise settles
    /// two checkpoint steps after the resolution does.
    undefined_t operator()(js_promise<T, E> resolution) const {
        if (*already_resolved_) { return {}; }
        *already_resolved_ = true;
        microtasks::program().enqueue([target = promise_, source = resolution.state_]() {
            promise_detail::perform_then(source, promise_detail::follow(target));
        });
        return {};
    }

private:
    promise_detail::state_ptr<T, E> promise_;
    std::shared_ptr<bool> already_resolved_;
};

/// A promise's reject function (27.2.1.3.1). For a promise that nothing can
/// reject (E is no_rejection) it exists, as a value the executor may hold,
/// but cannot be called.
template <class T, class E> class promise_reject_function {
public:
    promise_reject_function(promise_detail::state_ptr<T, E> promise,
                            std::shared_ptr<bool> already_resolved)
        : promise_(std::move(promise)), already_resolved_(std::move(already_resolved)) {}

    /// Rejects the promise with `reason`, unless it was already resolved.
    undefined_t operator()(E reason) const
    requires(!std::is_same_v<E, no_rejection>)
    {
        if (*already_resolved_) { return {}; }
        *already_resolved_ = true;
        promise_detail::reject(promise_, std::move(reason));
        return {};
    }

private:
    promise_detail::state_ptr<T, E> promise_;
    std::shared_ptr<bool> already_resolved_;
};

/// A JavaScript Promise whose value is a T and whose rejection reason is an E.
/// Copies are the same promise: `===` compares identity.
template <class T, class E> class js_promise {
    static_assert(!is_js_promise<T>, "a promise's value is never itself a promise");
    static_assert(!std::is_reference_v<T> && !std::is_reference_v<E>);

public:
    using value_type = T;
    using reason_type = E;

    friend bool operator==(const js_promise &, const js_promise &) = default;

private:
    template <class, class> friend class js_promise;
    template <class, class> friend class promise_resolve_function;
    friend struct promise_constructor;
    friend struct promise_then_method;

    js_promise() : state_(std::make_shared<promise_detail::state<T, E>>()) {}

    /// CreateResolvingFunctions (27.2.1.3) for this promise.
    [[nodiscard]] std::pair<promise_resolve_function<T, E>, promise_reject_function<T, E>>
    resolving_functions() const {
        auto already_resolved = std::make_shared<bool>(false);
        return {promise_resolve_function<T, E>{state_, already_resolved},
                promise_reject_function<T, E>{state_, already_resolved}};
    }

    promise_detail::state_ptr<T, E> state_;
};

namespace promise_detail {

/// The promise a `then` handler returning R makes: a handler that returns a
/// promise makes a promise of that promise's value.
template <class R, class E> struct then_result {
    using type = js_promise<R, E>;
};
template <class U, class E> struct then_result<js_promise<U, E>, E> {
    using type = js_promise<U, E>;
};

/// What a `then` handler returns, called with the value or, when it takes
/// no parameter, with nothing.
template <bool TakesValue, class F, class T> struct handler_result {
    using type = std::invoke_result_t<F &, const T &>;
};
template <class F, class T> struct handler_result<false, F, T> {
    using type = std::invoke_result_t<F &>;
};

/// Whether a handler result R, when it is a promise, rejects with an E.
template <class R, class E> inline constexpr bool rejects_as = true;
template <class U, class F, class E>
inline constexpr bool rejects_as<js_promise<U, F>, E> = std::is_same_v<F, E>;

} // namespace promise_detail

/// Promise.prototype.then (27.2.5.4) with an onFulfilled handler, as generated
/// code spells it: `ctnative::Promise.prototype.then.call(p, f)`.
struct promise_then_method {
    /// Returns the derived promise, which settles with what `on_fulfilled`
    /// returns (adopting it when that is a promise), or is rejected as `self`
    /// is, since a missing onRejected passes the reason through (27.2.2.1
    /// step 1.e). A handler that throws a js_exception<E> rejects it.
    template <class T, class E, class F>
    requires std::invocable<F &, const T &> || std::invocable<F &>
    auto call(const js_promise<T, E> & self, F on_fulfilled) const {
        // A handler that declares no parameter ignores the value, and one
        // that returns nothing fulfills with undefined.
        constexpr bool takes_value = std::invocable<F &, const T &>;
        using returned = typename promise_detail::handler_result<takes_value, F, T>::type;
        using R = std::conditional_t<std::is_void_v<returned>, undefined_t, returned>;
        static_assert(promise_detail::rejects_as<R, E>,
                      "a then handler's promise must reject with the same reason type");
        using derived_t = typename promise_detail::then_result<R, E>::type;
        derived_t derived;
        promise_detail::perform_then(
            self.state_,
            [handler = std::move(on_fulfilled), settle = derived.resolving_functions()](
                const promise_detail::state<T, E> & settled) mutable {
                // NewPromiseReactionJob (27.2.2.1).
                if (settled.now != promise_detail::state<T, E>::status::fulfilled) {
                    if constexpr (!std::is_same_v<E, no_rejection>) {
                        settle.second(*settled.reason);
                    }
                    return;
                }
                const auto invoke = [&]() -> decltype(auto) {
                    if constexpr (takes_value) {
                        return handler(*settled.value);
                    } else {
                        return handler();
                    }
                };
                const auto run = [&] {
                    if constexpr (std::is_void_v<returned>) {
                        invoke();
                        settle.first(undefined_t{});
                    } else {
                        settle.first(invoke());
                    }
                };
                if constexpr (std::is_same_v<E, no_rejection>) {
                    run();
                } else {
                    try {
                        run();
                    } catch (const js_exception<E> & thrown) { settle.second(thrown.value); }
                }
            });
        return derived;
    }
};

struct promise_prototype {
    promise_then_method then;
};

/// The Promise constructor and the members generated code calls:
/// `ctnative::Promise.construct<T, E>(executor)` for `new Promise(executor)`,
/// and `ctnative::Promise.resolve(v)`.
struct promise_constructor {
    promise_prototype prototype;

    /// `new Promise(executor)` (27.2.3.1): calls the executor at once with the
    /// new promise's resolve function, and its reject function when the
    /// executor takes two parameters. An executor that throws a
    /// js_exception<E> rejects the promise (step 10).
    template <class T, class E = no_rejection, class F>
    js_promise<T, E> construct(F && executor) const {
        js_promise<T, E> made;
        auto [resolve, reject] = made.resolving_functions();
        const auto run = [&] {
            if constexpr (std::invocable<F &, promise_resolve_function<T, E> &,
                                         promise_reject_function<T, E> &>) {
                executor(resolve, reject);
            } else {
                executor(resolve);
            }
        };
        if constexpr (std::is_same_v<E, no_rejection>) {
            run();
        } else {
            try {
                run();
            } catch (const js_exception<E> & thrown) { reject(thrown.value); }
        }
        return made;
    }

    /// Promise.resolve(x) (27.2.4.7) with a promise: PromiseResolve
    /// (27.2.4.7.1) step 1 returns that same promise.
    template <class T, class E> js_promise<T, E> resolve(js_promise<T, E> x) const { return x; }

    /// Promise.resolve(x) with any other value: a new promise fulfilled with
    /// it.
    template <class V>
    requires(!is_js_promise<std::remove_cvref_t<V>>)
    js_promise<std::remove_cvref_t<V>> resolve(V && x) const {
        js_promise<std::remove_cvref_t<V>> made;
        made.resolving_functions().first(std::forward<V>(x));
        return made;
    }
};
inline constexpr promise_constructor Promise{};

} // namespace ctnative
