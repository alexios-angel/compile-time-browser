#ifndef CTBROWSER__UA__HPP
#define CTBROWSER__UA__HPP

#ifndef CTBROWSER_IN_A_MODULE
#include <cstdint>
#include <string_view>
#endif

// The user-agent stylesheet: what every element looks like before the
// page says anything - values taken from Firefox (Gecko's html.css and
// the modern form-widget theme), em converted to px at the 16px base,
// and mapped onto the properties the layout engine supports (display,
// font-size, margin/padding, background-color, color, width/height).
//
// The engine consults it AFTER the page's stylesheet (author beats UA,
// the CSS cascade-origin rule); inline styles beat both.
//
// Engine-capability gaps vs Firefox, documented rather than faked:
//   - no bold/italic/underline (single bitmap font face) - h1-h6 and
//     b/strong/i/em/u render regular; a has the link color, no underline
//   - no monospace - pre/code keep their margins but share the one font
//     (Firefox draws them unshaded too; the font is the only difference)
//   - no table layout - table-family elements stack as blocks
//   - no attribute selectors - dialog (Firefox: display:none unless
//     [open]) renders open; hidden inputs are skipped by the widget
//     emitter instead
// The form-widget chrome (1px #8f8f9d frames, the #0060df checked
// accent) is drawn by layout's emit paths - layout has no border
// property, so borders live with the widgets.

namespace ctbrowser::detail {

inline constexpr std::string_view ua_css = R"(
	body { margin: 8px }

	h1 { font-size: 32px; margin: 11px 0 }
	h2 { font-size: 24px; margin: 20px 0 }
	h3 { font-size: 19px; margin: 16px 0 }
	h4 { font-size: 16px; margin: 21px 0 }
	h5 { font-size: 13px; margin: 27px 0 }
	h6 { font-size: 11px; margin: 37px 0 }

	p, blockquote, figure, ul, ol, dl, pre { margin: 16px 0 }
	blockquote, figure { margin-left: 40px; margin-right: 40px }
	ul, ol { padding-left: 40px }
	dd { margin-left: 40px }

	a { color: #0000ee }
	a:active { color: #ee0000 }

	button, select { background-color: #e9e9ed; color: #000000; padding: 1px 8px }
	button:hover, select:hover { background-color: #d0d0d7 }
	button:active { background-color: #b1b1b9 }
	button:disabled, select:disabled, input:disabled { color: #8f8f9d }
	input { background-color: #ffffff; padding: 1px 4px; width: 160px }

	hr { height: 2px; background-color: #808080; margin: 8px 0 }
	mark { background-color: #ffff00 }
	small { font-size: 13px }
	big { font-size: 19px }

	head, style, script, title, meta, link, template, datalist, param, source, track, area { display: none }
)";

// Firefox's modern form-widget theme colors (the chrome layout draws)
inline constexpr std::uint32_t ua_widget_frame = 0xFF8F8F9Du;  // borders
inline constexpr std::uint32_t ua_widget_accent = 0xFF0060DFu; // checked fill
inline constexpr std::uint32_t ua_widget_mark = 0xFFFFFFFFu;   // the check/dot

} // namespace ctbrowser::detail

#endif
