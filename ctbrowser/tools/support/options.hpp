//===- options.hpp - Command-line parsing -----------------------*- C++ -*-===//
//
// Command-line parsing for every program here that takes arguments, through
// Boost.ProgramOptions. The tools beside this directory parse argv through it,
// and so do the test programs that take arguments, in ctbrowser and ctcompile
// alike.
//
// A program declares what it accepts with Boost's own types (an
// options_description, plus a positional_options_description for bare
// arguments) and parse_options() reads argv against them. Every spelling the
// callers already use keeps working: lit RUN lines, CMake ARGS and the tools/
// scripts pass `--flag value` and bare paths, and Boost reads both (and
// `--flag=value`).
//
// A command line that does not fit is a usage error, never a run with a
// default nobody asked for: parse_options() prints why and the options, and
// the program returns its usage exit code.
//
// Link the ctbrowser-options target (ctbrowser/tools/support/CMakeLists.txt) to
// use it: it brings this directory and Boost::program_options, which is a
// compiled library, not header-only.
//
//===----------------------------------------------------------------------===//
#pragma once

#include <cstdio>
#include <optional>
#include <sstream>
#include <vector>

#include <boost/program_options.hpp>

namespace ctbrowser_cli {

/// A flag that takes exactly `count` values: `--size W H`. multitoken() would
/// take every bare argument after the flag, so `--size 800 600 page.html` would
/// read the page as a third size. Given twice it is an error, as any one-value
/// flag is, rather than four values of which the first two quietly win. Boost
/// owns what add_options() is handed, which is why this is passed as
/// `new exactly<int>(&size, 2)`, as po::value is.
template <class T> class exactly : public boost::program_options::typed_value<std::vector<T>> {
public:
    exactly(std::vector<T> * store, unsigned count)
        : boost::program_options::typed_value<std::vector<T>>(store), count_{count} {}
    [[nodiscard]] unsigned min_tokens() const override { return count_; }
    [[nodiscard]] unsigned max_tokens() const override { return count_; }
    void xparse(boost::any & store, const std::vector<std::string> & tokens) const override {
        boost::program_options::validators::check_first_occurrence(store);
        boost::program_options::typed_value<std::vector<T>>::xparse(store, tokens);
    }

private:
    unsigned count_;
};

/// Parses argv against `options` and `positional`. On a usage error it prints
/// why and the option list to stderr and returns nullopt; the caller then
/// returns its usage exit code.
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

} // namespace ctbrowser_cli
