module;
#include <cstdint>
#include <string_view>

#include <ctbrowser/core/core.hpp>

export module ctbrowser.style:ua;

// The user-agent stylesheet: what a document looks like before anyone styles it.
//
// Carried over from the previous engine's ua.hpp, which took its values from
// Gecko's html.css and Firefox's modern widget theme. It is not decoration -
// without it an <h1> is body text, a <ul> has no indent, a link is not blue, and
// <script> renders its own source. Every one of those is what "unstyled" looks
// like, and none of them is what a browser looks like.
//
// It loads at ORIGIN 0. The cascade in :engine puts author rules above it on
// ties, which is exactly the rule the CSS spec gives - so a page saying
// `h1 { font-size: 12px }` wins without anything here having to know about it.
//
// What is deliberately absent: the widget CHROME. the previous engine drew checkboxes, radios,
// scrollbar thumbs and menus from layout, using the palette below. the engine has no
// form widgets yet, so the palette is carried forward and the drawing is not -
// exporting colours nothing paints would be worse than the gap it hides.

export namespace ctbrowser::style {

// Firefox/Gecko defaults, trimmed to the properties this engine's layout and paint
// actually read. A declaration nothing consumes is a comment that costs
// selector matching, so the port is a subset on purpose - it grows when a
// consumer for the property does.
inline constexpr std::string_view ua_css = R"(
    body { margin: 8px; font-family: serif; color: #000000 }

    h1 { font-size: 32px; margin: 21px 0 }
    h2 { font-size: 24px; margin: 20px 0 }
    h3 { font-size: 19px; margin: 16px 0 }
    h4 { font-size: 16px; margin: 21px 0 }
    h5 { font-size: 13px; margin: 27px 0 }
    h6 { font-size: 11px; margin: 37px 0 }

    p, blockquote, figure, ul, ol, dl, pre { margin: 16px 0 }
    pre, textarea { white-space: pre; font-family: monospace }
    blockquote, figure { margin-left: 40px; margin-right: 40px }
    ul, ol { padding-left: 40px }
    dd { margin-left: 40px }
    li { display: block }

    a { color: #0000ee; text-decoration: underline; cursor: pointer }
    a:active { color: #ee0000 }
    input, textarea { cursor: text }

    button, select { background-color: #e9e9ed; color: #000000 }
    button:hover, select:hover { background-color: #d0d0d7 }
    button:active { background-color: #b1b1b9 }
    input, textarea { background-color: #ffffff }

    /* No width or padding on the controls: they are REPLACED elements, so
       their size comes from the element itself (layout's intrinsic_size_of),
       not from the cascade. A blanket `input { width: 160px }` here is what
       Gecko does, and it is wrong for checkboxes - it made them 160px wide. */
    th, td { padding: 1px }
    caption { text-align: center }
    /* Table parts are BLOCK-level here, not inline. Left inline, `normalise`
       wraps them in anonymous boxes and the table formatting context then looks
       for its rows and its caption among children that are no longer them - the
       caption simply vanished. */
    caption, thead, tbody, tfoot, tr, th, td { display: block }
    table { margin: 0 }

    hr { height: 2px; background-color: #808080; margin: 8px 0 }
    /* The padding-left is the gutter the disclosure triangle is drawn IN -
       unlike a list marker's, which is the parent <ul>'s padding. */
    summary { padding-left: 18px; cursor: pointer }
    mark { background-color: #ffff00 }
    small { font-size: 13px }
    big { font-size: 19px }

    head, style, script, title, meta, link, template, datalist, param, source,
    track, area, base, noscript { display: none }
)";

// The chrome palette, in 0xAARRGGBB. Nothing paints these yet - they are here
// because they are the Firefox values and re-deriving them later would be worse
// than carrying them. See the note above.
inline constexpr std::uint32_t ua_widget_frame = 0xFF8F8F9Du;
inline constexpr std::uint32_t ua_widget_accent = 0xFF0060DFu;
inline constexpr std::uint32_t ua_widget_mark = 0xFFFFFFFFu;
inline constexpr std::uint32_t ua_widget_field = 0xFFFFFFFFu;
inline constexpr std::uint32_t ua_selection_highlight = 0xFFB4D5FEu;
// A DISABLED control. Firefox greys the text and flattens the face; without
// this a disabled button is indistinguishable from one that works, which is
// the whole point of the attribute.
inline constexpr std::uint32_t ua_widget_disabled_text = 0xFF8F8F9Fu;
inline constexpr std::uint32_t ua_widget_disabled_face = 0xFFF0F0F4u;
inline constexpr std::uint32_t ua_canvas = 0xFFFFFFFFu; // the page behind everything
// The overlay scrollbar, in Firefox's greys.
// The frame `<table border=N>` draws, in Gecko's grey.
inline constexpr std::uint32_t ua_table_border = 0xFF808080u;
inline constexpr std::uint32_t ua_scrollbar_track = 0xFFF0F0F0u;
inline constexpr std::uint32_t ua_scrollbar_thumb = 0xFFC1C1C1u;
inline constexpr std::uint32_t ua_scrollbar_thumb_active = 0xFF8F8F8Fu;

// The origin the UA sheet loads at. Named rather than spelled 0 at call sites,
// because "add_sheet(css, 0)" reads like a magic number and it is the entire
// reason author rules beat these.
inline constexpr std::uint8_t ua_origin = 0;
inline constexpr std::uint8_t author_origin = 1;

} // namespace ctbrowser::style
