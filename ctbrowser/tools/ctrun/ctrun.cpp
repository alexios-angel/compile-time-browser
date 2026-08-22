// ctrun - the launcher a packaged application is made of.
//
// `ctcompile` does not generate C++ and does not need a compiler on the machine
// that runs the result. It takes THIS binary, appends the application bundle to
// it, and writes the two out as one file - so the thing a user copies is an
// ordinary executable that happens to have an application stuck on the end.
// The master plan asks for exactly this: "the launcher is a fixed library, not
// generated C++".
//
// So there are two ways in, and they are the same code:
//   ./myapp            the bundle is appended to this executable
//   ctrun app.ctapp    the bundle is a file, which is how one is debugged
//                      without copying a launcher around
#include <ctbrowser.hpp>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace {

[[nodiscard]] std::vector<std::byte> read_file(const std::filesystem::path & path) {
    std::ifstream in{path, std::ios::binary};
    if (!in) { return {}; }
    const std::string raw{std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};
    std::vector<std::byte> out(raw.size());
    for (std::size_t i = 0; i < raw.size(); ++i) {
        out[i] = static_cast<std::byte>(static_cast<unsigned char>(raw[i]));
    }
    return out;
}

} // namespace

int main(int argc, char ** argv) {
    // THE APPENDED BUNDLE FIRST, because that is what a packaged application is.
    // Reading our own bytes rather than argv[0] is what makes this work when the
    // application was found on the PATH or reached through a symlink.
    const std::vector<std::byte> self = ctbrowser::shell::this_executable_bytes();
    if (const std::span<const std::byte> found = ctbrowser::shell::find_appended_bundle(self);
        !found.empty()) {
        return ctbrowser::run_bundle(found);
    }

    if (argc < 2) {
        std::fprintf(stderr,
                     "usage: ctrun <application.ctapp>\n"
                     "  this launcher carries no application of its own. `ctcompile <dir>` makes\n"
                     "  a bundle, and a packaged application is this binary with one appended.\n");
        return 2;
    }
    const std::vector<std::byte> bundle = read_file(argv[1]);
    if (bundle.empty()) {
        std::fprintf(stderr, "ctrun: cannot read %s\n", argv[1]);
        return 2;
    }
    return ctbrowser::run_bundle(bundle);
}
