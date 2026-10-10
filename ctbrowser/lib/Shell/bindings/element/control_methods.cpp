//===- control_methods.cpp - Form control and canvas methods ----*- C++ -*-===//
//
// dom_bindings::install_control_methods: `click()` on HTMLElement, then the
// installers in control_methods/ for control value and focus, the canvas
// methods (getContext, toDataURL, toBlob), the table model (rows, cells,
// sections), the select and option model, form.elements, labels, constraint
// validation, the selection API, the entry list and FormData.
//
//===----------------------------------------------------------------------===//

#include "control_methods/helpers.hpp"

#include <ctbrowser/shell/page/input_types.hpp>

namespace ctbrowser::shell {

using namespace detail;
// The forms' number and text rules (HTML 4.10.5.1 and 2.3.5) live in
// shell/page/input_types.hpp, because the form store sanitises with them too.
using namespace input_types;

void dom_bindings::install_control_methods(context & cx) {
    const std::initializer_list<const char *> html = {"HTMLElement"};
    const auto method = [&](std::initializer_list<const char *> on, const char * name,
                            unsigned length, script::native_fn fn) {
        define_operation(cx, on, name, length, std::move(fn));
    };

    // `element.click()` is implemented by dom_bindings::click, next to the
    // engine's own mouse events, because a synthetic click is one of them.
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
    install_control_pickers(cx);
}

} // namespace ctbrowser::shell
