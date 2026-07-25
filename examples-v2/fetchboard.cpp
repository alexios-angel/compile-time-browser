// fetch, both ways: a resource baked into the binary and a real HTTP request.
//
// The registry is consulted first, so the first card answers with no socket at
// all. The second one goes to the network - and takes its catch branch when
// there is none, which is what `CTBROWSER_NETWORK=0` sets up. That is how this
// example can be a test: the run is deterministic and touches nothing outside
// the process.
//
// The page is what changed most in the port. v1's fetched at COMPILE time
// through a patched clang's std::fetch; v2 has a real HTTP client, so the page
// is written the way a page would be.

import ctbrowser;

#include <fstream>
#include <iterator>
#include <string>
#include <vector>

int main() {
	ctbrowser::app_options options;
	options.title = "fetchboard";
	options.width = 720;
	options.height = 420;

	// Bake the JSON in, which is what `app_options::assets` is for: the page
	// asks for "fetchboard-data.json" and never learns whether it came from the
	// binary, the disk or the network.
	std::ifstream in{"examples-v2/pages/fetchboard-data.json", std::ios::binary};
	const std::string text{std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};
	std::vector<std::byte> bytes(text.size());
	for (std::size_t i = 0; i < text.size(); ++i) {
		bytes[i] = static_cast<std::byte>(static_cast<unsigned char>(text[i]));
	}
	options.assets.push_back({"fetchboard-data.json", std::move(bytes)});

	return ctbrowser::run_app_file("examples-v2/pages/fetchboard.html", options);
}
