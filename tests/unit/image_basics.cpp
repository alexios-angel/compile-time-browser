// Images and the asset registry, end to end: decode, <img> sizing, painting,
// drawImage from script, and what fetch does with each kind of miss.
//
// The bitmaps here are BUILT, not read from disk - a test that depends on a
// binary blob checked in beside it fails for reasons that have nothing to do
// with the code.

#include <ctbrowser.hpp>

#include "check.hpp"

#include <cstdint>
#include <cstdio>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

using namespace ctbrowser;
using ctbrowser::shell::asset_registry;
using ctbrowser::shell::browser;
using ctbrowser::shell::decode_bmp;

namespace {

[[nodiscard]] node_id find_id(browser & page, std::string_view want) {
    const auto txn = page.doc().read();
    const atom key = page.atoms().intern("id");
    node_id found{};
    const auto walk = [&](auto && self, node_id at) -> void {
        if (!found && txn.attribute_value(at, key) == want) { found = at; }
        for (const node_id c : txn.children(at)) { self(self, c); }
    };
    walk(walk, txn.root());
    return found;
}

// The laid-out box of the first element with this tag.
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

[[nodiscard]] std::vector<std::byte> bytes_of(std::string_view text) {
    std::vector<std::byte> out(text.size());
    for (std::size_t i = 0; i < text.size(); ++i) {
        out[i] = static_cast<std::byte>(static_cast<unsigned char>(text[i]));
    }
    return out;
}

// Scripts report through console.log, the same way the other binding tests do.
[[nodiscard]] std::string logged(browser & page) {
    std::string out;
    for (const std::string & line : page.bindings().console_output()) {
        if (!out.empty()) { out += '|'; }
        out += line;
    }
    return out;
}

// A BMP, assembled byte by byte. 24bpp bottom-up is what image tools write by
// default, so that is the shape worth being sure about.
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
    // Row by row, because a BMP row is PADDED to four bytes - writing pixels
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
    // TRUNCATED image, and half of one is worse than none.
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

    // A second add REPLACES, so an application can override a built-in.
    assets.add("sprite.bmp", make_bmp(8, 8));
    CHECK(decode_bmp(assets.find("sprite.bmp")).width == 8);
}

void test_img_element() {
    ctbrowser::browser page{{.width = 200, .height = 120}};
    page.assets().add("cat.bmp", make_bmp(20, 10));
    page.load_html("<body><img src='cat.bmp'></body>");
    CHECK(page.frame());

    // The <img> is as big as its bitmap: no width attribute, so the size comes
    // from the decode. Before images existed this was zero.
    const rect box = box_of_tag(page, "img");
    CHECK(box.width == 20.0f);
    CHECK(box.height == 10.0f);

    // And it is actually PAINTED - the pixel in the middle of it is the
    // image's colour, not the page background.
    const auto image = page.read_pixels();
    CHECK(image.has_value());
    if (image) {
        const int x = static_cast<int>(box.x + box.width / 2);
        const int y = static_cast<int>(box.y + box.height / 2);
        CHECK(image->row(y)[static_cast<std::size_t>(x)] == 0xFF3366CCU);
    }
}

void test_img_sizing_rules() {
    ctbrowser::browser page{{.width = 200, .height = 120}};
    page.assets().add("cat.bmp", make_bmp(20, 10));

    // An explicit width with no height keeps the aspect ratio, which is what
    // `<img width=40>` on a 20x10 image means.
    page.load_html("<body><img src='cat.bmp' width='40'></body>");
    CHECK(page.frame());
    CHECK(box_of_tag(page, "img").width == 40.0f);
    CHECK(box_of_tag(page, "img").height == 20.0f);

    // A missing image is zero-sized rather than a broken-image box.
    page.load_html("<body><img src='absent.bmp'></body>");
    CHECK(page.frame());
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
    CHECK(page.frame());
    const auto pixels = page.canvases().pixels_of(find_id(page, "c"));
    CHECK(static_cast<bool>(pixels));
    if (pixels) {
        CHECK(pixels->at(6, 6) == 0xFF00FF00U);   // the plain draw
        CHECK(pixels->at(24, 24) == 0xFF00FF00U); // the source-rect draw
        CHECK(pixels->at(1, 1) == 0);             // and nothing outside them
    }
}

void test_img_in_canvas_from_element() {
    // drawImage takes an <img> ELEMENT as well as a loadImage handle. Pages use
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
    CHECK(page.frame());
    const auto pixels = page.canvases().pixels_of(find_id(page, "c"));
    if (pixels) { CHECK(pixels->at(4, 4) == 0xFFFF0000U); }
}

// THE EXACT SEQUENCE p5's loadImage RUNS, which is one piece of machinery with
// four parts and no value in testing them apart:
//
//   fetch the bytes -> new Blob([bytes]) -> URL.createObjectURL(blob)
//   -> new Image() with onload -> revoke the URL -> drawImage
//
// The revoke is the trap. It happens INSIDE onload, before the image is ever
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
    CHECK(page.frame());
    const auto pixels = page.canvases().pixels_of(find_id(page, "c"));
    CHECK(static_cast<bool>(pixels));
    if (pixels) {
        CHECK(pixels->at(6, 6) == 0xFF00FF00U);
        CHECK(pixels->at(1, 1) == 0);
    }
}

