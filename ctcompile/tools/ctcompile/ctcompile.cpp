//===- ctcompile/tools/ctcompile/ctcompile.cpp - Application packager -----===//
//
// ctcompile: an application directory in, an executable out.
//
//     ctcompile app/ -o myapp        then ./myapp
//
// What it does, stated plainly because the word "compiler" suggests more: it
// loads the page once with the engine that will run it, compiles every classic
// <script> to a program image, packs the page, its resources and those images
// into one bundle, and appends that bundle to a copy of a fixed launcher. The
// result is a single executable that starts without parsing its own
// JavaScript.
//
// That is the main speed-up: about 40% of a page load is reading JavaScript and
// 1.4% is executing it, so removing the parse takes p5-basic.html from
// 69.65 ms to 19.93 ms (measured; see ctcompile/docs/baseline/page-load.json).
// It does not generate native code: the bytecode still runs on the
// interpreter, which profiling (ctcompile/docs/baseline/page-load-profile.json)
// puts at 17.4% of the remaining time. Removing that cost is the job of the
// MLIR/EmitC native backends, which this tool does not use.
//
// It asks the engine what the application is rather than working it out.
// Which scripts a page compiles and which resources it requests are decided by
// rules in the browser; a packager with its own copy of them could drift, and
// the application would then silently compile from source and merely be slow.
// So the page is loaded, and then asked.
//
// The plan for this tool is ctcompile/docs/plans/ctcompile.md.
//
//===----------------------------------------------------------------------===//
#include <ctcompile/Support/Manifest.hpp>
#include <ctcompile/Support/Measure.hpp>
#include <ctcompile/Support/Version.hpp>

#include <ctbrowser.hpp>

#include "llvm/Option/ArgList.h"
#include "llvm/Option/OptTable.h"
#include "llvm/Support/Allocator.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/raw_ostream.h"

#include <bitset>
#include <exception>
#include <filesystem>
#include <iostream>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using llvm::opt::DefaultVis;
using llvm::opt::HelpHidden;

/// Option IDs generated from Options.td.
enum OptionID {
    OPT_INVALID,
#define OPTION(...) LLVM_MAKE_OPT_ID(__VA_ARGS__),
#include "Options.inc"
#undef OPTION
    OPT_LAST
};

#define OPTTABLE_STR_TABLE_CODE
#define OPTTABLE_PREFIXES_TABLE_CODE
#include "Options.inc"
#undef OPTTABLE_PREFIXES_TABLE_CODE
#undef OPTTABLE_STR_TABLE_CODE

constexpr llvm::opt::OptTable::Info option_info[] = {
#define OPTION(...) LLVM_CONSTRUCT_OPT_INFO(__VA_ARGS__),
#include "Options.inc"
#undef OPTION
};

/// The command-line option table generated from Options.td.
class Options : public llvm::opt::GenericOptTable {
public:
    Options() : GenericOptTable(OptionStrTable, OptionPrefixesTable, option_info) {
        setGroupedShortOptions(true);
        setDashDashParsing(true);
    }
};

} // namespace

/// A string's bytes, as a bundle entry stores them.
std::vector<std::byte> bytes_of(std::string_view text) {
    const std::span<const std::byte> bytes = std::as_bytes(std::span{text});
    return {bytes.begin(), bytes.end()};
}

using ctcompile::read_file;

/// Writes `bytes` to `path` atomically; prints the error and returns false on
/// failure.
bool write_file(const std::filesystem::path & path, std::span<const std::byte> bytes) {
    // LLVM writes a sibling temporary and renames only after a successful write.
    // An absolute path keeps a literal output named "-" from selecting stdout.
    if (auto error = llvm::writeToOutput(
            std::filesystem::absolute(path).string(), [&](llvm::raw_ostream & out) {
                out.write(reinterpret_cast<const char *>(bytes.data()), bytes.size());
                return llvm::Error::success();
            })) {
        std::cerr << "ctcompile: cannot write " << path << ": " << llvm::toString(std::move(error))
                  << '\n';
        return false;
    }
    return true;
}

