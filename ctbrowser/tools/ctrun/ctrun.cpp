//===- ctbrowser/tools/ctrun/ctrun.cpp - Packaged-application launcher ----===//
//
// The launcher a packaged application is made of. Packaging does not generate
// C++ or need a compiler on the target machine: `ctcompile` takes this binary,
// appends the application bundle to it and writes the two out as one file, so
// what a user copies is an ordinary executable with an application stuck on
// the end. The launcher is a fixed program, never generated code.
//
// There are two ways in, and they are the same code:
//   ./myapp            the bundle is appended to this executable
//   ctrun app.ctapp    the bundle is a file, which is how one is debugged
//                      without copying a launcher around
//
//===----------------------------------------------------------------------===//
#include <ctbrowser.hpp>

#include "options.hpp"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace {

/// The whole file as bytes; empty if it cannot be read.
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
    namespace po = boost::program_options;
    // Check for an appended bundle first, because that is what a packaged
    // application is.
    // Reading our own bytes rather than argv[0] is what makes this work when the
    // application was found on the PATH or reached through a symlink.
    const std::vector<std::byte> self = ctbrowser::shell::this_executable_bytes();
    if (const std::span<const std::byte> found = ctbrowser::shell::find_appended_bundle(self);
        !found.empty()) {
        // A packaged application answers --help and --info before starting, so
        // a user can find out what an executable they were handed is. These two
        // names are taken from the application; packaged apps cannot receive
        // arguments of their own yet, so nothing is shadowed. When they can,
        // switch to a `--ctrun-` prefix and a `--` separator.
        //
        // --info prints the manifest raw; it is already JSON.
        //
        // Every other argument is ignored, never refused, so nothing here can
        // stop the application starting. Boost reads only these two exact names
        // (allow_unregistered, no positional arguments, no guessing `--inf` as
        // `--info`, no sticky `-hx`), and the parsed options are walked in order
        // so the first one wins and a repeat is not an error.
        po::options_description options;
        auto add = options.add_options();
        add("help,h", "say what this is");
        add("info", "what it carries, as JSON");
        std::vector<po::option> given;
        try {
            given = po::command_line_parser(argc, argv)
                        .options(options)
                        .style(po::command_line_style::unix_style &
                               ~po::command_line_style::allow_guessing &
                               ~po::command_line_style::allow_sticky)
                        .allow_unregistered()
                        .run()
                        .options;
        } catch (const po::error &) {
            // `--info=x`, `--x=`: a command line Boost cannot read just starts
            // the application.
        }
        for (const po::option & flag : given) {
            if (flag.string_key == "help") {
                std::printf("This is a ctbrowser application, packaged by ctcompile.\n"
                            "  --info   what it carries, as JSON\n");
                return 0;
            }
            if (flag.string_key == "info") {
                const auto loaded = ctbrowser::shell::read_bundle(found);
                if (!loaded.ok) {
                    std::fprintf(stderr, "%s\n", loaded.error.c_str());
                    return 2;
                }
                const std::string manifest = loaded.value.meta("manifest");
                if (manifest.empty()) {
                    std::fprintf(stderr, "this application carries no manifest\n");
                    return 2;
                }
                std::fwrite(manifest.data(), 1, manifest.size(), stdout);
                return 0;
            }
        }
        return ctbrowser::run_bundle(found);
    }

    std::string application;
    po::options_description options{
        "usage: ctrun <application.ctapp>\n"
        "  this launcher carries no application of its own. `ctcompile <dir>` makes\n"
        "  a bundle, and a packaged application is this binary with one appended."};
    options.add_options()("application", po::value(&application)->required(),
                          "the bundle to run, as `ctcompile --bundle` wrote it");
    po::positional_options_description positional;
    positional.add("application", 1);
    if (!ctbrowser_cli::parse_options(argc, argv, options, positional)) { return 2; }
    const std::vector<std::byte> bundle = read_file(application);
    if (bundle.empty()) {
        std::fprintf(stderr, "ctrun: cannot read %s\n", application.c_str());
        return 2;
    }
    // A loose bundle is a developer's, so it takes the CTBROWSER_* overrides
    // whatever it was packaged with; an appended one (above) does not.
    return ctbrowser::run_bundle(bundle, {}, true);
}
