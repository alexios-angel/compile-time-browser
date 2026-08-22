// ctcompile - an application directory in, an executable out.
//
//     ctcompile app/ -o myapp        then ./myapp
//
// WHAT IT ACTUALLY DOES, stated plainly because the word "compiler" invites a
// bigger claim than this earns today. It loads the page once with the engine
// that will run it, compiles every classic <script> to a program image, packs
// the page, its resources and those images into one bundle, and appends that
// bundle to a copy of a fixed launcher. The result is a single executable that
// starts WITHOUT PARSING ITS OWN JAVASCRIPT.
//
// THAT IS THE WHOLE OF THE WIN AND IT IS MOST OF THE WIN. About forty percent
// of a page load is reading JavaScript and 1.4% is executing it, so removing
// the parse takes p5-basic.html from 69.65 ms to 19.93 - measured, in
// docs/baseline/page-load.json. What it does NOT do is generate native code:
// the bytecode still runs on the interpreter, and profiling the remainder
// (docs/baseline/page-load-profile.json) puts that at 17.4% of what is left.
// The MLIR and EmitC backends in Phases 7 through 12A are that 17.4%, and they
// are not here.
//
// IT ASKS THE ENGINE WHAT THE APPLICATION IS rather than working it out. Which
// scripts a page compiles and which resources it reaches for are decided by
// rules that live in the browser, and a packager holding a second copy of them
// is a packager free to drift - which shows up as an application that quietly
// compiles from source and is merely slow. So the page is LOADED, and then
// asked.
//
// The running plan is ctcompile/docs/plans/ctcompile.md.
#include <ctcompile/Support/Version.hpp>

#include <ctbrowser.hpp>

#include <boost/program_options.hpp>

