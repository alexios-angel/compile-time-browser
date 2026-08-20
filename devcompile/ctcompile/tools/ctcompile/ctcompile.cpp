// ctcompile - the whole-application compiler.
//
// A STUB, and honest about it. Phase -1 asks for a sibling project that builds
// an executable so the monorepo migration can be finished and verified before
// any compiler work starts; what this reports today is what it was built
// against, which is the one thing worth knowing about a compiler that cannot
// compile yet.
//
// THE COMMAND LINE IS REAL EVEN THOUGH THE COMPILER IS NOT. `--version` and
// `--help` work, and the options the pipeline will need are declared and
// rejected with a message naming the phase that implements them - which is
// more useful than an unknown-option error, and keeps the surface in one place
// as the phases land.
//
// The running plan is ctcompile/docs/plans/ctcompile.md.
//
// The pipeline this becomes: HTML through ctbrowser's own parser into a
// document blueprint, CSS into a compiled style program, JavaScript through
// ctjs and ctbrowser's bytecode compiler into a CTJS MLIR dialect, and out
// through EmitC to native objects.
#include <ctcompile/Support/Version.hpp>

#include <boost/program_options.hpp>

#include <exception>
#include <filesystem>
#include <iostream>
#include <string>

namespace po = boost::program_options;

int main(int argc, char ** argv) try {
    // VISIBLE and HIDDEN, which is what makes `--help` readable: the positional
    // application directory is documented in the usage line rather than listed
    // as an option nobody would pass by name.
    po::options_description visible{
        "ctcompile - ahead-of-time compiler for ctbrowser applications"};
    visible.add_options()                                        //
        ("help,h", "show this message")                          //
        ("version,v", "report the compiler and engine versions") //
        ("output,o", po::value<std::string>()->value_name("FILE"),
         "where to write the application executable") //
        ("verbose", po::bool_switch(), "report each stage as it runs");

    po::options_description hidden;
    hidden.add_options()("application", po::value<std::string>()->value_name("DIR"),
                         "the application directory to compile");

    po::options_description all;
    all.add(visible).add(hidden);
    po::positional_options_description positional;
    positional.add("application", 1);

    po::variables_map options;
    po::store(po::command_line_parser{argc, argv}.options(all).positional(positional).run(),
              options);
    po::notify(options);

    const auto usage = [&](std::ostream & out) -> std::ostream & {
        out << "usage: ctcompile [options] <application-directory>\n\n" << visible;
        return out;
    };

    if (options.count("help") != 0) {
        usage(std::cout) << "\nCompilation is not implemented yet; see ctcompile/README.md.\n";
        return 0;
    }
    if (options.count("version") != 0) {
        std::cout << "ctcompile " << ctcompile::version_string() << " ("
                  << ctcompile::engine_summary() << ")\n";
        return 0;
    }
    if (options.count("application") == 0) {
        usage(std::cerr);
        return 2;
    }

    // A directory that is not there is worth saying so about now, rather than
    // in whichever phase first opens it.
    const std::filesystem::path application{options["application"].as<std::string>()};
    if (!std::filesystem::is_directory(application)) {
        std::cerr << "ctcompile: " << application << " is not a directory\n";
        return 2;
    }
    std::cerr
        << "ctcompile: cannot compile " << application
        << " yet - the pipeline lands in Phases 0 through 20. This build exists so the\n"
           "           monorepo split could be verified end to end; see ctcompile/README.md.\n";
    return 3;
} catch (const po::error & bad) {
    // Boost's own message names the offending option, which is the useful half.
    std::cerr << "ctcompile: " << bad.what() << "\ntry `ctcompile --help`\n";
    return 2;
} catch (const std::exception & failed) {
    std::cerr << "ctcompile: " << failed.what() << '\n';
    return 1;
}
