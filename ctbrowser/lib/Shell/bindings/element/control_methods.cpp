// dom_bindings - click, form control value and focus, the canvas methods
// getContext, toDataURL and toBlob, the table model (rows, cells, sections),
// and the forms: the select and option model, form.elements, labels,
// constraint validation, the selection API, the entry list and FormData.

#include "control_methods/helpers.hpp"

#include <ctbrowser/shell/page/input_types.hpp>

namespace ctbrowser::shell {

using namespace detail;
// THE FORMS' NUMBERS AND TEXT, HTML 4.10.5.1 and 2.3.5, are
// shell/page/input_types.hpp - the form store sanitises with them too.
using namespace input_types;

void dom_bindings::install_control_methods(context & cx) {
    const std::initializer_list<const char *> html = {"HTMLElement"};
    const auto method = [&](std::initializer_list<const char *> on, const char * name,
                            unsigned length, script::native_fn fn) {
        define_operation(cx, on, name, length, std::move(fn));
    };

    // `element.click()` - the whole of it is in dom_bindings::click, beside the
    // engine's own mouse events, because it IS one of those.
    method(html, "click", 0, [this](context & c, std::span<value>) {
        (void)click(receiver(c));
        return value::undefined();
    });

    install_control_focus_canvas(cx);
    install_control_tables(cx);
    install_control_select_options(cx);
    install_control_collections_labels(cx);
    install_control_numbers_validation(cx);
    install_control_form_data(cx);
    install_control_text_selection(cx);
}

} // namespace ctbrowser::shell
