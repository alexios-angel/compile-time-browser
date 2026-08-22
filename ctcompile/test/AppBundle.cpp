// The application bundle: does it round-trip, does the loader refuse what it
// should, and does a packaged page ACTUALLY skip the parse?
//
// Same shape as ProgramImage.cpp, and for a sharper reason. A bundle is a table
// of offsets read out of a file, which is to say a list of instructions to go
// and look somewhere; and the thing it carries is program images, which are
// executable input. So the negative cases are the file.
//
// THE LAST SECTION IS THE ONE THAT MATTERS MOST. Everything above it checks
// that bytes survive a trip. That section checks the only property the whole
// feature exists for - that the images are actually USED - and it checks it
// with a counter, because a page that quietly compiled from source produces
// exactly the same document as one that did not.
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
#include <system_error>
#include <string>
#include <string_view>
#include <vector>

using ctbrowser::shell::app_bundle;
using ctbrowser::shell::browser;
using ctbrowser::shell::browser_options;
using ctbrowser::shell::append_bundle_to;
using ctbrowser::shell::bundle_kind;
using ctbrowser::shell::find_appended_bundle;
using ctbrowser::shell::read_bundle;
using ctbrowser::shell::write_bundle;

namespace {

int failures = 0;

void check(bool ok, std::string_view what) {
    if (!ok) {
        std::printf("FAIL %.*s\n", static_cast<int>(what.size()), what.data());
        ++failures;
    }
}

std::vector<std::byte> raw(std::string_view text) {
    return {reinterpret_cast<const std::byte *>(text.data()),
            reinterpret_cast<const std::byte *>(text.data() + text.size())};
}

std::string text_of(std::span<const std::byte> bytes) {
    return {reinterpret_cast<const char *>(bytes.data()), bytes.size()};
}

// A refusal, not a crash and not a success. Every negative case below goes
// through this so the message says which one failed.
void must_refuse(std::vector<std::byte> bytes, std::string_view what) {
    const auto loaded = read_bundle(bytes);
    if (loaded.ok) {
        std::printf("FAIL %.*s - it was ACCEPTED\n", static_cast<int>(what.size()), what.data());
        ++failures;
    } else if (loaded.error.empty()) {
        std::printf("FAIL %.*s - refused without saying why\n", static_cast<int>(what.size()),
                    what.data());
        ++failures;
    }
}

constexpr std::string_view page_html = "<!doctype html><title>t</title>\n"
                                       "<script>var first = 1;</script>\n"
                                       "<script>var second = first + 1;</script>\n";

} // namespace

