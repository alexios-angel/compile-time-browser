//===- Handlers.hpp - Identity of native function values --------*- C++ -*-===//
//
// A JavaScript function is an object, so two function values are `===` only
// when they are the same object: the closure made by one evaluation of a
// function expression. Native code represents a closure as a C++ callable
// (a lifted function bound to its captures), which C++ can copy and cannot
// compare. callable_identity is the identity that one evaluation gives it:
// generated code stamps a fresh one where JavaScript creates the closure
// (`identify(f)`), and every copy of the value keeps it, as every reference
// to a JavaScript function names the same object.
//
// Code that compares function values reads the identity: EventHandler's
// `handler.callable === fn`, and removeEventListener, which finds the
// registration added with the same callback (DOM 2.7.7).
//
// Only programs that compare function values include this header.
//
//===----------------------------------------------------------------------===//

#pragma once

#include <compare>
#include <cstdint>
#include <functional>
#include <type_traits>
#include <utility>

namespace ctnative {

/// Which closure creation made a function value. Equal identities are the
/// same JavaScript function object (SameValue, ECMA-262 7.2.10, on objects).
struct callable_identity {
    std::uint64_t value = 0;
    auto operator<=>(const callable_identity &) const = default;
};

/// A fresh identity, one per closure creation. A program is one realm on one
/// thread, so a plain counter is enough; zero is never handed out.
inline callable_identity new_callable_identity() {
    static std::uint64_t next = 0;
    return callable_identity{++next};
}

/// A callable together with the identity of the closure creation that made
/// it. Calling it calls the callable; copying it copies both, so a copy is
/// still the same function.
template <class F> struct identified_callable {
    callable_identity identity;
    F callable;

    template <class... A>
    requires std::is_invocable_v<F &, A...>
    decltype(auto) operator()(A &&... arguments) {
        return std::invoke(callable, std::forward<A>(arguments)...);
    }
    template <class... A>
    requires std::is_invocable_v<const F &, A...>
    decltype(auto) operator()(A &&... arguments) const {
        return std::invoke(callable, std::forward<A>(arguments)...);
    }
};

/// Evaluates a function expression: `f` with a fresh identity.
template <class F> identified_callable<std::decay_t<F>> identify(F && f) {
    return {new_callable_identity(), std::forward<F>(f)};
}

/// `a === b` for two function values.
template <class F, class G>
bool same_function(const identified_callable<F> & a, const identified_callable<G> & b) {
    return a.identity == b.identity;
}

} // namespace ctnative
