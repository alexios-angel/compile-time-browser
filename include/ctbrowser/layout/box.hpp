#pragma once
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include <ctbrowser/core/core.hpp>
#include <ctbrowser/dom/dom.hpp>
#include <ctbrowser/style/style.hpp>

#include <ctbrowser/layout/values.hpp>

// The box tree, which is NOT the DOM tree.
//
// This separation is the whole point of the stage. In the previous engine, layout wrote its
// results back onto the DOM node - n.x, n.y, n.w, n.h, ui_lines, ui_font_px -
// and that one decision caused three problems at once: layout could not run
// concurrently (it mutated shared state), the node struct carried thirty-odd
// fields that only layout cared about, and there was nowhere to put a box that
// has no element behind it.
//
// That last one matters more than it sounds. CSS requires boxes the document
// does not contain. A block container holding a mix of inline and block
// children has to wrap the inline runs in ANONYMOUS block boxes, or the
// formatting contexts interleave illegally. There is no DOM node to hang those
// on, which is why every real engine has a box tree and the previous engine could not do it.

namespace ctbrowser::layout {

using ctbrowser::node_id;
using ctbrowser::style::computed_style_ptr;

enum class box_kind : std::uint8_t {
    block,     // a block-level box
    inline_,   // an inline-level box
    text,      // a run of text
    anonymous, // a generated block wrapping inline children; no source element
    table,     // a <table>: its rows share COLUMN widths, which is the whole
               // reason it cannot be laid out as blocks - a block sizes itself
               // from its own content, and a cell has to size itself from a
               // column its siblings in other rows also occupy
    replaced,  // a box whose size comes from the ELEMENT, not its children:
               // <canvas>, <input>, <select>, <textarea>, <img>. CSS calls
               // these replaced elements, and the distinction is load bearing -
               // laying one out from its children gives a zero-size canvas.
};

struct box_node {
    box_kind kind = box_kind::block;
    node_id source; // empty for anonymous boxes
    // The element's tag, for the handful of decisions that are about WHAT an
    // element is rather than about its style - table parts, list items. A view
    // into the atom table, which outlives every box tree built from it.
    std::string_view tag;
    computed_style_ptr style; // shared; anonymous boxes inherit their parent's
    std::string text;         // text boxes only
    std::vector<box_node> children;

    // Resolved once at construction so the algorithms do not re-parse strings
    // for every layout pass. The style is interned and shared, so this is also
    // shared work in practice.
    length width{}, height{};
    // `min-width` / `max-width`. Ignored entirely before, which is why
    // `.container` filled its parent instead of stopping at the breakpoint's
    // max-width - and every child of it was then measured against the wrong
    // basis, so one missing property moved 27 elements on one fixture.
    length min_width{}, max_width{};
    // Was the horizontal margin written as the WORD `auto`?
    //
    // It cannot be read off `margin.left.is_auto()`, and that is a trap rather than
    // an inconvenience: `length{}` defaults to `unit::auto_`, so an UNSET margin and
    // an explicit `auto` are the same value. resolve() answers 0 for both, which is
    // right for an unset one and is why nothing noticed - until auto-margin centring
    // arrived and started centring every box with a definite width and no margins
    // at all. CSS says an unspecified margin is 0; only the keyword means auto.
    bool margin_left_auto = false;
    bool margin_right_auto = false;
    side_lengths margin{}, padding{};
    float font_size = 16;
    // THE LINE BOX'S HEIGHT, absolute, resolved once here for the same reason
    // font_size is: layout would otherwise re-parse a string per line.
    //
    // It was `font_size * 1.25` everywhere, hardcoded, so `line-height` did
    // nothing at all - and it is Bootstrap's most globally consequential
    // declaration, since `body { line-height: 1.5 }` moves the baseline of every
    // text box on the page.
    float line_height = 16 * 1.25f;
    // Resolved here for the same reason the lengths are: once per element,
    // rather than per glyph in the rasterizer, which has no cascade to ask.
    text_face face{};
    bool underline = false;
    bool line_through = false;
    // Generated content the recorder draws: a list item's number (0 = a disc,
    // i.e. an unordered list) and whether a <details> is open. Decided here
    // because it is a question about the DOCUMENT - which sibling this is, what
    // the parent element says - and the recorder has neither.
    int list_ordinal = 0;
    bool details_open = false;
    // A text box that is ONE SPACE from collapsed inter-element whitespace.
    // Rendered between inline content and dropped everywhere else; see
    // drop_collapsible_spaces.
    bool collapsible_space = false;
    // `<table border=N>`. A presentational attribute rather than CSS, and it
    // frames the cells as well as the table, so it rides on the box.
    float border_px = 0;
    // `white-space: pre`. The text keeps its newlines, and layout has to turn
    // them into line breaks rather than letting them reach the rasterizer.
    bool preformatted = false;

