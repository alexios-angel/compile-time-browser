// ctbrowser.paint: colours, recording, and what a display list is FOR.
//
// The interesting property is not that a fragment tree can be turned into
// drawing commands - it is that the result outlives the frame. the previous engine returned a
// paint_cmd vector that the shell drew and discarded, so a scroll or a caret
// blink re-ran layout. The tests here pin the properties that make a recorded
// list reusable: it is complete, it is ordered back-to-front, it is
// deterministic, and it can be queried per region so a tile pays only for what
// touches it.

#include <ctbrowser/core/core.hpp>
#include <ctbrowser/dom/dom.hpp>
#include <ctbrowser/layout/layout.hpp>
#include <ctbrowser/paint/paint.hpp>
#include <ctbrowser/style/style.hpp>

#include "check.hpp"
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

using namespace ctbrowser;
using namespace ctbrowser::paint;

namespace {

void check(bool ok, std::string_view what) {
    if (!ok) {
        std::printf("FAIL %s\n", std::string{what}.c_str());
        ++ctbrowser_test_failures;
    }
}

// The whole front of the pipeline, since a display list is only meaningful at
// the end of it.
struct fixture {
    atom_table atoms;
    document doc{atoms};
    style::engine styles{atoms};
    style::style_map resolved;
    layout::box_node tree;
    layout::fragment placed;

    void load(std::string_view html, std::string_view css, float viewport = 200) {
        (void)parse_html(doc, html);
        styles.add_sheet(css, 1);
        const auto txn = doc.read();
        resolved = styles.resolve_all(txn);
        layout::box_builder builder{atoms, resolved};
        tree = builder.build(txn, txn.root());
        const layout::engine eng{layout::monospace_measure()};
        placed = eng.run(tree, viewport);
    }

