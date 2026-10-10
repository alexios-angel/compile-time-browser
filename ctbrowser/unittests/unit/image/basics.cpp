//===- ctbrowser/unittests/unit/image/basics.cpp - Image tests --*- C++ -*-===//
//
// Images and the asset registry, end to end: decode, <img> sizing, painting,
// drawImage from script, canvas export and downloads, and what fetch does with
// each kind of miss.
//
// The bitmaps here are built, not read from disk: a test that depends on a
// binary blob checked in beside it fails for reasons that have nothing to do
// with the code.
//
//===----------------------------------------------------------------------===//

#include <ctbrowser.hpp>

#include "check.hpp"
#include "dom_probe.hpp"

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

using namespace ctbrowser;
using ctbrowser::shell::asset_registry;
using ctbrowser::shell::browser;
using ctbrowser::shell::decode_bmp;
using ctbrowser_test::find_id;

namespace {

/// Returns the laid-out box, in page coordinates, of the first element with
/// tag \p tag, or an empty rect if it has none.
[[nodiscard]] rect box_of_tag(browser & page, std::string_view tag) {
    const atom want = page.atoms().intern_lower(tag);
    node_id id{};
    {
        const auto txn = page.doc().read();
        const auto find = [&](auto && self, node_id at) -> void {
            if (!id && txn.tag(at).value_or(atom{}) == want) { id = at; }
            for (const node_id c : txn.children(at)) { self(self, c); }
        };
        find(find, txn.root());
    }
    const auto walk = [&](auto && self, const layout::fragment & f, float dx, float dy) -> rect {
        const rect box{f.bounds.x + dx, f.bounds.y + dy, f.bounds.width, f.bounds.height};
        if (f.source == id) { return box; }
        for (const auto & child : f.children) {
            if (const rect hit = self(self, child, box.x, box.y); !hit.empty()) { return hit; }
        }
        return rect{};
    };
    return walk(walk, page.fragments(), 0, 0);
}

/// Returns everything the page logged through console.log, `|`-joined.
[[nodiscard]] std::string logged(browser & page) {
    std::string out;
    for (const std::string & line : page.bindings().console_output()) {
        if (!out.empty()) { out += '|'; }
        out += line;
    }
    return out;
}

/// Builds a 24bpp BMP filled with \p colour (0xAARRGGBB), byte by byte.
/// Bottom-up is what image tools write by default, so that is the shape worth
/// being sure about; \p top_down writes a negative height instead.
[[nodiscard]] std::vector<std::byte> make_bmp(int width, int height,
                                              std::uint32_t colour = 0xFF3366CCU,
                                              bool top_down = false) {
    const std::size_t stride = (static_cast<std::size_t>(width) * 3U + 3U) & ~std::size_t{3};
    const std::size_t pixels = stride * static_cast<std::size_t>(height);
    std::vector<unsigned char> out(54 + pixels, 0);
    const auto put32 = [&out](std::size_t at, std::uint32_t v) {
        out[at] = static_cast<unsigned char>(v & 0xFF);
        out[at + 1] = static_cast<unsigned char>((v >> 8) & 0xFF);
        out[at + 2] = static_cast<unsigned char>((v >> 16) & 0xFF);
        out[at + 3] = static_cast<unsigned char>((v >> 24) & 0xFF);
    };
    out[0] = 'B';
    out[1] = 'M';
    put32(2, static_cast<std::uint32_t>(out.size()));
    put32(10, 54); // where the pixels start
    put32(14, 40); // BITMAPINFOHEADER
    put32(18, static_cast<std::uint32_t>(width));
    put32(22, static_cast<std::uint32_t>(top_down ? -height : height));
    out[26] = 1;  // planes
    out[28] = 24; // bits per pixel
    // Row by row, because a BMP row is padded to four bytes - writing pixels
    // straight through the block runs off the end of the last row.
    for (int y = 0; y < height; ++y) {
        const std::size_t row = 54 + stride * static_cast<std::size_t>(y);
        for (int x = 0; x < width; ++x) {
            const std::size_t at = row + static_cast<std::size_t>(x) * 3U;
            out[at] = static_cast<unsigned char>(colour & 0xFF);             // blue
            out[at + 1] = static_cast<unsigned char>((colour >> 8) & 0xFF);  // green
            out[at + 2] = static_cast<unsigned char>((colour >> 16) & 0xFF); // red
        }
    }
    std::vector<std::byte> bytes(out.size());
    for (std::size_t i = 0; i < out.size(); ++i) { bytes[i] = static_cast<std::byte>(out[i]); }
    return bytes;
}

void test_decode() {
    const auto image = decode_bmp(make_bmp(4, 3));
    CHECK(image.width == 4);
    CHECK(image.height == 3);
    // Opaque, and the channels in the right order - a BMP stores them BGR, so
    // getting this wrong swaps red and blue and looks almost right.
    CHECK(image.at(0, 0) == 0xFF3366CCU);
    CHECK(image.at(3, 2) == 0xFF3366CCU);

    // A top-down BMP (negative height) has to come out the same way up.
    const auto flipped = decode_bmp(make_bmp(4, 3, 0xFF3366CCU, true));
    CHECK(flipped.width == 4 && flipped.height == 3);
    CHECK(flipped.at(0, 0) == 0xFF3366CCU);

    // Rubbish in, empty out - never a crash and never a half-decoded image.
    CHECK(decode_bmp({}).empty());
    const std::vector<std::byte> whole = make_bmp(4, 3);
    const std::vector<std::byte> truncated{whole.begin(), whole.begin() + 30};
    CHECK(decode_bmp(truncated).empty());
    // A header that says 4x3 with only one row of pixels behind it is a
    // truncated image, and half of one is worse than none.
    const std::vector<std::byte> short_pixels{whole.begin(), whole.begin() + 60};
    CHECK(decode_bmp(short_pixels).empty());
}

void test_registry() {
    asset_registry assets;
    assets.add("sprite.bmp", make_bmp(2, 2));
    CHECK(assets.contains("sprite.bmp"));
    CHECK(!assets.contains("nope.bmp"));
    CHECK(!assets.load("sprite.bmp").empty());
    CHECK(assets.load("nope.bmp").empty());

    // A second add replaces, so an application can override a built-in.
    assets.add("sprite.bmp", make_bmp(8, 8));
    CHECK(decode_bmp(assets.find("sprite.bmp")).width == 8);
}

void test_img_element() {
    ctbrowser::browser page{{.width = 200, .height = 120}};
    page.assets().add("cat.bmp", make_bmp(20, 10));
    page.load_html("<body><img src='cat.bmp'></body>");
    page.frame();

    // The <img> is as big as its bitmap: with no width attribute the size comes
    // from the decode.
    const rect box = box_of_tag(page, "img");
    CHECK(box.width == 20.0f);
    CHECK(box.height == 10.0f);

    // And it is actually painted: the pixel in the middle of it is the
    // image's colour, not the page background.
    const int x = static_cast<int>(box.x + box.width / 2);
    const int y = static_cast<int>(box.y + box.height / 2);
    CHECK(page.read_pixels().row(y)[static_cast<std::size_t>(x)] == 0xFF3366CCU);
}

void test_img_sizing_rules() {
    ctbrowser::browser page{{.width = 200, .height = 120}};
    page.assets().add("cat.bmp", make_bmp(20, 10));

    // An explicit width with no height keeps the aspect ratio, which is what
    // `<img width=40>` on a 20x10 image means.
    page.load_html("<body><img src='cat.bmp' width='40'></body>");
    page.frame();
    CHECK(box_of_tag(page, "img").width == 40.0f);
    CHECK(box_of_tag(page, "img").height == 20.0f);

    // A missing image is zero-sized rather than a broken-image box.
    page.load_html("<body><img src='absent.bmp'></body>");
    page.frame();
    CHECK(box_of_tag(page, "img").width == 0.0f);
}

void test_script_images() {
    ctbrowser::browser page{{.width = 120, .height = 80}};
    page.assets().add("sprite.bmp", make_bmp(8, 8, 0xFF00FF00U));
    page.load_html(R"(<body>
      <canvas id='c' width='40' height='40'></canvas>
      <script>
        var img = loadImage('sprite.bmp');
        var ctx = document.getElementById('c').getContext('2d');
        ctx.drawImage(img, 4, 4);
        ctx.drawImage(img, 0, 0, 4, 4, 20, 20, 8, 8);
        console.log(imageWidth(img) + 'x' + imageHeight(img));
        console.log('missing=' + loadImage('nope.bmp'));
      </script>
    </body>)");
    CHECK(page.script_error().empty());
    // A failed load is -1, which a page can test for. Returning 0 would look
    // like a valid handle.
    CHECK(logged(page) == "8x8|missing=-1");

    // The sprite really landed on the canvas.
    page.frame();
    const auto pixels = page.canvases().pixels_of(find_id(page, "c"));
    CHECK(static_cast<bool>(pixels));
    if (pixels) {
        CHECK(pixels->at(6, 6) == 0xFF00FF00U);   // the plain draw
        CHECK(pixels->at(24, 24) == 0xFF00FF00U); // the source-rect draw
        CHECK(pixels->at(1, 1) == 0);             // and nothing outside them
    }
}

void test_img_in_canvas_from_element() {
    // drawImage takes an <img> element as well as a loadImage handle. Pages use
    // both, so both have to work.
    ctbrowser::browser page{{.width = 120, .height = 80}};
    page.assets().add("sprite.bmp", make_bmp(8, 8, 0xFFFF0000U));
    page.load_html(R"(<body>
      <img id='s' src='sprite.bmp'>
      <canvas id='c' width='40' height='40'></canvas>
      <script>
        var ctx = document.getElementById('c').getContext('2d');
        ctx.drawImage(document.getElementById('s'), 2, 2);
      </script>
    </body>)");
    CHECK(page.script_error().empty());
    page.frame();
    const auto pixels = page.canvases().pixels_of(find_id(page, "c"));
    if (pixels) { CHECK(pixels->at(4, 4) == 0xFFFF0000U); }
}

// The exact sequence p5's loadImage runs, which is one piece of machinery with
// four parts and no value in testing them apart:
//
//   fetch the bytes -> new Blob([bytes]) -> URL.createObjectURL(blob)
//   -> new Image() with onload -> revoke the URL -> drawImage
//
// The revoke is the trap. It happens inside onload, before the image is ever
// drawn, so an object URL that only names bytes in the registry has nothing left
// to resolve by the time drawImage asks. It works because image_store caches the
// decode by name - which is the browser's own rule, that revoking frees the
// bytes and not the decoded image.
void test_blob_object_url_and_image() {
    ctbrowser::browser page{{.width = 120, .height = 80}};
    page.assets().add("https://example.invalid/sprite.bmp", make_bmp(8, 8, 0xFF00FF00U));
    page.allow_network(false);
    page.load_html(R"(<body>
      <canvas id='c' width='40' height='40'></canvas>
      <script>
        async function boot() {
          const response = await fetch('https://example.invalid/sprite.bmp');
          const data = await response.bytes();
          const image = await new Promise(function (resolve, reject) {
            const img = new Image();
            const url = URL.createObjectURL(new Blob([data], { type: 'image/bmp' }));
            img.onerror = function (e) { URL.revokeObjectURL(url); reject(e); };
            img.onload = function () { URL.revokeObjectURL(url); resolve(img); };
            img.src = url;
          });
          console.log('loaded ' + image.width + 'x' + image.height +
                      ' natural=' + image.naturalWidth + ' complete=' + image.complete);
          document.getElementById('c').getContext('2d').drawImage(image, 4, 4);
        }
        boot();
      </script>
    </body>)");
    CHECK(page.script_error().empty());
    for (int frame = 0; frame < 20; ++frame) { page.tick(16); }
    CHECK(logged(page) == "loaded 8x8 natural=8 complete=true");

    // The pixels really arrived, through a URL that no longer resolves.
    page.frame();
    const auto pixels = page.canvases().pixels_of(find_id(page, "c"));
    CHECK(static_cast<bool>(pixels));
    if (pixels) {
        CHECK(pixels->at(6, 6) == 0xFF00FF00U);
        CHECK(pixels->at(1, 1) == 0);
    }
}

// A load that fails says so. Reporting success with a zero-sized image is the
// failure mode that hides: p5 would size a canvas to 0x0 and draw nothing, with
// no error anywhere.
void test_image_load_failure_is_reported() {
    ctbrowser::browser page{{.width = 100, .height = 60}};
    page.load_html(R"(<body><script>
      var img = new Image();
      img.onload = function () { console.log('load'); };
      img.onerror = function (e) { console.log(e.type + ' complete=' + img.complete); };
      img.src = 'absent.bmp';
      new Image().decode().then(function () { console.log('decoded'); },
                               function (e) { console.log('rejected: ' + e.message); });
    </script></body>)");
    CHECK(page.script_error().empty());
    for (int frame = 0; frame < 10; ++frame) { page.tick(16); }
    CHECK(logged(page) == "error complete=false|rejected: could not decode ");
}

// `new Image()` is an <img>: it can be appended and it lays out, which is what
// makes one implementation serve both. A parallel Image type would have needed
// the layout, the painter and drawImage each taught about it.
void test_a_constructed_image_is_an_element() {
    ctbrowser::browser page{{.width = 120, .height = 80}};
    page.assets().add("cat.bmp", make_bmp(20, 10));
    page.load_html(R"(<body><script>
      var img = new Image();
      img.src = 'cat.bmp';
      document.body.appendChild(img);
      console.log(img.tagName + ' ' + (img.parentNode === document.body));
    </script></body>)");
    CHECK(page.script_error().empty());
    CHECK(logged(page) == "IMG true");
    for (int frame = 0; frame < 4; ++frame) { page.tick(16); }
    page.frame();
    // Laid out at its intrinsic size, by the same code path an <img> in the
    // markup takes.
    CHECK(box_of_tag(page, "img").width == 20.0f);
    CHECK(box_of_tag(page, "img").height == 10.0f);
}

// The PNG this engine writes must be readable by things that are not this
// engine, so the check is structural: signature, chunk order, IHDR fields.
void test_encode_png() {
    const auto image = decode_bmp(make_bmp(4, 3, 0xFF3366CCU));
    const std::vector<std::byte> png = ctbrowser::shell::encode_png(image);
    CHECK(png.size() > 60);
    const auto byte = [&png](std::size_t i) { return static_cast<unsigned char>(png[i]); };
    // The signature, and IHDR/IDAT/IEND in order.
    CHECK(byte(0) == 0x89 && byte(1) == 'P' && byte(2) == 'N' && byte(3) == 'G');
    const std::string text{reinterpret_cast<const char *>(png.data()), png.size()};
    CHECK(text.find("IHDR") == 12);
    CHECK(text.find("IDAT") != std::string::npos);
    CHECK(text.rfind("IEND") == png.size() - 8);
    // The dimensions are big-endian in IHDR, at offset 16.
    CHECK(byte(16) == 0 && byte(17) == 0 && byte(18) == 0 && byte(19) == 4);
    CHECK(byte(20) == 0 && byte(21) == 0 && byte(22) == 0 && byte(23) == 3);
    CHECK(byte(24) == 8 && byte(25) == 6); // 8 bits, RGBA
    // Empty in, empty out - not a header with no pixels, which a decoder would
    // reject and which would look like a corrupt file rather than no file.
    CHECK(ctbrowser::shell::encode_png(ctbrowser::paint::bitmap{}).empty());
}

// Export, end to end: the path p5's save() takes, and the one place this
// engine invents a behaviour: an `<a download>` writes a file instead of raising
// a save dialog nobody is there to see.
//
//   canvas.toBlob(cb) -> new Blob -> URL.createObjectURL -> <a href download>
//   -> link.click() -> the bytes land on disk -> URL.revokeObjectURL
void test_export_writes_a_file() {
    ctbrowser::browser page{{.width = 80, .height = 60}};
    page.set_download_directory("../build/downloads");
    std::filesystem::remove("../build/downloads/sketch.png"); // no overwrite: a rerun's name
    page.load_html(R"(<body>
      <canvas id='c' width='8' height='8'></canvas>
      <script>
        var ctx = document.getElementById('c').getContext('2d');
        ctx.fillStyle = '#3366cc';
        ctx.fillRect(0, 0, 8, 8);
        function go() { document.getElementById('c').toBlob(function (blob) {
          console.log('blob ' + blob.size + ' ' + blob.type + ' ' +
                      (blob instanceof Blob));
          var url = URL.createObjectURL(blob);
          var link = document.createElement('a');
          link.href = url;
          link.download = 'sketch.png';
          link.click();
          URL.revokeObjectURL(url);
        }); }
      </script>
    </body>)");
    CHECK(page.script_error().empty());
    // A write needs a user gesture - one key press, as a player's click would be
    // - and the export starts inside it, the way a save button's handler would.
    (void)page.handle(ctbrowser::shell::input_event::key_press("a"));
    (void)page.run_script("go()");
    // toBlob is asynchronous - a page that wraps it in a promise depends on the
    // callback landing after the call returns - so the loop has to run.
    for (int frame = 0; frame < 4; ++frame) { page.tick(16); }

    const std::string blob_line = logged(page);
    CHECK(blob_line.find("image/png true") != std::string::npos);
    CHECK(page.downloads().size() == 1);
    if (page.downloads().size() == 1) {
        const auto & saved = page.downloads().front();
        CHECK(saved.name == "sketch.png");
        CHECK(saved.written);
        // A real PNG of a real 8x8 canvas: signature, IHDR, IDAT and IEND
        // alone are 8 + 25 + 12 + 12 bytes before any pixel, and it is a
        // real deflate stream (libpng), so a flat canvas compresses.
        CHECK(saved.bytes > 57);
        // And it is on the disk where it said it was, at the size it said.
        std::ifstream from_disk{saved.path, std::ios::binary | std::ios::ate};
        CHECK(from_disk.good());
        if (from_disk) { CHECK(static_cast<std::size_t>(from_disk.tellg()) == saved.bytes); }
    }
}

/// Downloads \p text through an `<a download=name>` click, behind a real key
/// press when \p gesture is set.
void download_text(browser & page, std::string_view name, std::string_view text,
                   bool gesture = true) {
    if (gesture) { (void)page.handle(ctbrowser::shell::input_event::key_press("a")); }
    (void)page.run_script("var link = document.createElement('a');"
                          "link.href = URL.createObjectURL(new Blob(['" +
                          std::string{text} + "']));link.download = '" + std::string{name} +
                          "';link.click();");
}

/// Returns the whole contents of the file at \p path, or "" if it cannot be read.
[[nodiscard]] std::string read_all(const std::string & path) {
    std::ifstream in{path, std::ios::binary};
    return {std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};
}

// A download name is a name, not a path. `download="../../x"` is a page choosing
// where to write on the host, and it does not get to.
void test_a_download_cannot_escape_its_directory() {
    ctbrowser::browser page{{.width = 60, .height = 40}};
    page.set_download_directory("../build/downloads");
    page.load_html("<body></body>");
    std::filesystem::remove("../build/downloads/escaped.txt");
    download_text(page, "../../escaped.txt", "x");
    CHECK(page.script_error().empty());
    CHECK(page.downloads().size() == 1);
    if (!page.downloads().empty()) {
        CHECK(page.downloads().front().name == "escaped.txt");
        CHECK(page.downloads().front().written);
        CHECK(page.downloads().front().path.find("../build/downloads") != std::string::npos);
    }
}

// What a page may call its file. version.dll beside a game's executable is a
// DLL the loader picks up, and CON.txt is a device, not a file.
void test_download_names_are_sanitised() {
    const auto name = [](std::string_view in) {
        return ctbrowser::shell::sanitize_download_name(in);
    };
    CHECK(name("save1.json") == "save1.json");
    CHECK(name("version.dll") == "download");
    CHECK(name("SETUP.EXE") == "download");
    CHECK(name("evil.dll.") == "download");
    CHECK(name("run.sh  ") == "download");
    CHECK(name("desktop.INI") == "download");
    CHECK(name("CON") == "download");
    CHECK(name("con.txt") == "download");
    CHECK(name("NUL.png") == "download");
    CHECK(name("com1.log") == "download");
    CHECK(name("LPT9") == "download");
    CHECK(name("com0.txt") == "com0.txt");
    CHECK(name("console.txt") == "console.txt");
    CHECK(name("notes.txt. .") == "notes.txt");
    CHECK(name("../x") == "x");
    CHECK(name("a\\b.txt") == "b.txt");
    CHECK(name("..") == "download");
    CHECK(name("") == "download");
    CHECK(name("a\nb.txt") == "download");
    CHECK(name("a:stream.txt") == "download");
    CHECK(name("what?.txt") == "download");
}

// No directory, no write: the default, and what run_app ships unless the
// application names a folder. The export is still recorded.
void test_download_without_a_directory_writes_nothing() {
    browser page{{.width = 60, .height = 40}};
    page.load_html("<body></body>");
    std::filesystem::remove("unset-probe.txt");
    download_text(page, "unset-probe.txt", "x");
    CHECK(page.downloads().size() == 1);
    if (!page.downloads().empty()) {
        CHECK(!page.downloads().front().written);
        CHECK(page.downloads().front().path.empty());
    }
    CHECK(!std::filesystem::exists("unset-probe.txt"));
}

// A script's click() with no gesture behind it, a file over the limit, and a
// name already taken: none writes over anything.
void test_download_gesture_limit_and_no_overwrite() {
    browser page{{.width = 60, .height = 40}};
    page.set_download_directory("../build/downloads");
    page.load_html("<body></body>");
    for (const char * stale : {"keep.txt", "keep-1.txt", "big.txt", "quiet.txt"}) {
        std::filesystem::remove(std::filesystem::path{"../build/downloads"} / stale);
    }
    download_text(page, "quiet.txt", "x", false);
    download_text(page, "keep.txt", "first");
    download_text(page, "keep.txt", "second");
    page.set_download_limit(4);
    download_text(page, "big.txt", "12345");
    CHECK(page.downloads().size() == 4);
    if (page.downloads().size() == 4) {
        CHECK(!page.downloads()[0].written);
        CHECK(page.downloads()[1].written && page.downloads()[2].written);
        CHECK(page.downloads()[2].path.ends_with("keep-1.txt"));
        CHECK(!page.downloads()[3].written);
    }
    CHECK(read_all("../build/downloads/keep.txt") == "first");
    CHECK(read_all("../build/downloads/keep-1.txt") == "second");
    CHECK(!std::filesystem::exists("../build/downloads/quiet.txt"));
    CHECK(!std::filesystem::exists("../build/downloads/big.txt"));
}

// `toDataURL` is the other way out, and the one a page puts in an <img src>.
void test_to_data_url() {
    ctbrowser::browser page{{.width = 60, .height = 40}};
    page.load_html(R"(<body>
      <canvas id='c' width='4' height='4'></canvas>
      <script>
        var url = document.getElementById('c').toDataURL();
        console.log(url.slice(0, 22) + '|len=' + (url.length > 60));
        // A round trip: the base64 in the URL decodes to a PNG signature.
        var raw = atob(url.slice('data:image/png;base64,'.length));
        console.log('sig=' + raw.charCodeAt(0) + ',' + raw.charCodeAt(1) +
                    raw.charCodeAt(2) + raw.charCodeAt(3));
      </script>
    </body>)");
    CHECK(page.script_error().empty());
    // 137 80 78 71 is the PNG signature, and reading it back through atob
    // proves the base64 is bytes rather than text.
    CHECK(logged(page) == "data:image/png;base64,|len=true|sig=137,807871");
}

// FileReader: the input side of the same machinery that does export.
//
// Nothing here can open a file picker, and it does not pretend to: an <input
// type=file> has an empty FileList because there is no user to choose with. What
// works is everything a page does with a file it already has, which is what a
// drag-and-drop shim and p5's own loaders need.
void test_file_reader() {
    ctbrowser::browser page{{.width = 100, .height = 60}};
    page.load_html(R"(<body><script>
      var f = new File(['hello world'], 'greeting.txt', { type: 'text/plain' });
      console.log('file ' + f.name + ' ' + f.size + ' ' + f.type + ' ' + (f instanceof Blob));
      var r = new FileReader();
      // Assigned AFTER the read is started, which is the whole reason the result
      // arrives on a later turn: a synchronous reader would fire before this line.
      r.readAsText(f);
      r.onload = function (e) {
        console.log('text ' + r.result + ' ' + (e.target === r) + ' ' + r.readyState);
      };
      var d = new FileReader();
      d.readAsDataURL(f);
      d.onload = function () { console.log('url ' + d.result); };
      var b = new FileReader();
      b.readAsArrayBuffer(f);
      b.onload = function () {
        var view = new Uint8Array(b.result);
        console.log('bytes ' + view.length + ' ' + view[0]);
      };
      var bad = new FileReader();
      bad.readAsText('not a blob');
      bad.onerror = function () { console.log('error ' + bad.error.name); };
    </script></body>)");
    CHECK(page.script_error().empty());
    for (int frame = 0; frame < 4; ++frame) { page.tick(16); }
    CHECK(logged(page) == "file greeting.txt 11 text/plain true|"
                          "text hello world true 2|"
                          "url data:text/plain;base64,aGVsbG8gd29ybGQ=|"
                          "bytes 11 104|"
                          "error NotReadableError");
}

// `new DOMParser().parseFromString(...)`, which is how p5's loadXML gets a
// document. It returns a real XML document (dom_bindings::parse_from_string),
// so the tag name keeps its case and the page walks ordinary elements.
void test_dom_parser() {
    ctbrowser::browser page{{.width = 100, .height = 60}};
    page.load_html(R"(<body><script>
      var doc = new DOMParser().parseFromString(
          '<list kind="demo"><item id="a">first</item><item>second</item></list>',
          'application/xml');
      var root = doc.documentElement;
      console.log('root ' + root.tagName + ' ' + root.children.length);
      console.log('attrs ' + root.attributes.length + ' ' + root.attributes[0].name +
                  '=' + root.attributes[0].value);
      var items = root.getElementsByTagName('item');
      console.log('items ' + items.length + ' ' + items[0].textContent + ' ' +
                  items[1].textContent);
      console.log('nested attr ' + items[0].attributes[0].nodeName + '=' +
                  items[0].attributes[0].nodeValue);
    </script></body>)");
    CHECK(page.script_error().empty());
    CHECK(logged(page) == "root list 2|attrs 1 kind=demo|items 2 first second|nested attr id=a");
}

void test_fetch_from_registry() {
    ctbrowser::browser page{{.width = 100, .height = 60}};
    page.assets().add("https://example.invalid/data.json",
                      bytes_of(R"({"n": 7, "who": "registry"})"));
    // Network off, so this can only be the registry - which is the point: a
    // page that bakes its resources in is reproducible and never blocks.
    page.allow_network(false);
    page.load_html(R"(<body><script>
      async function boot() {
        const r = await fetch('https://example.invalid/data.json');
        const doc = await r.json();
    console.log(r.status + ':' + doc.n + ':' + doc.who);
        const text = await (await fetch('https://example.invalid/data.json')).text();
        console.log('length=' + text.length);
      }
      boot();
    </script></body>)");
    CHECK(page.script_error().empty());
    // fetch is asynchronous, so the event loop has to run: each await costs a
    // turn and there are four here.
    for (int frame = 0; frame < 20; ++frame) { page.tick(16); }
    CHECK(logged(page) == "200:7:registry|length=27");
}

void test_fetch_rejects() {
    ctbrowser::browser page{{.width = 100, .height = 60}};
    page.allow_network(false);
    page.load_html(R"(<body><script>
      async function boot() {
        try {
          await fetch('https://example.invalid/missing');
          console.log('resolved');
        } catch (e) {
          console.log('rejected: ' + e.message);
        }
      }
      boot();
    </script></body>)");
    CHECK(page.script_error().empty());
    for (int frame = 0; frame < 20; ++frame) { page.tick(16); }
    // A network failure rejects: that is what a page's catch branch is written
    // for, and it is how fetchboard reports a resource that was not baked in.
    const std::string outcome = logged(page);
    CHECK(outcome.rfind("rejected:", 0) == 0);
    CHECK(outcome.find("not baked in") != std::string::npos);
}

// Headers that claim enormous images must fail before any allocation.
void test_decode_budget() {
    const auto bytes = [](std::initializer_list<unsigned> v) {
        std::vector<std::byte> out;
        for (const unsigned b : v) { out.push_back(static_cast<std::byte>(b)); }
        return out;
    };
    // PNG: signature + IHDR 65535x65535 RGB8 (valid CRC), no data.
    const auto png =
        bytes({0x89, 'P',  'N',  'G', 0x0D, 0x0A, 0x1A, 0x0A, 0, 0, 0, 13, 'I',  'H',  'D',  'R', 0,
               0,    0xFF, 0xFF, 0,   0,    0xFF, 0xFF, 8,    2, 0, 0, 0,  0x39, 0x67, 0x4E, 0x07});
    CHECK(ctbrowser::shell::decode_png(png).empty());
    // JPEG: SOI, SOF0 65535x65535 one component, SOS.
    const auto jpg = bytes({0xFF, 0xD8, 0xFF, 0xC0, 0, 11, 8, 0xFF, 0xFF, 0xFF, 0xFF, 1, 1,
                            0x11, 0,    0xFF, 0xDA, 0, 8,  1, 1,    0,    0,    63,   0});
    CHECK(ctbrowser::shell::decode_jpeg(jpg).empty());
    // BMP: 54-byte header, 24bpp; INT32_MIN height, then a huge square.
    const auto bmp = [](std::uint32_t w, std::uint32_t h) {
        std::vector<std::byte> out(54, std::byte{0});
        const auto put = [&](std::size_t at, std::uint32_t v, int n) {
            for (int i = 0; i < n; ++i) {
                out[at + static_cast<std::size_t>(i)] = static_cast<std::byte>(v >> (8 * i));
            }
        };
        out[0] = std::byte{'B'};
        out[1] = std::byte{'M'};
        put(10, 54, 4);
        put(14, 40, 4);
        put(18, w, 4);
        put(22, h, 4);
        put(28, 24, 2);
        return out;
    };
    CHECK(ctbrowser::shell::decode_bmp(bmp(1, 0x80000000U)).empty());
    CHECK(ctbrowser::shell::decode_bmp(bmp(65535, 65535)).empty());
    CHECK(ctbrowser::shell::decode_bmp(bmp(0x7FFFFFFFU, 0x7FFFFFFFU)).empty());
}

} // namespace

int main() {
    test_decode_budget();
    test_decode();
    test_registry();
    test_img_element();
    test_img_sizing_rules();
    test_script_images();
    test_img_in_canvas_from_element();
    test_file_reader();
    test_dom_parser();
    test_encode_png();
    test_export_writes_a_file();
    test_a_download_cannot_escape_its_directory();
    test_download_names_are_sanitised();
    test_download_without_a_directory_writes_nothing();
    test_download_gesture_limit_and_no_overwrite();
    test_to_data_url();
    test_blob_object_url_and_image();
    test_image_load_failure_is_reported();
    test_a_constructed_image_is_an_element();
    test_fetch_from_registry();
    test_fetch_rejects();

    REPORT("image_basics");
}