    // What a replaced element is sized as when the sheet says nothing. Zero
    // means it has none and falls back to the block rules.
    float intrinsic_width = 0;
    float intrinsic_height = 0;

    // The table parts. Kept as predicates on the box rather than as more
    // box_kinds: a row and a cell lay out as ordinary blocks, and only the
    // TABLE needs its own formatting context.
    [[nodiscard]] bool is_row() const noexcept { return tag == "tr"; }
    [[nodiscard]] bool is_row_group() const noexcept {
        return tag == "thead" || tag == "tbody" || tag == "tfoot";
    }
    [[nodiscard]] bool is_cell() const noexcept { return tag == "td" || tag == "th"; }

    [[nodiscard]] bool is_block_level() const noexcept {
        // A TABLE is block-level. Left out of this, a table shared a line with
        // whatever came before it - two tables sat side by side, and a table sat
        // beside the paragraph above it.
        return kind == box_kind::block || kind == box_kind::anonymous || kind == box_kind::table;
    }
    [[nodiscard]] bool is_replaced() const noexcept { return kind == box_kind::replaced; }
    [[nodiscard]] bool establishes_inline_context() const noexcept {
        // A block whose children are all inline runs an inline formatting
        // context; a block with block children runs a block one. Mixed content
        // never reaches here - the builder wraps it.
        if (kind == box_kind::replaced) { return false; } // its children are not laid out
        if (children.empty()) { return false; }
        for (const box_node & c : children) {
            if (c.is_block_level()) { return false; }
        }
        return true;
    }
    [[nodiscard]] std::size_t descendant_count() const noexcept {
        std::size_t n = 1;
        for (const box_node & c : children) { n += c.descendant_count(); }
        return n;
    }
};

// Builds the box tree from a document read view plus resolved styles.
class box_builder {
public:
    // The text measure is the SAME one layout and raster use. Guessing a
    // per-character width here instead is what made a <button> narrower than
    // its own label - the box was sized with one metric and drawn with another.
    // An <img>'s size comes from its DECODED BITMAP, which layout cannot get to
    // - decoding lives in the shell, above this. The browser fills this in;
    // unset, an <img> with no width/height attribute is zero-sized, which is
    // what it was before images existed.
    struct intrinsic_size {
        float width = 0;
        float height = 0;
    };
    std::function<intrinsic_size(node_id)> intrinsic_image;

    box_builder(atom_table & atoms, const style::style_map & styles, measure_text_fn measure = {})
        : atoms_(&atoms), styles_(&styles), measure_(std::move(measure)),
          display_(atoms.intern("display")), width_(atoms.intern("width")),
          height_(atoms.intern("height")), margin_(atoms.intern("margin")),
          padding_(atoms.intern("padding")), font_size_(atoms.intern("font-size")),
          margin_sides_{atoms.intern("margin-top"), atoms.intern("margin-right"),
                        atoms.intern("margin-bottom"), atoms.intern("margin-left")},
          padding_sides_{atoms.intern("padding-top"), atoms.intern("padding-right"),
                         atoms.intern("padding-bottom"), atoms.intern("padding-left")},
          font_family_(atoms.intern("font-family")), font_weight_(atoms.intern("font-weight")),
          font_style_(atoms.intern("font-style")),
          text_decoration_(atoms.intern("text-decoration")),
          white_space_(atoms.intern("white-space")), line_height_(atoms.intern("line-height")),
          min_width_(atoms.intern("min-width")), max_width_(atoms.intern("max-width")) {}

