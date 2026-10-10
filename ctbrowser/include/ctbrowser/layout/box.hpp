//===- box.hpp - The box tree and its builder ----------------- -*- C++ -*-===//
//
// The box tree, which is not the DOM tree, and box_builder, which builds it
// from a document and its computed styles.
//
// Layout results live here and not on DOM nodes, so layout does not mutate
// shared document state, and so boxes with no element behind them can exist.
// CSS requires such boxes: a block container with both inline and block
// children must wrap each inline run in an anonymous block box, and the DOM
// has no node to hang that box on.
//
//===----------------------------------------------------------------------===//
#pragma once
#include <algorithm>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include <ctbrowser/core/algorithms.hpp>
#include <ctbrowser/core/core.hpp>
#include <ctbrowser/dom/dom.hpp>
#include <ctbrowser/style/css/token.hpp>
#include <ctbrowser/style/engine.hpp>
#include <ctbrowser/style/style.hpp>

#include <ctbrowser/layout/values.hpp>

namespace ctbrowser::layout {

using ctbrowser::node_id;
using ctbrowser::style::computed_style_ptr;

/// What kind of box a box_node is, which picks its formatting context.
enum class box_kind : std::uint8_t {
    block,     ///< A block-level box.
    inline_,   ///< An inline-level box.
    text,      ///< A run of text.
    anonymous, ///< A generated block wrapping inline children; no source element.
    table,     ///< A <table>: its rows share column widths, so it cannot be laid
               ///< out as blocks. A block sizes itself from its own content; a
               ///< cell sizes itself from a column its siblings in other rows
               ///< also occupy.
    flex,      ///< A flex container. Like a table, its children's sizes are not
               ///< their own, because free space is distributed across them.
               ///< `display: inline-flex` is this kind too, with inline_level
               ///< set: the inside and outside of a display value are separate.
    replaced,  ///< A box whose size comes from the element, not its children:
               ///< <canvas>, <input>, <select>, <textarea>, <img>. CSS calls these
               ///< replaced elements; laying one out from its children would
               ///< give a zero-size canvas.
};

/// One box in the box tree, with every style property layout needs already
/// resolved.
struct box_node {
    box_kind kind = box_kind::block;
    node_id source; // empty for anonymous boxes
    /// A ::before or ::after box's originating element, and its text's: the
    /// box is not the element, so `source` stays empty and a geometry query
    /// never finds it, but a hit on a pseudo-element is a hit on the element
    /// it belongs to (CSS Pseudo 4 §4.1); Bootstrap's `.stretched-link::after`
    /// is an overlay whose clicks go to its <a>. Empty everywhere else.
    node_id origin;
    /// The element's tag, for the handful of decisions that are about what an
    /// element is rather than about its style (table parts, list items). A view
    /// into the atom table, which outlives every box tree built from it.
    std::string_view tag;
    computed_style_ptr style; // shared; anonymous boxes inherit their parent's
    std::string text;         // text boxes only
    std::vector<box_node> children;