    [[nodiscard]] std::shared_ptr<const display_list> record() {
        const recorder rec{atoms};
        return rec.record(placed);
    }
};

[[nodiscard]] std::size_t count_op(const display_list & list, paint_op op) {
    std::size_t n = 0;
    for (const paint_command & c : list.commands()) {
        if (c.op == op) { ++n; }
    }
    return n;
}

[[nodiscard]] const paint_command * first_fill_of(const display_list & list, color want) {
    for (const paint_command & c : list.commands()) {
        if (c.op == paint_op::fill_rect && c.fill == want) { return &c; }
    }
    return nullptr;
}

[[nodiscard]] std::size_t first_fill_index(const display_list & list, color want) {
    const auto commands = list.commands();
    for (std::size_t i = 0; i < commands.size(); ++i) {
        if (commands[i].op == paint_op::fill_rect && commands[i].fill == want) { return i; }
    }
    return commands.size();
}

[[nodiscard]] std::size_t first_text_index(const display_list & list, std::string_view want) {
    const auto commands = list.commands();
    for (std::size_t i = 0; i < commands.size(); ++i) {
        if (commands[i].op == paint_op::text_run && commands[i].text == want) { return i; }
    }
    return commands.size();
}

// --- colours --------------------------------------------------------------

void test_opacity_fades_a_whole_subtree() {
    // `opacity` is applied to what a subtree APPENDED rather than to each kind of
    // command, which is what makes it reach the background, the text and the
    // border alike. `.btn:disabled` is `opacity: .65` and it is on every disabled
    // control Bootstrap ships.
    paint::display_list list;
    list.fill(rect{0, 0, 10, 10}, color::rgba(255, 0, 0));
    const std::size_t from = list.size();
    list.fill(rect{0, 0, 10, 10}, color::rgba(0, 255, 0));
    list.text(rect{0, 0, 10, 10}, "x", 10, color::rgba(0, 0, 255));
    list.fade_from(from, 0.5f);
    const auto cmds = list.commands();
    check(cmds.size() == 3, "three commands");
    if (cmds.size() != 3) { return; }
    check(cmds[0].fill.alpha() == 255, "what came BEFORE the subtree is untouched");
    check(cmds[1].fill.alpha() == 128, "the fill inside it is faded");
    check(cmds[2].fill.alpha() == 128, "and so is the text, without knowing about it");
}

void test_a_list_marker_can_be_turned_off() {
    // `list-style: none` is on every Bootstrap nav and every `.list-unstyled`.
    // Left on, a bullet appears beside each item - a stray mark rather than a
    // wrong number, so no property diff would ever have seen it.
    const auto bullets = [](std::string_view css) {
        fixture f;
        f.load("<html><body><ul><li id=a>one</li></ul></body></html>", css, 200);
        return count_op(*f.record(), paint_op::fill_rect);
    };
    const std::size_t with = bullets("body { margin: 0 }");
    const std::size_t without = bullets("body { margin: 0 } ul { list-style: none }");
    check(with > without, "list-style: none removes the marker");
    check(without == 0, "...and nothing else was drawn to replace it");
}

void test_color_syntaxes() {
    const auto is = [](std::string_view text, std::uint32_t argb, std::string_view what) {
        const auto got = parse_color(text);
        check(got.has_value() && got->argb == argb, what);
        if (got && got->argb != argb) {
            std::printf("     %s -> %08X want %08X\n", std::string{text}.c_str(), got->argb, argb);
        }
    };
    is("#f00", 0xFFFF0000, "#rgb shorthand doubles each digit");
    is("#ff0000", 0xFFFF0000, "#rrggbb");
    is("#ff000080", 0x80FF0000, "#rrggbbaa carries alpha");
    is("#f008", 0x88FF0000, "#rgba shorthand");
    is("red", 0xFFFF0000, "a named colour");
    is("  blue  ", 0xFF0000FF, "surrounding space is ignored");
    is("rgb(255, 0, 0)", 0xFFFF0000, "rgb()");
    is("rgba(255, 0, 0, 0.5)", 0x80FF0000, "rgba() with a fractional alpha");
    is("transparent", 0x00000000, "transparent");

    // ASCII CASE-INSENSITIVE, which is not pedantry about the spec. Bootstrap's
    // `.text-bg-*` utilities write `RGBA(...)` in CAPITALS, 62 times, so a
    // case-sensitive prefix test dropped the background colour of every badge and
    // every coloured label on the page - and dropped it INVISIBLY to the parity
    // harness, which normalises both sides' text and so compared the two spellings
    // equal. The box was laid out, reported the right computed value, and painted
    // nothing. Only a screenshot could see it.
    is("RGBA(255, 0, 0, 0.5)", 0x80FF0000, "an uppercase function name is the same function");
    is("RGB(255, 0, 0)", 0xFFFF0000, "...and so is RGB()");
    is("Red", 0xFFFF0000, "a capitalised colour name");
    is("TRANSPARENT", 0x00000000, "and a shouted transparent");

    // The failure case matters as much: an unparseable colour must not become
    // black, or every typo in a stylesheet paints a black box.
    check(!parse_color("").has_value(), "empty is not a colour");
    check(!parse_color("notacolour").has_value(), "an unknown name is not a colour");
    check(!parse_color("#12345").has_value(), "a five-digit hex is not a colour");
    check(!parse_color("#gg0000").has_value(), "non-hex digits are not a colour");
}

void test_z_index_integer_syntax() {
    const auto positive = layout::parse_z_index("+2");
    check(positive.has_value() && *positive == 2, "z-index accepts an integer's leading plus");
    check(!layout::parse_z_index("+-1").has_value(), "z-index rejects two consecutive signs");
}

// --- recording ------------------------------------------------------------

void test_background_is_recorded() {
    fixture f;
    f.load("<html><body><div id=a></div></body></html>",
           "body { margin: 0; padding: 0 } #a { height: 30px; background-color: #ff0000 }");
    const auto list = f.record();
    const paint_command * fill = first_fill_of(*list, color::rgba(255, 0, 0));
    check(fill != nullptr, "a background-color becomes a fill");
    if (fill == nullptr) { return; }
    check(fill->bounds.width == 200 && fill->bounds.height == 30, "the fill covers the border box");
}

void test_nothing_is_recorded_for_nothing() {
    fixture f;
    f.load("<html><body><div id=a></div></body></html>", "#a { height: 30px }");
    const auto list = f.record();
    // A box with no background and no text draws nothing. Recording an
    // invisible command per element would make every list mostly noise, and
    // tile culling would have to filter it back out.
    check(count_op(*list, paint_op::fill_rect) == 0, "an unstyled box records no fill");
}

void test_text_is_recorded_with_the_inherited_colour() {
    fixture f;
    f.load("<html><body><div id=a>hello</div></body></html>", "body { color: #0000ff } #a { }");
    const auto list = f.record();
    std::size_t runs = 0;
    for (const paint_command & c : list->commands()) {
        if (c.op != paint_op::text_run) { continue; }
        ++runs;
        check(c.text == "hello", "the run carries its text");
        check(c.fill == color::rgba(0, 0, 255), "the text colour is inherited from body");
    }
    check(runs == 1, "one line of text records one run");
}

void test_paint_order_is_back_to_front() {
    fixture f;
    f.load("<html><body><div id=outer><div id=inner></div></div></body></html>",
           "body { margin: 0 } #outer { height: 40px; background-color: red } "
           "#inner { height: 10px; background-color: blue }");
    const auto list = f.record();
    std::size_t red_at = 0, blue_at = 0, i = 0;
    for (const paint_command & c : list->commands()) {
        if (c.fill == color::rgba(255, 0, 0)) { red_at = i; }
        if (c.fill == color::rgba(0, 0, 255)) { blue_at = i; }
        ++i;
    }
    // The parent's background has to be recorded before the child's, or the
    // child disappears under it. the previous engine needed a separate collect_backgrounds()
    // pre-pass to get this; the Appendix E phase walkers now preserve the
    // parent-before-child decoration order without that separate pass.
    check(red_at < blue_at, "a parent's background is recorded before its child's");
}

void test_all_block_decorations_precede_normal_inline_content() {
    fixture f;
    // Appendix E does not paint each normal subtree atomically. Every in-flow
    // block decoration in the context comes before the later inline-content
    // phase, so this later sibling's blue background belongs behind the earlier
    // sibling's text even though tree recursion would encounter the text first.
    f.load("<html><body><div id=first><span>front</span></div><div "
           "id=later></div></body></html>",
           "body { margin: 0 } #first, #later { width: 40px; height: 20px } "
           "#later { margin-top: -20px; background-color: blue }");
    const auto list = f.record();
    const std::size_t later = first_fill_index(*list, color::rgba(0, 0, 255));
    const std::size_t text = first_text_index(*list, "front");
    check(later < text, "a later block background paints behind earlier inline text");
}

void test_replaced_content_is_in_the_normal_content_phase() {
    fixture f;
    f.load("<html><body><div id=first><input></div><div id=later></div></body></html>",
           "body { margin: 0 } #first, #later { width: 40px; height: 20px } "
           "input { width: 20px; height: 20px } "
           "#later { margin-top: -20px; background-color: blue }");
    recorder rec{f.atoms};
    rec.paint_replaced = [](node_id source, const rect &, const rect & content,
                            const style::computed_style_ptr &, display_list & into) {
        into.fill(content, color::rgba(255, 0, 255), source);
    };
    const auto list = rec.record(f.placed);
    const std::size_t later = first_fill_index(*list, color::rgba(0, 0, 255));
    const std::size_t control = first_fill_index(*list, color::rgba(255, 0, 255));
    check(later < control, "a later block background paints behind earlier replaced content");
}

void test_z_index_orders_positioned_siblings() {
    fixture f;
    // Reverse the wanted order in the document. A recorder that still walks the
    // fragment tree literally paints blue last; z-index says red is in front.
    f.load("<html><body><div id=high></div><div id=low></div></body></html>",
           "body { margin: 0 } #high, #low { position: absolute; width: 20px; height: 20px } "
           "#high { z-index: 3; background-color: red } "
           "#low { z-index: 1; background-color: blue }");
    const auto list = f.record();
    const std::size_t red = first_fill_index(*list, color::rgba(255, 0, 0));
    const std::size_t blue = first_fill_index(*list, color::rgba(0, 0, 255));
    check(red < list->size() && blue < list->size(), "both positioned siblings are painted");
    check(blue < red, "a larger z-index paints in front of a later source sibling");
}

void test_equal_z_index_keeps_tree_order() {
    fixture f;
    f.load("<html><body><div id=first></div><div id=second></div></body></html>",
           "body { margin: 0 } #first, #second { position: absolute; z-index: 2; "
           "width: 20px; height: 20px } #first { background-color: red } "
           "#second { background-color: blue }");
    const auto list = f.record();
    const std::size_t red = first_fill_index(*list, color::rgba(255, 0, 0));
    const std::size_t blue = first_fill_index(*list, color::rgba(0, 0, 255));
    check(red < blue, "equal z-index contexts keep their tree order");
}

void test_a_negative_context_stays_above_its_context_background() {
    fixture f;
    // The negative child is deliberately last in the tree. Appendix E puts it
    // after the context's own background but before ordinary in-flow content.
    f.load("<html><body><div id=context><div id=flow></div><div "
           "id=negative></div></div></body></html>",
           "body { margin: 0 } #context { position: relative; z-index: 0; width: 40px; "
           "height: 40px; background-color: red } #flow { height: 10px; "
           "background-color: green } #negative { position: absolute; z-index: -1; "
           "width: 20px; height: 20px; background-color: blue }");
    const auto list = f.record();
    const std::size_t context = first_fill_index(*list, color::rgba(255, 0, 0));
    const std::size_t negative = first_fill_index(*list, color::rgba(0, 0, 255));
    const std::size_t flow = first_fill_index(*list, color::rgba(0, 128, 0));
    check(context < negative && negative < flow,
          "negative z-index paints after its context background and before in-flow content");
}

void test_a_context_marker_is_content_above_negative_descendants() {
    fixture f;
    // A list marker is generated inline content. It must not be bundled into
    // the context owner's decoration, which is below the negative child.
    f.load("<html><body><ol><li id=context>word<div id=negative></div></li></ol></body></html>",
           "body, ol { margin: 0 } #context { position: relative; z-index: 0; "
           "height: 20px; background-color: red } #negative { position: absolute; "
           "z-index: -1; width: 20px; height: 20px; background-color: blue }");
    const auto list = f.record();
    const std::size_t context = first_fill_index(*list, color::rgba(255, 0, 0));
    const std::size_t negative = first_fill_index(*list, color::rgba(0, 0, 255));
    const std::size_t marker = first_text_index(*list, "1.");
    check(context < negative && negative < marker,
          "a negative child paints between its context decoration and list marker");
}

void test_z_auto_descendants_escape_but_z_zero_descendants_do_not() {
    {
        fixture f;
        // `auto` does not establish a context. The blue z=2 descendant therefore
        // participates in the root context and escapes above the green z=1 sibling.
        f.load("<html><body><div id=automatic><div id=high></div></div><div "
               "id=one></div></body></html>",
               "body { margin: 0 } #automatic { position: relative; z-index: auto; height: 20px } "
               "#high { position: absolute; z-index: 2; width: 20px; height: 20px; "
               "background-color: blue } #one { position: relative; z-index: 1; height: 20px; "
               "background-color: green }");
        const auto list = f.record();
        const std::size_t high = first_fill_index(*list, color::rgba(0, 0, 255));
        const std::size_t one = first_fill_index(*list, color::rgba(0, 128, 0));
        check(one < high, "a positioned descendant escapes through a z-index:auto ancestor");
    }
    {
        fixture f;
        // The same tree with z=0 on the parent is a context. Its z=999 child is
        // trapped as one atomic unit below the later z=1 sibling.
        f.load("<html><body><div id=zero><div id=high></div></div><div id=one></div></body></html>",
               "body { margin: 0 } #zero { position: relative; z-index: 0; height: 20px; "
               "background-color: red } #high { position: absolute; z-index: 999; width: 20px; "
               "height: 20px; background-color: blue } #one { position: relative; z-index: 1; "
               "height: 20px; background-color: green }");
        const auto list = f.record();
        const std::size_t zero = first_fill_index(*list, color::rgba(255, 0, 0));
        const std::size_t high = first_fill_index(*list, color::rgba(0, 0, 255));
        const std::size_t one = first_fill_index(*list, color::rgba(0, 128, 0));
        check(zero < high && high < one,
              "a z-index:0 context traps its high-z descendant below a z-index:1 sibling");
    }
}

void test_positioned_auto_paints_after_later_in_flow_content() {
    fixture f;
    // Positioned z-index:auto content is in Appendix E's zero phase, after ALL
    // ordinary in-flow content in the context - even content later in the tree.
    f.load("<html><body><div id=automatic></div><div id=flow></div></body></html>",
           "body { margin: 0 } #automatic { position: relative; width: 20px; height: 20px; "
           "background-color: red } #flow { width: 20px; height: 20px; background-color: blue }");
    const auto list = f.record();
    const std::size_t automatic = first_fill_index(*list, color::rgba(255, 0, 0));
    const std::size_t flow = first_fill_index(*list, color::rgba(0, 0, 255));
    check(flow < automatic, "positioned z-index:auto paints after later in-flow content");
}

void test_opacity_establishes_one_atomic_context() {
    fixture f;
    // Opacity is a context even without position or z-index. The child's 999 is
    // trapped below the outside 1, and the context's fade reaches it ONCE.
    f.load("<html><body><div id=faded><div id=high></div></div><div id=one></div></body></html>",
           "body { margin: 0 } #faded { opacity: .5; height: 20px } "
           "#high { position: absolute; z-index: 999; width: 20px; height: 20px; "
           "background-color: red } #one { position: relative; z-index: 1; height: 20px; "
           "background-color: blue }");
    const auto list = f.record();
    const std::size_t high = first_fill_index(*list, color::rgba(255, 0, 0, 128));
    const std::size_t one = first_fill_index(*list, color::rgba(0, 0, 255));
    check(high < one, "an opacity context traps z=999 below an outside z=1 context");
    const paint_command * faded = first_fill_of(*list, color::rgba(255, 0, 0, 128));
    check(faded != nullptr && faded->fill.alpha() == 128,
          "the opacity context fades its descendant exactly once");
}

void test_a_contexts_background_is_outside_its_own_overflow_clip() {
    fixture f;
    f.load("<html><body><div id=context><div id=child></div></div></body></html>",
           "body { margin: 0 } #context { position: relative; z-index: 0; overflow: hidden; "
           "width: 10px; height: 10px; background-color: red } #child { width: 20px; "
           "height: 20px; background-color: blue }");
    const auto list = f.record();
    int depth = 0, context_depth = -1, child_depth = -1;
    for (const paint_command & command : list->commands()) {
        if (command.op == paint_op::push_clip) { ++depth; }
        if (command.op == paint_op::fill_rect && command.fill == color::rgba(255, 0, 0)) {
            context_depth = depth;
        }
        if (command.op == paint_op::fill_rect && command.fill == color::rgba(0, 0, 255)) {
            child_depth = depth;
        }
        if (command.op == paint_op::pop_clip) { --depth; }
    }
    check(context_depth == 0, "a context owner's background is outside its overflow clip");
    check(child_depth > 0, "the context's child is inside that overflow clip");
}

void test_transform_establishes_a_context_even_when_it_moves_nothing() {
    fixture f;
    // The zero translation is deliberate: context creation depends on the
    // computed transform being non-none, not on its resolved offset being nonzero.
    f.load(
        "<html><body><div id=transformed><div id=high></div></div><div id=one></div></body></html>",
        "body { margin: 0 } #transformed { transform: translate(0px, 0px); height: 20px } "
        "#high { position: absolute; z-index: 999; width: 20px; height: 20px; "
        "background-color: red } #one { position: relative; z-index: 1; height: 20px; "
        "background-color: blue }");
    const auto list = f.record();
    const std::size_t high = first_fill_index(*list, color::rgba(255, 0, 0));
    const std::size_t one = first_fill_index(*list, color::rgba(0, 0, 255));
    check(high < one, "a transform context traps z=999 below an outside z=1 context");
}

void test_z_index_is_ignored_on_an_ordinary_static_box() {
    fixture f;
    // The static box is later in the tree and claims a larger z-index. Neither
    // fact promotes it: ordinary in-flow content paints before positioned z=1.
    f.load("<html><body><div id=one></div><div id=static></div></body></html>",
           "body { margin: 0 } #one { position: relative; z-index: 1; width: 20px; height: 20px; "
           "background-color: red } #static { z-index: 999; width: 20px; height: 20px; "
           "background-color: blue }");
    const auto list = f.record();
    const std::size_t one = first_fill_index(*list, color::rgba(255, 0, 0));
    const std::size_t ordinary = first_fill_index(*list, color::rgba(0, 0, 255));
    check(ordinary < one, "z-index is ignored on a non-flex ordinary static box");
}

void test_z_index_applies_to_static_flex_items() {
    {
        fixture f;
        // A static flex item with integer z-index is a real context: its z=999
        // child is trapped below the outside z=1 context.
        f.load("<html><body><div id=flex><div id=item><div id=high></div></div></div><div "
               "id=one></div></body></html>",
               "body { margin: 0 } #flex { display: flex } #item { z-index: 0; width: 20px; "
               "height: 20px; background-color: red } #high { position: absolute; "
               "z-index: 999; width: 20px; height: 20px; background-color: blue } "
               "#one { position: relative; z-index: 1; width: 20px; height: 20px; "
               "background-color: green }");
        const auto list = f.record();
        const std::size_t item = first_fill_index(*list, color::rgba(255, 0, 0));
        const std::size_t high = first_fill_index(*list, color::rgba(0, 0, 255));
        const std::size_t one = first_fill_index(*list, color::rgba(0, 128, 0));
        check(item < high && high < one,
              "a static z=0 flex item traps its high-z child below outside z=1");
    }
    {
        fixture f;
        // The flex item's own integer is also its stack level, rather than only
        // a context-creation flag.
        f.load("<html><body><div id=flex><div id=item></div></div><div id=one></div></body></html>",
               "body { margin: 0 } #flex { display: flex } #item { z-index: 2; width: 20px; "
               "height: 20px; background-color: blue } #one { position: relative; "
               "z-index: 1; width: 20px; height: 20px; background-color: green }");
        const auto list = f.record();
        const std::size_t item = first_fill_index(*list, color::rgba(0, 0, 255));
        const std::size_t one = first_fill_index(*list, color::rgba(0, 128, 0));
        check(one < item, "a static flex item's integer z-index sets its stack level");
    }
}

void test_equal_negative_levels_keep_tree_order() {
    fixture f;
    f.load("<html><body><div id=first></div><div id=second></div></body></html>",
           "body { margin: 0 } #first, #second { position: absolute; z-index: -2; "
           "width: 20px; height: 20px } #first { background-color: red } "
           "#second { background-color: blue }");
    const auto list = f.record();
    const std::size_t first = first_fill_index(*list, color::rgba(255, 0, 0));
    const std::size_t second = first_fill_index(*list, color::rgba(0, 0, 255));
    check(first < second, "equal negative stack levels keep their tree order");
}

void test_an_escaped_context_keeps_its_ancestor_clip() {
    fixture f;
    // The blue child escapes the ancestor's STACKING context (there is none), but
    // not its overflow clip. The green z=1 sibling paints first and outside that
    // clip; then blue paints at z=2 while the clip is active.
    f.load("<html><body><div id=clip><div id=high></div></div><div id=one></div></body></html>",
           "body { margin: 0 } #clip { position: relative; z-index: auto; overflow: hidden; "
           "width: 10px; height: 10px } #high { position: absolute; z-index: 2; width: 20px; "
           "height: 20px; background-color: blue } #one { position: relative; z-index: 1; "
           "height: 20px; background-color: green }");
    const auto list = f.record();
    std::size_t high = list->size(), one = list->size();
    int clip_depth = 0, high_depth = -1, one_depth = -1;
    const auto commands = list->commands();
    for (std::size_t i = 0; i < commands.size(); ++i) {
        const paint_command & command = commands[i];
        if (command.op == paint_op::push_clip) { ++clip_depth; }
        if (command.op == paint_op::fill_rect && command.fill == color::rgba(0, 0, 255)) {
            high = i;
            high_depth = clip_depth;
        }
        if (command.op == paint_op::fill_rect && command.fill == color::rgba(0, 128, 0)) {
            one = i;
            one_depth = clip_depth;
        }
        if (command.op == paint_op::pop_clip) { --clip_depth; }
    }
    check(one < high, "the escaped z=2 context still paints after the z=1 sibling");
    check(one_depth == 0, "the outside sibling is not captured by the ancestor clip");
    check(high_depth > 0, "the escaped descendant remains inside its ancestor overflow clip");
}

void test_overflow_hidden_brackets_its_subtree() {
    fixture f;
    f.load("<html><body><div id=a><div id=b></div></div></body></html>",
           "#a { height: 20px; overflow: hidden } #b { height: 5px; background-color: red }");
    const auto list = f.record();
    check(count_op(*list, paint_op::push_clip) == 2,
          "overflow:hidden clips both split normal-paint phases");
    check(count_op(*list, paint_op::pop_clip) == 2, "and pops both clips");
    // Balance is the property that matters - an unbalanced clip corrupts
    // everything recorded after it, not just the subtree that pushed it.
    int depth = 0, worst = 0;
    for (const paint_command & c : list->commands()) {
        if (c.op == paint_op::push_clip) { ++depth; }
        if (c.op == paint_op::pop_clip) { --depth; }
        worst = depth < worst ? depth : worst;
    }
    check(depth == 0 && worst == 0, "clips are balanced and never pop below zero");
}

void test_recording_is_deterministic() {
    fixture f;
    f.load("<html><body><div id=a>text here</div><p>more</p></body></html>",
           "#a { background-color: #123456 } p { color: green }");
    const auto first = f.record();
    const auto second = f.record();
    check(first->size() == second->size(), "two recordings produce the same number of commands");
    bool same = first->size() == second->size();
    for (std::size_t i = 0; same && i < first->size(); ++i) {
        same = first->commands()[i] == second->commands()[i];
    }
    check(same, "...and identical commands - a golden image needs this");
}

// --- per-region query, which is what makes tiling worth doing --------------

void test_intersecting_culls_by_region() {
    display_list list;
    list.fill(rect{0, 0, 10, 10}, color::rgba(255, 0, 0));
    list.fill(rect{500, 500, 10, 10}, color::rgba(0, 255, 0));
    check(list.intersecting(rect{0, 0, 100, 100}).size() == 1,
          "a region sees only what touches it");
    check(list.intersecting(rect{400, 400, 200, 200}).size() == 1,
          "and the far region sees the other");
    check(list.intersecting(rect{2000, 2000, 10, 10}).empty(), "an empty region sees nothing");
}

void test_intersecting_drops_whole_clipped_groups() {
    display_list list;
    list.push_clip(rect{0, 0, 10, 10});
    list.fill(rect{0, 0, 10, 10}, color::rgba(255, 0, 0));
    list.pop_clip();
    list.fill(rect{500, 500, 10, 10}, color::rgba(0, 255, 0));

    const auto far_away = list.intersecting(rect{400, 400, 200, 200});
    // A clip that misses the region excludes everything inside it, so the whole
    // group goes rather than being clipped away pixel by pixel later.
    check(far_away.size() == 1, "a clipped group whose clip misses the region is dropped whole");
    if (far_away.size() == 1) {
        check(far_away[0].op == paint_op::fill_rect, "and what survives is the unclipped fill");
    }
}

void test_intersecting_keeps_clips_balanced() {
    display_list list;
    list.push_clip(rect{0, 0, 1000, 1000}); // kept
    list.push_clip(rect{900, 900, 10, 10}); // dropped for a near region
    list.fill(rect{900, 900, 10, 10}, color::rgba(255, 0, 0));
    list.pop_clip();
    list.push_clip(rect{0, 0, 5, 5}); // kept
    list.fill(rect{0, 0, 5, 5}, color::rgba(0, 255, 0));
    list.pop_clip();
    list.pop_clip();

    const auto near = list.intersecting(rect{0, 0, 50, 50});
    int depth = 0, worst = 0;
    for (const paint_command & c : near) {
        if (c.op == paint_op::push_clip) { ++depth; }
        if (c.op == paint_op::pop_clip) { --depth; }
        worst = depth < worst ? depth : worst;
    }
    // This is the case that a naive skip-counter gets wrong: a nested clip
    // inside a dropped group has to deepen the skip, or its pop unbalances the
    // list and every later command is drawn under the wrong clip.
    check(depth == 0 && worst == 0, "culling a nested clipped group leaves clips balanced");
}

void test_bounds_is_the_union() {
    display_list list;
    check(list.bounds().empty(), "an empty list has empty bounds");
    list.fill(rect{10, 10, 10, 10}, color::rgba(255, 0, 0));
    list.fill(rect{100, 50, 20, 20}, color::rgba(0, 255, 0));
    const rect b = list.bounds();
    check(b.x == 10 && b.y == 10 && b.right() == 120 && b.bottom() == 70,
          "bounds is the union of what was recorded");
}

// --- layers ---------------------------------------------------------------

void test_scrolling_moves_layers_not_commands() {
    fixture f;
    f.load("<html><body><div id=a></div></body></html>",
           "#a { height: 30px; background-color: red }");
    const recorder rec{f.atoms};
    layer_tree tree = rec.record_layers(f.placed);
    check(tree.size() == 1, "one page, one layer for now");
    const auto * before = tree.layers[0].contents.get();

    tree.scroll_to(0, 100);
    check(tree.layers[0].offset.y == -100, "scrolling sets the layer offset");
    // The identity check is the point: a scroll must not touch the recording.
    check(tree.layers[0].contents.get() == before, "and does not re-record anything");
}

void test_a_non_scrolling_layer_stays_put() {
    layer_tree tree;
    tree.layers.push_back(layer{std::make_shared<display_list>(), point{}, rect{}, true});
    tree.layers.push_back(layer{std::make_shared<display_list>(), point{}, rect{}, false});
    tree.scroll_to(0, 50);
    check(tree.layers[0].offset.y == -50, "a scrolling layer moves");
    check(tree.layers[1].offset.y == 0, "position:fixed content is a layer that does not");
}

} // namespace

