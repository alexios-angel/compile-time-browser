#include <ctbrowser/style/engine.hpp>

#include "check.hpp"

#include <string_view>

using namespace ctbrowser;

int main() {
    atom_table atoms;
    document doc{atoms};
    style::engine selectors{atoms};
    const node_id html = doc.create_element(atoms.intern("html"));
    const node_id body = doc.create_element(atoms.intern("body"));
    const node_id outer = doc.create_element(atoms.intern("div"));
    const node_id button = doc.create_element(atoms.intern("button"));
    const node_id icon = doc.create_element(atoms.intern("span"));
    doc.set_document_element(html);
    CHECK(doc.append_child(html, body).has_value());
    CHECK(doc.append_child(body, outer).has_value());
    CHECK(doc.append_child(outer, button).has_value());
    CHECK(doc.append_child(button, icon).has_value());
    CHECK(doc.set_attribute(button, atoms.intern("data-bs-toggle"), "button").has_value());
    const auto txn = doc.read();
    const auto closest = [&](node_id from, std::string_view text) {
        bool bad = false;
        const auto parsed = style::css::parse_selector_text(text, atoms, bad);
        CHECK(!bad);
        return selectors.closest(txn, from, parsed.selectors);
    };

    // Bootstrap's delegated Button lookup starts at a descendant of its button.
    CHECK(closest(icon, "[data-bs-toggle=button]") == button);
    CHECK(closest(button, "[data-bs-toggle=button]") == button);
    CHECK(closest(icon, "div, button") == button);
    CHECK(closest(icon, "body > div") == outer);
    CHECK(closest(icon, ":scope") == icon);
    CHECK(closest(icon, "button > :scope") == icon);
    CHECK(!closest(icon, "div > :scope"));
    CHECK(!closest(icon, ".missing"));
    CHECK(!closest(icon, "span::before"));
    CHECK(!closest({}, "*"));
    CHECK(!closest(node_id{icon.slot, icon.generation + 1}, "*"));
    CHECK(!selectors.closest(txn, icon, {}));

    // Detachment preserves node identity and ordinary ancestors within the subtree.
    CHECK(doc.remove_child(outer).has_value());
    CHECK(closest(icon, "[data-bs-toggle=button]") == button);
    CHECK(closest(icon, "div") == outer);
    CHECK(!closest(icon, "body"));
    CHECK(doc.append_child(body, outer).has_value());
    CHECK(closest(icon, "body") == body);

    const node_id shadow = doc.attach_shadow(outer, true).value();
    const node_id shadow_button = doc.create_element(atoms.intern("button"));
    const node_id shadow_icon = doc.create_element(atoms.intern("span"));
    CHECK(doc.append_child(shadow, shadow_button).has_value());
    CHECK(doc.append_child(shadow_button, shadow_icon).has_value());
    CHECK(closest(shadow_icon, "button") == shadow_button);
    CHECK(!closest(shadow_icon, "div"));
    CHECK(!closest(shadow_icon, "body"));
    CHECK(closest(icon, "body") == body); // earlier queries do not retain their scope
    REPORT("style_closest");
}