    // `line-height`, to an absolute px.
    //
    //   normal          the fallback factor. NOT the font's own metrics: those
    //                   come from the measure function, which is injected and
    //                   which a golden pins to font8x8 - so deriving `normal`
    //                   from them would make every existing render move for a
    //                   reason that has nothing to do with the sheet.
    //   <number>        a factor on THIS element's font size, which is why a
    //                   unitless value is the one that inherits usefully
    //   <length>        itself
    //   <percentage>    a factor, resolved against this element's font size
    //
    // NOT MODELLED: a PERCENTAGE computes to a length in CSS, so a child should
    // inherit the resolved px rather than the percentage. Inheriting the text
    // re-resolves it against the child's own size. The same is true of `em`, and
    // both go away when the cascade folds units.
    [[nodiscard]] static float resolve_line_height(std::string_view text, float font_size) {
        constexpr float normal_factor = 1.25f;
        const std::string_view value = trimmed(text);
        if (value.empty() || value == "normal") { return font_size * normal_factor; }
        const length parsed = parse_length(value);
        // A bare number is not a length: parse_length calls a unitless value
        // `unit::none` and treats it as px, which is right for `width: 5` and
        // wrong here - `line-height: 1.5` is one and a half times the font size,
        // not one and a half pixels.
        if (parsed.u == unit::none) { return parsed.value * font_size; }
        if (parsed.u == unit::percent) { return parsed.value / 100.0f * font_size; }
        if (parsed.is_auto()) { return font_size * normal_factor; }
        return parsed.resolve(font_size, font_size);
    }

    [[nodiscard]] box_node build(const read_txn & txn, node_id root);

private:
    [[nodiscard]] computed_style_ptr style_of(node_id id) const;
    [[nodiscard]] std::string_view prop(const computed_style_ptr & s, atom name) const;