    // Every length below is parsed once when the box is built, so the
    // algorithms do not re-parse strings for every layout pass.
    length width{}, height{};
    /// `min-width` / `max-width`.
    length min_width{}, max_width{};
    /// `min-height` / `max-height`. Flex consumes them on either main or cross
    /// axis; block flow clamps its finished border box with the same
    /// max-then-min rule. A non-zero minimum also participates in the narrow
    /// empty-block margin-collapse exception.
    length min_height{}, max_height{};
    /// `box-sizing: border-box`: the author's width and height name the border
    /// box. Under the initial `content-box` they name the content box (CSS UI 3
    /// §3.1). border_box_size (layout/algorithm/common.hpp) applies the
    /// difference.
    bool border_box = false;
    /// Whether the horizontal margin was written as the keyword `auto`.
    ///
    /// It cannot be read off `margin.left.is_auto()`: `length{}` defaults to
    /// `unit::auto_`, so an unset margin and an explicit `auto` are the same
    /// value. CSS says an unspecified margin is 0; only the keyword centres a
    /// box.
    bool margin_left_auto = false;
    bool margin_right_auto = false;
    /// The vertical pair, read by flex only. An auto margin on a flex item
    /// absorbs the line's free space (§9.5) on whichever axis it is on, and a
    /// `column` container's main axis is the vertical one. block_flow ignores
    /// them, which is correct: a vertical auto margin outside a flex container
    /// is zero.
    bool margin_top_auto = false;
    bool margin_bottom_auto = false;
    /// Set for `display: inline-flex` and `inline-block`. The box runs a flex
    /// or block formatting context inside while it takes part in its parent's
    /// inline formatting context outside, which is what the two-value `display`
    /// syntax says.
    ///
    /// A bool rather than another box_kind, because `inline-flex` and `flex`
    /// lay out their children the same way and would otherwise duplicate every
    /// branch that dispatches on kind. Consulted only for kinds that would
    /// otherwise be block-level: `inline_`, `text` and `replaced` are
    /// inline-level by kind and leave this false.
    bool inline_level = false;
    /// `overflow` other than visible/clip establishes an independent block
    /// formatting context. Its children's margins still collapse with each
    /// other, but the first/last ones cannot escape through this box's edges,
    /// which matters most when the same box clips their paint.
    bool blocks_margin_collapse = false;
    /// The CSS Overflow 3 classification. `scroll_container` is `overflow`
    /// hidden/scroll/auto/overlay on either axis: the box has a scrolling box
    /// and a scroll offset (CSSOM View reads and writes it). `clips_overflow`
    /// adds `clip`, which clips paint and the scrollable overflow contribution
    /// to an ancestor but scrolls nothing. Both false is `visible`, where a
    /// descendant that spills out still counts toward the ancestor's scrollable
    /// overflow rectangle (layout/overflow.hpp).
    bool scroll_container = false;
    bool clips_overflow = false;
    /// Flows layout does not model: right-to-left direction, which reverses
    /// the inline axis, and a vertical writing mode, which turns both axes.
    /// Layout places everything horizontal-tb, left to right, and a question
    /// whose answer depends on the flow (where a reversed flex container's
    /// scrolling starts) is answered in that one mode on each flagged axis.
    bool unmodelled_inline = false, unmodelled_block = false;
    /// `height: stretch` (CSS Sizing 4 §3.1): the box's margin box fills its
    /// containing block's height when that is definite, and it is `auto` when
    /// it is not. `height` itself reads `auto` for it, as parse_length has it.
    bool height_stretch = false;
    /// Size containment (CSS Containment 2 §3.2): the box is sized as if it
    /// had no content, on both axes for `contain: size` and `container-type:
    /// size`, on the inline axis for `inline-size`, so a query container's
    /// size never depends on what its queries style.
    bool contain_block_size = false;
    bool contain_inline_size = false;
    side_lengths margin{}, padding{};
    /// `border-collapse` and `border-spacing` on a <table>. The defaults are
    /// `separate` with 2px spacing. Bootstrap's Reboot sets `collapse`, which
    /// removes the gap between cells.
    bool collapse_borders = false;
    length border_spacing{2, unit::px};
    /// Where this box is placed, and by whom. A `static` box is placed by its
    /// parent. A `relative` box is laid out in flow and then moved. An
    /// `absolute` or `fixed` box is taken out of flow and placed against an
    /// ancestor, which is why positioning is a separate pass over the fragment
    /// tree (layout/position.hpp).
    position_kind position = position_kind::static_;
    /// `auto` is absence, integer zero is present. Paint uses this to build the
    /// stacking-context tree; layout itself does not change geometry for it.
    std::optional<int> z_index;
    side_lengths inset{}; // top / right / bottom / left
    /// `transform: translate(x, y)`, which for a translation alone is a pure
    /// offset. Bootstrap's `.translate-middle` anchors centred overlays this
    /// way, and its percentages are of the box's own size.
    translation translate{};
    /// Any computed transform establishes a stacking context, including a zero
    /// translation. The translation values alone cannot retain that fact
    /// because both `none` and `translate(0)` resolve to the same offsets.
    bool transformed = false;
    /// The used border width on each side. It is zero on any side whose
    /// `border-style` is `none`, `hidden` or absent, as CSS requires, so a bare
    /// `border-width: 1px` draws and measures nothing. See border_of().
    side_lengths border{};
    /// Every flex property, resolved. See flex_spec: a box carries both the
    /// container half and the item half because it may be both at once.
    flex_spec flex{};
    float font_size = 16;
    /// The line box's height in px, resolved once from `line-height` (see
    /// resolve_line_height) so layout does not re-parse it for every line.
    float line_height = 16 * 1.25f;
    /// The font face, resolved once per element rather than per glyph in the
    /// rasterizer, which has no cascade to ask.
    text_face face{};
    /// `text-align`, as the share of a line's leftover space that goes before
    /// it (see parse_text_align). Inherited through the cascade, which is what
    /// makes `.text-center` on a container centre the text of everything
    /// inside it.
    float text_align = 0;
    /// `opacity`. Read by paint rather than by layout (it changes no geometry)
    /// and resolved here so the recorder has one place to ask. 1 is opaque,
    /// which is both the initial value and the fallback for an unreadable one.
    float opacity = 1;
    /// Whether this box draws a list marker: its display is `list-item` and its
    /// `list-style-type` is not `none`. Either condition alone removes the
    /// marker (Bootstrap's `.list-group-item` uses `display: flex`, and
    /// `.list-unstyled` sets `none`).
    bool list_marker = false;
    bool underline = false;
    bool line_through = false;
    /// Generated content the recorder draws: a list item's number (0 = a disc,
    /// i.e. an unordered list) and whether a <details> is open. Decided here
    /// because it is a question about the document (which sibling this is,
    /// what the parent element says), and the recorder has neither.
    int list_ordinal = 0;
    bool details_open = false;
    /// A text box that is one space from collapsed inter-element whitespace.
    /// Rendered between inline content and dropped everywhere else; see
    /// drop_collapsible_spaces.
    bool collapsible_space = false;
    /// `<table border=N>`. A presentational attribute rather than CSS, and it
    /// frames the cells as well as the table, so it rides on the box.
    float border_px = 0;
    /// `white-space: pre`. The text keeps its newlines, and layout has to turn
    /// them into line breaks rather than letting them reach the rasterizer.
    bool preformatted = false;

    /// What a replaced element is sized as when the sheet says nothing. Zero
    /// means it has none and falls back to the block rules.
    float intrinsic_width = 0;
    float intrinsic_height = 0;

    // The table parts. Kept as predicates on the box rather than as more
    // box_kinds: a row and a cell lay out as ordinary blocks, and only the
    // table needs its own formatting context.
    [[nodiscard]] bool is_row() const noexcept { return tag == "tr"; }
    [[nodiscard]] bool is_row_group() const noexcept {
        return tag == "thead" || tag == "tbody" || tag == "tfoot";
    }
    [[nodiscard]] bool is_cell() const noexcept { return tag == "td" || tag == "th"; }

    [[nodiscard]] bool is_block_level() const noexcept {
        // `inline-flex` first: the box is one of the kinds below, at an inline
        // level. Testing the kind first would make an inline-flex container
        // block-level and break the line it is supposed to sit on.
        if (inline_level) { return false; }
        // Tables and flex containers are block-level too, so they never share
        // a line with their siblings.
        return kind == box_kind::block || kind == box_kind::anonymous || kind == box_kind::table ||
               kind == box_kind::flex;
    }
    [[nodiscard]] bool is_replaced() const noexcept { return kind == box_kind::replaced; }
    /// Whether the box is out of flow: it reserves no space among its siblings
    /// and its position comes from an ancestor rather than from its parent's
    /// cursor. `relative` is not out of flow: it keeps its slot and is only
    /// painted elsewhere, which is why a relative box leaves a hole and an
    /// absolute one does not.
    [[nodiscard]] bool is_out_of_flow() const noexcept {
        return position == position_kind::absolute || position == position_kind::fixed;
    }
    /// Whether this box establishes a containing block for absolutely
    /// positioned descendants: anything but `static`, per CSS 2.1 §10.1. That
    /// is why `.position-relative` with no offsets is a common declaration: it
    /// exists to be an anchor.
    [[nodiscard]] bool is_positioned() const noexcept { return position != position_kind::static_; }
    /// Whether this box lays out its children with inline_flow.
    [[nodiscard]] bool establishes_inline_context() const noexcept {
        // A block whose children are all inline runs an inline formatting
        // context; a block with block children runs a block one. Mixed content
        // never reaches here - the builder wraps it.
        if (kind == box_kind::replaced) { return false; } // its children are not laid out
        // A flex container's children are flex items, never line participants,
        // whatever they contain. Without this check a `.d-flex` holding text
        // would run the inline algorithm.
        if (kind == box_kind::flex) { return false; }
        bool has_in_flow_child = false;
        for (const box_node & c : children) {
            // An out-of-flow child leaves a static-position marker, not a line
            // box. Counting one would make a block containing only
            // `position:absolute` content one line high, and stop its empty
            // vertical margins collapsing through it.
            if (c.is_out_of_flow()) { continue; }
            has_in_flow_child = true;
            if (c.is_block_level()) { return false; }
        }
        return has_in_flow_child;
    }
};

/// Builds the box tree from a document read view plus resolved styles.
///
/// The text measure passed in should be the same one layout and raster use,
/// so a <button> sized from its label is drawn with the same metric.
class box_builder {
public:
    /// The natural size of an <img> or <svg>. Layout cannot decode images, so
    /// the embedder supplies this through `intrinsic_image`. When it is unset,
    /// an <img> with no width/height attribute has zero size.
    struct intrinsic_size {
        float width = 0;
        float height = 0;
    };
    std::function<intrinsic_size(node_id)> intrinsic_image;

