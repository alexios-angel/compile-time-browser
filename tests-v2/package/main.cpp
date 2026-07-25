// What an application looks like: one import, one link target, no SDL.
//
// This file is the acceptance criterion for the whole packaging story. If it
// compiles against an installed prefix, an outside project can use ctbrowser.

import ctbrowser;

#include <cstdio>

int main() {
	ctbrowser::app_options options;
	options.width = 200;
	options.height = 120;
	options.max_frames = 1;

	// Headless, one frame, no window - so this runs anywhere, including a CI
	// container with no display.
	const int result = ctbrowser::run_app(
	    "<title>package</title><style>p{color:#0000ff}</style><p>installed</p>", options);
	std::printf(result == 0 ? "package check ok\n" : "package check FAILED\n");
	return result;
}
