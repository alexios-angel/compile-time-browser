//===- ctbrowser/examples/demos/fetchboard.cpp - fetch() demo - -*- C++ -*-===//
//
// fetch(), both ways: a resource baked into the binary and a real HTTP request.
//
// The asset registry is consulted first, so the first card is answered with no
// socket at all. The second goes to the network, and takes its catch branch
// when there is none, which is what `CTBROWSER_NETWORK=0` sets up. That makes
// the run deterministic, so this example can be a test.
//
// The page calls fetch() at run time like any web page; the engine has a real
// HTTP client.
//
//===----------------------------------------------------------------------===//

#include <ctbrowser.hpp>

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
    std::ifstream in{"examples/pages/fetchboard-data.json", std::ios::binary};
    const std::string text{std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};
    std::vector<std::byte> bytes(text.size());
    for (std::size_t i = 0; i < text.size(); ++i) {
        bytes[i] = static_cast<std::byte>(static_cast<unsigned char>(text[i]));
    }
    options.assets.push_back({"fetchboard-data.json", std::move(bytes)});

    return ctbrowser::run_app_file("examples/pages/fetchboard.html", options);
}