    box_builder(atom_table & atoms, const style::style_map & styles, measure_text_fn measure = {})
        : atoms_(&atoms), styles_(&styles), measure_(std::move(measure)),
          display_(atoms.intern("display")), width_(atoms.intern("width")),
          height_(atoms.intern("height")), font_size_(atoms.intern("font-size")),
          margin_sides_{atoms.intern("margin-top"), atoms.intern("margin-right"),
                        atoms.intern("margin-bottom"), atoms.intern("margin-left")},
          padding_sides_{atoms.intern("padding-top"), atoms.intern("padding-right"),
                         atoms.intern("padding-bottom"), atoms.intern("padding-left")},
          font_family_(atoms.intern("font-family")), font_weight_(atoms.intern("font-weight")),
          font_style_(atoms.intern("font-style")),
          text_decoration_(atoms.intern("text-decoration-line")),
          white_space_(atoms.intern("white-space")), line_height_(atoms.intern("line-height")),
          min_width_(atoms.intern("min-width")), max_width_(atoms.intern("max-width")),
          min_height_(atoms.intern("min-height")), max_height_(atoms.intern("max-height")),
          border_width_(atoms.intern("border-width")), border_style_(atoms.intern("border-style")),
          border_sides_{atoms.intern("border-top-width"), atoms.intern("border-right-width"),
                        atoms.intern("border-bottom-width"), atoms.intern("border-left-width")},
          border_style_sides_{atoms.intern("border-top-style"), atoms.intern("border-right-style"),
                              atoms.intern("border-bottom-style"),
                              atoms.intern("border-left-style")},
          position_(atoms.intern("position")), z_index_(atoms.intern("z-index")),
          transform_(atoms.intern("transform")), text_align_(atoms.intern("text-align")),
          opacity_(atoms.intern("opacity")), overflow_x_(atoms.intern("overflow-x")),
          overflow_y_(atoms.intern("overflow-y")), contain_(atoms.intern("contain")),
          container_type_(atoms.intern("container-type")),
          list_style_type_(atoms.intern("list-style-type")),
          border_collapse_(atoms.intern("border-collapse")),
          border_spacing_(atoms.intern("border-spacing")),
          inset_sides_{atoms.intern("top"), atoms.intern("right"), atoms.intern("bottom"),
                       atoms.intern("left")},
          inset_inline_{atoms.intern("inset-inline-start"), atoms.intern("inset-inline-end"),
                        atoms.intern("inset-block-start"), atoms.intern("inset-block-end")},
          box_sizing_(atoms.intern("box-sizing")), direction_(atoms.intern("direction")),
          writing_mode_(atoms.intern("writing-mode")),
          flex_{atoms.intern("flex-direction"),  atoms.intern("flex-wrap"),
                atoms.intern("justify-content"), atoms.intern("align-items"),
                atoms.intern("align-content"),   atoms.intern("align-self"),
                atoms.intern("flex-grow"),       atoms.intern("flex-shrink"),
                atoms.intern("flex-basis"),      atoms.intern("order"),
                atoms.intern("row-gap"),         atoms.intern("column-gap")} {}

    /// Resolves `line-height` to px.
    ///
    ///   normal          1.25 times the font size. Not the font's own metrics:
    ///                   those come from the injected measure, which goldens
    ///                   pin to font8x8, so deriving `normal` from them would
    ///                   move every render for reasons unrelated to the sheet.
    ///   <number>        a factor on this element's font size, which is why a
    ///                   unitless value is the one that inherits usefully
    ///   <length>        itself
    ///   <percentage>    a factor, resolved against this element's font size
    ///
    /// Not modelled: a percentage computes to a length in CSS, so a child
    /// should inherit the resolved px rather than the percentage. Inheriting
    /// the text re-resolves it against the child's own size. The same is true
    /// of `em`, and both go away when the cascade folds units.
    [[nodiscard]] static float resolve_line_height(std::string_view text, float font_size) {
        constexpr float normal_factor = 1.25f;
        const std::string_view value = trimmed(text);
        if (value.empty() || value == "normal") { return font_size * normal_factor; }
        const length parsed = parse_length(value);
        // A bare number is not a length: parse_length calls a unitless value
        // `unit::none` and treats it as px, which is right for `width: 5` and
        // wrong here: `line-height: 1.5` is one and a half times the font
        // size, not one and a half pixels.
        if (parsed.u == unit::none) { return parsed.value * font_size; }
        if (parsed.u == unit::percent) { return parsed.value / 100.0f * font_size; }
        if (parsed.is_auto()) { return font_size * normal_factor; }
        return parsed.resolve(font_size, font_size);
    }

    /// Builds the box tree for the subtree at `root`.
    [[nodiscard]] box_node build(const read_txn & txn, node_id root);

private:
    [[nodiscard]] computed_style_ptr style_of(node_id id) const;
    [[nodiscard]] std::string_view prop(const computed_style_ptr & s, atom name) const;

