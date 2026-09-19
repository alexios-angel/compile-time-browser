#pragma once

#include "../../objects/internal.hpp"
#include "../iterator_internal.hpp"

namespace ctbrowser::script::builtins_detail::iterator_helper {

using detail::iterator_record;

inline constexpr std::string_view helper_slot = "@#IteratorHelper";

inline constexpr std::string_view wrapped_slot = "@#Iterated";

enum class helper_kind : std::uint8_t {
    map,
    filter,
    take,
    drop,
    flat_map,
    concat,
    chunks,
    windows,
    zip
};

enum class gen_state : std::uint8_t {
    suspended_start,
    suspended_yield,
    executing,
    completed
};

[[nodiscard]] value slot(object_object * o, std::string_view name);
[[nodiscard]] object_object * helper_state(context & cx, value self, const char * method);
[[nodiscard]] iterator_record record_of(object_object * state, const char * prefix);
void store_record(object_object * state, const char * prefix, const iterator_record & rec);
[[nodiscard]] bool step_underlying(context & cx, object_object * state, const char * prefix,
                                   bool & done, value & out);
[[nodiscard]] bool call_or_close(context & cx, value fn, std::span<const value> args, value closing,
                                 value & out);
[[nodiscard]] double counter_of(object_object * state);
void bump_counter(object_object * state);
[[nodiscard]] bool close_open(context & cx, object_object * state, bool quietly, value);
[[nodiscard]] bool helper_step(context & cx, helper_kind kind, object_object * state, bool & done,
                               value & out);
[[nodiscard]] gen_state state_of(object_object * state);
void set_state(object_object * state, gen_state s);
[[nodiscard]] value helper_next(context & cx);
[[nodiscard]] value helper_return(context & cx);
[[nodiscard]] value make_helper(context & cx, object_object * helper_proto, helper_kind kind,
                                object_object * state);
[[nodiscard]] bool limit_arg(context & cx, value self, value raw, bool whole, double & out);
[[nodiscard]] bool size_arg(context & cx, value self, value raw, double & out);
[[nodiscard]] bool skip_arg(context & cx, value self, value raw, double & out);
[[nodiscard]] bool callable_or_close(context & cx, value self, value fn, const char * what);
[[nodiscard]] value ignoring_setter(context & cx, object_object * home, const std::string & key,
                                    value v);

} // namespace ctbrowser::script::builtins_detail::iterator_helper
