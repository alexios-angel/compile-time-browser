//===- Callable.hpp - Borrowed callables for callback parameters -*- C++ -*-===//
//
// ctnative::function_ref, the parameter type of a function that only calls
// a callback it is given, during the call. Generated code binds a lifted
// callback to its captures in a local, `auto v = std::bind_front(callback,
// captures...)`, and passes the callee a function_ref to that local. The
// callee never stores the callback, so it borrows the caller's callable
// instead of owning a copy: no allocation and no copy.
//
// Also ctnative::parameter_t, for a closure set: a value that is one of a
// proved, finite set of lifted callables, each bound to its own captures.
// ctcompile emits one struct per set (EmitC/ClosureSets.cpp), with a tag
// naming the member and each member's captures as named fields; a capture
// whose type only the lifted function's signature spells, such as the
// pointer to an arrow's lexical `this`, is declared as parameter_t of that
// function. Only programs with a borrowed parameter or a closure set include
// this header.
//
//===----------------------------------------------------------------------===//

#pragma once

#include <functional>
#include <memory>
#include <tuple>
#include <type_traits>
#include <utility>

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

namespace detail {
template <class Function> struct parameters_of;
template <class R, class... P> struct parameters_of<R (*)(P...)> {
    using type = std::tuple<std::decay_t<P>...>;
};
} // namespace detail

/// The type of function `F`'s parameter `I`, without reference or cv: what a
/// closure-set member's capture is declared as when only `F`'s signature
/// spells its type.
template <auto F, std::size_t I>
using parameter_t = std::tuple_element_t<I, typename detail::parameters_of<decltype(F)>::type>;

} // namespace ctnative
