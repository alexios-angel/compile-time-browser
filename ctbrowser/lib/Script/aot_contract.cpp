//===- aot_contract.cpp - Compile-time checks of the AOT helper ABI -------===//
//
// The runtime compiles its own ABI. ctbrowser/aot/aot.hpp expands
// aot_helpers.def into an enum and the `extern "C"` prototypes, and the
// presets that most need the check (`browser`, `browser-no-llvm`, `asan`,
// `tsan`, `windows`) build no ctcompile at all. So this translation unit
// exists only to be compiled: it defines and declares nothing, every line is a
// `static_assert` about the contract, and because it is part of
// `ctbrowser-script` every configuration checks it.
//
//===----------------------------------------------------------------------===//
#include <ctbrowser/aot/aot.hpp>

#include <ctbrowser/script/bytecode.hpp>
#include <ctbrowser/script/value.hpp>

#include <cstdint>
#include <string_view>
#include <tuple>
#include <type_traits>

namespace ctbrowser::aot {
namespace {

/// Decomposes a helper's declared prototype into its result type and whether
/// its first parameter is the frame handle. The prototype, not the table's
/// cell text, is what the backends call, so asserting about `decltype(name)`
/// checks the thing that has consequences.
template <typename F> struct signature;
template <typename R, typename... A> struct signature<R(A...)> {
    using result = R;
    static constexpr bool takes_frame =
        sizeof...(A) > 0 &&
        std::is_same_v<std::tuple_element_t<0, std::tuple<A..., int>>, ct_aot_frame *>;
};

} // namespace

// The status classifier, pinned.
// The one row every status in the vocabulary is defined against. Deriving
// `ct_aot_status`'s underlying type from this prototype means a change to the
// table's `ret` column moves the enum; this says the prototype itself is what
// the table declares, so the column cannot move unnoticed in the other
// direction either.
static_assert(std::is_same_v<decltype(ct_aot_check), std::int32_t(ct_aot_frame *)>,
              "aot_helpers.def: ct_aot_check is the status classifier and is "
              "int32_t(ct_aot_frame *)");

static_assert(std::is_same_v<std::underlying_type_t<ct_aot_status>,
                             signature<decltype(ct_aot_check)>::result>,
              "the status vocabulary's type IS the classifier's return type");

// A signed result is a status, and a status carries a frame.
// The table states this and names its single exception in prose at
// aot_helpers.def's ct_aot_to_int32: a signed int32 return that is data, not
// a status, and takes no frame handle, which is the mechanical tell. Every row
// that returns `int32_t` takes a frame handle first, except that one row.
// Asserting it two-sidedly is what makes the prose
// load-bearing: a new signed-returning helper that forgets its frame handle,
// or a second data-returning exception added without a word, both stop the
// build here.
#define CT_AOT_HELPER(name_, ret_, params_, may_throw_, may_reenter_, is_safepoint_)               \
    static_assert(!std::is_same_v<ret_, std::int32_t> ||                                           \
                      signature<decltype(name_)>::takes_frame ||                                   \
                      std::string_view{#name_} == "ct_aot_to_int32",                               \
                  #name_ ": a signed result is a STATUS, and no status-returning helper "          \
                         "lacks its frame handle - see ct_aot_to_int32 for the one "               \
                         "exception the table documents");
#include <ctbrowser/aot/aot_helpers.def>
#undef CT_AOT_HELPER

// Nothing comes back in memory.
// A status in the return register and data through an out-pointer, never a
// struct by value. A helper returning a struct by value would put a
// hidden pointer in the first argument slot on every ABI this targets, which
// silently shifts every other parameter - and the two backends would disagree
// about where the frame handle is.
#define CT_AOT_HELPER(name_, ret_, params_, may_throw_, may_reenter_, is_safepoint_)               \
    static_assert(std::is_void_v<ret_> || std::is_scalar_v<ret_>,                                  \
                  #name_ ": every helper answers in a register - a struct by value would "         \
                         "add a hidden out-pointer ahead of the frame handle");
#include <ctbrowser/aot/aot_helpers.def>
#undef CT_AOT_HELPER

// A value and the uint64_t it travels as.
// Every JavaScript value in the table is spelled `uint64_t`, so the boundary
// reinterprets a `script::value` as sixty-four bare bits rather than going
// through bits()/from_bits(). Size and trivial copyability are pinned beside
// the type itself; what is not pinned anywhere else is that a pointer to one is
// a pointer to the other, which is what `const uint64_t *argv` requires, and that
// the bit map is the identity rather than some transformation.
static_assert(sizeof(script::value) == sizeof(std::uint64_t),
              "a JS value is one machine word, which is what the ABI passes");
static_assert(alignof(script::value) == alignof(std::uint64_t),
              "and a `const uint64_t *` may address a span of them");
static_assert(std::is_trivially_copyable_v<script::value>,
              "so a register span crosses the boundary as bytes");
static_assert(script::value::from_bits(0x7FF8'0000'0000'0001ull).bits() == 0x7FF8'0000'0000'0001ull,
              "the bit map is the IDENTITY - a value that stored its bits "
              "transformed would satisfy every assertion above and make every "
              "uint64_t in the table mean something else");
static_assert(script::value::from_bits(0ull).bits() == 0ull &&
                  script::value::from_bits(~0ull).bits() == ~0ull,
              "at both ends of the range, so a mask or a sign extension in "
              "either direction is caught too");

} // namespace ctbrowser::aot