    void build_children(const read_txn & txn, node_id parent, box_node & into, float inherited_font,
                        const text_face & inherited_face = {}, bool inherited_underline = false,
                        bool inherited_line_through = false, bool preserve_whitespace = false) {
        int ordinal = 0;
        // A <details> without `open` shows only its <summary>. Asked once, here,
        // because the state is on the PARENT and the children are what it hides.
        const bool closed_details = atoms_->text(txn.tag(parent).value_or(atom{})) == "details" &&
                                    !txn.has_attribute(parent, atoms_->intern("open"));
        for (const node_id child : txn.children(parent)) {
            const node_kind kind = txn.kind(child).value_or(node_kind::comment);
            if (kind == node_kind::text) {
                const std::string_view text = txn.text(child);
                // A WHITESPACE-ONLY text node is not always nothing. Between two
                // inline-level boxes it collapses to one SPACE and is rendered -
                // `</label> <input>` is a label, a space and a field. Dropping it
                // outright glued every label to its control and ran the words of
                // `<b>a</b> <b>b</b>` together. It IS nothing between blocks, and
                // at the start or end of a line; drop_collapsible_spaces below
                // decides that once the siblings are known.
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
                // COLLAPSED, unless the element preserves whitespace. HTML folds
                // every run of space/tab/newline into ONE SPACE, and the engine never
                // did: the newlines in a page's own source went straight to the
                // rasterizer. font8x8 drew nothing for them so nobody noticed;
                // a real font draws .notdef, which is a BOX, and a line full of
                // boxes is what finally showed it up. It also broke wrapping -
                // `words_that_fit` splits on ' ' alone, so two words joined by a
                // newline were one unbreakable word and the line overflowed.
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
            const display_kind d =
                parse_display(prop(style, display_), default_display_for(atoms_->text(tag)));
            if (d == display_kind::none) { continue; } // pruned: no box at all
            // A CLOSED <details> shows its <summary> AND NOTHING ELSE. This
            // cannot be a UA rule: `details > :not(summary) { display: none }`
            // needs a selector the cascade does not have, and the state lives on
            // the parent rather than on the hidden child.
            if (closed_details && atoms_->text(tag) != "summary") { continue; }

            box_node b;
            const std::string_view tag_text = atoms_->text(tag);
            b.kind = is_replaced_tag(tag_text) ? box_kind::replaced
                     : tag_text == "table"
                         ? box_kind::table
                         : (d == display_kind::inline_level ? box_kind::inline_ : box_kind::block);
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
            b.width = parse_length(prop(style, width_));
            b.height = parse_length(prop(style, height_));
            // PRESENTATIONAL ATTRIBUTES. `<table width=400>` and `<td width=50>`
            // are how a great deal of existing HTML sizes a table, and they mean
            // the CSS property - at the bottom of the cascade, so a sheet still
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
            // The LONGHANDS, which the style engine expands every shorthand
            // into. Reading `margin`/`padding` directly meant a sheet that said
            // `padding-left` was ignored outright - including the UA sheet's own
            // `ul { padding-left: 40px }` and `summary { padding-left: 18px }`.
            b.margin = sides_of(style, margin_sides_);
            b.padding = sides_of(style, padding_sides_);
            b.min_width = parse_length(prop(style, min_width_));
            b.max_width = parse_length(prop(style, max_width_));
            b.margin_left_auto = trimmed(prop(style, margin_sides_.left)) == "auto";
            b.margin_right_auto = trimmed(prop(style, margin_sides_.right)) == "auto";
            const length fs = parse_length(prop(style, font_size_));
            b.font_size =
                fs.is_auto() ? inherited_font : fs.resolve(inherited_font, inherited_font);
            b.line_height = resolve_line_height(prop(style, line_height_), b.font_size);
            b.face = face_of(style, inherited_face);
            b.underline = inherited_underline;
            b.line_through = inherited_line_through;
            if (const std::string_view decoration = prop(style, text_decoration_);
                !decoration.empty()) {
                // `none` CLEARS what was inherited, which is how a link inside
                // underlined text turns its own underline off.
                b.underline = decoration.find("underline") != std::string_view::npos;
                b.line_through = decoration.find("line-through") != std::string_view::npos;
            }

            if (b.kind == box_kind::replaced) {
                intrinsic_size_of(txn, child, tag_text, b);
            } else {
                // `white-space: pre` INHERITS, which is what makes markup inside
                // a <pre> keep its layout.
                const std::string_view white_space = prop(style, white_space_);
                const bool preserve =
                    white_space.empty() ? preserve_whitespace : white_space.starts_with("pre");
                build_children(txn, child, b, b.font_size, b.face, b.underline, b.line_through,
                               preserve);
                drop_collapsible_spaces(b);
                normalise(b);
            }
            into.children.push_back(std::move(b));
        }
    }

    // Where a replaced element's own size comes from. A <canvas> takes its
    // width/height ATTRIBUTES (not CSS - those scale the bitmap, per spec);
    // form controls get the sizes Firefox uses, so an unstyled form looks like
    // a form rather than like a row of zero-height boxes.
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
        if (tag == "img" || tag == "svg") {
            // The attributes WIN over the natural size - that is how a page
            // scales a graphic - and ONE of them scales the other through the
            // aspect ratio, which is why this asks whether each was specified
            // rather than defaulting each to the natural size independently.
            //
            // Shared by <img> and <svg> because the rule is the same rule; what
            // differs is only where the natural size comes from, and both
            // arrive through the same injected callback. For an SVG that is a
            // scan of the markup rather than a decoded bitmap, so this stays
            // true of a build with no SVG rasteriser at all.
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
        // The room a control keeps around its text is `control_text_inset` per
        // side, which is what the painter actually insets by - see the note on
        // the constant. Reserving less than that is why a field's text used to
        // start inside its own padding.
        const float chrome_x = 2 * control_text_inset;
        const float chrome_y = 2 * control_border_inset;
        if (tag == "textarea") {
            const float columns = attribute_number("cols", 20);
            const float rows = attribute_number("rows", 2);
            into.intrinsic_width = columns * text_width("0", into.font_size) + chrome_x;
            into.intrinsic_height = rows * into.font_size * 1.25f + chrome_y;
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
            into.intrinsic_width = size * text_width("0", into.font_size) + chrome_x;
            into.intrinsic_height = into.font_size * 1.25f + chrome_y;
            return;
        }
        if (tag == "select") {
            // AS WIDE AS ITS WIDEST OPTION, not a fixed twelve characters. The
            // constant made every select the same size whatever it held - a
            // three-letter colour picker as wide as a country list - where a
            // real one shrinks to what it shows.
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
            // The gutter on the right is the drop-down arrow's; the painter
            // draws it 12 in from the edge, so this is what keeps the label
            // from running underneath it.
            into.intrinsic_width = widest + chrome_x + 20;
            into.intrinsic_height = into.font_size * 1.25f + chrome_y;
            return;
        }
        if (tag == "button") {
            // A button is as wide as its LABEL. It is the one replaced element
            // whose intrinsic size comes from its content, which is why it is
            // measured here rather than being a constant - a button that fills
            // the line is not a button.
            std::string label;
            const auto walk = [&](auto && self, node_id at) -> void {
                label += txn.text(at);
                for (const node_id child : txn.children(at)) { self(self, child); }
            };
            walk(walk, id);
            into.intrinsic_width = text_width(label, into.font_size) + 16;
            into.intrinsic_height = into.font_size * 1.25f + 6;
            return;
        }
    }

    // ANONYMOUS BOX GENERATION. If a block container has both block-level and
    // inline-level children, the inline runs get wrapped in anonymous blocks.
    // Without this the two formatting contexts interleave and neither
    // algorithm has a well-defined input.
    // A collapsible space is rendered only BETWEEN inline-level content. At the
    // start or end of its parent, or next to a block-level sibling, it is
    // nothing at all - which is what stops `<div>\n<p>x</p>\n</div>` from
    // drawing two spaces around the paragraph.
    static void drop_collapsible_spaces(box_node & parent);

    static void normalise(box_node & parent);

    // The four longhands of one box-edge property.
    struct side_atoms {
        atom top, right, bottom, left;
    };
    [[nodiscard]] side_lengths sides_of(const computed_style_ptr & style,
                                        const side_atoms & names) const {
        side_lengths out;
        out.top = parse_length(prop(style, names.top));
        out.right = parse_length(prop(style, names.right));
        out.bottom = parse_length(prop(style, names.bottom));
        out.left = parse_length(prop(style, names.left));
        return out;
    }

    [[nodiscard]] static side_lengths parse_sides(std::string_view shorthand);

    [[nodiscard]] static std::string_view trimmed(std::string_view v);

    // A character's width at a font size, through the injected measure when
    // there is one and a monospace stand-in when there is not.
    [[nodiscard]] float text_width(std::string_view text, float font_size,
                                   const text_face & face = {}) const {
        if (measure_) { return measure_(text, font_size, face); }
        return static_cast<float>(text.size()) * font_size * 0.6f;
    }

    // font-family is a LIST of alternatives, and choosing among them is layout's
    // job - by the time a tile is rastered there is no cascade left to ask. The
    // first name wins here; a real fallback chain needs to know which faces are
    // loaded, which is the backend's business, so the backend gets the whole
    // list only if the first name misses.
    // A bare number, for a presentational attribute. Not parse_length: that
    // wants a unit, and `width="400"` has none.
    // HTML whitespace: every run of space, tab, newline, carriage return or
    // form feed is ONE SPACE.
    [[nodiscard]] static std::string collapse_whitespace(std::string_view text);

    [[nodiscard]] static float parse_number(std::string_view text);

    [[nodiscard]] static std::string first_family(std::string_view list);

    // INHERITED, like the cascade says: an element with no font-family of its
    // own is drawn in its parent's. The resolver takes what it inherited and
    // overrides only what this element states, which is the same shape
    // font-size already used.
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
    atom display_, width_, height_, margin_, padding_, font_size_;
    side_atoms margin_sides_, padding_sides_;
    atom font_family_, font_weight_, font_style_, text_decoration_, white_space_;
    atom line_height_;
    atom min_width_, max_width_;
};

} // namespace ctbrowser::layout
