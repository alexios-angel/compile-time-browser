// The element gallery: what every HTML element looks like OUT OF THE
// BOX - no page CSS beyond a width cap - rendered Firefox-style by the
// UA stylesheet (ua.hpp) and the embedded Tinos/Fira Sans/Cousine
// faces (fonts.hpp). Headings are bold serif on the Gecko scale, links
// are underlined #0000ee, lists get their markers, tables their grid,
// pre/code go monospace, and a page @font-face (Press Start 2P) proves
// MULTIPLE fonts coexist in one document.
//
// Build: cmake --build --preset default --target ctbrowser-example-elements

#include <ctbrowser.hpp>
#include <ctbrowser/app.hpp>
#include <SDL3/SDL_main.h>

#if defined(__has_builtin)
#	if __has_builtin(__builtin_std_embed)
#		pragma clang diagnostic push
#		pragma clang diagnostic ignored "-Wc++2d-extensions"
#depend "examples/assets/fonts/**"
#		pragma clang diagnostic pop
#	endif
#endif

using app = ctbrowser::page<R"(<!DOCTYPE html>
<title>element gallery</title>
<style>
	@font-face { font-family: 'Press Start 2P'; src: url("examples/assets/fonts/PressStart2P-Regular.ttf"); }
	body { width: 760px }
	#arcade { font-family: 'Press Start 2P', monospace; font-size: 16px; color: #7722cc }
	.sans { font-family: sans-serif }
	.mono { font-family: monospace }
</style>
<h1>Heading one</h1>
<h2>Heading two</h2>
<h3>Heading three</h3>
<h4>Heading four</h4>

<p>A paragraph in the default serif (Tinos). Each inline style renders
as its own row (block layout - no inline flow yet):</p>
<div><b>bold text</b></div>
<div><i>italic text</i></div>
<div><u>underlined text</u></div>
<div><s>struck text</s></div>
<div><mark>marked text</mark></div>
<div><code>code_text()</code></div>
<div><small>small text</small></div>
<div><big>big text</big></div>

<p class=sans>The same page can switch families per element - this
paragraph is Fira Sans:</p>
<div class=sans><b>bold Fira Sans</b></div>
<div class=sans><i>italic Fira Sans</i></div>

<p class=mono>And this is Cousine, the monospace face.</p>

<h3 id=arcade>PRESS START - a page @font-face</h3>

<blockquote>A blockquote indents forty pixels each side, exactly the
Gecko default.</blockquote>

<ul><li>an unordered item</li><li>another, with its disc</li></ul>
<ol><li>ordered one</li><li>ordered two</li></ol>

<pre>pre {
    preserves: layout;
}</pre>

<hr>

<p>Below: a link, underlined Gecko blue - clicking it opens the
system browser.</p>
<a href=https://example.com>example.com - the anchor default action</a>

<table border=1>
	<caption>a bordered table</caption>
	<tr><th>op</th><th>result</th></tr>
	<tr><td>1 + 1</td><td>2</td></tr>
	<tr><td>6 x 7</td><td>42</td></tr>
</table>

<table>
	<tr><th>plain</th><th>table</th></tr>
	<tr><td>no</td><td>borders</td></tr>
</table>

<details open><summary>details, open</summary><p>disclosed content.</p></details>
<details><summary>details, closed (click me)</summary><p>hidden until opened.</p></details>
<script></script>)">;

int main(int, char **) {
	ctbrowser::app_options opts;
	opts.width = 800;
	opts.height = 1280; // the gallery is tall and the engine does not scroll
	return ctbrowser::run_app<app>(opts);
}
