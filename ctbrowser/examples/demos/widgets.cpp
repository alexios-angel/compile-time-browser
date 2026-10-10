//===- ctbrowser/examples/demos/widgets.cpp - Form widget gallery demo ----===//
//
// The form gallery: text and password fields, a textarea, checkboxes, radios, a
// <select> with a real popup, submit/reset/disabled buttons, <details>, a link,
// and a script that reacts to every one of them.
//
// It is the interaction test page, as `elements` is the rendering one. All of
// it works: typing and the caret, focus, checkbox and radio groups, the select
// popup, submit and reset, the clipboard (Ctrl+C/X/V and the right-click menu),
// and system cursors over links and fields. The page uses only ordinary web
// APIs.
//
//===----------------------------------------------------------------------===//

#include <ctbrowser.hpp>

int main() {
    ctbrowser::app_options options;
    options.title = "widget gallery";
    options.width = 640;
    options.height = 560;
    return ctbrowser::run_app_file("examples/pages/widgets.html", options);
}
