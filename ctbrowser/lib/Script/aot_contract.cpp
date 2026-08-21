// THE RUNTIME COMPILES ITS OWN ABI, which until now nothing did.
//
// `ctbrowser/include/ctbrowser/aot/aot.hpp` expands aot_helpers.def into an
// enum and sixty-eight `extern "C"` prototypes, and the ONLY file in the
// repository that included it was `ctcompile/test/Inventories.cpp`. That is the
// wrong way round twice over. The runtime owns the table - Principle 11 says so
// - and the presets that most need the check are exactly the ones that never
// ran it: `browser`, `browser-no-llvm`, `asan`, `tsan` and `windows` all
// configure with `CTBROWSER_ENABLE_PROJECTS` empty, so ctcompile is not built
// and neither the ABI nor `EngineContract.hpp` is parsed at all. Verified in
// the asan build tree, which has no `ctcompile/` directory in it.
//
// So this translation unit exists to be COMPILED. It defines nothing and
// declares nothing; every line in it is a `static_assert` about the contract,
// and it is part of `ctbrowser-script`, so every configuration checks it.
//
// WHY NOT A NEW `lib/AOT/` SUBSYSTEM, which the master plan asks for: the tree
// documents ten subsystems, each one directory, one aggregate header and one
// CMake target, and an eleventh with no functions in it would break that for a
// file of assertions. It gets its own directory when Phase 4 gives it bodies.
#include <ctbrowser/aot/aot.hpp>

#include <ctbrowser/script/bytecode.hpp>
#include <ctbrowser/script/value.hpp>

#include <cstdint>
#include <string_view>
#include <tuple>
#include <type_traits>

namespace ctbrowser::aot {
namespace {

// --- the shape of a row, read off the DECLARED PROTOTYPE ------------------
// Not off a stringisation of the table's cells: the prototypes are what the
// backends actually call, so asserting about `decltype(name)` checks the thing
// that has consequences.
template <typename F> struct signature;
template <typename R, typename... A> struct signature<R(A...)> {
    using result = R;
    static constexpr bool takes_frame =
        sizeof...(A) > 0 &&
        std::is_same_v<std::tuple_element_t<0, std::tuple<A..., int>>, ct_aot_frame *>;
};

} // namespace

// --- THE CLASSIFIER, PINNED ------------------------------------------------
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

// --- A SIGNED RESULT IS A STATUS, AND A STATUS CARRIES A FRAME -------------
// The table states this and names its single exception in prose
// (aot_helpers.def, ct_aot_to_int32: "THE ONE DELIBERATE EXCEPTION to the
// return-type rule - a signed int32 return that is DATA, not a status ... It
// takes no frame handle, which is the mechanical tell"). Twenty-five rows
// return `int32_t`; twenty-four of them take a frame handle first, and the one
// that does not is that row. Asserting it two-sidedly is what makes the prose
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

// --- NOTHING COMES BACK IN MEMORY -----------------------------------------
// Phase 2 rejected the plan's `struct ct_aot_result { uint64_t; kind; }` in
// favour of a status in the return register and data through an out-pointer,
// and nothing enforced it. A helper returning a struct by value would put a
// hidden pointer in the first argument slot on every ABI this targets, which
// silently shifts every other parameter - and the two backends would disagree
// about where the frame handle is.
#define CT_AOT_HELPER(name_, ret_, params_, may_throw_, may_reenter_, is_safepoint_)               \
    static_assert(std::is_void_v<ret_> || std::is_scalar_v<ret_>,                                  \
                  #name_ ": every helper answers in a register - a struct by value would "         \
                         "add a hidden out-pointer ahead of the frame handle");
#include <ctbrowser/aot/aot_helpers.def>
#undef CT_AOT_HELPER

// --- A VALUE AND THE uint64_t IT TRAVELS AS --------------------------------
// Every JavaScript value in the table is spelled `uint64_t`, so the boundary
// reinterprets a `script::value` as sixty-four bare bits rather than going
// through bits()/from_bits(). Size and trivial copyability are pinned beside
// the type itself; what is NOT pinned anywhere is that a POINTER to one is a
// pointer to the other, which is what `const uint64_t *argv` requires, and that
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
// WHAT THIS DOES NOT COVER, said rather than left to be assumed: it pins
// from_bits/bits(), which is the pair the ABI boundary uses. It cannot pin the
// engine's own constructors - `value::undefined()` and its siblings are not
// constexpr - so a change that made `number(1.0).bits()` disagree with the
// NaN-boxing the table assumes would pass here. That belongs in a test rather
// than an assertion, and vm_basics already exercises every constructor.

} // namespace ctbrowser::aot
