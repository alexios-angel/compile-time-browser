//===- ctcompile/test/Packaging/AppBundle.cpp - App bundle tests ----------===//
//
// Tests the application bundle: does it round-trip, does the loader refuse what
// it should, and does a packaged page actually skip parsing its scripts?
//
// Shaped like ProgramImage.cpp, for a sharper reason. A bundle is a table of
// offsets read out of a file, that is, a list of instructions to go and look
// somewhere, and what it carries is program images, which are executable
// input. So most of the file is negative cases.
//
// The most important section is "a page packaged with its images does not
// parse its scripts": the others check that bytes survive a round trip, while
// that one checks, with a counter, that the images are actually used. A page
// that silently compiled from source renders exactly the same document.
//
//===----------------------------------------------------------------------===//
#include <ctbrowser/script/compile.hpp>
#include <ctbrowser/script/program_image.hpp>
#include <ctbrowser/shell/app_bundle.hpp>
#include <ctbrowser/shell/browser.hpp>
#include <ctbrowser/shell/page/assets.hpp>

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include "check.hpp"
#include "options.hpp"

using ctbrowser::shell::app_bundle;
using ctbrowser::shell::append_bundle_to;
using ctbrowser::shell::browser;
using ctbrowser::shell::browser_options;
using ctbrowser::shell::bundle_kind;
using ctbrowser::shell::find_appended_bundle;
using ctbrowser::shell::read_bundle;
using ctbrowser::shell::write_bundle;

namespace {

std::vector<std::byte> raw(std::string_view text) {
    return {reinterpret_cast<const std::byte *>(text.data()),
            reinterpret_cast<const std::byte *>(text.data() + text.size())};
}

std::string text_of(std::span<const std::byte> bytes) {
    return {reinterpret_cast<const char *>(bytes.data()), bytes.size()};
}

/// Fails the test unless read_bundle refuses `bytes` with an error message:
/// a refusal, not a crash and not a success. `what` names the case in the
/// failure message.
void must_refuse(std::vector<std::byte> bytes, std::string_view what) {
    const auto loaded = read_bundle(bytes);
    if (loaded.ok) {
        std::printf("FAIL %.*s - it was ACCEPTED\n", static_cast<int>(what.size()), what.data());
        ++ctbrowser_test_failures;
    } else if (loaded.error.empty()) {
        std::printf("FAIL %.*s - refused without saying why\n", static_cast<int>(what.size()),
                    what.data());
        ++ctbrowser_test_failures;
    }
}

constexpr std::string_view page_html = "<!doctype html><title>t</title>\n"
                                       "<script>var first = 1;</script>\n"
                                       "<script>var second = first + 1;</script>\n";

} // namespace