// A LOAD THAT FAILS SAYS SO. Reporting success with a zero-sized image is the
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

// `new Image()` IS an <img>: it can be appended and it lays out, which is what
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
    CHECK(page.frame());
    // Laid out at its intrinsic size, by the same code path an <img> in the
    // markup takes.
    CHECK(box_of_tag(page, "img").width == 20.0f);
    CHECK(box_of_tag(page, "img").height == 10.0f);
}

// The PNG this engine writes must be readable by things that are not this
// engine, so the check is structural AND independent: the file is written out
// and tools/check-png.py decodes it with Python's own zlib.
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

    // Written for tools/check-png.py, which is what proves the deflate stream
    // and both checksums are right rather than merely well-shaped.
    std::ofstream out{"build/render-encode.png", std::ios::binary};
    out.write(reinterpret_cast<const char *>(png.data()), static_cast<std::streamsize>(png.size()));
}

// EXPORT, END TO END - the path p5's save() takes, and the one place this engine
// invents a behaviour: an `<a download>` writes a file instead of raising a save
// dialog nobody is there to see.
//
//   canvas.toBlob(cb) -> new Blob -> URL.createObjectURL -> <a href download>
//   -> link.click() -> the bytes land on disk -> URL.revokeObjectURL
//
// Every link in that chain was missing at the start of this: click() did not
// exist, so p5's entire export API was a silent no-op.
void test_export_writes_a_file() {
    ctbrowser::browser page{{.width = 80, .height = 60}};
    page.set_download_directory("build/downloads");
    page.load_html(R"(<body>
      <canvas id='c' width='8' height='8'></canvas>
      <script>
        var ctx = document.getElementById('c').getContext('2d');
        ctx.fillStyle = '#3366cc';
        ctx.fillRect(0, 0, 8, 8);
        document.getElementById('c').toBlob(function (blob) {
          console.log('blob ' + blob.size + ' ' + blob.type + ' ' +
                      (blob instanceof Blob));
          var url = URL.createObjectURL(blob);
          var link = document.createElement('a');
          link.href = url;
          link.download = 'sketch.png';
          link.click();
          URL.revokeObjectURL(url);
        });
      </script>
    </body>)");
    CHECK(page.script_error().empty());
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
        // A real PNG of a real 8x8 canvas: the header alone is 8 + 25 bytes, and
        // 64 RGBA pixels plus filter bytes cannot be smaller than 264.
        CHECK(saved.bytes > 300);
        // And it is on the disk where it said it was, at the size it said.
        std::ifstream from_disk{saved.path, std::ios::binary | std::ios::ate};
        CHECK(from_disk.good());
        if (from_disk) { CHECK(static_cast<std::size_t>(from_disk.tellg()) == saved.bytes); }
    }
}

// A DOWNLOAD NAME IS A NAME, NOT A PATH. `download="../../x"` is a page choosing
// where to write on the host, and it does not get to.
void test_a_download_cannot_escape_its_directory() {
    ctbrowser::browser page{{.width = 60, .height = 40}};
    page.set_download_directory("build/downloads");
    page.load_html(R"(<body><script>
      var url = URL.createObjectURL(new Blob(['x']));
      var link = document.createElement('a');
      link.href = url;
      link.download = '../../escaped.txt';
      link.click();
    </script></body>)");
    CHECK(page.script_error().empty());
    CHECK(page.downloads().size() == 1);
    if (!page.downloads().empty()) {
        CHECK(page.downloads().front().name == ".._.._escaped.txt");
        CHECK(page.downloads().front().path.find("build/downloads") != std::string::npos);
    }
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

// FileReader - THE INPUT SIDE of the same machinery that does export.
//
// Nothing here can open a file picker, and it does not pretend to: an <input
// type=file> has an EMPTY FileList because there is no user to choose with. What
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
// document. Built on the fragment parse innerHTML already does, so everything a
// page walks afterwards is the ordinary element surface.
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
    // The tag name comes back UPPERCASE because this is the HTML parser - see the
    // DOMParser binding, where that deviation is written down.
    CHECK(logged(page) == "root LIST 2|attrs 1 kind=demo|items 2 first second|nested attr id=a");
}

void test_fetch_from_registry() {
    ctbrowser::browser page{{.width = 100, .height = 60}};
    page.assets().add("https://example.invalid/data.json",
                      bytes_of(R"({"n": 7, "who": "registry"})"));
    // Network OFF, so this can only be the registry - which is the point: a
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
    // FETCH IS ASYNCHRONOUS, so the event loop has to run. It used to do the
    // work inside the call and hand back a settled promise, which meant this
    // read its result before load_html returned - and meant a page could not
    // do anything while a request was outstanding. Each await costs a turn and
    // there are four here.
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
    // A network failure REJECTS - that is what a page's catch branch is written
    // for, and it is how fetchboard reports a resource that was not baked in.
    const std::string outcome = logged(page);
    CHECK(outcome.rfind("rejected:", 0) == 0);
    CHECK(outcome.find("not baked in") != std::string::npos);
}

} // namespace

int main() {
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
    test_to_data_url();
    test_blob_object_url_and_image();
    test_image_load_failure_is_reported();
    test_a_constructed_image_is_an_element();
    test_fetch_from_registry();
    test_fetch_rejects();

    REPORT("image_basics");
}
