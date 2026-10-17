//===- Callable.hpp - Borrowed callables for callback parameters -*- C++ -*-===//
//
// ctnative::function_ref, the parameter type of a function that only calls
// a callback it is given, during the call. Generated code binds a lifted
// callback to its captures in a local, `auto v = std::bind_front(callback,
// captures...)`, and passes the callee a function_ref to that local. The
// callee never stores the callback, so it borrows the caller's callable
// instead of owning a copy: no allocation and no copy.
//
// Also ctnative::bound and ctnative::bind, one member of a closure set: a
// value that is one of a proved, finite set of lifted callables, each bound to
// its own captures. The set is a std::variant of bound members, and a call
// through it is invoke_callable, which std::visit dispatches to the member the
// variant holds. Only programs with a borrowed parameter or a closure set
// include this header.
//
//===----------------------------------------------------------------------===//

#pragma once

#include <functional>
#include <memory>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>

namespace ctnative {

template <class Signature> class function_ref;

/// A non-owning reference to a callable object invoked as `R(A...)`, like
/// C++26's std::function_ref, which C++23 does not have. It holds the
/// object's address and a function that calls it, so it is trivially
/// copyable and two pointers wide. The referenced object must outlive every
/// call through the reference; the compiler gives this type only to
/// parameters it proved are called during the call that receives them and
/// never stored, returned or compared (ClosureLifting/BorrowedCallbacks.cpp).
/// Unlike std::function_ref it refers only to an lvalue, so a temporary can
/// never be referred to past the end of its statement, and a function name,
/// which is not an object, is refused; bind either to a local first.
template <class R, class... A> class function_ref<R(A...)> {
public:
    /// References `callable`, which must be invocable as `R(A...)`.
    template <class F>
    requires(!std::is_same_v<std::remove_cv_t<F>, function_ref> && std::is_object_v<F> &&
             std::is_invocable_r_v<R, F &, A...>)
    function_ref(F & callable) noexcept
        : object_(const_cast<void *>(static_cast<const void *>(std::addressof(callable)))),
          call_([](void * object, A... arguments) -> R {
              return std::invoke_r<R>(*static_cast<F *>(object), std::forward<A>(arguments)...);
          }) {}

    /// Calls the referenced object with `arguments`.
    R operator()(A... arguments) const { return call_(object_, std::forward<A>(arguments)...); }

private:
    void * object_;
    R (*call_)(void *, A...);
};

/// One member of a closure set: the lifted function `F`, whose leading
/// parameters are its captures, bound to their values. Each `F` is its own
/// type, so two members with the same signature and capture types are still
/// two alternatives of the set's std::variant. The captures are copies, which
/// is what a closure set proves safe (ClosureLifting/ClosureSets.cpp refuses
/// a binding written after it is captured).
template <auto F, class... Captures> struct bound {
    std::tuple<Captures...> captures;

    /// Calls `F` with the captures, then `arguments`.
    template <class... A> decltype(auto) operator()(A &&... arguments) const {
        return std::apply(
            [&](const Captures &... values) -> decltype(auto) {
                return F(values..., std::forward<A>(arguments)...);
            },
            captures);
    }
};

/// Binds `F` to `captures`, as the closure that makes one member of a set.
template <auto F, class... Captures>
bound<F, std::decay_t<Captures>...> bind(Captures &&... captures) {
    return {{std::forward<Captures>(captures)...}};
}

/// Calls the member of a closure set that `callable` holds. Admission proved
/// every member takes `Args` and returns one type.
template <class... Members, class... Args>
auto invoke_callable(const std::variant<Members...> & callable, Args... args) {
    return std::visit([&](const auto & member) { return member(args...); }, callable);
}

} // namespace ctnative
