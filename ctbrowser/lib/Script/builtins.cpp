// The standard library, assembled. Every install_* lives in builtins/.

#include "builtins/internal.hpp"

namespace ctbrowser::script {

using namespace builtins_detail;

void install_builtins(context & cx, std::uint64_t seed) {
    install_math(cx, seed);
    install_generator(cx);
    install_class_defined(cx);
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
}

} // namespace ctbrowser::script
