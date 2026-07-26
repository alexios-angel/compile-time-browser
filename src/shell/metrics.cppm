module;
#include <string_view>

export module ctbrowser.shell:metrics;

import ctbrowser.layout;
import ctbrowser.raster;

// The one place layout's idea of a font and raster's meet.
//
// It lives HERE because it has to name both, and neither may name the other:
// paint and raster sit downstream of layout, so layout importing raster would
// invert the direction the whole pipeline runs in. The shell is the assembly,
// so the shell converts - which is the same reason the browser owns the fonts.

export namespace ctbrowser::shell {

// Layout metrics backed by a real font. The SAME object answers "how wide is
// this run" and "where is its baseline" that the rasterizer will draw with -
// text lands where layout thought it would only if one thing answers both.
[[nodiscard]] inline ctbrowser::layout::measure_text_fn
metrics_for(const ctbrowser::raster::font_backend & fonts) {
	ctbrowser::layout::measure_text_fn out;
	out.measure = [&fonts](std::string_view text, float size,
	                       const ctbrowser::layout::text_face & face) {
		return fonts.advance(text, size, face.family, face.bold, face.italic);
	};
	out.ascent_of = [&fonts](float size, const ctbrowser::layout::text_face & face) {
		return fonts.ascent(size, face.family, face.bold, face.italic);
	};
	out.descent_of = [&fonts](float size, const ctbrowser::layout::text_face & face) {
		return fonts.descent(size, face.family, face.bold, face.italic);
	};
	return out;
}

// The always-available one, for tests and for a build with no font files.
[[nodiscard]] inline ctbrowser::layout::measure_text_fn font8x8_metrics() {
	return metrics_for(ctbrowser::raster::font8x8_fonts());
}

} // namespace ctbrowser::shell
