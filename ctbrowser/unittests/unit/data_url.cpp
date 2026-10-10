//===- ctbrowser/unittests/unit/data_url.cpp - data: URL tests ------------===//
//
// `data:` URLs: a resource that carries its own bytes. Libraries ship their
// own images as data: URLs (Phaser's texture manager loads three base64 PNGs
// at boot and never starts if one fails), and a failed load can be invisible
// to the page, so these tests assert the decoded bytes, not just that nothing
// threw.
//
// The main image is a BMP because `decode_bmp` is built into the engine, so
// the tests cover the whole path (registry miss, data URL, decode, onload) in
// a headless build with no SDL3_image.
//
//===----------------------------------------------------------------------===//

#include <cstdio>
#include <string>

#include <ctbrowser.hpp>

#include "check.hpp"

namespace {

using ctbrowser::base64_decode;
using ctbrowser::shell::browser;
using ctbrowser::shell::browser_options;
using ctbrowser::shell::data_url;
using ctbrowser::shell::is_data_url;
using ctbrowser::shell::parse_data_url;

/// The 2x2 image of `bmp_2x2` as a PNG, so the two decoders can be compared
/// against each other rather than each against itself. Generated with zlib.
constexpr const char * png_2x2 =
    "data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAAIAAAACCAYAAABytg0kAAAAEklEQVR42mP4z8DwHwyB"
    "NBgAAEnICfcD2WTxAAAAAElFTkSuQmCC";

/// An 8x8 JPEG with four quadrants (red, green / blue, white): one full MCU, so
/// the decoder has a real block to decode rather than a degenerate one.
/// Compressed at quality 95 with no chroma subsampling, because the assertions
/// below sample quadrant pixels and 4:2:0 would blur the quadrant edges into
/// them. Generated with libjpeg-turbo.
constexpr const char * jpeg_8x8 =
    "data:image/jpeg;base64,"
    "/9j/4AAQSkZJRgABAQAAAQABAAD/2wBDAAIBAQEBAQIBAQECAgICAgQDAgICAgUEBAMEBgUGBgYFBgYGBwkIBgcJ"
    "BwYGCAsICQoKCgoKBggLDAsKDAkKCgr/2wBDAQICAgICAgUDAwUKBwYHCgoKCgoKCgoKCgoKCgoKCgoKCgoKCgoK"
    "CgoKCgoKCgoKCgoKCgoKCgoKCgoKCgoKCgr/wAARCAAIAAgDAREAAhEBAxEB/8QAHwAAAQUBAQEBAQEAAAAAAAAA"
    "AAECAwQFBgcICQoL/8QAtRAAAgEDAwIEAwUFBAQAAAF9AQIDAAQRBRIhMUEGE1FhByJxFDKBkaEII0KxwRVS0fAk"
    "M2JyggkKFhcYGRolJicoKSo0NTY3ODk6Q0RFRkdISUpTVFVWV1hZWmNkZWZnaGlqc3R1dnd4eXqDhIWGh4iJipKT"
    "lJWWl5iZmqKjpKWmp6ipqrKztLW2t7i5usLDxMXGx8jJytLT1NXW19jZ2uHi4+Tl5ufo6erx8vP09fb3+Pn6/8QA"
    "HwEAAwEBAQEBAQEBAQAAAAAAAAECAwQFBgcICQoL/8QAtREAAgECBAQDBAcFBAQAAQJ3AAECAxEEBSExBhJBUQdh"
    "cRMiMoEIFEKRobHBCSMzUvAVYnLRChYkNOEl8RcYGRomJygpKjU2Nzg5OkNERUZHSElKU1RVVldYWVpjZGVmZ2hp"
    "anN0dXZ3eHl6goOEhYaHiImKkpOUlZaXmJmaoqOkpaanqKmqsrO0tba3uLm6wsPExcbHyMnK0tPU1dbX2Nna4uPk"
    "5ebn6Onq8vP09fb3+Pn6/9oADAMBAAIRAxEAPwBv/Bu/8Fv+G+v+Fv8A/FS/8In/AMIn/wAI/wD8uf2/7V9q/tL/"
    "AG4dm37P/tZ39scn0mPoM/8AEOP7K/4yD2/t/b/8wnJy8nsf+omd78/la3W+i+mDxL/xOj/Yn7r+yv7K+s/a+te1"
    "+tfV/LD8nJ9X/v8ANz/Z5fe//9k=";

/// A 2x2 24-bit BMP: red, green / blue, white, top row first as displayed.
/// Note the row order: a BMP with a positive height is stored bottom-up, so
/// the file's first row is the image's last. Comparing against `png_2x2`
/// catches a decoder (or generator) that gets this wrong.
constexpr const char * bmp_2x2 =
    "data:image/bmp;base64,Qk1GAAAAAAAAADYAAAAoAAAAAgAAAAIAAAABABgAAAAAABAAAAATCwAAEwsAAAAAAAAA"
    "AAAA/wAA////AAAAAP8A/wAAAA==";

void test_the_parser() {
    CHECK(is_data_url("data:,x"));
    // The scheme is ASCII case-insensitive, like every other scheme.
    CHECK(is_data_url("DATA:,x"));
    CHECK(!is_data_url("http://example.com/"));
    CHECK(!is_data_url("dat"));

    data_url parsed;
    // Without a comma it is not a data URL: there is no payload to be lenient
    // about.
    CHECK(!parse_data_url("data:text/plain", parsed));

    CHECK(parse_data_url("data:text/plain;base64,aGVsbG8=", parsed));
    CHECK_EQ(parsed.mime, std::string{"text/plain"});
    CHECK_EQ(std::string(reinterpret_cast<const char *>(parsed.bytes.data()), parsed.bytes.size()),
             std::string{"hello"});

    // The percent-encoded form, which RFC 2397 allows and few generators use.
    CHECK(parse_data_url("data:,a%20b%2Fc", parsed));
    CHECK_EQ(std::string(reinterpret_cast<const char *>(parsed.bytes.data()), parsed.bytes.size()),
             std::string{"a b/c"});
    // No media type means the RFC's default, not an empty one.
    CHECK_EQ(parsed.mime, std::string{"text/plain"});

    // A media type parameter is dropped, and `;base64` is only the encoding
    // marker when it is last; a type merely containing the word is not one.
    CHECK(parse_data_url("data:text/html;charset=utf-8,hi", parsed));
    CHECK_EQ(parsed.mime, std::string{"text/html"});
    CHECK_EQ(std::string(reinterpret_cast<const char *>(parsed.bytes.data()), parsed.bytes.size()),
             std::string{"hi"});
}

/// base64_decode is lenient like `atob`: it skips characters outside the
/// alphabet, including the newline MIME encoders insert every 76 characters,
/// and accepts missing padding.
void test_the_decoder() {
    CHECK_EQ(base64_decode("aGVsbG8="), std::string{"hello"});
    CHECK_EQ(base64_decode("aGVsbG8"), std::string{"hello"});    // padding optional
    CHECK_EQ(base64_decode("aGVs\nbG8="), std::string{"hello"}); // wrapped
    CHECK_EQ(base64_decode("aGVs bG8="), std::string{"hello"});
    CHECK_EQ(base64_decode(""), std::string{});
    // A byte outside the alphabet is skipped rather than fatal, as `atob` is.
    CHECK_EQ(base64_decode("aGVs*bG8="), std::string{"hello"});
    // Every alphabet character round-trips, `+` and `/` included - the two a
    // percent-encoder would have eaten had data URLs gone through the lenient
    // pre-encoding path the rest of url.cpp uses.
    CHECK_EQ(base64_decode("Pz8/Pg=="), std::string{"??"
                                                    "?>"});
}

/// Data URLs resolve through the asset registry, so one code path serves an
/// <img>, a fetch, a CSS url() and a <script src>.
void test_the_registry() {
    ctbrowser::shell::asset_registry assets;
    const std::vector<std::byte> bytes = assets.load("data:text/plain;base64,aGVsbG8=");
    CHECK_EQ(std::string(reinterpret_cast<const char *>(bytes.data()), bytes.size()),
             std::string{"hello"});

    // The registry is still consulted first, so a page may override one by name.
    const std::string override_text = "seeded";
    assets.add(
        "data:text/plain;base64,aGVsbG8=",
        std::vector<std::byte>{reinterpret_cast<const std::byte *>(override_text.data()),
                               reinterpret_cast<const std::byte *>(override_text.data() + 6)});
    const std::vector<std::byte> seeded = assets.load("data:text/plain;base64,aGVsbG8=");
    CHECK_EQ(std::string(reinterpret_cast<const char *>(seeded.data()), seeded.size()),
             override_text);

    // A data URL that decodes to nothing stays empty rather than falling
    // through to the filesystem, where "data:..." is not a path anybody meant.
    CHECK(assets.load("data:,").empty());
}

/// Through a page: everything above can pass while an <img> still fires
/// `error`, so this checks the decoded image on a real element.
void test_an_image_element() {
    browser page{browser_options{200, 200}};
    page.load_html(std::string{R"(<html><body><img id="a" src=")"} + bmp_2x2 +
                   R"(" width="20" height="20"></body></html>)");
    // The load is announced on a turn, as every image load in this engine is.
    for (int i = 0; i < 4; ++i) { page.tick(16); }

    const auto ask = [&page](const char * expression) {
        const std::size_t before = page.bindings().console_output().size();
        (void)page.run_script(std::string{"console.log('=' + String("} + expression + "));");
        const auto & said = page.bindings().console_output();
        for (std::size_t i = said.size(); i-- > before;) {
            if (said[i].starts_with("=")) { return said[i].substr(1); }
        }
        return std::string{"<no answer>"};
    };

    // naturalWidth is the decoded size, so it is zero for a load that failed
    // and 2 for the bitmap that is actually in there; "the element exists"
    // would be true either way.
    CHECK_EQ(ask("document.getElementById('a').naturalWidth"), std::string{"2"});
    CHECK_EQ(ask("document.getElementById('a').naturalHeight"), std::string{"2"});
}

/// A script-created Image, which is the form libraries use: Phaser, p5 and
/// other texture loaders build one and assign `src` rather than writing markup.
void test_a_scripted_image() {
    browser page{browser_options{200, 200}};
    page.load_html("<html><body></body></html>");
    (void)page.run_script(std::string{R"(
        window.__fired = 'neither';
        var im = new Image();
        im.onload = function () { window.__fired = 'load:' + im.width + 'x' + im.height; };
        im.onerror = function () { window.__fired = 'error'; };
        im.src = ')"} + bmp_2x2 +
                          "';");
    for (int i = 0; i < 4; ++i) { page.tick(16); }

    const std::size_t before = page.bindings().console_output().size();
    (void)page.run_script("console.log('=' + String(window.__fired));");
    std::string answer = "<no answer>";
    const auto & said = page.bindings().console_output();
    for (std::size_t i = said.size(); i-- > before;) {
        if (said[i].starts_with("=")) {
            answer = said[i].substr(1);
            break;
        }
    }
    CHECK_EQ(answer, std::string{"load:2x2"});
}

/// PNG decodes through libpng in a build with no SDL. `test/` and `unittests/`
/// are SDL-free (`test/lint/api_surface` enforces it), so if PNG decoding came
/// only from an application-layer hook, every PNG here would be a zero-sized
/// image. The result is compared with the BMP pixel for pixel: two decoders
/// agreeing on the same four pixels is evidence, "it returned something" is not.
void test_a_png_decodes_without_sdl() {
    browser page{browser_options{200, 200}};
    page.load_html(std::string{R"(<html><body><img id="p" src=")"} + png_2x2 +
                   R"("><img id="b" src=")" + bmp_2x2 + R"("></body></html>)");
    for (int i = 0; i < 4; ++i) { page.tick(16); }

    const auto ask = [&page](const std::string & expression) {
        const std::size_t before = page.bindings().console_output().size();
        (void)page.run_script("console.log('=' + String(" + expression + "));");
        const auto & said = page.bindings().console_output();
        for (std::size_t i = said.size(); i-- > before;) {
            if (said[i].starts_with("=")) { return said[i].substr(1); }
        }
        return std::string{"<no answer>"};
    };

    CHECK_EQ(ask("document.getElementById('p').naturalWidth"), std::string{"2"});
    CHECK_EQ(ask("document.getElementById('p').naturalHeight"), std::string{"2"});

    // The pixels themselves, read back through a canvas: red, green / blue,
    // white in both, or one of the two decoders is wrong about row order,
    // channel order or alpha - and a size check would not notice any of them.
    const std::string read = R"JS((function () {
        var out = [];
        ['p', 'b'].forEach(function (id) {
            var c = document.createElement('canvas');
            c.width = 2; c.height = 2;
            var g = c.getContext('2d');
            g.drawImage(document.getElementById(id), 0, 0);
            var d = g.getImageData(0, 0, 2, 2).data;
            var one = [];
            for (var i = 0; i < 16; i += 4) {
                one.push(d[i] + ',' + d[i+1] + ',' + d[i+2] + ',' + d[i+3]);
            }
            out.push(one.join(' '));
        });
        return out[0] === out[1] ? 'same: ' + out[0] : 'DIFFER png=' + out[0] + ' bmp=' + out[1];
    })())JS";
    CHECK_EQ(ask(read), std::string{"same: 255,0,0,255 0,255,0,255 0,0,255,255 255,255,255,255"});
}

