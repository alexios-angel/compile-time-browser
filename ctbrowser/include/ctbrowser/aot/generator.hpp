#pragma once

#include <coroutine>
#include <cstddef>
#include <exception>
#include <iterator>
#include <memory>
#include <ranges>
#include <type_traits>
#include <utility>
#include <version>

// THE NATIVE BACKEND'S GENERATOR TYPE - Phase 58, Stage 58A.
//
// `24-native-cpp-backend.md` maps `function*` to a C++ coroutine and `yield` to
// `co_yield`, and calls it "the cleanest mapping in the whole specification".
// It cannot be written with `std::generator`: MEASURED on the devbox, neither
// GCC 13.3.0 nor clang 18.1.3 ships <generator>, and libc++ is not installed,
// so `#include <generator>` produces a build that works on one machine and
// fails on the box that is this project's only gate. This is that type over
// C++20 <coroutine>, which BOTH compilers have.
//
// A RUNTIME HEADER, so the ODS-first rule of `23-lexical-implementation.md` §1
// does not reach it - §1.2 exempts anything that is not IR. There is no
// TableGen here because there is no operation, attribute or type here: this is
// the C++ the emitter will PRINT, not the compiler that prints it. Any IR Phase
// 58B adds is a different file and that rule applies to it in full.
//
// WHAT IT DELIBERATELY DOES NOT DO, because a JS generator does three things a
// C++ generator cannot, and every one of them is load-bearing on this project's
// own corpora:
//
//   1. `.next(v)` SENDS A VALUE IN. `const x = yield y` reads it. `co_yield`
//      produces nothing - the assertion at the bottom of this file proves that
//      about std::generator too, so adopting the standard type does not fix it.
//   2. `.throw(e)` INJECTS AN EXCEPTION AT THE SUSPENSION POINT. A C++
//      coroutine can only be resumed, never re-entered throwing.
//   3. `return v` IS OBSERVABLE as `{value: v, done: true}`. `return_void`
//      below matches std::generator, and a range-for cannot see a return value
//      in either. `for...of` cannot see it in JavaScript either, so the
//      mapping is exact for `for...of` and lossy for explicit `.next()`.
//
// Those three are the ELIGIBILITY PREDICATE for Stage 58B, and they are not a
// corner: ctbrowser/vendor/babylon/babylon.js has 622 `function*` bodies and
// ALL 622 of them are the fourth argument to the minified TypeScript
// `__awaiter`, which drives them with `n.next(e)`, `n.throw(e)` and
// `e.done ? r(e.value)`. See ctcompile/docs/plans/async-and-generators.md.
namespace ctbrowser::ctnative {

// DOES THE STANDARD LIBRARY HAVE <generator>? Two questions, not one: the
// header can exist while the feature is not (a partial C++23 mode), so the
// feature-test macro is what decides. <version> is included above for it.
#if defined(__has_include)
#if __has_include(<generator>)
#if defined(__cpp_lib_generator) && __cpp_lib_generator >= 202207L
#define CTNATIVE_HAS_STD_GENERATOR 1
#endif
#endif
#endif
#if !defined(CTNATIVE_HAS_STD_GENERATOR)
#define CTNATIVE_HAS_STD_GENERATOR 0
#endif

// AND IS IT ADOPTED? A SEPARATE QUESTION, and the separation is the finding.
//
// The plan says "adopt std::generator behind __has_include when a compiler
// grows one". Taken literally - alias it the moment the header appears - that
// reintroduces exactly the disease it was written to cure: the type would then
// differ between the devbox and a newer machine, silently, in ways a program
// can see. `std::generator<T>`'s reference type is `T&&`, not `T&`, so
// `auto & x = *it` compiles on one and not the other; it takes an allocator
// parameter this one does not; and this project byte-compares goldens across
// two toolchains. A knob that is one -D is cheap. A type that changes under you
// is not.
//
// So: detection is automatic, ADOPTION IS EXPLICIT. Set
// -DCTNATIVE_USE_STD_GENERATOR=1 when a machine has it and you mean it.
#if !defined(CTNATIVE_USE_STD_GENERATOR)
#define CTNATIVE_USE_STD_GENERATOR 0
#endif
#if CTNATIVE_USE_STD_GENERATOR && !CTNATIVE_HAS_STD_GENERATOR
#error "CTNATIVE_USE_STD_GENERATOR=1 but this standard library has no <generator>"
#endif

#if CTNATIVE_USE_STD_GENERATOR

template <class T> using generator = std::generator<T>;

#else

// A LAZY, MOVE-ONLY, SINGLE-PASS GENERATOR.
//
// Lazy because a JS generator runs nothing until the first `.next()` - a
// property `ctbrowser/unittests/js/vm_basics.cpp` pins by name - which is
// `initial_suspend` returning `suspend_always`.
template <class T> class generator {
public:
    using value_type = std::remove_cvref_t<T>;

    class promise_type {
    public:
        generator get_return_object() noexcept {
            return generator{std::coroutine_handle<promise_type>::from_promise(*this)};
        }

        // NOTHING RUNS UNTIL THE FIRST RESUME, and the coroutine stays alive
        // after the body ends so `done()` can be asked. `final_suspend` must
        // be noexcept; the handle is destroyed by ~generator, not by the frame.
        std::suspend_always initial_suspend() noexcept { return {}; }
        std::suspend_always final_suspend() noexcept { return {}; }

        // THE YIELDED VALUE IS BORROWED, NOT COPIED. `co_yield e` materialises
        // its operand as a temporary whose lifetime runs to the end of the
        // full-expression - which contains the suspension - so the address is
        // valid for exactly as long as the consumer is looking at it, and no
        // longer. This is the same contract std::generator has.
        std::suspend_always yield_value(value_type & produced) noexcept {
            produced_ = std::addressof(produced);
            return {};
        }
        std::suspend_always yield_value(value_type && produced) noexcept {
            produced_ = std::addressof(produced);
            return {};
        }

        // `return_void`, NOT `return_value`, and it matches std::generator.
        // A JS `return v` inside a generator IS observable through `.next()`,
        // so a body containing one is outside the mapping - see the header
        // comment. Making this `return_value` would fork this type from the
        // standard one for a case the standard iteration interface still could
        // not show.
        void return_void() const noexcept {}

        // AN ESCAPING EXCEPTION IS HELD, NOT RETHROWN HERE. Rethrowing inside
        // `unhandled_exception` unwinds out of the coroutine's own frame;
        // holding it lets the iterator rethrow on the consumer's stack, which
        // is where a JS `throw` inside a generator body surfaces too.
        void unhandled_exception() noexcept { failure_ = std::current_exception(); }

        [[nodiscard]] value_type & produced() const noexcept { return *produced_; }
        void rethrow_if_failed() const {
            if (failure_) { std::rethrow_exception(failure_); }
        }

    private:
        value_type * produced_ = nullptr;
        std::exception_ptr failure_;
    };

    // SINGLE-PASS ON PURPOSE. `iterator_concept` is spelled out rather than
    // left to iterator_traits, which would otherwise deduce
    // random_access_iterator_tag from the primary template and let
    // std::ranges believe a suspended coroutine can be revisited.
    class iterator {
    public:
        using iterator_concept = std::input_iterator_tag;
        using value_type = generator::value_type;
        using difference_type = std::ptrdiff_t;

        iterator() noexcept = default;
        explicit iterator(std::coroutine_handle<promise_type> from) noexcept : handle_(from) {}

        value_type & operator*() const noexcept { return handle_.promise().produced(); }
        iterator & operator++() {
            advance();
            return *this;
        }
        // VOID, because a single-pass iterator cannot hand back a copy of
        // itself that still means anything.
        void operator++(int) { advance(); }

        [[nodiscard]] bool operator==(std::default_sentinel_t) const noexcept {
            return !handle_ || handle_.done();
        }

    private:
        void advance() {
            handle_.resume();
            handle_.promise().rethrow_if_failed();
        }

        std::coroutine_handle<promise_type> handle_{};
    };

    generator() noexcept = default;
    generator(const generator &) = delete;
    generator & operator=(const generator &) = delete;
    generator(generator && other) noexcept : handle_(std::exchange(other.handle_, {})) {}
    generator & operator=(generator && other) noexcept {
        if (this != &other) {
            destroy();
            handle_ = std::exchange(other.handle_, {});
        }
        return *this;
    }
    ~generator() { destroy(); }

    // AT MOST ONCE. `begin()` performs the first resume, so calling it twice
    // advances the generator rather than restarting it - std::generator says
    // the same thing, and a single-pass range is allowed to.
    iterator begin() {
        if (handle_) {
            handle_.resume();
            handle_.promise().rethrow_if_failed();
        }
        return iterator{handle_};
    }
    [[nodiscard]] std::default_sentinel_t end() const noexcept { return {}; }

private:
    explicit generator(std::coroutine_handle<promise_type> from) noexcept : handle_(from) {}

    void destroy() noexcept {
        if (handle_) { handle_.destroy(); }
        handle_ = {};
    }

    std::coroutine_handle<promise_type> handle_{};
};

#endif // CTNATIVE_USE_STD_GENERATOR

// THE INVARIANTS, AS BUILD ERRORS. "There is no CI. Invariants become tests or
// static_asserts. Prefer a build error to a test." Every one of these holds for
// std::generator too, which is what makes the switch above safe to throw.
static_assert(!std::is_copy_constructible_v<generator<int>>,
              "a generator owns a coroutine frame: copying one would run the same body twice or "
              "destroy it twice");
static_assert(!std::is_copy_assignable_v<generator<int>>, "and the same for assignment");
static_assert(std::is_move_constructible_v<generator<int>>,
              "a generator must be returnable from the function that is the coroutine");
static_assert(std::input_iterator<std::ranges::iterator_t<generator<int>>>,
              "the iterator is an input iterator - single-pass, which a suspended frame is");
static_assert(std::ranges::input_range<generator<int>>, "and the generator is an input range");
static_assert(!std::ranges::forward_range<generator<int>>,
              "and NOT a forward range: a forward range may be walked twice and this cannot be");

// THE ELIGIBILITY PREDICATE FOR STAGE 58B, WRITTEN AS AN ASSERTION.
//
// `co_yield` produces NOTHING. A JS `const x = yield y` reads what `.next(v)`
// sent in, and there is no expression in C++ that can receive it through this
// interface - not here and not in std::generator, whose promise's
// `yield_value` is specified to return `suspend_always` as well. So a
// generator body whose yield expression is USED is outside the mapping, and
// that is a fact about the language rather than about this file.
//
// It is why Phase 58 does not compile Babylon: all 622 of its generators are
// `__awaiter`-driven, and `__awaiter`'s whole mechanism is `n.next(e)`.
namespace detail {
using yield_promise = std::coroutine_traits<generator<int>>::promise_type;
using yield_awaiter = decltype(std::declval<yield_promise &>().yield_value(std::declval<int>()));
using yield_result = decltype(std::declval<yield_awaiter &>().await_resume());
} // namespace detail
static_assert(std::is_void_v<detail::yield_result>,
              "co_yield yields nothing back, so a JS generator that reads `yield` cannot be one "
              "of these - which is the whole of Babylon");

} // namespace ctbrowser::ctnative