/// Whether two paths name the same file, including through symlinks and hard
/// links; used to stop the outputs from overwriting an input.
bool same_file(const std::filesystem::path & left, const std::filesystem::path & right) {
    if (std::filesystem::weakly_canonical(std::filesystem::absolute(left)) ==
        std::filesystem::weakly_canonical(std::filesystem::absolute(right))) {
        return true;
    }
    std::error_code error;
    return std::filesystem::equivalent(left, right, error);
}

/// What loading the page reported: the scripts it compiles, its module
/// scripts, the resources it requested (with their bytes), any load error,
/// the font directory used, and how many frames the probe ran.
///
/// The page is loaded once, headless, by the same browser that will run it,
/// and then asked the two questions only it can answer: which scripts it
/// compiles, and which resources it requested. A packager that re-derived
/// either would keep a second copy of an engine rule, free to drift from the
/// one that decides at run time.
struct probed_page {
    std::vector<std::string> scripts;
    std::vector<std::string> modules;
    std::vector<std::pair<std::string, std::vector<std::byte>>> resources;
    std::string page_error;
    std::string font_directory;
    bool real_fonts = false;
    std::size_t ticks = 0;
};

/// Loads the application's entry page in a headless browser and records what
/// it needs (see probed_page).
probed_page probe_page(const llvm::opt::InputArgList & options, const std::filesystem::path & entry,
                       const std::string & html) {
    probed_page page;
    ctbrowser::browser probe{ctbrowser::browser_options{800, 600}};
    // The page's own directory, so `<script src>` and an `<img src>` resolve
    // exactly as they do when the page is opened from there.
    probe.assets().set_base_path(entry.parent_path());
    // Unconfined: this is the developer's own page, packaged from their tree,
    // and the corpus reaches `../../vendor/`. The bundle it produces is sealed.
    probe.assets().set_confine(false);

    // Load the real fonts before the page, as run_app does. The vendored faces
    // are served through the asset registry, and a packaged application's
    // registry is sealed, so faces not requested here would be missing at run
    // time and the app would silently fall back to the bitmap font. Requesting
    // them now puts them in requested(), and the loop below packs them.
    //
    // The directory is recorded because it is part of each face's registry key
    // ("<dir>/Tinos-Regular.ttf").
    page.font_directory = options.hasArg(OPT_fonts) ? options.getLastArgValue(OPT_fonts).str()
                                                    : ctbrowser::browser::default_font_directory();
    page.real_fonts = probe.use_real_fonts(page.font_directory);

    probe.load_html(html);

    // Then let the page run, because loading is not when a page asks for its
    // resources. `fetch` and an `img.src =` assignment queue the request, which
    // `tick` drains; p5 loads in `preload` and Phaser in the first game step,
    // both inside a callback. Asking straight after load_html would see only
    // the markup's resources and miss every sprite, atlas and level the
    // scripts request, and the package would be broken at run time.
    //
    // Run until it stops asking rather than for a fixed number of frames.
    // Sixty frames is a ceiling for a page that asks for something new
    // forever, not a target; a static page settles on the second frame.
    constexpr std::size_t frame_ceiling = 60;
    std::size_t asked_for = probe.assets().requested().size();
    for (; page.ticks < frame_ceiling; ++page.ticks) {
        probe.tick(16.0);
        const std::size_t now = probe.assets().requested().size();
        if (now == asked_for && page.ticks > 0) { break; }
        asked_for = now;
    }

    page.scripts = probe.script_sources();
    page.modules = probe.module_sources();
    page.page_error = probe.script_error();

    // Load resource bytes through the same registry that served the page, so
    // names resolve exactly as they did at load time. A fresh registry would
    // use a different lookup rule (see the header of
    // ctbrowser/shell/page/assets.hpp) and could package the wrong file under
    // the right name.
    //
    // Copy the names first: load() appends to requested(), so iterating the
    // live list while loading could invalidate the iterator.
    const std::vector<std::string> wanted = probe.assets().requested();
    for (const std::string & name : wanted) {
        page.resources.emplace_back(name, probe.assets().load(name));
    }
    return page;
}

