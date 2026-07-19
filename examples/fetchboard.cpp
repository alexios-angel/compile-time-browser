// The compile-time network example: examples/fetchboard.html awaits
// fetch() of REAL resources - example.com and a commit-pinned
// raw.githubusercontent.com JSON - which the build fetched via
// std::fetch and embedded (assets.hpp scans the script's fetch("...")
// literals). The runtime opens no sockets; await just unwraps the
// settled bytes.
//
// Build: make FETCH=1 fetchboard      (plain `make fetchboard` builds
// offline and the page explains what is missing; needs SDL3)

#include <ctbrowser.hpp>
#include <ctbrowser/app.hpp>
#include <SDL3/SDL_main.h>

using fetchboard = ctbrowser::page<
#include "fetchboard.inc"
>;

int main(int, char **) {
	ctbrowser::app_options opts;
	opts.width = 640;
	opts.height = 400;
	return ctbrowser::run_app<fetchboard>(opts);
}
