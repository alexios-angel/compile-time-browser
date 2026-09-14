#pragma once

namespace ctcompile::ctnative::lowering_detail {

inline constexpr char kMethodTableHelpers[] = R"cpp(
namespace ctnative {
// ctcompile: initialize a proved immutable owning callable field
template <auto Member, class Table, class Callable>
void method_set(const std::shared_ptr<Table> & table, Callable callable) {
    table.get()->*Member = std::move(callable);
}
// ctcompile: copy the owning callable from its proved initialized field
template <auto Member, class Table>
auto method_get(const std::shared_ptr<Table> & table) {
    return table.get()->*Member;
}
// ctcompile: invoke a stored callable with its concrete argument signature
template <class Callable, class... Args>
auto invoke_callable(const Callable & callable, Args... args) {
    return callable(args...);
}
// The fixed member cannot be extracted as a callable that owns its captures.
template <auto Member, class Table, class... Args>
auto invoke_session(const std::shared_ptr<Table> & table, Args... args) {
    return (table.get()->*Member)(std::move(args)...);
}
} // namespace ctnative
)cpp";

} // namespace ctcompile::ctnative::lowering_detail