/// The launcher to build the executable from: --launcher, else ctrun beside
/// this compiler, which is where a build puts them both. It is found through
/// /proc/self/exe rather than argv[0], so it still works when ctcompile was
/// reached through the PATH or a symlink. Empty when neither is known.
std::filesystem::path resolve_launcher(const llvm::opt::InputArgList & options) {
    std::filesystem::path launcher{options.getLastArgValue(OPT_launcher).str()};
    if (launcher.empty()) {
        std::error_code failed;
        const std::filesystem::path self = std::filesystem::read_symlink("/proc/self/exe", failed);
        if (!failed) { launcher = self.parent_path() / "ctrun"; }
    }
    return launcher;
}

int main(int argc, char ** argv) try {
    Options table;
    llvm::BumpPtrAllocator allocator;
    llvm::StringSaver saver{allocator};
    bool invalid = false;
    auto options = table.parseArgs(argc, argv, OPT_UNKNOWN, saver, [&](llvm::StringRef message) {
        llvm::errs() << "ctcompile: " << message << '\n';
        invalid = true;
    });
    if (invalid) {
        std::cerr << "try `ctcompile --help`\n";
        return 2;
    }
    std::bitset<OPT_LAST> seen;
    for (const auto * argument : options) {
        unsigned id = argument->getOption().getID();
        if (id == OPT_application) { id = OPT_INPUT; }
        if (seen.test(id)) {
            llvm::errs() << "ctcompile: " << argument->getSpelling()
                         << " may only be specified once\ntry `ctcompile --help`\n";
            return 2;
        }
        seen.set(id);
    }
    const auto help = [&](llvm::raw_ostream & to) {
        table.printHelp(to, "ctcompile [options] <application-directory>",
                        "ctcompile - ahead-of-time compiler for ctbrowser applications");
    };
    if (options.hasArg(OPT_help)) {
        help(llvm::outs());
        llvm::outs() << "\nPackages classic scripts as bytecode in a launcher executable "
                        "(or a .ctapp with --bundle).\n"
                        "This command does not compile JavaScript to native C++.\n";
        return 0;
    }
    if (options.hasArg(OPT_version)) {
        std::cout << "ctcompile " << ctcompile::version_string() << " ("
                  << ctcompile::engine_summary() << ")\n";
        return 0;
    }
    const auto * application_arg = options.getLastArg(OPT_INPUT, OPT_application);
    if (!application_arg) {
        help(llvm::errs());
        return 2;
    }

    // Report a missing directory now, rather than in whichever step first
    // opens it.
    const std::filesystem::path application{application_arg->getValue()};
    if (!std::filesystem::is_directory(application)) {
        std::cerr << "ctcompile: " << application << " is not a directory\n";
        return 2;
    }
    const bool verbose = options.hasArg(OPT_verbose);
    const std::filesystem::path entry =
        application / options.getLastArgValue(OPT_entry, "index.html").str();
    if (!std::filesystem::is_regular_file(entry)) {
        std::cerr << "ctcompile: " << entry << " is not a file - name the page with --entry\n";
        return 2;
    }
    const bool bundle_only = options.hasArg(OPT_bundle);
    const std::filesystem::path out =
        options.hasArg(OPT_output)
            ? std::filesystem::path{options.getLastArgValue(OPT_output).str()}
            : std::filesystem::path{entry.stem().string() + (bundle_only ? ".ctapp" : "")};
    const std::filesystem::path manifest_path{options.getLastArgValue(OPT_manifest).str()};
    if (out.empty() || (options.hasArg(OPT_manifest) && manifest_path.empty())) {
        std::cerr << "ctcompile: output and manifest paths must not be empty\n";
        return 2;
    }
    if (!manifest_path.empty() && same_file(out, manifest_path)) {
        std::cerr << "ctcompile: output and manifest must name different files\n";
        return 2;
    }
    const auto aliases_input = [&](const std::filesystem::path & input) {
        if (same_file(out, input) || (!manifest_path.empty() && same_file(manifest_path, input))) {
            std::cerr << "ctcompile: output or manifest would overwrite input " << input << '\n';
            return true;
        }
        return false;
    };
    if (aliases_input(entry)) { return 2; }

    const std::string html = read_file(entry);
    if (html.empty()) {
        std::cerr << "ctcompile: " << entry << " is empty\n";
        return 2;
    }

    ctbrowser::shell::app_bundle bundle;
    auto [scripts, modules, resources, page_error, font_directory, real_fonts, ticks] =
        probe_page(options, entry, html);

    if (verbose) {
        std::cerr << "ctcompile:   probe  settled after " << ticks << " frame(s), "
                  << resources.size() << " resource(s) asked for\n";
    }

    // Module scripts cannot be compiled ahead of time yet: there is no image
    // path into `load_module`, so a packaged application would still parse all
    // of its JavaScript at every start while every count here reads zero.
    // Refuse rather than ship something no faster than before.
    if (!modules.empty()) {
        std::cerr << "ctcompile: " << entry << " has " << modules.size()
                  << " module script(s) (<script type=\"module\">), which cannot be compiled "
                     "ahead of time yet\n";
        return 1;
    }

    // A page that already failed is not packaged. The main case is a
    // `<script src>` that did not resolve: the engine records the error and
    // carries on with that script absent, so every later count looks clean and
    // the application would ship without its library.
    if (!page_error.empty()) {
        std::cerr << "ctcompile: " << entry << " does not load cleanly, so it is not packaged:\n"
                  << "           " << page_error << '\n';
        return 1;
    }

    bundle.entries.push_back({ctbrowser::shell::bundle_kind::html, {}, bytes_of(html)});

    // The resources, under the names the document used. p5-basic.html asks
    // for `../../vendor/p5/p5.js`, which is not a path relative to the
    // application directory and not something a packager could invent.
    ctcompile::manifest record;
    record.compiler_version = ctcompile::version_string();
    record.engine = ctcompile::engine_summary();
    record.entry = entry.filename().string();
    record.bundle_format = ctbrowser::shell::bundle_format_version();
    record.image_format = ctbrowser::script::image_format_version();
    record.engine_fingerprint = ctbrowser::script::image_fingerprint();

    std::size_t assets_packed = 0;
    for (auto & [name, bytes] : resources) {
        if (bytes.empty()) {
            // A name the page asked for that nothing answered. It got nothing
            // during this load either, so the page already tolerates it, but a
            // packaged application is sealed and will not look on disk, so
            // this is the last chance to report it.
            std::cerr << "ctcompile: warning: " << name
                      << " was asked for and nothing answered; the packaged application will not "
                         "find it either\n";
            continue;
        }
        ++assets_packed;
        record.resources.push_back({name, bytes.size()});
        if (verbose) {
            std::cerr << "ctcompile:   asset  " << name << "  " << bytes.size() << " bytes\n";
        }
        bundle.entries.push_back({ctbrowser::shell::bundle_kind::asset, name, std::move(bytes)});
    }

    // The scripts, compiled to program images.
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
        record.scripts.push_back({record.scripts.size(), text.size(), image.size(),
                                  compiled.functions.size(),
                                  ctbrowser::script::image_source_hash(text)});
        bundle.entries.push_back(
            {ctbrowser::shell::bundle_kind::script_image, {}, std::move(image)});
    }

    if (real_fonts) {
        // Store the faces under a fixed `fonts/` prefix, not the build
        // machine's directory. The registry key for a face is
        // "<directory>/Tinos-Regular.ttf", and the directory that found them is
        // typically an absolute path into somebody's checkout; shipping that
        // would bake a build path into every application. The rename is a
        // prefix substitution on names the engine produced, not a second
        // implementation of how a face is named.
        const std::string from = font_directory + "/";
        const auto rename = [&](std::string & name) {
            if (name.rfind(from, 0) == 0) { name = "fonts/" + name.substr(from.size()); }
        };
        for (ctbrowser::shell::bundle_entry & one : bundle.entries) {
            if (one.kind == ctbrowser::shell::bundle_kind::asset) { rename(one.name); }
        }
        for (ctcompile::manifest_resource & one : record.resources) { rename(one.name); }
        font_directory = "fonts";
        record.font_directory = font_directory;
        bundle.entries.push_back(
            {ctbrowser::shell::bundle_kind::meta, "font_path", bytes_of(font_directory)});
    } else {
        // Always printed, not only with --verbose: an application that falls
        // back to the bitmap font exits 0 and just looks worse, so say it
        // every time and say how to fix it.
        std::cerr << "ctcompile: warning: no fonts were found in \"" << font_directory
                  << "\", so the application will render with the built-in bitmap font"
                     " - name the directory with --fonts\n";
    }
    bundle.entries.push_back(
        {ctbrowser::shell::bundle_kind::meta, "title", bytes_of(entry.stem().string())});
    // Off unless asked: a shipped application ignores CTBROWSER_TEST_FRAMES and
    // the other test overrides, which would otherwise let a player uncap or
    // speed it up.
    if (options.hasArg(OPT_allow_env_overrides)) {
        bundle.entries.push_back(
            {ctbrowser::shell::bundle_kind::meta, "env_overrides", bytes_of("1")});
    }

    // The manifest goes in the bundle, so a packaged application can always say
    // what it is without the directory it was built from. `bundle_bytes` is
    // the one field it cannot hold (adding the manifest changes it), so it
    // stays 0 in the copy inside and is filled in for the copy on disk.
    {
        const std::string json = ctcompile::to_json(record);
        bundle.entries.push_back({ctbrowser::shell::bundle_kind::meta, "manifest", bytes_of(json)});
    }

    const std::vector<std::byte> bytes = ctbrowser::shell::write_bundle(bundle);
    if (bytes.empty()) {
        std::cerr << "ctcompile: " << ctbrowser::shell::bundle_write_error() << '\n';
        return 1;
    }

    // An executable, not a bundle, unless the caller asks for the bundle alone.
    //
    // Nothing is generated or compiled here. `ctrun` is a fixed launcher this
    // project builds like any other tool, and a packaged application is a
    // byte-for-byte copy of it with the bundle appended and a trailer saying
    // where that starts. A linked ELF ignores what follows its last section,
    // so the machine that runs the result needs no toolchain and this needs no
    // linker.
    std::vector<std::byte> written = bytes;
    if (!bundle_only) {
        const std::filesystem::path launcher = resolve_launcher(options);
        if (launcher.empty() || !std::filesystem::is_regular_file(launcher)) {
            std::cerr << "ctcompile: cannot find a launcher to build an executable from"
                      << (launcher.empty() ? std::string{} : " at " + launcher.string())
                      << "\n           name one with --launcher, or ask for the bundle alone "
                         "with --bundle\n";
            return 1;
        }
        if (aliases_input(launcher)) { return 2; }
        const std::vector<std::byte> base = bytes_of(read_file(launcher));
        if (base.empty()) {
            std::cerr << "ctcompile: cannot read the launcher " << launcher << '\n';
            return 1;
        }
        written = ctbrowser::shell::append_bundle_to(base, bytes);
    }

    if (!write_file(out, written)) { return 1; }
    // The manifest beside it, when asked. `bundle_bytes` is filled in only here,
    // because it is the size of the file just written, which the copy inside
    // the bundle cannot know.
    if (options.hasArg(OPT_manifest)) {
        record.bundle_bytes = written.size();
        const std::string manifest = ctcompile::to_json(record);
        if (!write_file(manifest_path, std::as_bytes(std::span{manifest}))) { return 1; }
    }

    if (!bundle_only) {
        std::filesystem::permissions(out,
                                     std::filesystem::perms::owner_exec |
                                         std::filesystem::perms::group_exec |
                                         std::filesystem::perms::others_exec,
                                     std::filesystem::perm_options::add);
    }

    std::cerr << "ctcompile: " << out << " - " << scripts.size() << " script"
              << (scripts.size() == 1 ? "" : "s") << " compiled, " << assets_packed << " resource"
              << (assets_packed == 1 ? "" : "s") << ", " << written.size() << " bytes"
              << (bundle_only ? " (bundle)" : " (executable)") << '\n';
    return 0;
} catch (const std::exception & failed) {
    std::cerr << "ctcompile: " << failed.what() << '\n';
    return 1;
}
