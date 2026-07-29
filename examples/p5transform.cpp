// p5.js transforms: push/pop, translate, rotate, scale, and nesting.
//
// p5 keeps its OWN transform stack and pushes it onto the canvas with
// setTransform once per frame, so this exercises the seam between two stacks
// rather than the canvas's alone. Every square on the page is the same rect()
// call under a different matrix; the grey one at the origin is drawn outside
// every push, and is only in the corner if the stack unwound exactly.

#include <ctbrowser.hpp>

int main() {
    ctbrowser::app_options options;
    options.title = "p5.js transforms";
    options.width = 340;
    options.height = 260;
    return ctbrowser::run_app_file("examples/pages/p5-transform.html", options);
}
