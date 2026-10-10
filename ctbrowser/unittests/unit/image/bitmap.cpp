// createImageBitmap and ImageBitmap (HTML 8.10): a bitmap from a canvas, an
// ImageData or another bitmap, cropped, drawn back with drawImage and detached
// by close(); and the REJECTIONS - a Blob that is no image an
// InvalidStateError (what promise-rejection-events.html's UA-triggered
// rejection is), an empty crop a RangeError, a non-image a TypeError.

#include <ctbrowser.hpp>

#include "check.hpp"

#include <string>
#include <vector>

using ctbrowser::shell::browser;
using ctbrowser::shell::browser_options;

int main() {
    browser page{browser_options{400, 300}};
    page.load_html(R"html(<!doctype html><html><body>
        <canvas id=a width=4 height=3></canvas><canvas id=b width=4 height=3></canvas>
        <script>
        const seen = [];
        const a = document.getElementById('a').getContext('2d');
        a.fillStyle = 'rgb(255, 0, 0)';
        a.fillRect(0, 0, 4, 3);
        const why = p => p.then(() => 'resolved', e => e.name);
        (async () => {
            const bitmap = await createImageBitmap(document.getElementById('a'));
            seen.push(bitmap instanceof ImageBitmap, bitmap.width + 'x' + bitmap.height);
            const part = await createImageBitmap(bitmap, 1, 1, 2, -1);
            seen.push(part.width + 'x' + part.height);
            const b = document.getElementById('b').getContext('2d');
            b.drawImage(bitmap, 0, 0);
            seen.push(Array.from(b.getImageData(1, 1, 1, 1).data).join(','));
            const data = new ImageData(new Uint8ClampedArray([0, 0, 255, 255]), 1, 1);
            seen.push((await createImageBitmap(data)).width);
            seen.push(await why(createImageBitmap(new Blob())));
            seen.push(await why(createImageBitmap(bitmap, 0, 0, 0, 1)));
            seen.push(await why(createImageBitmap({})));
            // Dimensions that do not describe the data, a NaN crop and a
            // crop past the pixel budget: refused, never allocated.
            const lying = new ImageData(1, 1);
            lying.width = 1e5; lying.height = 1e5;
            seen.push(await why(createImageBitmap(lying)));
            seen.push(await why(createImageBitmap(bitmap, 0, 0, NaN, 1)));
            seen.push(await why(createImageBitmap(bitmap, 1e300, 0, 1e5, 1e5)));
            bitmap.close();
            seen.push(bitmap.width, await why(createImageBitmap(bitmap)));
            let threw = 'no';
            try { new ImageBitmap(); } catch (e) { threw = e.name; }
            seen.push(threw);
            console.log(seen.join(' '));
        })();
        </script></body></html>)html");
    CHECK_EQ(page.script_error(), std::string{});
    for (int i = 0; i < 4; ++i) { (void)page.tick(16.0); }
    const std::vector<std::string> & logged = page.bindings().console_output();
    CHECK(!logged.empty());
    if (!logged.empty()) {
        CHECK_EQ(logged.back(),
                 std::string{"true 4x3 2x1 255,0,0,255 1 InvalidStateError "
                             "RangeError TypeError InvalidStateError RangeError RangeError "
                             "0 InvalidStateError TypeError"});
    }
    REPORT("image_bitmap");
}