    void build_children(const read_txn & txn, node_id parent, box_node & into, float inherited_font,
                        const text_face & inherited_face = {}, bool inherited_underline = false,
                        bool inherited_line_through = false, bool preserve_whitespace = false) {
        int ordinal = 0;
        // A <details> without `open` shows only its <summary>. Asked once, here,
        // because the state is on the parent and the children are what it hides.
        const bool closed_details = atoms_->text(txn.tag(parent).value_or(atom{})) == "details" &&
                                    !txn.has_attribute(parent, atoms_->intern("open"));
        for (const node_id child : txn.children(parent)) {
            const node_kind kind = txn.kind(child).value_or(node_kind::comment);
            if (is_text_kind(kind)) { // Text and its CDATA subclass; nothing else draws
                const std::string_view text = txn.text(child);
                // A whitespace-only text node is not always nothing. Between two
                // inline-level boxes it collapses to one space and is rendered:
                // `</label> <input>` is a label, a space and a field, and
                // `<b>a</b> <b>b</b>` keeps its words apart. It is nothing
                // between blocks and at the start or end of a line;
                // drop_collapsible_spaces decides that once the siblings are
                // known.
                if (!preserve_whitespace && trimmed(text).empty()) {
                    box_node space;
                    space.kind = box_kind::text;
                    space.source = child;
                    space.style = into.style;
                    space.text = " ";
                    space.collapsible_space = true;
                    space.font_size = inherited_font;
                    space.line_height = into.line_height;
                    space.face = inherited_face;
                    space.underline = inherited_underline;
                    space.line_through = inherited_line_through;
                    into.children.push_back(std::move(space));
                    continue;
                }
                box_node t;
                t.kind = box_kind::text;
                t.source = child;
                t.style = into.style;
                // Collapse whitespace unless the element preserves it. HTML folds
                // each run of whitespace into one space. Without this, newlines
                // reach the rasterizer (where a real font draws them as .notdef
                // boxes), and words_that_fit, which splits only on ' ', cannot
                // break the line there.
                t.text = preserve_whitespace ? std::string{text} : collapse_whitespace(text);
                t.preformatted = preserve_whitespace;
                t.font_size = inherited_font;
                t.line_height = into.line_height;
                // A text box has no style of its own; it is drawn in whatever
                // its parent element resolved to.
                t.face = inherited_face;
                t.underline = inherited_underline;
                t.line_through = inherited_line_through;
                into.children.push_back(std::move(t));
                continue;
            }
            if (kind != node_kind::element) { continue; } // comments produce nothing

            const computed_style_ptr style = style_of(child);
            const atom tag = txn.tag(child).value_or(atom{});
            display_kind d =
                parse_display(prop(style, display_), default_display_for(atoms_->text(tag)));
            if (d == display_kind::none) { continue; } // pruned: no box at all
            // A flex item's display is blockified. `into` already has its kind by
            // the time it recurses, so the parent's formatting context is known
            // here and this needs no second pass. See blockify().
            if (into.kind == box_kind::flex) { d = blockify(d); }
            // A closed <details> shows its <summary> and nothing else. This
            // cannot be a UA rule: `details > :not(summary) { display: none }`
            // needs a selector the cascade does not have, and the state lives on
            // the parent rather than on the hidden child.
            if (closed_details && atoms_->text(tag) != "summary") { continue; }

            box_node b;
            const std::string_view tag_text = atoms_->text(tag);
            b.kind = is_replaced_tag(tag_text) ? box_kind::replaced
                     : tag_text == "table"     ? box_kind::table
                     : (d == display_kind::flex || d == display_kind::inline_flex)
                         ? box_kind::flex
                         : (d == display_kind::inline_level ? box_kind::inline_ : box_kind::block);
            // `list-item` is a block box; only its marker differs, and that is
            // paint's business rather than a formatting context's.
            // The outside of the display value is a separate question from the
            // inside: `inline-flex` is the flex kind at an inline level and
            // `inline-block` is the block kind at one. It is set from the
            // display value rather than inferred from the kind, because neither
            // implies the other. Both take their width from
            // shrink_to_fit_width instead of filling the containing block.
            b.inline_level = d == display_kind::inline_flex || d == display_kind::inline_block;
            b.source = child;
            b.tag = tag_text;
            b.style = style;
            // `border` on a table frames the table and every cell in it. The
            // attribute is inherited down to the cells here because that is
            // where the recorder can see it.
            if (tag_text == "table") {
                const std::string_view attribute =
                    txn.attribute_value(child, atoms_->intern("border"));
                if (!attribute.empty()) { b.border_px = std::max(0.0f, parse_number(attribute)); }
            } else if (b.is_cell() || b.is_row_group() || b.is_row()) {
                b.border_px = into.border_px > 0 && b.is_cell() ? 1.0f : 0.0f;
                if (b.is_row() || b.is_row_group()) { b.border_px = into.border_px; }
            }
            if (tag_text == "li") {
                // An <ol> numbers its items and a <ul> does not; the ordinal is
                // this item's position among its element siblings.
                const std::string_view parent_tag = atoms_->text(txn.tag(parent).value_or(atom{}));
                b.list_ordinal = parent_tag == "ol" ? ++ordinal : 0;
            }
            if (tag_text == "summary") {
                b.details_open = txn.has_attribute(parent, atoms_->intern("open"));
            }
            // What the style says of the box, the same for an element's box and
            // for a pseudo-element's (style_box, below).
            style_box(b, style, d, inherited_font, inherited_face, inherited_underline,
                      inherited_line_through);
            // Presentational attributes. `<table width=400>` and `<td width=50>`
            // are how a great deal of existing HTML sizes a table, and they mean
            // the CSS property, at the bottom of the cascade, so a sheet still
            // wins. A bare number is pixels; `width="50%"` is a percentage.
            if (b.width.is_auto() && (tag_text == "table" || tag_text == "td" || tag_text == "th" ||
                                      tag_text == "col")) {
                const std::string_view attribute =
                    txn.attribute_value(child, atoms_->intern("width"));
                if (!attribute.empty()) {
                    const length parsed = parse_length(attribute);
                    b.width = parsed.is_auto() && attribute.find('%') == std::string_view::npos
                                  ? length{parse_number(attribute), unit::px}
                                  : parsed;
                }
            }
            // The body's overflow propagates to the viewport when the root's is
            // `visible` (CSS Overflow 3 §3.3), and the body's own used value is
            // then `visible`: it is not a scroll container, clips nothing and
            // keeps its margins collapsing. Otherwise `body { overflow: hidden }`
            // would make the body, not the page, the box scrollIntoView
            // scrolls. The root box is the one with no tag (build()).
            if (tag_text == "body" && into.tag.empty() && into.source) {
                const auto visible = [&](atom name) {
                    const std::string_view v = trimmed(prop(into.style, name));
                    return v.empty() || ascii_iequals(v, "visible");
                };
                if (visible(overflow_x_) && visible(overflow_y_)) {
                    b.scroll_container = b.clips_overflow = b.blocks_margin_collapse = false;
                }
            }

            if (b.kind == box_kind::replaced) {
                intrinsic_size_of(txn, child, tag_text, b);
            } else {
                // `white-space: pre` inherits, which is what makes markup inside
                // a <pre> keep its layout.
                const std::string_view white_space = prop(style, white_space_);
                const bool preserve =
                    white_space.empty() ? preserve_whitespace : white_space.starts_with("pre");
                build_children(txn, child, b, b.font_size, b.face, b.underline, b.line_through,
                               preserve);
                generate_pseudo_elements(txn, child, b, preserve);
                drop_collapsible_spaces(b);
                normalise(b);
            }
            into.children.push_back(std::move(b));
        }
    }