int main(int argc, char ** argv) {
    namespace po = boost::program_options;
    std::string out_dir;
    po::options_description options{"usage: ctcompile-test-app_bundle [OUT_DIR]"};
    options.add_options()("out-dir", po::value(&out_dir),
                          "also write the two bundles the launcher must refuse into OUT_DIR");
    po::positional_options_description positional;
    positional.add("out-dir", 1);
    if (!ctbrowser_cli::parse_options(argc, argv, options, positional)) { return 2; }

    // ---- The round trip --------------------------------------------------
    app_bundle made;
    made.entries.push_back({bundle_kind::meta, "title", raw("A Packaged Page")});
    made.entries.push_back({bundle_kind::html, {}, raw(page_html)});
    // A name that is not a path relative to anything: the case a directory of
    // files cannot represent, and the reason the bundle is a table.
    made.entries.push_back({bundle_kind::asset, "../../vendor/p5/p5.js", raw("// a library\n")});
    made.entries.push_back(
        {bundle_kind::asset, "sprites.bmp",
         std::vector<std::byte>{std::byte{'B'}, std::byte{'M'}, std::byte{0}, std::byte{0}}});
    made.entries.push_back({bundle_kind::script_image, {}, raw("not really an image")});

    const std::vector<std::byte> bytes = write_bundle(made);
    check(!bytes.empty(), "a bundle writes");

    const auto back = read_bundle(bytes);
    check(back.ok, "and reads back");
    if (!back.ok) {
        std::printf("     (%s)\n", back.error.c_str());
        return 1;
    }
    check(back.value.entries.size() == made.entries.size(), "with every entry");
    check(back.value.meta("title") == "A Packaged Page", "the meta row survives");
    check(back.value.meta("absent").empty(), "and an absent key is empty, not the first row");
    check(text_of(back.value.html()) == page_html, "the document survives byte for byte");
    for (std::size_t i = 0; i < back.value.entries.size() && i < made.entries.size(); ++i) {
        check(back.value.entries[i].kind == made.entries[i].kind &&
                  back.value.entries[i].name == made.entries[i].name &&
                  back.value.entries[i].bytes == made.entries[i].bytes,
              "entry " + std::to_string(i) + " round-trips exactly");
    }
    // An empty blob is not a missing blob. A zero-length asset is legal to
    // package, and the offset arithmetic treats it as a special case.
    {
        app_bundle with_empty;
        with_empty.entries.push_back({bundle_kind::html, {}, raw(page_html)});
        with_empty.entries.push_back({bundle_kind::asset, "empty.css", {}});
        const auto reread = read_bundle(write_bundle(with_empty));
        check(reread.ok && reread.value.entries.size() == 2 &&
                  reread.value.entries[1].bytes.empty(),
              "a zero-length asset round-trips as a zero-length asset");
    }

    // ---- What the loader must refuse -------------------------------------
    must_refuse({}, "nothing at all");
    must_refuse(raw("CTAP"), "a magic number and then the end of the file");
    {
        std::vector<std::byte> wrong = bytes;
        wrong[0] = static_cast<std::byte>('X');
        must_refuse(std::move(wrong), "the wrong magic");
    }
    {
        // The version field is bytes 4..7.
        std::vector<std::byte> wrong = bytes;
        wrong[4] = static_cast<std::byte>(99);
        must_refuse(std::move(wrong), "a format version this build does not read");
    }
    {
        // The engine fingerprint, bytes 8..15. It stops a bundle built by
        // another ctbrowser from being run by this one, where the images inside
        // would describe different instructions with the same bytes. One
        // flipped bit is the whole test.
        std::vector<std::byte> wrong = bytes;
        wrong[8] ^= static_cast<std::byte>(1);
        must_refuse(std::move(wrong), "a bundle from a different engine build");
    }
    {
        // An entry count the file is too short to hold (a huge reserve).
        std::vector<std::byte> wrong = bytes;
        for (std::size_t i = 0; i < 4; ++i) { wrong[16 + i] = static_cast<std::byte>(0xFF); }
        must_refuse(std::move(wrong), "an entry count of four billion");
    }
    {
        // A payload that does not fit inside the file it is in.
        std::vector<std::byte> wrong = bytes;
        for (std::size_t i = 0; i < 8; ++i) { wrong[20 + i] = static_cast<std::byte>(0xFF); }
        must_refuse(std::move(wrong), "a payload offset past the end");
    }
    {
        // No document. A bundle of images and assets with nothing to run is
        // not an application, and this is the only place that notices.
        app_bundle headless;
        headless.entries.push_back({bundle_kind::asset, "a.css", raw("body{}")});
        must_refuse(write_bundle(headless), "a bundle carrying no document");
    }
    {
        // Every entry claims the whole payload. Each blob passes the check
        // that it lies inside the payload (they all start at zero), yet
        // together they ask the reader for a multiple of the file's size. The
        // loader must bound the total, not only each read: at four hundred
        // thousand rows that is the difference between a refusal and an OOM
        // kill.
        //
        // The field is found by walking the table the same way the writer laid
        // it out: header, then one row per entry of kind, name length, name,
        // offset, length.
        constexpr std::size_t header_bytes = 4 + 4 + 8 + 4 + 8 + 8;
        std::uint64_t payload_length = 0;
        for (const auto & one : made.entries) { payload_length += one.bytes.size(); }
        const std::size_t length_field = header_bytes + 4 + 4 + made.entries[0].name.size() + 8;
        std::vector<std::byte> greedy = bytes;
        check(payload_length > made.entries[0].bytes.size(),
              "the fixture has more than one blob, so this case is a real one");
        for (std::size_t i = 0; i < 8; ++i) {
            greedy[length_field + i] = static_cast<std::byte>((payload_length >> (8 * i)) & 0xFF);
        }
        must_refuse(std::move(greedy), "entries that together claim more than the payload holds");
    }

    // Every truncation, because a partial copy is the ordinary way this file
    // arrives broken and any one of these lengths could be the one that reads
    // past the end. None may crash and none may be accepted.
    for (std::size_t n = 0; n < bytes.size(); ++n) {
        std::vector<std::byte> cut{bytes.begin(), bytes.begin() + static_cast<std::ptrdiff_t>(n)};
        if (read_bundle(cut).ok) {
            std::printf("FAIL a bundle truncated to %zu of %zu bytes was accepted\n", n,
                        bytes.size());
            ++ctbrowser_test_failures;
            break;
        }
    }

    // ---- The bundle appended to an executable -----------------------------
    const std::vector<std::byte> launcher = raw(std::string(4096, 'L'));
    {
        const std::vector<std::byte> joined = append_bundle_to(launcher, bytes);
        check(joined.size() > launcher.size() + bytes.size(), "appending adds a trailer");
        const std::span<const std::byte> found = find_appended_bundle(joined);
        check(found.size() == bytes.size(), "and the bundle is found again at its full length");
        check(std::vector<std::byte>{found.begin(), found.end()} == bytes,
              "byte for byte, so the launcher reads what the packager wrote");
        check(read_bundle(found).ok, "and it still loads");

        // A launcher with nothing appended, which is the ordinary case for the
        // launcher itself and must not be mistaken for an application.
        check(find_appended_bundle(launcher).empty(), "a bare launcher has no bundle");
        check(find_appended_bundle({}).empty(), "and neither does an empty file");

        // A truncated copy. The trailer is gone, so there is no bundle; the
        // dangerous answer here would be a bundle at a made-up offset.
        std::vector<std::byte> cut{joined.begin(), joined.end() - 4};
        check(find_appended_bundle(cut).empty(), "a truncated executable has no bundle");

        // The trailer magic in the launcher's own data. A launcher that happens
        // to contain these eight bytes (a string literal, say) must not be
        // read as carrying a bundle, which is why the trailer has to account
        // for the whole file rather than just being present.
        std::vector<std::byte> decoy = launcher;
        const std::vector<std::byte> magic = raw("CTAPTAIL");
        decoy.insert(decoy.end(), magic.begin(), magic.end());
        decoy.resize(decoy.size() + 16);
        check(find_appended_bundle(decoy).empty(),
              "the trailer magic alone does not make a bundle");

        // Packaging a packaged executable. Re-running the compiler over its own
        // output has to give the latest bundle, not the first one.
        app_bundle second;
        second.entries.push_back({bundle_kind::meta, "title", raw("The Second One")});
        second.entries.push_back({bundle_kind::html, {}, raw(page_html)});
        const std::vector<std::byte> second_bytes = write_bundle(second);
        const std::vector<std::byte> twice = append_bundle_to(joined, second_bytes);
        const auto found_twice = read_bundle(find_appended_bundle(twice));
        check(found_twice.ok && found_twice.value.meta("title") == "The Second One",
              "appending over an already-packaged file finds the newest bundle");
    }

    // ---- The property all of this exists for ------------------------------
    //
    // A page packaged with its images does not parse its scripts. Nothing above
    // can see that: the document, the values and the render are identical
    // either way, and a page that silently recompiled would pass every check in
    // this file up to here. The counter is the only witness.
    {
        // The script text comes from the engine, and getting this wrong is the
        // whole trap. A script's source is not the bytes between its tags: the
        // walk in lib/Shell/browser/scripts.cpp appends a newline to each one,
        // so that a trailing `//` comment terminates and a <script src> and
        // the inline text after it are two lines rather than one. An image
        // built from the text an author typed hashes differently and is never
        // looked up, and the page still works, just slowly. So a packager asks
        // the engine for the text, and so does this test.
        std::vector<std::string> sources;
        {
            browser probe{browser_options{100, 100}};
            probe.load_html(page_html);
            sources = probe.script_sources();
        }
        check(sources.size() == 2, "the page has two classic scripts");
        if (sources.size() != 2) { return 1; }

        const auto image_of = [](const std::string & source) {
            ctbrowser::script::program compiled = ctbrowser::script::compiler::compile(source);
            return ctbrowser::script::write_image(compiled);
        };

        browser packaged{browser_options{100, 100}};
        check(packaged.add_script_image(image_of(sources[0])), "an image is accepted");
        check(packaged.add_script_image(image_of(sources[1])), "and so is the second");
        packaged.load_html(page_html);
        if (packaged.scripts_compiled_from_source() != 0) {
            std::printf("FAIL %zu of %zu scripts were compiled from source despite their images\n",
                        packaged.scripts_compiled_from_source(), packaged.script_sources().size());
            for (const std::string & text : packaged.script_sources()) {
                std::printf(
                    "     page has  %016llx  <<%s>>\n",
                    static_cast<unsigned long long>(ctbrowser::script::image_source_hash(text)),
                    text.c_str());
            }
            ++ctbrowser_test_failures;
        }
        check(packaged.script_error().empty(), "and the page ran cleanly");

        // That newline, pinned. It is the difference between a packaged
        // application and one that silently compiles from source, and it is
        // invisible in the HTML, so only a failing test stops someone from
        // tidying it away.
        check(sources[0] == "var first = 1;\n",
              "a script's source is its text plus the newline the walk adds");
        {
            browser typed{browser_options{100, 100}};
            check(typed.add_script_image(image_of("var first = 1;")),
                  "an image of the text WITHOUT that newline is still a valid image");
            typed.load_html(page_html);
            check(typed.scripts_compiled_from_source() == 2,
                  "but it matches nothing - which is why a packager must ask, not assume");
        }

        // The negative case: the same page with an image for a different
        // script, so every lookup misses. A counter wired to a constant would
        // still read zero here; this one must not.
        browser mismatched{browser_options{100, 100}};
        check(mismatched.add_script_image(image_of("var somethingElse = 1;\n")),
              "a non-matching image is still a valid image");
        mismatched.load_html(page_html);
        check(mismatched.scripts_compiled_from_source() == 2,
              "a page whose images do not match compiles both scripts from source");
        check(mismatched.script_error().empty(),
              "and still runs correctly - which is exactly why this needs a counter");

        // With no images at all, the count is the number of scripts. This pins
        // the counter to an exact value rather than just zero or nonzero.
        browser plain{browser_options{100, 100}};
        plain.load_html(page_html);
        check(plain.scripts_compiled_from_source() == 2, "an unpackaged page compiles both");
    }

    // ---- A sealed registry looks nowhere else ------------------------------
    //
    // This is what makes a packaged application self-contained. Unsealed, a
    // name that misses is resolved against the working directory, so an
    // application missing a resource serves whatever happens to sit next to
    // the user under that name, and works only on the machine that built it.
    // The two cases here use the same registry and the same name; only the
    // seal differs.
    {
        const std::string name = "ctbrowser-seal-probe.txt";
        {
            std::ofstream out{name, std::ios::binary};
            out << "bytes from the working directory\n";
        }
        ctbrowser::shell::asset_registry open_registry;
        check(!open_registry.load(name).empty(),
              "an unsealed registry falls back to the working directory");

        ctbrowser::shell::asset_registry sealed;
        sealed.set_sealed(true);
        check(sealed.load(name).empty(), "AND A SEALED ONE DOES NOT - a miss stays a miss");
        // What a sealed registry does still answer: what was baked into it.
        sealed.add(name, raw("baked"));
        check(text_of(sealed.load(name)) == "baked",
              "a sealed registry still answers from what it carries");
        check(sealed.requested().size() == 1,
              "and it still records the question, so a packager hears about it");
        std::error_code ignored;
        std::filesystem::remove(name, ignored);
    }

    // ---- Two bundles for the launcher to refuse ----------------------------
    //
    // Written out rather than run here, because the guard they exercise lives
    // in run_app, which needs a window. roundtrip.test feeds them to the real
    // launcher (ctrun); without an output directory this does nothing.
    if (!out_dir.empty()) {
        const auto emit = [&out_dir](const std::string & file, const app_bundle & what) {
            const std::vector<std::byte> raw_bytes = write_bundle(what);
            std::ofstream out{std::filesystem::path{out_dir} / file, std::ios::binary};
            out.write(reinterpret_cast<const char *>(raw_bytes.data()),
                      static_cast<std::streamsize>(raw_bytes.size()));
        };
        // A page with scripts and no images at all. The run guard must refuse
        // it: checking only whether any images arrived would skip this case
        // instead of asking whether the images worked.
        app_bundle no_images;
        no_images.entries.push_back({bundle_kind::html, {}, raw(page_html)});
        emit("no-images.ctapp", no_images);

        // A page of module scripts, which no image can cover: there is no
        // image path into load_module, so every count reads a truthful zero
        // while the application parses all of its JavaScript at every start.
        app_bundle module_page;
        module_page.entries.push_back({bundle_kind::html,
                                       {},
                                       raw("<!doctype html><title>m</title>\n"
                                           "<script type=\"module\">var a = 1;</script>\n")});
        emit("module-page.ctapp", module_page);
    }

    if (ctbrowser_test_failures == 0) {
        std::printf("ok app_bundle (%zu byte bundle, round trip, %zu truncations refused)\n",
                    bytes.size(), bytes.size());
    }
    return ctbrowser_test_failures == 0 ? 0 : 1;
}
