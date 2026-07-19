// The web-compat example: the MDN breakout tutorial, verbatim, from
// the actual examples/pong.html FILE - the build wraps it into a
// raw-string .inc that #include's straight into the page<...> template
// argument, and the engine's web API (document/addEventListener/
// requestAnimationFrame/canvas paths) does the rest. ArrowLeft/
// ArrowRight or the mouse move the paddle.
//
// Build: make pong   (or the CMake examples; needs SDL3)

#include <ctbrowser.hpp>
#include <ctbrowser/app.hpp>
#include <SDL3/SDL_main.h>

using pong = ctbrowser::page<
#include "pong.inc"
>;

int main(int, char **) {
	ctbrowser::app_options opts;
	opts.width = 480;
	opts.height = 320;
	opts.logical_w = 480;
	opts.logical_h = 320;
	return ctbrowser::run_app<pong>(opts);
}