    /// Reads everything the style says of a box into its fields: the sizes,
    /// edges, insets, overflow, containment, flex, font and decoration. `d` is
    /// the display it was built for and `b.kind` is already set; the element's
    /// own attributes and its parent's overflow are the caller's business.
    void style_box(box_node & b, const computed_style_ptr & style, display_kind d,
                   float inherited_font, const text_face & inherited_face, bool inherited_underline,
                   bool inherited_line_through) {
        b.width = parse_length(prop(style, width_));
        // A keyword height is kept as written: the block axis has no
        // intrinsic size to name, so it behaves as `auto` (CSS Sizing 3
        // §5.1) - has_definite_height says so - but a calc-size() over it
        // still runs its calculation over that automatic height.
        b.height = parse_length(prop(style, height_));
        {
            // ...and a calc-size() over `stretch` runs over that size.
            std::string_view basis = trimmed(prop(style, height_));
            while (basis.starts_with("calc-size(")) {
                basis = trim(basis.substr(10), " \t");
                basis = trim(basis.substr(0, basis.find_first_of(", \t")), " \t");
            }
            b.height_stretch =
                ascii_iequals_any(basis, {"stretch", "-webkit-fill-available", "-moz-available"});
        }
        b.border_box = trimmed(prop(style, box_sizing_)) == "border-box";
        {
            const std::string_view mode = trimmed(prop(style, writing_mode_));
            b.unmodelled_block = !mode.empty() && !ascii_iequals(mode, "horizontal-tb");
            b.unmodelled_inline =
                b.unmodelled_block || ascii_iequals(trimmed(prop(style, direction_)), "rtl");
        }
        b.collapse_borders = trimmed(prop(style, border_collapse_)) == "collapse";
        if (const std::string_view spacing = trimmed(prop(style, border_spacing_));
            !spacing.empty()) {
            // Only the first (horizontal) value of the two-value form is read,
            // because this engine models a single spacing for both axes.
            const length parsed = parse_length(spacing);
            if (!parsed.is_auto()) { b.border_spacing = parsed; }
        }
        b.position = parse_position(trimmed(prop(style, position_)));
        b.z_index = parse_z_index(prop(style, z_index_));
        const auto scrollable_overflow = [&](atom name) {
            const std::string_view value = trimmed(prop(style, name));
            return ascii_iequals(value, "hidden") || ascii_iequals(value, "scroll") ||
                   ascii_iequals(value, "auto") || ascii_iequals(value, "overlay");
        };
        b.blocks_margin_collapse =
            scrollable_overflow(overflow_x_) || scrollable_overflow(overflow_y_);
        b.scroll_container = b.blocks_margin_collapse;
        b.clips_overflow = b.scroll_container ||
                           ascii_iequals(trimmed(prop(style, overflow_x_)), "clip") ||
                           ascii_iequals(trimmed(prop(style, overflow_y_)), "clip");
        // Containment does not apply to an inline box or a table (§3.2):
        // only a box of its own - block-level, or an atomic inline - and
        // no table part is contained.
        if (b.kind != box_kind::inline_ && b.kind != box_kind::text && b.kind != box_kind::table &&
            !trimmed(prop(style, display_)).starts_with("table")) {
            for (const atom name : {contain_, container_type_}) {
                for (const std::string_view word : split_top_level(prop(style, name), " \t\n")) {
                    const bool both = ascii_iequals_any(word, {"size", "strict"});
                    b.contain_block_size = b.contain_block_size || both;
                    b.contain_inline_size =
                        b.contain_inline_size || both || ascii_iequals(word, "inline-size");
                }
            }
        }
        b.inset = sides_of(style, inset_sides_);
        // The logical insets, CSS Logical 1 §4.1: `inset-inline-start` is
        // `left` in a horizontal-tb, left-to-right document, which is the
        // only writing mode here. The cascade keeps the logical longhand as
        // its own declaration, so it is read as the physical one's fallback.
        if (b.inset.left.is_auto()) { b.inset.left = parse_length(prop(style, inset_inline_.top)); }
        if (b.inset.right.is_auto()) {
            b.inset.right = parse_length(prop(style, inset_inline_.right));
        }
        if (b.inset.top.is_auto()) {
            b.inset.top = parse_length(prop(style, inset_inline_.bottom));
        }
        if (b.inset.bottom.is_auto()) {
            b.inset.bottom = parse_length(prop(style, inset_inline_.left));
        }
        const std::string_view transform = trimmed(prop(style, transform_));
        b.transformed = !transform.empty() && !ascii_iequals(transform, "none");
        b.translate = parse_translate(transform);
        // Read the per-side longhands, which the cascade expands every
        // shorthand into, and not `margin`/`padding`.
        b.margin = sides_of(style, margin_sides_);
        b.padding = sides_of(style, padding_sides_);
        b.border = border_of(style);
        b.min_width = parse_length(prop(style, min_width_));
        b.max_width = parse_length(prop(style, max_width_));
        b.min_height = parse_length(prop(style, min_height_));
        b.max_height = parse_length(prop(style, max_height_));
        b.flex = flex_of(style);
        b.margin_left_auto = trimmed(prop(style, margin_sides_.left)) == "auto";
        b.margin_right_auto = trimmed(prop(style, margin_sides_.right)) == "auto";
        b.margin_top_auto = trimmed(prop(style, margin_sides_.top)) == "auto";
        b.margin_bottom_auto = trimmed(prop(style, margin_sides_.bottom)) == "auto";
        const length fs = parse_length(prop(style, font_size_));
        b.font_size = fs.is_auto() ? inherited_font : fs.resolve(inherited_font, inherited_font);
        b.line_height = resolve_line_height(prop(style, line_height_), b.font_size);
        b.face = face_of(style, inherited_face);
        b.text_align = parse_text_align(trimmed(prop(style, text_align_)));
        b.opacity = parse_opacity(prop(style, opacity_));
        b.list_marker =
            d == display_kind::list_item && trimmed(prop(style, list_style_type_)) != "none";
        b.underline = inherited_underline;
        b.line_through = inherited_line_through;
        if (const std::string_view decoration = prop(style, text_decoration_);
            !decoration.empty()) {
            // `none` clears what was inherited, which is how a link inside
            // underlined text turns its own underline off.
            b.underline = decoration.contains("underline");
            b.line_through = decoration.contains("line-through");
        }
    }

