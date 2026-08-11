#pragma once
#include <cstdint>
#include <string_view>

#include <ctbrowser/core/core.hpp>

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

namespace ctbrowser::style {

// Firefox/Gecko defaults, trimmed to the properties this engine's layout and paint
// actually read. A declaration nothing consumes is a comment that costs
// selector matching, so the port is a subset on purpose - it grows when a
// consumer for the property does.
inline constexpr std::string_view ua_css = R"(
    body { margin: 8px; font-family: serif; color: #000000 }

    /* In `em`, as the real sheets write them, rather than pre-multiplied. The
       numbers here used to be those products worked out against a 16px parent,
       which is right for font-size and WRONG for margin: a margin's `em`
       resolves against the element's OWN size. h6 was the worst of it - 2.33em
       of an 11px h6 is 25px, not the 37px a 16px basis gives - and that error
       is why headings sat further apart here than anywhere else. */
    h1 { font-size: 2em; margin: 0.67em 0; font-weight: bold }
    h2 { font-size: 1.5em; margin: 0.83em 0; font-weight: bold }
    h3 { font-size: 1.17em; margin: 1em 0; font-weight: bold }
    h4 { font-size: 1em; margin: 1.33em 0; font-weight: bold }
    h5 { font-size: 0.83em; margin: 1.67em 0; font-weight: bold }
    h6 { font-size: 0.67em; margin: 2.33em 0; font-weight: bold }

    /* The text-level defaults. Every one of these was missing, so `<b>` was not
       bold and `<i>` was not italic - the resolver has always understood them,
       there was simply no rule saying so. */
    b, strong, th { font-weight: bold }
    i, em, cite, var, dfn, address { font-style: italic }
    u, ins { text-decoration: underline }
    s, del, strike { text-decoration: line-through }
    code, kbd, samp, tt { font-family: monospace }

    p, blockquote, figure, ul, ol, dl, pre { margin: 16px 0 }
    pre, textarea { white-space: pre; font-family: monospace }
    blockquote, figure { margin-left: 40px; margin-right: 40px }
    ul, ol { padding-left: 40px }
    dd { margin-left: 40px }
    /* `list-item`, not `block`: the marker belongs to the DISPLAY, so any sheet
       that says otherwise - `.list-group-item` is `display: flex` - takes it
       away, which is what a browser does and what stops a bullet appearing
       beside every item of every Bootstrap nav. */
    li { display: list-item }

    a { color: #0000ee; text-decoration: underline; cursor: pointer }
    a:active { color: #ee0000 }
    input, textarea { cursor: text }

    /* A <button> IS NOT A REPLACED ELEMENT, and this is where its size now
       comes from. It used to be sized by layout as `text_width(label) + 16` by
       `font_size * 1.25 + 6`, which shrink-wrapped by accident - a replaced
       element takes its size from the element - and could not be overridden by
       anything, because the cascade has no say in an intrinsic size. That made
       every `.btn` on a Bootstrap page unstyleable: its padding, its border and
       its radius were all ignored.

       These numbers reproduce the old ones exactly. 8px a side is the 16, 3px
       top and bottom is the 6, and the line box supplies the rest - so the box
       is the same size it always was, and now a sheet can change it. The one
       thing lost is `text-align: center`, which no formatting context reads
       yet; a button's label sits left until that lands. */
    button { display: inline-block; padding: 3px 8px; border: 1px solid #8f8f9d }
    button, select { background-color: #e9e9ed; color: #000000 }
    button:hover, select:hover { background-color: #d0d0d7 }
    button:active { background-color: #b1b1b9 }
    /* LAST, so it beats :hover and :active on source order - the two carry the
       same specificity and a disabled button must not light up under the
       pointer. These are ua_widget_disabled_text and _face below: the widget
       painter used to apply them itself, which it can no longer do for a button
       it no longer paints. Saying it here is where it belonged anyway, because
       a page can then override it. */
    button:disabled { color: #8f8f9f; background-color: #f0f0f4 }
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

// The chrome palette, in 0xAARRGGBB - the Firefox values. Every one of these is
// painted now, by browser::paint_replaced: the frame is a control's border, the
// accent fills a checked box, and the mark is the tick and the radio's dot.
// (This said "nothing paints these yet" long after they all did.)
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
