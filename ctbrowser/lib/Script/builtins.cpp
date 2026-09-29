// The standard library, assembled. Every install_* lives in builtins/.

#include "builtins/internal.hpp"

namespace ctbrowser::script {

namespace builtins_detail {
void install_temporal(context & cx); // builtins/temporal/install.cpp
} // namespace builtins_detail

using namespace builtins_detail;

void install_builtins(context & cx, std::uint64_t seed) {
    install_math(cx, seed);
    install_generator(cx);
    install_class_defined(cx);
    install_destructuring_iteration(cx);
    install_regexp(cx);
    install_symbol(cx);
    install_collections(cx);
    install_errors(cx);
    install_proxy(cx);
    install_function(cx);
    install_typed_arrays(cx);
    install_dynamic_function(cx);
    install_array(cx);
    install_string(cx);
    install_number(cx);
    install_boolean(cx);
    install_structured_clone(cx);
    install_base64(cx);
    install_object(cx);
    install_json(cx);
    install_date(cx);
    install_globals(cx);
    install_promise(cx);
    install_temporal(cx);
    install_shadow_realm(cx);
    // Everything above is an intrinsic, and none of it is enumerable.
    cx.mark_globals_builtin();
}

realm_record & create_intrinsic_realm(context & cx) {
    realm_record & made = cx.create_empty_realm();
    // Everything below lands in the new realm: define_global and
    // set_prototype write the CURRENT realm's table.
    const context::realm_scope in{cx, made};
    cx.set_global_this(cx.make_global_object(made));
    install_builtins(cx);
    return made;
}

} // namespace ctbrowser::script
