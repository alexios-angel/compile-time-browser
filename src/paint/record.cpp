#include <ctbrowser/paint/record.hpp>

#include <algorithm>
#include <vector>

// record: the method bodies.
// The header says what these do; this says how.

namespace ctbrowser::paint {

std::shared_ptr<const display_list> recorder::record(const fragment & root) const {
    auto list = std::make_shared<display_list>();
    paint_ref root_ref;
    root_ref.value = &root;
    root_ref.inherited_text = default_text_color;
    emit_stacking_context(root_ref, *list);
    return list;
}

layer_tree recorder::record_layers(const fragment & root) const {
    layer_tree tree;
    tree.layers.push_back(layer{record(root), point{}, rect{}, true});
    return tree;
}

std::string_view recorder::prop(const computed_style_ptr & s, atom name) const {
    return s ? s->get(name) : std::string_view{};
}

color recorder::text_color_of(const paint_ref & ref) const {
    if (ref.value == nullptr || ref.value->box == nullptr) { return ref.inherited_text; }
    if (const auto parsed = parse_color(prop(ref.value->box->style, color_))) { return *parsed; }
    return ref.inherited_text;
}

rect recorder::box_of(const paint_ref & ref) const {
    const fragment & f = *ref.value;
    return rect{f.bounds.x + ref.dx, f.bounds.y + ref.dy, f.bounds.width, f.bounds.height};
}

bool recorder::clips_children(const fragment & f) const {
    const computed_style_ptr style = f.box != nullptr ? f.box->style : computed_style_ptr{};
    return ascii_iequals(prop(style, overflow_x_), "hidden") ||
           ascii_iequals(prop(style, overflow_y_), "hidden");
}

bool recorder::is_flex_item(const fragment & parent, const fragment & child) {
    return parent.box != nullptr && parent.box->kind == layout::box_kind::flex &&
           child.box != nullptr && !child.box->is_out_of_flow();
}

bool recorder::establishes_stacking_context(const fragment & f, bool flex_item) const {
    if (f.box == nullptr) { return false; }
    const layout::box_node & box = *f.box;
    // CSS 2.1's positioned integer-z contexts are the centre of this rung.
    // Flexbox extends that rule to in-flow flex items even when position is
    // static. The parent relation is deliberately supplied by the caller:
    // `box_node` describes the item, not the formatting context containing it.
    // Opacity and transform have to join them now rather than later: both make
    // their descendants atomic, and opacity's existing subtree fade is only
    // correct while every command in that subtree remains contiguous.
    if ((box.is_positioned() || flex_item) && box.z_index.has_value()) { return true; }
    if (box.opacity < 1.0f || box.transformed) { return true; }
    // Fixed and sticky boxes establish contexts even with z-index:auto. They
    // remain in this display list until S11c gives them non-scrolling layers.
    return box.position == layout::position_kind::fixed ||
           box.position == layout::position_kind::sticky;
}

int recorder::stack_level(const fragment & f, bool flex_item) {
    if (f.box != nullptr && (f.box->is_positioned() || flex_item) && f.box->z_index.has_value()) {
        return *f.box->z_index;
    }
    return 0;
}

bool recorder::is_normal_atomic(const fragment & parent, const fragment & child) {
    // Appendix E paints an inline-level box as one source-ordered entry in the
    // normal-content phase. A flex item has the same pseudo-stacking-context
    // behaviour even when it is blockified: its ordinary descendants stay with
    // it, while real descendant contexts were already collected by the nearest
    // real context and still escape.
    return is_flex_item(parent, child) || child.box == nullptr || !child.box->is_block_level();
}

recorder::paint_ref recorder::child_ref(const paint_ref & parent, const fragment & child,
                                        color inherited_text) const {
    paint_ref out;
    out.value = &child;
    out.dx = parent.dx + parent.value->bounds.x;
    out.dy = parent.dy + parent.value->bounds.y;
    out.inherited_text = inherited_text;
    return out;
}

void recorder::collect_context_contents(const paint_ref & parent, std::span<const rect> clips,
                                        std::size_t & order, context_contents & out) const {
    const color inherited = text_color_of(parent);
    for (const fragment & child : parent.value->children) {
        paint_ref ref = child_ref(parent, child, inherited);
        ref.clips.assign(clips.begin(), clips.end());
        const bool flex_item = is_flex_item(*parent.value, child);
        ref.level = stack_level(child, flex_item);
        if (establishes_stacking_context(child, flex_item)) {
            ref.order = order++;
            out.contexts.push_back(std::move(ref));
            continue; // its descendants are trapped in that atomic context
        }

        std::vector<rect> descendant_clips{clips.begin(), clips.end()};
        if (clips_children(child)) { descendant_clips.push_back(box_of(ref)); }
        if (child.box != nullptr && child.box->is_positioned()) {
            ref.order = order++;
            out.positioned.push_back(ref);
            // An auto-z positioned box is not a context. Its descendant
            // contexts AND positioned boxes still escape to THIS context.
            // Appendix E treats this box as a pseudo-context for its ordinary
            // contents only; it does not trap either kind of descendant.
            collect_context_contents(ref, descendant_clips, order, out);
            continue;
        }
        collect_context_contents(ref, descendant_clips, order, out);
    }
}

void recorder::push_clips(std::span<const rect> clips, display_list & into) {
    for (const rect & clip : clips) { into.push_clip(clip); }
}

void recorder::pop_clips(std::size_t count, display_list & into) {
    while (count-- > 0) { into.pop_clip(); }
}

void recorder::emit_normal_backgrounds(const paint_ref & parent, display_list & into) const {
    const color inherited = text_color_of(parent);
    for (const fragment & child : parent.value->children) {
        const bool flex_item = is_flex_item(*parent.value, child);
        if (establishes_stacking_context(child, flex_item) ||
            (child.box != nullptr && child.box->is_positioned())) {
            continue;
        }
        if (is_normal_atomic(*parent.value, child)) { continue; }

        const paint_ref ref = child_ref(parent, child, inherited);
        emit_decoration(ref, text_color_of(ref), into);
        const bool clipped = clips_children(child);
        if (clipped) { into.push_clip(box_of(ref)); }
        emit_normal_backgrounds(ref, into);
        if (clipped) { into.pop_clip(); }
    }
}

void recorder::emit_normal_contents(const paint_ref & parent, display_list & into) const {
    const color inherited = text_color_of(parent);
    for (const fragment & child : parent.value->children) {
        const bool flex_item = is_flex_item(*parent.value, child);
        if (establishes_stacking_context(child, flex_item) ||
            (child.box != nullptr && child.box->is_positioned())) {
            continue;
        }

        const paint_ref ref = child_ref(parent, child, inherited);
        if (is_normal_atomic(*parent.value, child)) {
            emit_normal_atomic(ref, into);
            continue;
        }

        emit_own_content(ref, text_color_of(ref), into);
        const bool clipped = clips_children(child);
        if (clipped) { into.push_clip(box_of(ref)); }
        emit_normal_contents(ref, into);
        if (clipped) { into.pop_clip(); }
    }
}

void recorder::emit_normal_atomic(const paint_ref & ref, display_list & into) const {
    const color text_color = text_color_of(ref);
    emit_decoration(ref, text_color, into);
    if (ref.value->box != nullptr && ref.value->box->is_replaced()) {
        emit_own_content(ref, text_color, into);
        return;
    }

    const bool clipped = clips_children(*ref.value);
    if (clipped) { into.push_clip(box_of(ref)); }
    emit_normal_backgrounds(ref, into);
    if (clipped) { into.pop_clip(); }

    emit_own_content(ref, text_color, into);
    if (clipped) { into.push_clip(box_of(ref)); }
    emit_normal_contents(ref, into);
    if (clipped) { into.pop_clip(); }
}

void recorder::emit_positioned(const paint_ref & root, display_list & into) const {
    const color text_color = text_color_of(root);
    emit_decoration(root, text_color, into);
    if (root.value->box != nullptr && root.value->box->is_replaced()) {
        emit_own_content(root, text_color, into);
        return;
    }

    const bool clipped = clips_children(*root.value);
    if (clipped) { into.push_clip(box_of(root)); }
    // Appendix E treats an auto-z positioned box as a pseudo-context for its
    // ordinary contents only. Positioned descendants and real descendant
    // contexts were extracted into the nearest real context and are skipped.
    emit_normal_backgrounds(root, into);
    if (clipped) { into.pop_clip(); }

    emit_own_content(root, text_color, into);
    if (clipped) { into.push_clip(box_of(root)); }
    emit_normal_contents(root, into);
    if (clipped) { into.pop_clip(); }
}

void recorder::emit_stacking_context(const paint_ref & root, display_list & into) const {
    // CSS opacity makes the descendants one atomic stacking context. The flat
    // display list still approximates group compositing by folding alpha into
    // each command; doing that after the complete context is recorded keeps its
    // membership/order intact and fades every command exactly once. See
    // display_list::fade_from for the known overlap limitation.
    const std::size_t opaque_from = into.size();
    const color text_color = text_color_of(root);
    emit_decoration(root, text_color, into);
    if (root.value->box != nullptr && root.value->box->is_replaced()) {
        emit_own_content(root, text_color, into);
        into.fade_from(opaque_from, root.value->box->opacity);
        return;
    }

    context_contents contents;
    std::size_t order = 0;
    collect_context_contents(root, {}, order, contents);

    std::vector<const paint_ref *> negative;
    std::vector<const paint_ref *> positive;
    struct zero_entry {
        const paint_ref * ref = nullptr;
        bool context = false;
    };
    std::vector<zero_entry> zero;
    for (const paint_ref & ref : contents.contexts) {
        if (ref.level < 0) {
            negative.push_back(&ref);
        } else if (ref.level > 0) {
            positive.push_back(&ref);
        } else {
            zero.push_back(zero_entry{&ref, true});
        }
    }
    for (const paint_ref & ref : contents.positioned) { zero.push_back(zero_entry{&ref, false}); }
    const auto by_level_then_tree = [](const paint_ref * a, const paint_ref * b) {
        return a->level != b->level ? a->level < b->level : a->order < b->order;
    };
    std::stable_sort(negative.begin(), negative.end(), by_level_then_tree);
    std::stable_sort(positive.begin(), positive.end(), by_level_then_tree);
    std::sort(zero.begin(), zero.end(), [](const zero_entry & a, const zero_entry & b) {
        return a.ref->order < b.ref->order;
    });

    const auto emit_context = [&](const paint_ref & ref) {
        push_clips(ref.clips, into);
        emit_stacking_context(ref, into);
        pop_clips(ref.clips.size(), into);
    };

    const bool clipped = clips_children(*root.value);
    if (clipped) { into.push_clip(box_of(root)); }
    for (const paint_ref * ref : negative) { emit_context(*ref); }

    // CSS 2.1 Appendix E separates every in-flow block's decoration from later
    // inline/text/replaced/marker content. That distinction matters across
    // siblings: a later block background stays behind an earlier text run.
    emit_normal_backgrounds(root, into);
    if (clipped) { into.pop_clip(); }

    // The context owner's generated marker belongs to the content phase, not
    // its decoration, so a negative child stays between the two. Like the
    // owner's background, a marker is outside its own overflow clip.
    emit_own_content(root, text_color, into);
    if (clipped) { into.push_clip(box_of(root)); }
    emit_normal_contents(root, into);

    // Auto/zero positioned descendants follow all ordinary content; positive
    // contexts follow those. Equal levels retain tree order and every real
    // context is emitted atomically.
    for (const zero_entry & entry : zero) {
        push_clips(entry.ref->clips, into);
        if (entry.context) {
            emit_stacking_context(*entry.ref, into);
        } else {
            emit_positioned(*entry.ref, into);
        }
        pop_clips(entry.ref->clips.size(), into);
    }
    for (const paint_ref * ref : positive) { emit_context(*ref); }
    if (clipped) { into.pop_clip(); }

    const float opacity = root.value->box != nullptr ? root.value->box->opacity : 1.0f;
    into.fade_from(opaque_from, opacity);
}

void recorder::stroke(const rect & box, float t, color c, node_id source, display_list & into) {
    into.fill(rect{box.x, box.y, box.width, t}, c, source);
    into.fill(rect{box.x, box.bottom() - t, box.width, t}, c, source);
    into.fill(rect{box.x, box.y + t, t, box.height - 2 * t}, c, source);
    into.fill(rect{box.right() - t, box.y + t, t, box.height - 2 * t}, c, source);
}

} // namespace ctbrowser::paint
