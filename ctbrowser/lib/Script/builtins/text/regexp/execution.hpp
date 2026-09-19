#pragma once

#include "../internal.hpp"
#include <map>

namespace ctbrowser::script::builtins_detail::regexp_detail {

using regex_cache = std::map<std::string, std::shared_ptr<rx::rx_prog>>;

inline constexpr std::string_view slot_source = "@#RegExpSource";

inline constexpr std::string_view slot_flags = "@#RegExpFlags";

[[nodiscard]] std::shared_ptr<rx::rx_prog> compiled(const std::shared_ptr<regex_cache> & cache,
                                                    const std::string & source,
                                                    const std::string & flags);
[[nodiscard]] object_object * regexp_table(value v);
[[nodiscard]] object_object * this_regexp(context & cx, const char * method);
[[nodiscard]] std::string escape_pattern(const std::string & source);
[[nodiscard]] value exec_result(context & cx, const rx::rx_prog & program, const std::string & s,
                                const rx::rx_match & m);
bool has_regexp_matcher(value v);
bool regexp_slots(value v, std::string & source, std::string & flags);
std::size_t advance_string_index(const std::string & s, std::size_t index, bool full_unicode);
bool get_last_index(context & cx, value rx, double & out);
bool set_last_index(context & cx, value rx, double v);
value regexp_exec(context & cx, value rx, value subject);
[[nodiscard]] value builtin_exec(context & cx, const std::shared_ptr<regex_cache> & cache, value rx,
                                 const std::string & subject);
[[nodiscard]] value species_constructor(context & cx, value o, value fallback);
[[nodiscard]] bool read_flags(context & cx, value rx, std::string & out);
[[nodiscard]] bool object_this(context & cx, value self, const char * method);
[[nodiscard]] bool matched_text(context & cx, value result, std::string & out);
value regexp_string_iterator_next(context & cx, std::span<value>);

} // namespace ctbrowser::script::builtins_detail::regexp_detail