int main(int argc, char ** argv) {
    // ---- the round trip -------------------------------------------------
    app_bundle made;
    made.entries.push_back({bundle_kind::meta, "title", raw("A Packaged Page")});
    made.entries.push_back({bundle_kind::html, {}, raw(page_html)});
    // A NAME THAT IS NOT A PATH RELATIVE TO ANYTHING, which is the case a
    // directory of files cannot represent and the reason this is a table.
    made.entries.push_back({bundle_kind::asset, "../../vendor/p5/p5.js", raw("// a library\n")});
    made.entries.push_back({bundle_kind::asset, "sprites.bmp",
                        std::vector<std::byte>{std::byte{'B'}, std::byte{'M'},
                                               std::byte{0}, std::byte{0}}});
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
    // AN EMPTY BLOB IS NOT A MISSING BLOB. A zero-length asset is a legal thing
    // to package and the offset arithmetic treats it as a special case.
    {
        app_bundle with_empty;
        with_empty.entries.push_back({bundle_kind::html, {}, raw(page_html)});
        with_empty.entries.push_back({bundle_kind::asset, "empty.css", {}});
        const auto reread = read_bundle(write_bundle(with_empty));
        check(reread.ok && reread.value.entries.size() == 2 &&
                  reread.value.entries[1].bytes.empty(),
              "a zero-length asset round-trips as a zero-length asset");
    }

    // ---- what the loader must refuse ------------------------------------
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
        // THE ENGINE FINGERPRINT, bytes 8..15. This is the check that stops a
        // bundle built by another ctbrowser from being run by this one, where
        // the images inside describe different instructions with the same
        // bytes. One flipped bit is the whole test.
        std::vector<std::byte> wrong = bytes;
        wrong[8] ^= static_cast<std::byte>(1);
        must_refuse(std::move(wrong), "a bundle from a different engine build");
    }
    {
        // A count the file cannot pay for - the reserve-the-world case.
        std::vector<std::byte> wrong = bytes;
        for (std::size_t i = 0; i < 4; ++i) {
            wrong[16 + i] = static_cast<std::byte>(0xFF);
        }
        must_refuse(std::move(wrong), "an entry count of four billion");
    }
    {
        // A payload that does not fit inside the file it is in.
        std::vector<std::byte> wrong = bytes;
        for (std::size_t i = 0; i < 8; ++i) {
            wrong[20 + i] = static_cast<std::byte>(0xFF);
        }
        must_refuse(std::move(wrong), "a payload offset past the end");
    }
    {
        // NO DOCUMENT. A bundle of images and assets with nothing to run is not
        // an application, and this is the only place that notices.
        app_bundle headless;
        headless.entries.push_back({bundle_kind::asset, "a.css", raw("body{}")});
        must_refuse(write_bundle(headless), "a bundle carrying no document");
    }
    {
        // EVERY ENTRY CLAIMING THE WHOLE PAYLOAD. Each blob passes the check
        // that it lies inside the payload - they all start at zero - and
        // together they ask the reader for a multiple of the file's size. This
        // is the difference between bounding each read and bounding the total,
        // and at four hundred thousand rows it is the difference between a
        // refusal and an OOM kill.
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

    // EVERY TRUNCATION, because a partial copy is the ordinary way this file
    // arrives broken and any one of these lengths could be the one that reads
    // past the end. None may crash and none may be accepted.
    for (std::size_t n = 0; n < bytes.size(); ++n) {
        std::vector<std::byte> cut{bytes.begin(), bytes.begin() + static_cast<std::ptrdiff_t>(n)};
        if (read_bundle(cut).ok) {
            std::printf("FAIL a bundle truncated to %zu of %zu bytes was accepted\n", n,
                        bytes.size());
            ++failures;
            break;
        }
    }

    // ---- the bundle stuck on the end of an executable --------------------
    const std::vector<std::byte> launcher = raw(std::string(4096, 'L'));
    {
        const std::vector<std::byte> joined = append_bundle_to(launcher, bytes);
        check(joined.size() > launcher.size() + bytes.size(), "appending adds a trailer");
        const std::span<const std::byte> found = find_appended_bundle(joined);
        check(found.size() == bytes.size(), "and the bundle is found again at its full length");
        check(std::vector<std::byte>{found.begin(), found.end()} == bytes,
              "byte for byte, so the launcher reads what the packager wrote");
        check(read_bundle(found).ok, "and it still loads");

        // A LAUNCHER WITH NOTHING APPENDED, which is the ordinary case for the
        // launcher itself and must not be mistaken for an application.
        check(find_appended_bundle(launcher).empty(), "a bare launcher has no bundle");
        check(find_appended_bundle({}).empty(), "and neither does an empty file");

        // A TRUNCATED COPY. The trailer is gone, so there is no bundle - the
        // dangerous answer here is a bundle at a made-up offset.
        std::vector<std::byte> cut{joined.begin(), joined.end() - 4};
        check(find_appended_bundle(cut).empty(), "a truncated executable has no bundle");

        // THE TRAILER MAGIC IN THE LAUNCHER'S OWN DATA. A launcher that happens
        // to contain these eight bytes - a string literal, say - must not be
        // read as carrying a bundle, which is why the trailer has to account
        // for the whole file rather than just being present.
        std::vector<std::byte> decoy = launcher;
        const std::vector<std::byte> magic = raw("CTAPTAIL");
        decoy.insert(decoy.end(), magic.begin(), magic.end());
        decoy.resize(decoy.size() + 16);
        check(find_appended_bundle(decoy).empty(),
              "the trailer magic alone does not make a bundle");

        // PACKAGING A PACKAGED EXECUTABLE. Re-running the compiler over its own
        // output has to give the LATEST bundle, not the first one.
        app_bundle second;
        second.entries.push_back({bundle_kind::meta, "title", raw("The Second One")});
        second.entries.push_back({bundle_kind::html, {}, raw(page_html)});
        const std::vector<std::byte> second_bytes = write_bundle(second);
        const std::vector<std::byte> twice = append_bundle_to(joined, second_bytes);
        const auto found_twice = read_bundle(find_appended_bundle(twice));
        check(found_twice.ok && found_twice.value.meta("title") == "The Second One",
              "appending over an already-packaged file finds the newest bundle");
    }

    // ---- and the only property any of this exists for ---------------------
    //
    // A PAGE PACKAGED WITH ITS IMAGES DOES NOT PARSE ITS SCRIPTS. Nothing above
    // can see that: the document, the values and the render are identical
    // either way, and a page that silently recompiled would pass every check in
    // this file up to here. The counter is the only witness.
    {
        // THE TEXT COMES FROM THE ENGINE, and getting this wrong is the whole
        // trap. A script's source is NOT the bytes between its tags: the walk
        // in browser.cpp appends a newline to each one, so that a trailing `//`
        // comment terminates and so that a <script src> and the inline text
        // after it are two lines rather than one. An image built from the text
        // an author typed hashes differently and is never looked up - and the
        // page still works, just slowly. So a packager asks, and so does this.
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
                std::printf("     page has  %016llx  <<%s>>\n",
                            static_cast<unsigned long long>(
                                ctbrowser::script::image_source_hash(text)),
                            text.c_str());
            }
            ++failures;
        }
        check(packaged.script_error().empty(), "and the page ran cleanly");

        // THAT NEWLINE, PINNED. It is the difference between a packaged
        // application and one that silently compiles from source, it is
        // invisible in the HTML, and the only thing stopping someone tidying it
        // away is a test that fails when they do.
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

        // THE BLINDED ARM. Same page, an image for a DIFFERENT script, so every
        // lookup misses. If the counter above were wired to a constant this
        // would still read zero, and it does not.
        browser mismatched{browser_options{100, 100}};
        check(mismatched.add_script_image(image_of("var somethingElse = 1;\n")),
              "a non-matching image is still a valid image");
        mismatched.load_html(page_html);
        check(mismatched.scripts_compiled_from_source() == 2,
              "a page whose images do not match compiles both scripts from source");
        check(mismatched.script_error().empty(),
              "and still runs correctly - which is exactly why this needs a counter");

        // AND WITH NO IMAGES AT ALL, the number is the number of scripts. This
        // pins the counter to something other than zero-or-not.
        browser plain{browser_options{100, 100}};
        plain.load_html(page_html);
        check(plain.scripts_compiled_from_source() == 2, "an unpackaged page compiles both");
    }

    // ---- a sealed registry looks nowhere ---------------------------------
    //
    // WHAT A PACKAGED APPLICATION IS. Unsealed, a name that misses is resolved
    // against the WORKING DIRECTORY - so an application missing a resource
    // serves whatever happens to sit next to the user under the name its own
    // document asked for, and works on the machine that built it. The two arms
    // here are the same registry and the same name; only the seal differs.
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
        // What it DOES still answer: what was baked into it.
        sealed.add(name, raw("baked"));
        check(text_of(sealed.load(name)) == "baked",
              "a sealed registry still answers from what it carries");
        check(sealed.requested().size() == 1,
              "and it still records the question, so a packager hears about it");
        std::error_code ignored;
        std::filesystem::remove(name, ignored);
    }

    // ---- two bundles for the launcher to refuse ---------------------------
    //
    // Written out rather than run here, because the guard they exercise lives
    // in run_app, which needs a window. check-package.cmake feeds them to the
    // real launcher; without an output directory this does nothing.
    if (argc > 1) {
        const std::filesystem::path out_dir{argv[1]};
        const auto emit = [&out_dir](const std::string & file, const app_bundle & what) {
            const std::vector<std::byte> raw_bytes = write_bundle(what);
            std::ofstream out{out_dir / file, std::ios::binary};
            out.write(reinterpret_cast<const char *>(raw_bytes.data()),
                      static_cast<std::streamsize>(raw_bytes.size()));
        };
        // A PAGE WITH SCRIPTS AND NO IMAGES AT ALL. This is the case the run
        // guard used to skip entirely, because it tested whether any images had
        // arrived before asking whether they had worked.
        app_bundle no_images;
        no_images.entries.push_back({bundle_kind::html, {}, raw(page_html)});
        emit("no-images.ctapp", no_images);

        // AND A PAGE OF MODULE SCRIPTS, which no image can cover: there is no
        // image path into load_module, so every count reads a truthful zero
        // while the application parses all of its JavaScript at every start.
        app_bundle module_page;
        module_page.entries.push_back(
            {bundle_kind::html, {},
             raw("<!doctype html><title>m</title>\n"
                 "<script type=\"module\">var a = 1;</script>\n")});
        emit("module-page.ctapp", module_page);
    }

    if (failures == 0) {
        std::printf("ok app_bundle (%zu byte bundle, round trip, %zu truncations refused)\n",
                    bytes.size(), bytes.size());
    }
    return failures == 0 ? 0 : 1;
}
