//===- ua.hpp - User-agent stylesheet ---------------------------*- C++ -*-===//
//
// The user-agent stylesheet: what a document looks like before anyone styles
// it. Values from Gecko's html.css and Firefox's modern widget theme.
//
// It loads at origin 0 (ua_origin). The cascade in style::engine ranks author
// rules above it, the order the CSS cascade specifies, so a page saying
// `h1 { font-size: 12px }` wins with nothing here knowing about it.
//
//===----------------------------------------------------------------------===//
#pragma once
#include <cstdint>
#include <string_view>

#include <ctbrowser/core/core.hpp>

namespace ctbrowser::style {

/// Firefox/Gecko defaults, trimmed to the properties this engine's layout and
/// paint read. A declaration nothing consumes still costs selector matching,
/// so the port is deliberately a subset that grows when a consumer for a
/// property appears.
inline constexpr std::string_view ua_css = R"(
    body { margin: 8px; font-family: serif; color: #000000 }

    /* In `em`, as the real sheets write them, rather than pre-multiplied against
       a 16px parent: a margin's `em` resolves against the element's OWN size. */
    h1 { font-size: 2em; margin: 0.67em 0; font-weight: bold }
    h2 { font-size: 1.5em; margin: 0.83em 0; font-weight: bold }
    h3 { font-size: 1.17em; margin: 1em 0; font-weight: bold }
    h4 { font-size: 1em; margin: 1.33em 0; font-weight: bold }
    h5 { font-size: 0.83em; margin: 1.67em 0; font-weight: bold }
    h6 { font-size: 0.67em; margin: 2.33em 0; font-weight: bold }

    /* The text-level defaults. */
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
    /* HTML's rendering section, `:focus-visible { outline: auto }`: a focused
       element the UA would show a ring for shows the platform's own
       (focus-visible-017-2). The longhand, because `auto` is an outline
       colour too and the shorthand's split would hand it to the colour. */
    :focus-visible { outline-style: auto }
    input, textarea { cursor: text }

    /* A <button> IS NOT A REPLACED ELEMENT: its size comes from here, so a sheet
       can change it. `text-align: center` is absent because no formatting
       context reads it yet; a button's label sits left until that lands. */
    button { display: inline-block; padding: 3px 8px; border: 1px solid #8f8f9d }
    button, select { background-color: #e9e9ed; color: #000000 }
    button:hover, select:hover { background-color: #d0d0d7 }
    button:active { background-color: #b1b1b9 }
    /* LAST, so it beats :hover and :active on source order - the two carry the
       same specificity and a disabled button must not light up under the
       pointer. These are ua_widget_disabled_text and _face below. */
    button:disabled { color: #8f8f9f; background-color: #f0f0f4 }
    input, textarea { background-color: #ffffff }
    /* THE ROOM A CONTROL KEEPS AROUND ITS TEXT, as real padding and a real
       border rather than constants in the engine, so a stylesheet can change
       it. */
    input, textarea, select { padding: 1px 5px; border: 1px solid #8f8f9d }
    /* A CHECKBOX IS A GLYPH, not a field: its 13x13 IS the whole control, and
       the padding a text field keeps around its text would make it 25 wide. */
    input[type=checkbox], input[type=radio] { padding: 0; border-width: 0 }
    /* The disabled face, in the sheet for the same reason the button's is. */
    input:disabled, textarea:disabled, select:disabled { background-color: #f0f0f4 }

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
    /* HTML's rendering section, 15.3.1: a `hidden` element is not rendered -
       `until-found` hides by content-visibility instead, and an <embed> keeps
       its box. */
    [hidden]:not([hidden=until-found i]):not(embed) { display: none }
)";

/// The widget palette, as 0xAARRGGBB Firefox values painted by
/// browser::paint_replaced: the frame is a control's border, the accent fills a
/// checked box, and the mark is the tick and the radio's dot.
inline constexpr std::uint32_t ua_widget_frame = 0xFF8F8F9Du;
inline constexpr std::uint32_t ua_widget_accent = 0xFF0060DFu;
inline constexpr std::uint32_t ua_widget_mark = 0xFFFFFFFFu;
inline constexpr std::uint32_t ua_widget_field = 0xFFFFFFFFu;
inline constexpr std::uint32_t ua_selection_highlight = 0xFFB4D5FEu;
/// A disabled control's colours. Firefox greys the text and flattens the face,
/// so a disabled button is visibly different from one that works.
inline constexpr std::uint32_t ua_widget_disabled_text = 0xFF8F8F9Fu;
inline constexpr std::uint32_t ua_widget_disabled_face = 0xFFF0F0F4u;
inline constexpr std::uint32_t ua_canvas = 0xFFFFFFFFu; // The page behind everything.
/// The frame `<table border=N>` draws, in Gecko's grey.
inline constexpr std::uint32_t ua_table_border = 0xFF808080u;
/// The overlay scrollbar, in Firefox's greys.
inline constexpr std::uint32_t ua_scrollbar_track = 0xFFF0F0F0u;
inline constexpr std::uint32_t ua_scrollbar_thumb = 0xFFC1C1C1u;
inline constexpr std::uint32_t ua_scrollbar_thumb_active = 0xFF8F8F8Fu;

/// The cascade origins: the UA sheet loads at 0 and author sheets at 1, which
/// is why author rules beat the UA's. Named so call sites do not read like
/// `add_sheet(css, 0)`.
inline constexpr std::uint8_t ua_origin = 0;
inline constexpr std::uint8_t author_origin = 1;

} // namespace ctbrowser::style
