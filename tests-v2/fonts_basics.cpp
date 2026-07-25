// Fonts: which face a run is drawn in, and how it gets there.
//
// The engine used to have no answer at all - `measure_text_fn` was
// (text, size), so a page could ask for bold 20px Fira Sans and be measured in
// whatever the rasterizer felt like, then drawn in something else again. The
// interesting tests are therefore about AGREEMENT: the face layout resolved is
// the face the command carries, and the width layout measured is the width the
// rasterizer draws.

import ctbrowser;

#include "check.hpp"

#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

using namespace ctbrowser;
using ctbrowser::shell::browser;
using ctbrowser::shell::browser_options;

namespace {

void check(bool ok, std::string_view what) {
	if (!ok) {
		std::printf("FAIL %s\n", std::string{what}.c_str());
		++ctbrowser_test_failures;
	}
}

// Every text command the page draws, in order.
[[nodiscard]] std::vector<paint::paint_command> text_commands(browser & page) {
	std::vector<paint::paint_command> out;
	for (const auto & layer : page.layers().layers) {
		if (!layer.contents) { continue; }
		for (const auto & command : layer.contents->commands()) {
			if (command.op == paint::paint_op::text_run) { out.push_back(command); }
		}
	}
	return out;
}

[[nodiscard]] const paint::paint_command * run_saying(browser & page, std::string_view text) {
	static std::vector<paint::paint_command> held;
	held = text_commands(page);
	for (const auto & command : held) {
		if (command.text.find(text) != std::string::npos) { return &command; }
	}
	return nullptr;
}

// --- the face reaches the command -----------------------------------------

void test_family_weight_and_style_resolve() {
	browser page{browser_options{400, 200}};
	page.load_html(R"(<body>
	  <p style="font-family: Fira Sans; font-weight: bold">bolded</p>
	  <p style="font-family: 'Cousine', monospace; font-style: italic">sloped</p>
	  <p style="font-weight: 300">light</p>
	  <p style="font-weight: 700">heavy</p>
	</body>)");
	check(page.frame().has_value(), "the page renders");

	const paint::paint_command * bolded = run_saying(page, "bolded");
	check(bolded != nullptr, "the bold run was recorded");
	if (bolded != nullptr) {
		check(bolded->face.family == "Fira Sans", "the family reaches the command");
		check(bolded->face.bold, "and the weight");
		check(!bolded->face.italic, "which does not turn on italic");
	}

	const paint::paint_command * sloped = run_saying(page, "sloped");
	if (sloped != nullptr) {
		// The FIRST name of the list, unquoted: choosing among the alternatives
		// is layout's job, and a quoted name is the same name.
		check(sloped->face.family == "Cousine", "the first family of a list, unquoted");
		check(sloped->face.italic, "and the style");
	}

	// The numeric weights, at the 600 boundary CSS draws.
	const paint::paint_command * light = run_saying(page, "light");
	if (light != nullptr) { check(!light->face.bold, "300 is not bold"); }
	const paint::paint_command * heavy = run_saying(page, "heavy");
	if (heavy != nullptr) { check(heavy->face.bold, "700 is"); }
}

void test_the_face_inherits() {
	browser page{browser_options{400, 200}};
	page.load_html(R"(<body>
	  <div style="font-family: Fira Sans; font-weight: bold">
	    outer
	    <span>nested</span>
	    <span style="font-weight: normal">reset</span>
	  </div>
	</body>)");
	check(page.frame().has_value(), "the page renders");

	// A child with no font of its own is drawn in its parent's, which is what
	// makes `body { font-family: ... }` mean anything at all.
	const paint::paint_command * nested = run_saying(page, "nested");
	check(nested != nullptr, "the nested run exists");
	if (nested != nullptr) {
		check(nested->face.family == "Fira Sans", "the family is inherited");
		check(nested->face.bold, "and so is the weight");
	}
	// ...and a child that states its own overrides only that.
	const paint::paint_command * reset = run_saying(page, "reset");
	if (reset != nullptr) {
		check(reset->face.family == "Fira Sans", "the family still comes from the parent");
		check(!reset->face.bold, "the weight it stated wins");
	}
}

void test_decoration() {
	browser page{browser_options{400, 200}};
	// The UA sheet underlines links, so this is also the check that the UA
	// stylesheet's decoration reaches the rasterizer.
	page.load_html(R"(<body>
	  <a href="#">linked</a>
	  <p style="text-decoration: line-through">struck</p>
	  <p style="text-decoration: none">plain</p>
	</body>)");
	check(page.frame().has_value(), "the page renders");

	if (const paint::paint_command * linked = run_saying(page, "linked")) {
		check(linked->decoration == paint::text_decoration::underline, "a link is underlined");
	}
	if (const paint::paint_command * struck = run_saying(page, "struck")) {
		check(struck->decoration == paint::text_decoration::line_through, "line-through");
	}
	if (const paint::paint_command * plain = run_saying(page, "plain")) {
		check(plain->decoration == paint::text_decoration::none, "and none is none");
	}
}

void test_the_underline_is_actually_drawn() {
	// The band is the rasterizer's job, so the proof is pixels: a link's row of
	// pixels below the glyphs is its colour, and the same page without the
	// decoration has nothing there.
	const auto ink_below_text = [](std::string_view html) {
		browser page{browser_options{200, 80}};
		page.load_html(std::string{html});
		(void)page.frame();
		const auto image = page.read_pixels();
		std::size_t found = 0;
		if (image) {
			for (int y = 0; y < image->height(); ++y) {
				const auto row = image->row(y);
				for (int x = 0; x < image->width(); ++x) {
					// The UA link colour, #0000ee.
					if ((row[static_cast<std::size_t>(x)] & 0x00FFFFFFU) == 0x0000EEU) { ++found; }
				}
			}
		}
		return found;
	};
	const std::size_t underlined = ink_below_text("<body><a href='#'>link</a></body>");
	const std::size_t bare =
	    ink_below_text("<body><a href='#' style='text-decoration: none'>link</a></body>");
	check(underlined > bare, "the underline puts ink on the page");
	check(bare > 0, "and the text itself is still drawn without it");
}

// --- layout and raster agree ----------------------------------------------

void test_layout_measures_with_the_drawing_font() {
	// font8x8 advances by 8 * scale per code point, and layout must measure
	// with THAT - a run whose recorded box is narrower than what gets drawn is
	// text that overflows its own line.
	browser page{browser_options{600, 120}};
	page.load_html("<body><p>abcdef</p></body>");
	check(page.frame().has_value(), "the page renders");
	if (const paint::paint_command * run = run_saying(page, "abcdef")) {
		const float drawn = raster::font8x8_advance("abcdef", run->font_size);
		check(run->bounds.width == drawn, "the recorded box is exactly the drawn width");
	}
}

void test_font8x8_quantises_and_says_so() {
	// font8x8 scales an 8x8 cell by an INTEGER factor - round(size/8) - so every
	// size in a bucket renders identically: 12px through 19px are all scale 2.
	// That is a property of the font rather than a rounding bug, and it is
	// asserted here so it cannot be mistaken for one. An outline backend
	// removes it.
	//
	// (The plan said "16px and 20px render identically". They do not: 20/8
	// rounds to 3. The bucket boundary is at 20px, not past it.)
	check(raster::font8x8_advance("x", 12) == raster::font8x8_advance("x", 19),
	      "12px and 19px are the same in font8x8");
	check(raster::font8x8_advance("x", 16) == raster::font8x8_advance("x", 18),
	      "and so are 16px and 18px");
	check(raster::font8x8_advance("x", 19) < raster::font8x8_advance("x", 20),
	      "20px is the next bucket up");
}

} // namespace

int main() {
	test_family_weight_and_style_resolve();
	test_the_face_inherits();
	test_decoration();
	test_the_underline_is_actually_drawn();
	test_layout_measures_with_the_drawing_font();
	test_font8x8_quantises_and_says_so();

	REPORT("fonts_basics");
}
