#pragma once
// COMMAND LINES, for the test programs that take one: Boost.ProgramOptions.
//
// A test declares what it accepts with Boost's own types - an
// options_description, plus a positional_options_description for bare
// arguments - and parse_options() reads argv against them. Every spelling the
// callers already use keeps working: lit RUN lines, CMake ARGS and the tools/
// scripts pass `--flag value` and bare paths, and Boost reads both (and
// `--flag=value`).
//
// A command line that does not fit is a usage error, never a run with a
// default nobody asked for: parse_options() prints why and the options, and
// the test returns its usage exit code.
//
// Boost.ProgramOptions is COMPILED, so a test that includes this links
// Boost::program_options too; cmake/modules/CTTest.cmake finds it.

#include <cstdio>
#include <optional>
#include <sstream>

#include <boost/program_options.hpp>

namespace ctbrowser_test {

[[nodiscard]] inline std::optional<boost::program_options::variables_map> parse_options(
    int argc, char ** argv, const boost::program_options::options_description & options,
    const boost::program_options::positional_options_description & positional = {}) {
    namespace po = boost::program_options;
    po::variables_map vm;
    try {
        po::store(po::command_line_parser(argc, argv).options(options).positional(positional).run(),
                  vm);
        po::notify(vm);
    } catch (const po::error & e) {
        std::ostringstream usage;
        usage << options;
        std::fprintf(stderr, "%s\n%s", e.what(), usage.str().c_str());
        return std::nullopt;
    }
    return vm;
}

} // namespace ctbrowser_test
