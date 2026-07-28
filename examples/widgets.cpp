// The form gallery: text and password fields, a textarea, checkboxes, radios,
// a <select> with a real popup, submit/reset/disabled buttons, <details>, a
// link, and a script that reacts to every one of them.
//
// It is the interaction proof, the way `elements` is the rendering one. As of
// stage 8 all of it works: typing and the caret, focus, checkbox and radio
// groups, the select popup, submit and reset, the clipboard (Ctrl+C/X/V and
// the right-click menu), and system cursors over links and fields.
//
// The page is the previous engine's, unchanged apart from being a file rather than an NTTP -
// it was already written against the ordinary web APIs.

#include <ctbrowser/ctbrowser.hpp>

int main() {
    ctbrowser::app_options options;
    options.title = "widget gallery";
    options.width = 640;
    options.height = 560;
    return ctbrowser::run_app_file("examples/pages/widgets.html", options);
}