    /// Adds `::before` and `::after` boxes (CSS Pseudo 4 §4.1) as the first and
    /// last child of their element's box, when the cascade filed a style for
    /// one (engine::pseudo_key_of) and its `content` computes to something
    /// (`normal` and `none` make no box). The box has no source, like an
    /// anonymous one: it is not the element, so a query for the element's box
    /// must not find it, and it has no node of its own.
    void generate_pseudo_elements(const read_txn & txn, node_id element, box_node & into,
                                  bool preserve_whitespace) {
        for (const bool after : {false, true}) {
            const auto found = styles_->find(style::engine::pseudo_key_of(element, after));
            if (found == styles_->end() || !found->second) { continue; }
            const computed_style_ptr & style = found->second;
            const std::optional<std::string> text =
                generated_text(txn, element, prop(style, atoms_->intern("content")));
            if (!text) { continue; }
            // `inline` is a pseudo-element's initial display, blockified in a
            // flex container as any child is.
            display_kind d = parse_display(prop(style, display_), display_kind::inline_level);
            if (d == display_kind::none) { continue; }
            if (into.kind == box_kind::flex) { d = blockify(d); }
            box_node g;
            g.kind = (d == display_kind::flex || d == display_kind::inline_flex) ? box_kind::flex
                     : d == display_kind::inline_level                           ? box_kind::inline_
                                                                                 : box_kind::block;
            g.inline_level = d == display_kind::inline_flex || d == display_kind::inline_block;
            g.style = style;
            g.origin = element;
            style_box(g, style, d, into.font_size, into.face, into.underline, into.line_through);
            // An empty string is no text at all: `content: ""` is a box with
            // nothing in it, which is what a clearfix or a sized block wants,
            // and an empty text run would make a line of it.
            if (!text->empty()) {
                const std::string_view white_space = prop(style, white_space_);
                const bool preserve =
                    white_space.empty() ? preserve_whitespace : white_space.starts_with("pre");
                box_node t;
                t.kind = box_kind::text;
                t.style = style;
                t.origin = element;
                t.text = preserve ? *text : collapse_whitespace(*text);
                t.preformatted = preserve;
                t.font_size = g.font_size;
                t.line_height = g.line_height;
                t.face = g.face;
                t.underline = g.underline;
                t.line_through = g.line_through;
                g.children.push_back(std::move(t));
            }
            normalise(g);
            if (after) {
                into.children.push_back(std::move(g));
            } else {
                into.children.insert(into.children.begin(), std::move(g));
            }
        }
    }

    /// The text a `content` value generates (CSS Generated Content 3 §1.1): its
    /// strings and each `attr()`'s attribute of the element, in order, up to
    /// the `/` that begins the alternative text. nullopt for `normal` and
    /// `none`, which generate no box.
    // ponytail: counter(), counters(), the quotes and url() generate nothing -
    // counters need the whole tree's counter scopes; add them when a page
    // reads a list's numbers off its ::before.
    [[nodiscard]] std::optional<std::string> generated_text(const read_txn & txn, node_id element,
                                                            std::string_view content) const {
        content = trimmed(content);
        if (content.empty() || ascii_iequals(content, "none") || ascii_iequals(content, "normal")) {
            return std::nullopt;
        }
        namespace css = style::css;
        const css::token_stream s = css::tokenize(content);
        std::string out;
        for (std::size_t i = 0; i < s.tokens.size(); ++i) {
            const css::css_token & t = s.tokens[i];
            if (t.type == css::token_type::delim && s.text_of(t) == "/") { break; }
            if (t.type == css::token_type::string) {
                out += s.value_of(t);
                continue;
            }
            if (t.type != css::token_type::function) { continue; }
            const bool attr = ascii_iequals(s.text_of(t), "attr(");
            bool named = false;
            std::size_t depth = 1;
            while (depth > 0 && ++i < s.tokens.size()) {
                const css::css_token & in = s.tokens[i];
                if (in.type == css::token_type::function ||
                    in.type == css::token_type::open_paren) {
                    ++depth;
                } else if (in.type == css::token_type::close_paren) {
                    --depth;
                } else if (attr && !named && depth == 1 && in.type == css::token_type::ident) {
                    out += txn.attribute_value(element, atoms_->intern(s.text_of(in)));
                    named = true;
                }
            }
        }
        return out;
    }