#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

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
        ("entry", po::value<std::string>()->value_name("FILE"),
         "the page to package, relative to the application directory (default index.html)") //
        ("bundle", po::bool_switch(),
         "write the .ctapp bundle alone instead of an executable") //
        ("launcher", po::value<std::string>()->value_name("FILE"),
         "the launcher to build the executable from (default: ctrun beside this compiler)") //
        ("fonts", po::value<std::string>()->value_name("DIR"),
         "where the vendored faces are (default $CTBROWSER_FONT_PATH, else `fonts`)") //
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
        usage(std::cout) << "\nPackages an application into a .ctapp its launcher can run.\n";
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
    const bool verbose = options["verbose"].as<bool>();
    const std::filesystem::path entry = options.count("entry") != 0
                                            ? application / options["entry"].as<std::string>()
                                            : application / "index.html";
    if (!std::filesystem::is_regular_file(entry)) {
        std::cerr << "ctcompile: " << entry << " is not a file - name the page with --entry\n";
        return 2;
    }

    // ASK THE ENGINE WHAT THIS APPLICATION IS, rather than working it out here.
    // The page is loaded once, headless, by the same browser that will run it -
    // and then it is asked two questions only it can answer: which scripts it
    // compiles, and which resources it went looking for. A packager that
    // re-derived either would be keeping a second copy of a rule that lives in
    // the engine, free to drift from the one that decides at run time.
    std::string html;
    {
        std::ifstream in{entry, std::ios::binary};
        html.assign(std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{});
    }
    if (html.empty()) {
        std::cerr << "ctcompile: " << entry << " is empty\n";
        return 2;
    }

    ctbrowser::shell::app_bundle bundle;
    std::vector<std::string> scripts;
    std::vector<std::string> modules;
    std::vector<std::pair<std::string, std::vector<std::byte>>> resources;
    std::string page_error;
    std::string font_directory;
    bool real_fonts = false;
    std::size_t ticks = 0;
    {
        ctbrowser::browser probe{ctbrowser::browser_options{800, 600}};
        // The page's own directory, so `<script src>` and an `<img src>` resolve
        // exactly as they do when the page is opened from there.
        probe.assets().set_base_path(entry.parent_path());

        // REAL FONTS, BEFORE THE PAGE LOADS, exactly as run_app does it.
        //
        // Not a detail, and it was found by comparing renders: the vendored
        // faces are loaded THROUGH the asset registry, and a packaged
        // application's registry is sealed - so an application packaged without
        // them silently drops to the built-in bitmap font. It exits 0, it
        // renders, and it just looks worse, which is the failure this whole
        // tool is built to refuse. Asking here puts the faces in `requested()`
        // and the loop below packs them like any other resource.
        //
        // The DIRECTORY is recorded in the bundle because it is part of the
        // name: the registry key is "<dir>/Tinos-Regular.ttf", so a run that
        // resolved a different directory would ask for a name nothing carries.
        font_directory = options.count("fonts") != 0 ? options["fonts"].as<std::string>()
                                                     : ctbrowser::browser::default_font_directory();
        real_fonts = probe.use_real_fonts(font_directory);

        probe.load_html(html);

        // AND THEN LET IT RUN, because a load is not when a page asks for its
        // resources. `fetch` and an `img.src =` assignment QUEUE the request and
        // it is drained from `tick`; p5 loads in `preload` and Phaser in the
        // first game step, both inside a callback. Asking straight after
        // load_html sees the markup's resources and NOTHING a script wanted -
        // which is every sprite, atlas and level in the applications this is
        // for, packaged as a warning-free success and broken at run time.
        //
        // UNTIL IT STOPS ASKING, rather than for a fixed number of frames: a
        // count is a guess about someone else's loading screen. Sixty frames is
        // a ceiling on a page that asks for something new forever, not a
        // target - a static page settles on the second one.
        constexpr std::size_t frame_ceiling = 60;
        std::size_t asked_for = probe.assets().requested().size();
        for (; ticks < frame_ceiling; ++ticks) {
            probe.tick(16.0);
            const std::size_t now = probe.assets().requested().size();
            if (now == asked_for && ticks > 0) { break; }
            asked_for = now;
        }

        scripts = probe.script_sources();
        modules = probe.module_sources();
        page_error = probe.script_error();

        // THE RESOURCES, THROUGH THE REGISTRY THAT ANSWERED THE PAGE. This used
        // to build a fresh `asset_registry{}` with no base path, which probes
        // the working directory and two levels above it - a DIFFERENT rule from
        // the one the page was answered by, and the header of assets.hpp spends
        // a paragraph explaining that a packager must not keep a second copy of
        // it. It could resolve a name from ctcompile's own build tree that the
        // page resolved from the application, and ship those bytes under the
        // right name with nothing printed.
        //
        // The names are COPIED first: `load` records what it was asked for, so
        // iterating the live list while calling it is iterating a vector that
        // can grow underneath.
        const std::vector<std::string> wanted = probe.assets().requested();
        for (const std::string & name : wanted) {
            resources.emplace_back(name, probe.assets().load(name));
        }
    }

    // A PAGE THAT ALREADY FAILED IS NOT PACKAGED. The sharpest case is a
    // `<script src>` that did not resolve: the engine records it and then
    // carries on with that script simply ABSENT, so every later count is clean
    // and the application would ship missing its library. Refusing here is the
    // only place that catches it.
    if (verbose) {
        std::cerr << "ctcompile:   probe  settled after " << ticks << " frame(s), "
                  << resources.size() << " resource(s) asked for\n";
    }

    // MODULE SCRIPTS CANNOT BE COMPILED AHEAD OF TIME YET, and shipping the
    // application anyway is the silent failure this tool exists to avoid: there
    // is no image path into `load_module`, so it would parse all of its
    // JavaScript at every start while every count here reads a truthful zero.
    // Better to say so than to package something whose only defect is that it
    // is exactly as slow as it was before.
    if (!modules.empty()) {
        std::cerr << "ctcompile: " << entry << " has " << modules.size()
                  << " module script(s) (<script type=\"module\">), which cannot be compiled "
                     "ahead of time yet\n";
        return 1;
    }

    if (!page_error.empty()) {
        std::cerr << "ctcompile: " << entry << " does not load cleanly, so it is not packaged:\n"
                  << "           " << page_error << '\n';
        return 1;
    }

    bundle.entries.push_back(
        {ctbrowser::shell::bundle_kind::html,
         {},
         std::vector<std::byte>(reinterpret_cast<const std::byte *>(html.data()),
                                reinterpret_cast<const std::byte *>(html.data() + html.size()))});

    // THE RESOURCES, UNDER THE NAMES THE DOCUMENT USED. p5-basic.html asks for
    // `../../vendor/p5/p5.js`, which is not a path relative to the application
    // directory and is not something a packager could invent.
    std::size_t assets_packed = 0;
    for (auto & [name, bytes] : resources) {
        if (bytes.empty()) {
            // A NAME THE PAGE ASKED FOR AND NOTHING ANSWERED. It got nothing
            // during this load too, so the page already tolerates it - but a
            // packaged application is SEALED and will not look on disk, so this
            // is the last chance anyone hears about it.
            std::cerr << "ctcompile: warning: " << name
                      << " was asked for and nothing answered; the packaged application will not "
                         "find it either\n";
            continue;
        }
        ++assets_packed;
        if (verbose) {
            std::cerr << "ctcompile:   asset  " << name << "  " << bytes.size() << " bytes\n";
        }
        bundle.entries.push_back({ctbrowser::shell::bundle_kind::asset, name, std::move(bytes)});
    }

    // AND THE SCRIPTS, COMPILED. This is the whole point: reading JavaScript is
    // about forty percent of a page load, and an image removes it.
    for (const std::string & text : scripts) {
        const auto compiled = ctbrowser::script::compiler::compile(text);
        if (!compiled.ok) {
            std::cerr << "ctcompile: a script does not compile: " << compiled.error << '\n';
            return 1;
        }
        std::vector<std::byte> image = ctbrowser::script::write_image(compiled);
        if (image.empty()) {
            std::cerr << "ctcompile: " << ctbrowser::script::write_error() << '\n';
            return 1;
        }
        if (verbose) {
            std::cerr << "ctcompile:   script " << text.size() << " bytes of source -> "
                      << image.size() << " bytes of image (" << compiled.functions.size()
                      << " functions)\n";
        }
        bundle.entries.push_back(
            {ctbrowser::shell::bundle_kind::script_image, {}, std::move(image)});
    }

    if (real_fonts) {
        bundle.entries.push_back(
            {ctbrowser::shell::bundle_kind::meta, "font_path",
             std::vector<std::byte>(reinterpret_cast<const std::byte *>(font_directory.data()),
                                    reinterpret_cast<const std::byte *>(font_directory.data() +
                                                                        font_directory.size()))});
    } else {
        // NOT BEHIND --verbose. An application that renders with the built-in
        // bitmap font because the faces were not found where this looked is an
        // application that exits 0 and just looks worse, which is the failure
        // this tool exists to refuse. Say it every time, and say where to put
        // them.
        std::cerr << "ctcompile: warning: no fonts were found in \"" << font_directory
                  << "\", so the application will render with the built-in bitmap font"
                     " - name the directory with --fonts\n";
    }
    bundle.entries.push_back(
        {ctbrowser::shell::bundle_kind::meta, "title", std::vector<std::byte>{}});
    {
        const std::string name = entry.stem().string();
        auto & title = bundle.entries.back().bytes;
        title.assign(reinterpret_cast<const std::byte *>(name.data()),
                     reinterpret_cast<const std::byte *>(name.data() + name.size()));
    }

    const std::vector<std::byte> bytes = ctbrowser::shell::write_bundle(bundle);
    if (bytes.empty()) {
        std::cerr << "ctcompile: " << ctbrowser::shell::bundle_write_error() << '\n';
        return 1;
    }

    // AN EXECUTABLE, NOT A BUNDLE, unless the caller asks for the bundle alone.
    //
    // NOTHING IS GENERATED AND NOTHING IS COMPILED HERE. `ctrun` is a fixed
    // launcher this project builds like any other tool, and a packaged
    // application is a byte-for-byte copy of it with the bundle stuck on the end
    // and a trailer saying where that starts. A linked ELF does not care what
    // follows its last section - so the machine that RUNS the result needs no
    // toolchain, and this needs no linker.
    const bool bundle_only = options["bundle"].as<bool>();
    const std::filesystem::path out =
        options.count("output") != 0
            ? std::filesystem::path{options["output"].as<std::string>()}
            : std::filesystem::path{entry.stem().string() + (bundle_only ? ".ctapp" : "")};

    std::vector<std::byte> written = bytes;
    if (!bundle_only) {
        std::filesystem::path launcher =
            options.count("launcher") != 0
                ? std::filesystem::path{options["launcher"].as<std::string>()}
                : std::filesystem::path{};
        if (launcher.empty()) {
            // BESIDE THIS COMPILER, which is where a build puts them both, and
            // found through /proc/self/exe rather than argv[0] so it still works
            // when ctcompile was reached through the PATH or a symlink.
            std::error_code failed;
            const std::filesystem::path self =
                std::filesystem::read_symlink("/proc/self/exe", failed);
            if (!failed) { launcher = self.parent_path() / "ctrun"; }
        }
        if (launcher.empty() || !std::filesystem::is_regular_file(launcher)) {
            std::cerr << "ctcompile: cannot find a launcher to build an executable from"
                      << (launcher.empty() ? std::string{} : " at " + launcher.string())
                      << "\n           name one with --launcher, or ask for the bundle alone "
                         "with --bundle\n";
            return 1;
        }
        std::vector<std::byte> base;
        {
            std::ifstream in{launcher, std::ios::binary};
            const std::string raw{std::istreambuf_iterator<char>{in},
                                  std::istreambuf_iterator<char>{}};
            base.resize(raw.size());
            for (std::size_t i = 0; i < raw.size(); ++i) {
                base[i] = static_cast<std::byte>(static_cast<unsigned char>(raw[i]));
            }
        }
        if (base.empty()) {
            std::cerr << "ctcompile: cannot read the launcher " << launcher << '\n';
            return 1;
        }
        written = ctbrowser::shell::append_bundle_to(base, bytes);
    }

    {
        std::ofstream write{out, std::ios::binary};
        if (!write) {
            std::cerr << "ctcompile: cannot write " << out << '\n';
            return 1;
        }
        write.write(reinterpret_cast<const char *>(written.data()),
                    static_cast<std::streamsize>(written.size()));
    }
    if (!bundle_only) {
        std::error_code ignored;
        std::filesystem::permissions(out,
                                     std::filesystem::perms::owner_exec |
                                         std::filesystem::perms::group_exec |
                                         std::filesystem::perms::others_exec,
                                     std::filesystem::perm_options::add, ignored);
    }

    std::cerr << "ctcompile: " << out << " - " << scripts.size() << " script"
              << (scripts.size() == 1 ? "" : "s") << " compiled, " << assets_packed << " resource"
              << (assets_packed == 1 ? "" : "s") << ", " << written.size() << " bytes"
              << (bundle_only ? " (bundle)" : " (executable)") << '\n';
    return 0;
} catch (const po::error & bad) {
    // Boost's own message names the offending option, which is the useful half.
    std::cerr << "ctcompile: " << bad.what() << "\ntry `ctcompile --help`\n";
    return 2;
} catch (const std::exception & failed) {
    std::cerr << "ctcompile: " << failed.what() << '\n';
    return 1;
}