int main() {
    test_color_syntaxes();
    test_z_index_integer_syntax();
    test_opacity_fades_a_whole_subtree();
    test_a_list_marker_can_be_turned_off();

    test_background_is_recorded();
    test_nothing_is_recorded_for_nothing();
    test_text_is_recorded_with_the_inherited_colour();
    test_paint_order_is_back_to_front();
    test_all_block_decorations_precede_normal_inline_content();
    test_replaced_content_is_in_the_normal_content_phase();
    test_z_index_orders_positioned_siblings();
    test_equal_z_index_keeps_tree_order();
    test_a_negative_context_stays_above_its_context_background();
    test_a_context_marker_is_content_above_negative_descendants();
    test_z_auto_descendants_escape_but_z_zero_descendants_do_not();
    test_positioned_auto_paints_after_later_in_flow_content();
    test_opacity_establishes_one_atomic_context();
    test_a_contexts_background_is_outside_its_own_overflow_clip();
    test_transform_establishes_a_context_even_when_it_moves_nothing();
    test_z_index_is_ignored_on_an_ordinary_static_box();
    test_z_index_applies_to_static_flex_items();
    test_equal_negative_levels_keep_tree_order();
    test_an_escaped_context_keeps_its_ancestor_clip();
    test_overflow_hidden_brackets_its_subtree();
    test_recording_is_deterministic();

    test_intersecting_culls_by_region();
    test_intersecting_drops_whole_clipped_groups();
    test_intersecting_keeps_clips_balanced();
    test_bounds_is_the_union();

    test_scrolling_moves_layers_not_commands();
    test_a_non_scrolling_layer_stays_put();

    REPORT("paint_basics");
}