    /// Sets a replaced element's own size. A <canvas> takes its width/height
    /// attributes (not CSS, which scales the bitmap, per spec); form controls
    /// get the sizes Firefox uses, so an unstyled form looks like a form rather
    /// than like a row of zero-height boxes.
    void intrinsic_size_of(const read_txn & txn, node_id id, std::string_view tag,
                           box_node & into) const {
        const auto attribute_number = [&](std::string_view name, float fallback) {
            const std::string_view text = txn.attribute_value(id, atoms_->intern(name));
            if (text.empty()) { return fallback; }
            const length parsed = parse_length(text);
            return parsed.is_auto() ? fallback : parsed.resolve(0, into.font_size);
        };
        if (tag == "canvas") {
            into.intrinsic_width = attribute_number("width", 300); // the HTML defaults
            into.intrinsic_height = attribute_number("height", 150);
            return;
        }
        // <iframe>, <embed>, <object> and <video> default to 300x150 (CSS 2
        // §10.3.2), unless their `width`/`height` attributes say otherwise
        // (HTML 15.4.1). For an iframe this box is also the nested document's
        // viewport, so its media queries read this size.
        if (tag == "iframe" || tag == "embed" || tag == "object" || tag == "video") {
            into.intrinsic_width = attribute_number("width", 300);
            into.intrinsic_height = attribute_number("height", 150);
            return;
        }
        if (tag == "img" || tag == "svg") {
            // The attributes win over the natural size (that is how a page
            // scales a graphic), and one of them alone scales the other through
            // the aspect ratio, which is why this asks whether each was
            // specified rather than defaulting each to the natural size.
            //
            // Shared by <img> and <svg>; only the source of the natural size
            // differs, and both arrive through the same injected callback. For
            // an SVG that is a scan of the markup rather than a decoded bitmap,
            // so this holds in a build with no SVG rasteriser.
            const intrinsic_size natural = intrinsic_image ? intrinsic_image(id) : intrinsic_size{};
            const bool has_width = !txn.attribute_value(id, atoms_->intern("width")).empty();
            const bool has_height = !txn.attribute_value(id, atoms_->intern("height")).empty();
            const float width = attribute_number("width", natural.width);
            const float height = attribute_number("height", natural.height);
            into.intrinsic_width = width;
            into.intrinsic_height = height;
            if (natural.width > 0 && natural.height > 0) {
                if (has_width && !has_height) {
                    into.intrinsic_height = width * natural.height / natural.width;
                } else if (has_height && !has_width) {
                    into.intrinsic_width = height * natural.width / natural.height;
                }
            }
            // CSS's default size for a replaced element with no intrinsic size
            // of its own. An <img> uses 0 (a missing image takes no room, which
            // is what a browser does with a broken src); an <svg> that says
            // nothing about its size is still a graphic and gets the standard
            // box, the same as a <canvas> does.
            if (tag == "svg" && into.intrinsic_width <= 0 && into.intrinsic_height <= 0) {
                into.intrinsic_width = 300;
                into.intrinsic_height = 150;
            }
            return;
        }
        // A form control's intrinsic size is its content box only; layout_box
        // adds padding and border outside it. The height is the computed line
        // height, not a fixed multiple of the font size, because controls obey
        // `line-height`.
        if (tag == "textarea") {
            const float columns = attribute_number("cols", 20);
            const float rows = attribute_number("rows", 2);
            into.intrinsic_width = columns * text_width("0", into.font_size);
            into.intrinsic_height = rows * into.line_height;
            return;
        }
        if (tag == "input") {
            const std::string_view type = txn.attribute_value(id, atoms_->intern("type"));
            if (type == "checkbox" || type == "radio") {
                into.intrinsic_width = 13; // Firefox's widget size
                into.intrinsic_height = 13;
                return;
            }
            const float size = attribute_number("size", 20);
            into.intrinsic_width = size * text_width("0", into.font_size);
            into.intrinsic_height = into.line_height;
            return;
        }
        if (tag == "select") {
            // As wide as its widest option, as a real select is.
            float widest = 0;
            const atom option = atoms_->intern_lower("option");
            const auto walk = [&](auto && self, node_id at) -> void {
                if (txn.tag(at).value_or(atom{}) == option) {
                    std::string label;
                    const auto text = [&](auto && me, node_id node) -> void {
                        label += txn.text(node);
                        for (const node_id child : txn.children(node)) { me(me, child); }
                    };
                    text(text, at);
                    widest = std::max(widest, text_width(label, into.font_size));
                }
                for (const node_id child : txn.children(at)) { self(self, child); }
            };
            walk(walk, id);
            // The 20 is the drop-down arrow's gutter, which the painter draws in;
            // it keeps the label from running under the arrow. It is not
            // padding: a sheet's `padding-right` is added outside it.
            into.intrinsic_width = widest + 20;
            into.intrinsic_height = into.line_height;
            return;
        }
        if (tag == "button") {
            // A button is as wide as its label. It is the one replaced element
            // whose intrinsic size comes from its content, so it is measured
            // here rather than being a constant.
            std::string label;
            const auto walk = [&](auto && self, node_id at) -> void {
                label += txn.text(at);
                for (const node_id child : txn.children(at)) { self(self, child); }
            };
            walk(walk, id);
            // 16px of horizontal and 6px of vertical room around the label for
            // the UA button chrome; 1.25 is the `line-height: normal` factor.
            // Unreachable while <button> is not in is_replaced_tag
            // (layout/values.hpp): only replaced boxes come here.
            into.intrinsic_width = text_width(label, into.font_size) + 16;
            into.intrinsic_height = into.font_size * 1.25f + 6;
            return;
        }
    }

    /// Removes collapsible spaces that render as nothing. A collapsible space is
    /// rendered only between inline-level content; at the start or end of its
    /// parent, or next to a block-level sibling, it is dropped, which stops
    /// `<div>\n<p>x</p>\n</div>` from drawing two spaces around the paragraph.
    static void drop_collapsible_spaces(box_node & parent);

    /// Anonymous box generation: when a block container has both block-level
    /// and inline-level children, each run of inline children is wrapped in an
    /// anonymous block, so that no formatting context has mixed children.
    static void normalise(box_node & parent);

    /// One anonymous box around a run of inline-level children. Shared by the
    /// block rule and the flex one, which differ in when they wrap rather than
    /// in what they produce.
    [[nodiscard]] static box_node wrap_run(const box_node & parent, std::vector<box_node> & run);

    /// Reads the flex longhands, never the shorthands. `flex: 1 0 0` and
    /// `gap: 1rem` are expanded by the cascade (style/engine.hpp) at the
    /// shorthand's own position in the sorted fold, which is what lets a
    /// `.flex-grow-0` utility written after `.col` win; reading the shorthand
    /// here would break that source order.
    ///
    /// A negative grow or shrink factor is invalid and takes the initial value,
    /// per Flexbox 1 §7.2, rather than being clamped to zero, which would make
    /// `flex-shrink: -1` mean "never shrink" instead of "ignored".
    [[nodiscard]] flex_spec flex_of(const computed_style_ptr & style) const {
        flex_spec out;
        out.direction = parse_flex_direction(trimmed(prop(style, flex_.direction)));
        out.wrap = parse_flex_wrap(trimmed(prop(style, flex_.wrap)));
        out.justify = parse_flex_align(trimmed(prop(style, flex_.justify)), flex_align::normal);
        out.items = parse_flex_align(trimmed(prop(style, flex_.items)), flex_align::normal);
        out.line_align = parse_flex_align(trimmed(prop(style, flex_.content)), flex_align::normal);
        out.self = parse_flex_align(trimmed(prop(style, flex_.self)), flex_align::auto_);
        out.row_gap = parse_gap(trimmed(prop(style, flex_.row_gap)));
        out.column_gap = parse_gap(trimmed(prop(style, flex_.column_gap)));
        // `>= 0` also sends NaN to the initial value, which the freeze loop's
        // arithmetic depends on.
        const float grow = parse_flex_number(prop(style, flex_.grow), 0);
        const float shrink = parse_flex_number(prop(style, flex_.shrink), 1);
        out.grow = grow >= 0 ? grow : 0.0f;
        out.shrink = shrink >= 0 ? shrink : 1.0f;
        const std::string_view basis = trimmed(prop(style, flex_.basis));
        // `content` is the item's max-content size (Flexbox §7.2.3), unlike
        // `auto`, which defers to the item's `width` or `height` first.
        out.basis = basis == "content" ? length{0, unit::max_content} : parse_length(basis);
        out.order = static_cast<int>(parse_flex_number(prop(style, flex_.order), 0));
        return out;
    }