/// JPEG decodes through libjpeg-turbo in a build with no SDL, for the same
/// reason as the PNG test above.
///
/// JPEG is lossy, so colour assertions have a tolerance. The bugs this guards
/// against are a channel swap, a row flip, or an alpha byte left undefined,
/// each of which moves a channel by a hundred or more, not a few counts of
/// ringing. TurboJPEG's RGBA formats do not write the fourth byte, so an alpha
/// of 0 rather than 255 is the specific bug this guards.
void test_a_jpeg_decodes_without_sdl() {
    browser page{browser_options{200, 200}};
    page.load_html(std::string{R"(<html><body><img id="j" src=")"} + jpeg_8x8 +
                   R"("></body></html>)");
    for (int i = 0; i < 4; ++i) { page.tick(16); }

    const auto ask = [&page](const std::string & expression) {
        const std::size_t before = page.bindings().console_output().size();
        (void)page.run_script("console.log('=' + String(" + expression + "));");
        const auto & said = page.bindings().console_output();
        for (std::size_t i = said.size(); i-- > before;) {
            if (said[i].starts_with("=")) { return said[i].substr(1); }
        }
        return std::string{"<no answer>"};
    };

    CHECK_EQ(ask("document.getElementById('j').naturalWidth"), std::string{"8"});
    CHECK_EQ(ask("document.getElementById('j').naturalHeight"), std::string{"8"});

    // One pixel from the middle of each quadrant, named rather than compared
    // exactly: a channel within 24 of where it should be is this JPEG, and
    // anything else is a decoder bug.
    const std::string read = R"JS((function () {
        var c = document.createElement('canvas');
        c.width = 8; c.height = 8;
        var g = c.getContext('2d');
        g.drawImage(document.getElementById('j'), 0, 0);
        var d = g.getImageData(0, 0, 8, 8).data;
        var near = function (a, b) { return Math.abs(a - b) <= 24; };
        var at = function (x, y) { var i = (y * 8 + x) * 4; return [d[i], d[i+1], d[i+2], d[i+3]]; };
        var want = { '2,2': [255,0,0], '5,2': [0,255,0], '2,5': [0,0,255], '5,5': [255,255,255] };
        var bad = [];
        for (var k in want) {
            var xy = k.split(','), p = at(+xy[0], +xy[1]), w = want[k];
            if (!near(p[0],w[0]) || !near(p[1],w[1]) || !near(p[2],w[2])) {
                bad.push(k + '=' + p.slice(0,3).join('/') + ' want ' + w.join('/'));
            }
            // ALPHA IS THE ONE THAT IS EXACT. JPEG has none, so the decoder
            // must put it there; TurboJPEG leaves the byte undefined.
            if (p[3] !== 255) { bad.push(k + ' alpha=' + p[3]); }
        }
        return bad.length ? bad.join('; ') : 'ok';
    })())JS";
    CHECK_EQ(ask(read), std::string{"ok"});
}

} // namespace

int main() {
    test_the_parser();
    test_the_decoder();
    test_the_registry();
    test_an_image_element();
    test_a_scripted_image();
    test_a_png_decodes_without_sdl();
    test_a_jpeg_decodes_without_sdl();
    REPORT("data_url");
}
