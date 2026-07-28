#include <ctbrowser/paint/record.hpp>

// record: the method bodies.
// The header says what these do; this says how.

namespace ctbrowser::paint {

std::shared_ptr<const display_list> recorder::record(const fragment & root) const {
    auto list = std::make_shared<display_list>();
    emit(root, 0, 0, default_text_color, *list);
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

void recorder::stroke(const rect & box, float t, color c, node_id source, display_list & into) {
    into.fill(rect{box.x, box.y, box.width, t}, c, source);
    into.fill(rect{box.x, box.bottom() - t, box.width, t}, c, source);
    into.fill(rect{box.x, box.y + t, t, box.height - 2 * t}, c, source);
    into.fill(rect{box.right() - t, box.y + t, t, box.height - 2 * t}, c, source);
}

} // namespace ctbrowser::paint