    /// The used border width on each side.
    ///
    /// Each side reads its own longhands (`border-bottom-width`,
    /// `border-bottom-style`) and falls back to the uniform properties, so
    /// `border-bottom: 1px solid` insets only the bottom. `border-style` decides
    /// whether there is a border: a `border-width` with no style is a used width
    /// of zero (CSS 2.1 §8.5.3), which is what makes `border: 0` and a bare
    /// `border-width: 2px` behave differently.
    [[nodiscard]] side_lengths border_of(const computed_style_ptr & style) const {
        const auto one = [&](atom width_name, atom style_name) {
            std::string_view drawn = trimmed(prop(style, style_name));
            if (drawn.empty()) { drawn = trimmed(prop(style, border_style_)); }
            if (drawn.empty() || drawn == "none" || drawn == "hidden") { return length{}; }
            std::string_view text = trimmed(prop(style, width_name));
            if (text.empty()) { text = trimmed(prop(style, border_width_)); }
            // `medium` is the initial value and the one keyword the shorthand
            // expansion can produce; CSS puts it at 3px and so does every browser.
            if (text == "medium") { return length{3, unit::px}; }
            const length w = parse_length(text);
            return w.is_auto() ? length{} : w;
        };
        return side_lengths{one(border_sides_.top, border_style_sides_.top),
                            one(border_sides_.right, border_style_sides_.right),
                            one(border_sides_.bottom, border_style_sides_.bottom),
                            one(border_sides_.left, border_style_sides_.left)};
    }

    /// The four longhands of one box-edge property.
    struct side_atoms {
        atom top, right, bottom, left;
    };
    /// The twelve flex longhands.
    struct flex_atoms {
        atom direction, wrap, justify, items, content, self;
        atom grow, shrink, basis, order, row_gap, column_gap;
    };
    /// Parses the four longhands `names` of one box-edge property.
    [[nodiscard]] side_lengths sides_of(const computed_style_ptr & style,
                                        const side_atoms & names) const {
        side_lengths out;
        out.top = parse_length(prop(style, names.top));
        out.right = parse_length(prop(style, names.right));
        out.bottom = parse_length(prop(style, names.bottom));
        out.left = parse_length(prop(style, names.left));
        return out;
    }

    [[nodiscard]] static std::string_view trimmed(std::string_view v) noexcept {
        return trim(v, html_whitespace);
    }

    /// The advance width of `text` at `font_size`, from the injected measure if
    /// there is one, or else from a 0.6em-per-character monospace estimate.
    [[nodiscard]] float text_width(std::string_view text, float font_size,
                                   const text_face & face = {}) const {
        if (measure_) { return measure_(text, font_size, face); }
        return static_cast<float>(text.size()) * font_size * 0.6f;
    }

    /// Folds every run of HTML whitespace (space, tab, LF, CR, FF) into one
    /// space.
    [[nodiscard]] static std::string collapse_whitespace(std::string_view text);

    /// The leading number of a presentational attribute such as `width="400"`,
    /// or 0 when it does not start with one.
    [[nodiscard]] static float parse_number(std::string_view text);

    /// The first name in a font-family list. Choosing among the alternatives
    /// happens here because by the time a tile is rastered there is no cascade
    /// to ask; a real fallback chain needs to know which faces are loaded,
    /// which is the font backend's business.
    [[nodiscard]] static std::string first_family(std::string_view list);

    /// The element's font face. It is inherited, as the cascade says: an
    /// element with no font-family of its own is drawn in its parent's, so this
    /// starts from `inherited` and overrides only what this element states.
    [[nodiscard]] text_face face_of(const computed_style_ptr & style,
                                    const text_face & inherited) const {
        text_face out = inherited;
        if (const std::string_view family = prop(style, font_family_); !family.empty()) {
            out.family = first_family(family);
        }
        if (const std::string_view weight = prop(style, font_weight_); !weight.empty()) {
            // 600 and up is bold, per the CSS mapping; `bolder`/`lighter` are
            // relative and are treated as their common case.
            int numeric = 0;
            const auto parsed =
                std::from_chars(weight.data(), weight.data() + weight.size(), numeric);
            if (parsed.ec == std::errc{}) {
                out.bold = numeric >= 600;
            } else {
                out.bold = weight == "bold" || weight == "bolder";
            }
        }
        if (const std::string_view style_text = prop(style, font_style_); !style_text.empty()) {
            out.italic = style_text == "italic" || style_text == "oblique";
        }
        return out;
    }

    atom_table * atoms_;
    const style::style_map * styles_;
    measure_text_fn measure_;
    atom display_, width_, height_, font_size_;
    side_atoms margin_sides_, padding_sides_;
    atom font_family_, font_weight_, font_style_, text_decoration_, white_space_;
    atom line_height_;
    atom min_width_, max_width_, min_height_, max_height_;
    atom border_width_, border_style_;
    side_atoms border_sides_, border_style_sides_;
    atom position_, z_index_, transform_, text_align_, opacity_, overflow_x_, overflow_y_;
    atom contain_, container_type_;
    atom list_style_type_;
    atom border_collapse_, border_spacing_;
    side_atoms inset_sides_;
    // inset-inline-start, inset-inline-end, inset-block-start, inset-block-end -
    // in the side_atoms slots top, right, bottom, left respectively.
    side_atoms inset_inline_;
    atom box_sizing_;
    atom direction_, writing_mode_;
    flex_atoms flex_;
};

} // namespace ctbrowser::layout
