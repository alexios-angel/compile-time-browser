// p5.js input: pointer and keyboard, reaching a sketch.
//
// The one p5 page here with no golden, deliberately: what it draws is a
// function of input, and input is what tools/compare.py supplies - the same
// clicks and keystrokes through this and through Chrome, side by side, so the
// two can be watched rather than guessed at.
//
//   tools/compare.py start --engine=ctbrowse --engine=chrome --headed
//       --delay 400 examples/pages/p5-events.html
//
// (written without a trailing backslash on purpose: a `\` at the end of a `//`
// line splices the NEXT line into the comment, so gcc's -Wcomment rejects it
// and -Werror stops the build. Clang says nothing, which is why it survived
// here until this tree was first compiled with gcc on the devbox.)
//
// p5 2.x listens for POINTER events and converts each one's viewport
// coordinates through the canvas's getBoundingClientRect(). Every link in that
// chain has to work or the sketch renders perfectly and never responds, which
// is indistinguishable from a sketch with no interaction in it - so the page
// prints the state it derived, and a screenshot says which link failed.

#include <ctbrowser.hpp>

int main() {
    ctbrowser::app_options options;
    options.title = "p5.js input";
    options.width = 340;
    options.height = 260;
    return ctbrowser::run_app_file("examples/pages/p5-events.html", options);
}
