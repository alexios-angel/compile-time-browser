// dom_bindings - the element wrapper - what `document.querySelector` hands back.
//
// One of six files carved out of a 3,926-line bindings.cpp on 2026-08-09.
// These are all member functions of one class declared in
// include/ctbrowser/shell/bindings.hpp, so they split across translation
// units with nothing to declare and no linkage to arrange.

#include <ctbrowser/core/algorithms.hpp>
#include <ctbrowser/shell/bindings.hpp>
#include <ctbrowser/shell/net/url.hpp>

#include <numbers>
#include <optional>

// dom_bindings' method bodies - the API a page's script actually calls.
//
// The header lists what a page can reach; this is how each one works.

namespace ctbrowser::shell {

value dom_bindings::wrap(context & cx, node_id id) {
    if (!id) { return value::null(); }
    if (const auto it = wrappers_.find(pack(id)); it != wrappers_.end()) {
        refresh_element(cx, *it->second, id);
        return value::object(it->second);
    }
    auto * obj = static_cast<script::object_object *>(cx.make_object().as_heap());
    value wrapper = value::object(obj);
    obj->set(std::string{handle_property}, value::number(static_cast<double>(pack(id))));
    // WHICH INTERFACE THIS ELEMENT IS, so `instanceof` can answer. Only the two
    // a library actually asks about are distinguished; everything else stays a
    // plain object rather than growing a hierarchy nothing reads.
    {
        const auto txn = doc_->read();
        const std::string_view tag = atoms_->text(txn.tag(id).value_or(atom{}));
        if (tag == "canvas" && canvas_element_prototype_.is_object()) {
            obj->prototype = canvas_element_prototype_;
        } else if (tag == "img" && image_element_prototype_.is_object()) {
            obj->prototype = image_element_prototype_;
        }
    }
    install_element_methods(cx, *obj);
    install_element_views(cx, *obj, id);
    refresh_element(cx, *obj, id);
    wrappers_.emplace(pack(id), obj);
    return wrapper;
}

std::uint64_t dom_bindings::pack(node_id id) {
    return (static_cast<std::uint64_t>(id.generation) << 32) | id.slot;
}

node_id dom_bindings::unpack(std::uint64_t bits) {
    node_id id;
    id.slot = static_cast<std::uint32_t>(bits & 0xFFFFFFFFu);
    id.generation = static_cast<std::uint32_t>(bits >> 32);
    return id;
}

node_id dom_bindings::receiver(context & cx) {
    const value self = cx.current_this();
    if (!self.is_object()) { return node_id{}; }
    auto * obj = static_cast<script::object_object *>(self.as_heap());
    const value * slot = obj->find(std::string{handle_property});
    if (slot == nullptr) { return node_id{}; }
    return unpack(static_cast<std::uint64_t>(context::to_number(*slot)));
}

void dom_bindings::refresh_element(context & cx, script::object_object & obj, node_id id) {
    const auto txn = doc_->read();
    // `tagName` is UPPERCASE for an HTML element, and lowercase was a silent
    // wrong answer: p5.js branches on `elt.tagName === 'INPUT'` and on
    // `child.tagName === param` in its XML module, so every such comparison was
    // false and the code behind it never ran. An SVG element keeps its own case -
    // tagName is the qualified name, and only HTML uppercases it.
    {
        std::string tag_name{atoms_->text(txn.tag(id).value_or(atom{}))};
        if (txn.element_ns(id) == node_ns::html) {
            for (char & c : tag_name) {
                if (c >= 'a' && c <= 'z') { c = static_cast<char>(c - 'a' + 'A'); }
            }
        }
        obj.set("tagName", cx.string(tag_name));
        // `nodeName` AND `nodeType`, which every tree-walking page reads and
        // this wrapper did not have. They are not aliases of `tagName`: a
        // wrapper is made for text and comment nodes too - `childNodes` hands
        // them out - and for those the tag is empty, so `tagName` is "" and
        // `nodeName` is "#text". A page that switches on nodeType to decide
        // whether to recurse got `undefined` and took no branch at all.
        const node_kind kind = txn.kind(id).value_or(node_kind::element);
        switch (kind) {
        case node_kind::element:
            obj.set("nodeName", cx.string(tag_name));
            obj.set("nodeType", value::number(1));
            break;
        case node_kind::text:
            obj.set("nodeName", cx.string("#text"));
            obj.set("nodeType", value::number(3));
            break;
        case node_kind::comment:
            obj.set("nodeName", cx.string("#comment"));
            obj.set("nodeType", value::number(8));
            break;
        case node_kind::document:
            obj.set("nodeName", cx.string("#document"));
            obj.set("nodeType", value::number(9));
            break;
        }
        // `localName` is the tag WITHOUT the case fold - `tagName` uppercases an
        // HTML element's and localName never does, which is exactly the pair the
        // suite compares against each other.
        obj.set("localName",
                kind == node_kind::element
                    ? cx.string(std::string{atoms_->text(txn.tag(id).value_or(atom{}))})
                    : value::undefined());
    }
    // `id`, `className`, `width` and `height` are NOT set here: they are
    // accessors over the attributes, installed once in install_element_views.
    // As data properties they were write-only in the wrong direction - a page
    // assigning `el.id = 'x'` changed the wrapper and nothing else, and the
    // next refresh put the old value back. p5.js names its canvas and sizes it
    // that way, so both writes vanished.
    const std::string_view tag_text = atoms_->text(txn.tag(id).value_or(atom{}));

    refresh_control(cx, obj, txn, id, tag_text);

    const rect box = box_of(id);
    obj.set("offsetLeft", value::number(static_cast<double>(box.x)));
    obj.set("offsetTop", value::number(static_cast<double>(box.y)));
    obj.set("offsetWidth", value::number(static_cast<double>(box.width)));
    obj.set("offsetHeight", value::number(static_cast<double>(box.height)));
    // `clientWidth`/`clientHeight` - the CONTENT box, and the only way a page
    // asks how big the viewport is: p5's own windowWidth and windowHeight are
    // `document.documentElement.clientWidth`, so both of them read `undefined`
    // and every sketch that sizes itself to the window got NaN.
    //
    // THE ROOT'S CLIENT RECTANGLE IS THE VIEWPORT, and its two axes come from
    // different places on purpose:
    //
    //   width  - the root element's own box. That box fills the initial
    //            containing block, so its width IS the layout viewport: 15px
    //            narrower than the window when the page overflows and a
    //            scrollbar appears. Reading it from the box rather than from a
    //            number the shell pushes in removes an ordering hazard - script
    //            bindings are installed lazily, so whichever of layout and
    //            install ran last decided the answer, and the wrong one won.
    //            Bootstrap's `.container` centred itself in 1009px while the
    //            page was told it had 1024.
    //   height - the VIEWPORT's, not the box's. The root box is as tall as the
    //            document, and `document.documentElement.clientHeight` means
    //            "how tall is the window", which is what p5's windowHeight and
    //            every self-sizing sketch is asking.
    //
    // The body is an ordinary element here: its client box is its own, which is
    // what Chrome reports and differs from the root's whenever the UA margin is
    // in play. For anything else the content box is the border box - nothing
    // here has a scrollbar of its own, and borders are not yet in the box
    // arithmetic.
    //
    // Before the first layout the root has no box, and a sketch that sizes itself
    // in `setup()` would read zero - which is how p5's windowWidth broke when
    // this moved off the number the shell pushes in. The window stands in, FOR
    // THE ROOT ONLY: an ordinary element with no box has a client width of zero,
    // and handing it the viewport instead told Babylon its canvas was
    // window-sized before layout had given it any size at all, which failed
    // WebGL setup outright. "No box" and "as wide as the window" are the same
    // thing for the root and nothing else.
    const bool is_root = tag_text == "html";
    obj.set("clientWidth",
            value::number(is_root && box.width <= 0 ? viewport_width_
                                                    : static_cast<double>(box.width)));
    obj.set("clientHeight",
            value::number(is_root ? viewport_height_ : static_cast<double>(box.height)));
    // `element.attributes` - a live-ish NamedNodeMap, as an array of {name,
    // value} with the aliases a page reads. p5's XML module walks it for
    // getAttributeCount, listAttributes and setName, so an absent one made every
    // attribute of a parsed document invisible.
    {
        const value list = cx.make_array();
        auto * items = static_cast<script::array_object *>(list.as_heap());
        for (const attribute & held : txn.attributes(id)) {
            auto * pair = static_cast<script::object_object *>(cx.make_object().as_heap());
            const std::string text{atoms_->text(held.name)};
            pair->set("name", cx.string(text));
            pair->set("nodeName", cx.string(text)); // the older spelling p5 uses
            pair->set("value", cx.string(held.value));
            pair->set("nodeValue", cx.string(held.value));
            items->items.push_back(value::object(pair));
        }
        obj.set("attributes", list);
    }
    obj.set("clientLeft", value::number(0));
    obj.set("clientTop", value::number(0));
    obj.set("scrollWidth", value::number(static_cast<double>(box.width)));
    obj.set("scrollHeight", value::number(static_cast<double>(box.height)));
}

namespace {

// `backgroundColor` -> `background-color`. The IDL name and the CSS name are
// different spellings of the same property, and the attribute the style engine
// parses wants the CSS one.
std::string css_property_name(std::string_view idl) {
    std::string out;
    for (const char c : idl) {
        if (c >= 'A' && c <= 'Z') {
            out += '-';
            out += static_cast<char>(c - 'A' + 'a');
        } else {
            out += c;
        }
    }
    return out;
}

// The declarations an object holds, as a `style` attribute. Serialising the
// whole object on every write is what keeps the two representations from
// drifting: there is one source of truth, the object, and the attribute is
// derived from it.
std::string style_attribute(script::object_object & held, context & cx) {
    std::string out;
    for (const auto & [name, v] : held.props) {
        if (v.is_nullish()) { continue; }
        // `setProperty` and friends live on the same object, and a CSS value is
        // never a function - without this the methods serialise themselves into
        // the attribute as `set-property: function;`.
        if (v.is_callable()) { continue; }
        const std::string text = cx.to_string(v);
        // Assigning "" REMOVES a declaration, which is how a page turns one
        // off - emitting `display: ;` instead would leave the old value in
        // place as far as the parser is concerned.
        if (text.empty()) { continue; }
        out += css_property_name(name);
        out += ": ";
        out += text;
        out += "; ";
    }
    return out;
}

// The declarations already in a `style` attribute, so a write through
// `el.style` extends what the author wrote rather than replacing it. The whole
// object is re-serialised on every write, so anything not read back here is
// LOST on the first assignment - which silently deleted the width and height an
// element was sized by.
void seed_declarations(script::object_object & held, context & cx, std::string_view text) {
    std::size_t i = 0;
    while (i < text.size()) {
        const std::size_t colon = text.find(':', i);
        if (colon == std::string_view::npos) { break; }
        std::size_t end = text.find(';', colon);
        if (end == std::string_view::npos) { end = text.size(); }
        const auto trim = [](std::string_view piece) {
            const std::size_t first = piece.find_first_not_of(" \t\n\r\f");
            if (first == std::string_view::npos) { return std::string_view{}; }
            return piece.substr(first, piece.find_last_not_of(" \t\n\r\f") - first + 1);
        };
        const std::string_view name = trim(text.substr(i, colon - i));
        const std::string_view v = trim(text.substr(colon + 1, end - colon - 1));
        if (!name.empty()) { held.set(std::string{name}, cx.string(std::string{v})); }
        i = end + 1;
    }
}

// The tokens of a `class` attribute. Whitespace-separated, and a class list
// operation is defined in terms of them rather than of the string.
std::vector<std::string> class_tokens(std::string_view text) {
    std::vector<std::string> out;
    std::size_t i = 0;
    while (i < text.size()) {
        const std::size_t start = text.find_first_not_of(" \t\n\r\f", i);
        if (start == std::string_view::npos) { break; }
        const std::size_t end = text.find_first_of(" \t\n\r\f", start);
        out.emplace_back(text.substr(start, end == std::string_view::npos ? end : end - start));
        i = end == std::string_view::npos ? text.size() : end;
    }
    return out;
}

} // namespace

void dom_bindings::install_element_views(context & cx, script::object_object & obj, node_id id) {
    // --- element.style
    //
    // A PROXY, because a style object has no fixed set of properties: a page
    // may write any CSS property and the write has to reach the document. The
    // proxy's target holds the declarations and the `set` trap re-serialises it
    // into the element's `style` attribute - which the style engine already
    // parses, so there is no second representation to keep in step.
    //
    // BOTH traps canonicalise the name, because `backgroundColor` and
    // `background-color` are two spellings of ONE property. Storing them as
    // written put both in the attribute and made a read miss a write.
    auto * held = static_cast<script::object_object *>(cx.make_object().as_heap());
    {
        const auto txn = doc_->read();
        seed_declarations(*held, cx, txn.attribute_value(id, atoms_->intern("style")));
    }
    const value target = value::object(held);
    auto * handler = static_cast<script::object_object *>(cx.make_object().as_heap());
    const auto trap = [&](std::string name, script::native_fn fn) {
        handler->set(name, value::object(cx.allocate<script::native_object>(name, std::move(fn))));
    };
    trap("get", [](context & c, std::span<value> args) {
        if (args.size() < 2 || !args[0].is_object()) { return value::undefined(); }
        auto * store = static_cast<script::object_object *>(args[0].as_heap());
        const std::string name = c.to_string(args[1]);
        // The raw name first: that is where setProperty and getPropertyValue
        // live, and canonicalising them turns them into `set-property`.
        if (const value * found = store->find(name)) { return *found; }
        const value * found = store->find(css_property_name(name));
        return found == nullptr ? value::undefined() : *found;
    });
    trap("set", [this, id](context & c, std::span<value> args) {
        if (args.size() < 3 || !args[0].is_object()) { return value::boolean(false); }
        auto * store = static_cast<script::object_object *>(args[0].as_heap());
        store->set(css_property_name(c.to_string(args[1])), args[2]);
        (void)doc_->set_attribute(id, atoms_->intern("style"), style_attribute(*store, c));
        mutated();
        return value::boolean(true);
    });
    const value style_view =
        value::object(cx.allocate<script::proxy_object>(target, value::object(handler)));

    // `setProperty` / `getPropertyValue` / `removeProperty` take the CSS
    // spelling rather than the IDL one, so they are the only way to reach a
    // custom property (`--x`) - which no identifier can name.
    const auto declaration_method = [&](std::string name, script::native_fn fn) {
        held->set(name, value::object(cx.allocate<script::native_object>(name, std::move(fn))));
    };
    declaration_method("setProperty", [this, id, held](context & c, std::span<value> args) {
        held->set(arg_string(c, args, 0), args.size() > 1 ? args[1] : value::undefined());
        (void)doc_->set_attribute(id, atoms_->intern("style"), style_attribute(*held, c));
        mutated();
        return value::undefined();
    });
    declaration_method("removeProperty", [this, id, held](context & c, std::span<value> args) {
        held->erase(arg_string(c, args, 0));
        (void)doc_->set_attribute(id, atoms_->intern("style"), style_attribute(*held, c));
        mutated();
        return value::undefined();
    });
    declaration_method("getPropertyValue", [held](context & c, std::span<value> args) {
        const value * found = held->find(arg_string(c, args, 0));
        return found == nullptr ? c.string("") : c.string(c.to_string(*found));
    });
    obj.set("style", style_view);

    // --- the reflected attributes
    //
    // `id`, `className`, `width` and `height` are IDL attributes that REFLECT
    // content attributes: reading one reads the attribute and writing one
    // writes it. As plain data properties they only ever went one way - the
    // refresh copied the attribute onto the wrapper, and a page's assignment
    // changed the wrapper alone and was overwritten by the next refresh.
    //
    // Nothing said so. p5.js gives its canvas an id and a size that way and
    // both writes disappeared, leaving a nameless 300x150 canvas that no
    // amount of drawing could make the right size.
    const auto reflect_string = [&](std::string property, std::string attribute) {
        obj.define_accessor(
            property,
            value::object(cx.allocate<script::native_object>(
                property,
                [this, id, attribute](context & c, std::span<value>) {
                    const auto txn = doc_->read();
                    return c.string(
                        std::string{txn.attribute_value(id, atoms_->intern(attribute))});
                })),
            value::object(cx.allocate<script::native_object>(
                property, [this, id, attribute](context & c, std::span<value> a) {
                    (void)doc_->set_attribute(id, atoms_->intern(attribute), arg_string(c, a, 0));
                    mutated();
                    return value::undefined();
                })));
    };
    reflect_string("id", "id");
    reflect_string("className", "class");
    // THE REST OF THE ORDINARY IDL ATTRIBUTES, and leaving them out was the same
    // one-way bug as `id` used to be: `link.href = url` set a property on the
    // wrapper that no attribute, no layout and no default action ever read.
    //
    // That is what made p5's export do nothing. downloadFile builds `<a href
    // download>` entirely from script, so BOTH attributes were invisible - the
    // click found an anchor with no href and no download and treated it as a
    // click on nothing.
    //
    // Named rather than generic, because reflection is per element in the spec
    // and a made-up `el.foo = 1` must NOT become an attribute. These are the ones
    // a page assigns.
    for (const auto & [property, attribute] :
         std::initializer_list<std::pair<const char *, const char *>>{
             {"href", "href"},
             {"download", "download"},
             {"target", "target"},
             {"rel", "rel"},
             {"alt", "alt"},
             {"title", "title"},
             {"name", "name"},
             {"placeholder", "placeholder"},
             // `type` is what a page branches on for a control - p5 itself does
             // `elt.tagName === 'INPUT' && elt.type === 'checkbox'` - and
             // createFileInput builds its element with setAttribute('type',
             // 'file') and then hands back something whose `.type` read undefined.
             // NOT `value` or `src`: both have live accessors of their own - a
             // control's value tracks what the user typed rather than the
             // attribute, and an <img>'s src has to start a load. A generic
             // reflection here overwrites those and quietly wins.
             {"type", "type"},
             {"htmlFor", "for"}}) {
        reflect_string(property, attribute);
    }

    // `innerHTML` and `textContent` are ACCESSORS over the tree, not properties
    // on the wrapper. As properties, assigning markup stored a string, built no
    // nodes, rendered nothing and reported nothing - and reading one back gave
    // whatever the page last wrote rather than what the DOM actually holds.
    const auto tree_property = [&](std::string property, script::native_fn read,
                                   script::native_fn write) {
        obj.define_accessor(
            property, value::object(cx.allocate<script::native_object>(property, std::move(read))),
            value::object(cx.allocate<script::native_object>(property, std::move(write))));
    };
    tree_property(
        "innerHTML", [this, id](context & c, std::span<value>) { return c.string(inner_html(id)); },
        [this, id](context & c, std::span<value> a) {
            set_inner_html(id, arg_string(c, a, 0));
            return value::undefined();
        });
    tree_property(
        "textContent",
        [this, id](context & c, std::span<value>) { return c.string(text_content(id)); },
        [this, id](context & c, std::span<value> a) {
            // Text, never markup: that is the whole point of the property, and
            // the reason a page reaches for it instead of innerHTML.
            set_text(id, arg_string(c, a, 0));
            return value::undefined();
        });

    // `value` and `checked` ARE ACCESSORS, on a control.
    //
    // They were data properties written by refresh_control on whatever tick it
    // next ran. A page that creates a control and reads it back in the same
    // statement - `createInput('hello').value()`, which is p5's own DOM library
    // - therefore read the property as it was before the value existed. The
    // header's note that "the VM has no property accessors" was true when it
    // was written and is not any more.
    //
    // refresh_control still runs: it writes BACK a property assignment into the
    // control, which is how `input.value = ''` clears a field. These make the
    // READ live, which is the half that could not be done before.
    {
        const auto txn = doc_->read();
        const std::string_view tag = atoms_->text(txn.tag(id).value_or(atom{}));
        const std::string_view type = txn.attribute_value(id, atoms_->intern("type"));
        if (control_kind_of(tag, type) != control_kind::none) {
            obj.define_accessor("value",
                                value::object(cx.allocate<script::native_object>(
                                    "value",
                                    [this, id](context & c, std::span<value>) {
                                        const auto read = doc_->read();
                                        return c.string(forms_->state_of(read, *atoms_, id).value);
                                    })),
                                value::object(cx.allocate<script::native_object>(
                                    "value", [this, id](context & c, std::span<value> a) {
                                        const auto read = doc_->read();
                                        control_state & control =
                                            forms_->state_of(read, *atoms_, id);
                                        control.value = arg_string(c, a, 0);
                                        control.caret = control.value.size();
                                        control.selection = control.caret;
                                        // An assignment DIRTIES the control, so the `value`
                                        // attribute stops being the answer - otherwise setting
                                        // it to "" would be undone by the next read.
                                        control.value_edited = true;
                                        // The browser has to learn a control changed, or the
                                        // paint is stale until something else marks it.
                                        wrote_to_control_ = true;
                                        mutated();
                                        return value::undefined();
                                    })));
            obj.define_accessor("checked",
                                value::object(cx.allocate<script::native_object>(
                                    "checked",
                                    [this, id](context &, std::span<value>) {
                                        const auto read = doc_->read();
                                        return value::boolean(
                                            forms_->state_of(read, *atoms_, id).checked);
                                    })),
                                value::object(cx.allocate<script::native_object>(
                                    "checked", [this, id](context &, std::span<value> a) {
                                        const auto read = doc_->read();
                                        forms_->state_of(read, *atoms_, id).checked =
                                            !a.empty() && context::truthy(a[0]);
                                        wrote_to_control_ = true;
                                        mutated();
                                        return value::undefined();
                                    })));
        }
    }

    // `parentNode` and `children` are ACCESSORS because the tree moves. A
    // wrapper built when an element was detached and refreshed later would hand
    // back the parent it had at wrapping time, which for an element p5 creates
    // and then appends is null forever.
    const auto navigate = [&](std::string property, script::native_fn fn) {
        obj.define_accessor(
            property, value::object(cx.allocate<script::native_object>(property, std::move(fn))),
            value::undefined());
    };
    navigate("parentNode", [this, id](context & c, std::span<value>) {
        const auto txn = doc_->read();
        return wrap(c, txn.parent(id));
    });
    navigate("parentElement", [this, id](context & c, std::span<value>) {
        const auto txn = doc_->read();
        return wrap(c, txn.parent(id));
    });
    // `childNodes` is EVERY child, text nodes included; `children` is the
    // elements only. Both exist because they answer different questions, and a
    // page that wants the text nodes has no other way to reach them.
    navigate("childNodes", [this, id](context & c, std::span<value>) {
        value list = c.make_array();
        auto * items = static_cast<script::array_object *>(list.as_heap());
        const auto txn = doc_->read();
        for (const node_id child : txn.children(id)) { items->items.push_back(wrap(c, child)); }
        return list;
    });
    navigate("children", [this, id](context & c, std::span<value>) {
        value list = c.make_array();
        auto * items = static_cast<script::array_object *>(list.as_heap());
        const auto txn = doc_->read();
        for (const node_id child : txn.children(id)) {
            if (txn.tag(child).has_value()) { items->items.push_back(wrap(c, child)); }
        }
        return list;
    });

    // `width` and `height` are numbers, and on a <canvas> they are the size of
    // its PIXEL BUFFER rather than of its laid-out box - `canvas.width / 2` is
    // the first line of most canvas pages. Assigning one resizes the surface,
    // which is what the spec means by a canvas being reset by the assignment.
    const auto reflect_size = [&](std::string property, double fallback) {
        obj.define_accessor(
            property,
            value::object(cx.allocate<script::native_object>(
                property,
                [this, id, property, fallback](context &, std::span<value>) {
                    const auto txn = doc_->read();
                    const std::string_view text = txn.attribute_value(id, atoms_->intern(property));
                    double parsed = 0;
                    bool any = false;
                    for (const char c : text) {
                        if (c < '0' || c > '9') { break; }
                        parsed = parsed * 10 + (c - '0');
                        any = true;
                    }
                    return value::number(any ? parsed : fallback);
                })),
            value::object(cx.allocate<script::native_object>(
                property, [this, id, property](context &, std::span<value> a) {
                    const double want = arg_number(a, 0);
                    (void)doc_->set_attribute(id, atoms_->intern(property),
                                              std::to_string(static_cast<long long>(want)));
                    // The SURFACE follows, or the canvas keeps drawing into a
                    // buffer of the size it was created at and everything past
                    // that edge is silently discarded.
                    if (canvases_ != nullptr) {
                        const auto txn = doc_->read();
                        const auto number = [&](std::string_view name, int missing) {
                            const std::string_view text =
                                txn.attribute_value(id, atoms_->intern(name));
                            int out = 0;
                            bool any = false;
                            for (const char c : text) {
                                if (c < '0' || c > '9') { break; }
                                out = out * 10 + (c - '0');
                                any = true;
                            }
                            return any ? out : missing;
                        };
                        const int w = number("width", 300);
                        const int h = number("height", 150);
                        canvases_->resize(id, w, h);
                        // And the WebGL context over the same canvas, which held
                        // a pointer INTO the buffer that resize just replaced.
                        resize_webgl_context(id, w, h);
                    }
                    mutated();
                    return value::undefined();
                })));
    };
    {
        const auto txn = doc_->read();
        const std::string_view tag = atoms_->text(txn.tag(id).value_or(atom{}));
        if (tag == "canvas") {
            // The HTML defaults, which a page that omits the attributes relies on.
            reflect_size("width", 300);
            reflect_size("height", 150);
        } else if (tag == "img") {
            install_image_views(cx, obj, id);
        } else if (tag == "input" && txn.attribute_value(id, atoms_->intern("type")) == "file") {
            // AN EMPTY FileList, and it has to EXIST. There is no user here to
            // choose a file, so this is always empty - but `event.target.files`
            // is what every change handler iterates, and undefined there is a
            // TypeError on the first line of the handler rather than a quiet
            // nothing-was-chosen.
            const value files = cx.make_array();
            static_cast<script::array_object *>(files.as_heap())->items.clear();
            obj.set("files", files);
        } else if (txn.has_attribute(id, atoms_->intern("width")) ||
                   txn.has_attribute(id, atoms_->intern("height"))) {
            reflect_size("width", 0);
            reflect_size("height", 0);
        }
    }

    // --- element.classList
    //
    // Every operation reads the attribute, edits the token list and writes it
    // back, so nothing is cached and a class added by the parser, by
    // setAttribute or by the style engine is seen by all of them.
    auto * list = static_cast<script::object_object *>(cx.make_object().as_heap());
    const auto tokens_now = [this, id] {
        const auto txn = doc_->read();
        return class_tokens(txn.attribute_value(id, atoms_->intern("class")));
    };
    const auto write_tokens = [this, id](const std::vector<std::string> & tokens) {
        std::string text;
        for (const std::string & token : tokens) {
            if (!text.empty()) { text += ' '; }
            text += token;
        }
        (void)doc_->set_attribute(id, atoms_->intern("class"), text);
        mutated();
    };
    const auto list_method = [&](std::string name, script::native_fn fn) {
        list->set(name, value::object(cx.allocate<script::native_object>(name, std::move(fn))));
    };
    list_method("add", [tokens_now, write_tokens](context & c, std::span<value> args) {
        std::vector<std::string> tokens = tokens_now();
        for (const value & v : args) {
            const std::string token = c.to_string(v);
            if (token.empty()) { continue; }
            if (std::find(tokens.begin(), tokens.end(), token) == tokens.end()) {
                tokens.push_back(token);
            }
        }
        write_tokens(tokens);
        return value::undefined();
    });
    list_method("remove", [tokens_now, write_tokens](context & c, std::span<value> args) {
        std::vector<std::string> tokens = tokens_now();
        for (const value & v : args) {
            const std::string token = c.to_string(v);
            std::erase(tokens, token);
        }
        write_tokens(tokens);
        return value::undefined();
    });
    list_method("contains", [tokens_now](context & c, std::span<value> args) {
        const std::vector<std::string> tokens = tokens_now();
        return value::boolean(std::find(tokens.begin(), tokens.end(), arg_string(c, args, 0)) !=
                              tokens.end());
    });
    list_method("toggle", [tokens_now, write_tokens](context & c, std::span<value> args) {
        std::vector<std::string> tokens = tokens_now();
        const std::string token = arg_string(c, args, 0);
        // The two-argument form FORCES a state rather than flipping it -
        // `classList.toggle("on", isOn)` is the idiom, and treating the second
        // argument as absent turns it into a flip that is right half the time.
        const bool present = std::find(tokens.begin(), tokens.end(), token) != tokens.end();
        const bool want = args.size() > 1 ? context::truthy(args[1]) : !present;
        if (want && !present) { tokens.push_back(token); }
        if (!want && present) { std::erase(tokens, token); }
        write_tokens(tokens);
        return value::boolean(want);
    });
    list_method("item", [tokens_now](context & c, std::span<value> args) {
        const std::vector<std::string> tokens = tokens_now();
        const auto i = static_cast<std::ptrdiff_t>(
            context::to_number(args.empty() ? value::undefined() : args[0]));
        if (i < 0 || static_cast<std::size_t>(i) >= tokens.size()) { return value::null(); }
        return c.string(tokens[static_cast<std::size_t>(i)]);
    });
    // An ACCESSOR, not a number: the count changes whenever the attribute does,
    // and a data property would report whatever it was when the element was
    // first wrapped.
    list->define_accessor("length",
                          value::object(cx.allocate<script::native_object>(
                              "length",
                              [tokens_now](context &, std::span<value>) {
                                  return value::number(static_cast<double>(tokens_now().size()));
                              })),
                          value::undefined());
    obj.set("classList", value::object(list));
}

rect dom_bindings::box_of(node_id id) const {
    if (fragments_ == nullptr) { return rect{}; }
    const auto find = [&](auto && self, const layout::fragment & f, float dx,
                          float dy) -> std::optional<rect> {
        const rect box{f.bounds.x + dx, f.bounds.y + dy, f.bounds.width, f.bounds.height};
        if (f.source == id) { return box; }
        for (const auto & child : f.children) {
            if (const std::optional<rect> hit = self(self, child, box.x, box.y)) { return hit; }
        }
        return std::nullopt;
    };
    return find(find, *fragments_, 0, 0).value_or(rect{});
}

void dom_bindings::install_element_methods(context & cx, script::object_object & obj) {
    const auto method = [&](std::string name, script::native_fn fn) {
        obj.set(name, value::object(cx.allocate<script::native_object>(name, std::move(fn))));
    };

    // THE `on...` HANDLER PROPERTIES, PRESENT AND NULL.
    //
    // A browser gives every element one of these per event, defaulting to null,
    // and libraries FEATURE-DETECT with `'onwheel' in element`. Assignment
    // already worked here - `el.onclick = fn` made a property - but `in`
    // answered false, because the property did not exist until something wrote
    // it. That is not a distinction a page can be expected to know about.
    //
    // IT COST A ZOOM. Babylon picks which wheel event to listen for with
    //
    //     "onwheel" in document.createElement("div") ? "wheel"
    //       : document.onmousewheel !== undefined ? "mousewheel" : "DOMMouseScroll"
    //
    // so it fell all the way through to DOMMouseScroll - a Firefox-only name
    // nothing here dispatches - and its ArcRotateCamera could be dragged and
    // not zoomed. Every listener was attached, every event was sent, and the
    // two sets had different names: the same shape as the pointerdown/mousedown
    // fault this file already records.
    //
    // EXACTLY THE EVENTS THIS ENGINE CAN DISPATCH, and no more. A handler
    // property for an event that never fires is a detection that answers yes
    // and a page that then waits forever - which is worse than answering no.
    for (const char * handler :
         {"onclick", "onwheel", "onmousedown", "onmouseup", "onmousemove", "oncontextmenu",
          "onpointerdown", "onpointerup", "onpointermove", "onkeydown", "onkeyup", "oninput",
          "onchange", "onsubmit", "ontoggle", "onfocus", "onblur", "onload", "onerror"}) {
        if (obj.find(handler) == nullptr) { obj.set(handler, value::null()); }
    }

    // `element.click()` - CLICKING WITHOUT A MOUSE.
    //
    // It was absent, and that is how p5's save() reaches the outside world:
    // downloadFile makes an <a href download>, calls click() on it, and revokes
    // the URL on the next line. So the whole export path was one missing method
    // wide, and the failure was that nothing happened - no error, no file.
    //
    // Both halves, in the right order: the event first, through the ordinary
    // capture-and-bubble dispatch, and the DEFAULT ACTION after it unless a
    // listener called preventDefault. A click() that only dispatched would leave
    // `link.click()` doing nothing and `checkbox.click()` not checking anything.
    method("click", [this](context & c, std::span<value> args) {
        (void)args;
        const node_id id = receiver(c);
        if (!id) { return value::undefined(); }
        if (!dispatch_event("click", id, make_event(c, "click", id)) && on_activate_) {
            on_activate_(id);
        }
        return value::undefined();
    });

    method("setAttribute", [this](context & c, std::span<value> args) {
        const node_id id = receiver(c);
        if (!id) { return value::undefined(); }
        (void)doc_->set_attribute(id, atoms_->intern_lower(arg_string(c, args, 0)),
                                  arg_string(c, args, 1));
        mutated();
        return value::undefined();
    });
    method("getAttribute", [this](context & c, std::span<value> args) {
        const node_id id = receiver(c);
        if (!id) { return value::null(); }
        const auto txn = doc_->read();
        // PRESENT-BUT-EMPTY is not absent. `<details open>`, `<input
        // disabled>` and `<option selected>` all have an empty value, and
        // returning null for them made every boolean attribute unreadable
        // from script - the one shape of attribute that is only ever tested
        // for presence.
        const atom name = atoms_->intern_lower(arg_string(c, args, 0));
        if (!txn.has_attribute(id, name)) { return value::null(); }
        return c.string(std::string{txn.attribute_value(id, name)});
    });
    // THE OTHER TWO HALVES OF THE ATTRIBUTE API. `setAttribute` and
    // `getAttribute` were here and these were not, so an attribute could be
    // written and read and never taken away: `el.removeAttribute('class')` was a
    // TypeError, and `el.hasAttribute('disabled')` - the correct way to ask
    // about a boolean attribute - did not exist at all, leaving `getAttribute()
    // !== null` as the only spelling and undefined behaviour for the page that
    // did not know it.
    method("removeAttribute", [this](context & c, std::span<value> args) {
        const node_id id = receiver(c);
        if (!id) { return value::undefined(); }
        (void)doc_->remove_attribute(id, atoms_->intern_lower(arg_string(c, args, 0)));
        mutated();
        return value::undefined();
    });
    method("hasAttribute", [this](context & c, std::span<value> args) {
        const node_id id = receiver(c);
        if (!id) { return value::boolean(false); }
        return value::boolean(
            doc_->read().has_attribute(id, atoms_->intern_lower(arg_string(c, args, 0))));
    });
    method("setText", [this](context & c, std::span<value> args) {
        set_text(id_or_nothing(c), arg_string(c, args, 0));
        return value::undefined();
    });
    method("getText", [this](context & c, std::span<value>) {
        const node_id id = receiver(c);
        return id ? c.string(text_of(id)) : c.string(std::string{});
    });
    method("addClass", [this](context & c, std::span<value> args) {
        edit_classes(receiver(c), arg_string(c, args, 0), true);
        return value::undefined();
    });
    method("removeClass", [this](context & c, std::span<value> args) {
        edit_classes(receiver(c), arg_string(c, args, 0), false);
        return value::undefined();
    });
    method("hasClass", [this](context & c, std::span<value> args) {
        const node_id id = receiver(c);
        if (!id) { return value::boolean(false); }
        const auto txn = doc_->read();
        const std::string want = arg_string(c, args, 0);
        for (const std::string_view cls : split(txn.attribute_value(id, atoms_->intern("class")))) {
            if (cls == want) { return value::boolean(true); }
        }
        return value::boolean(false);
    });
    // `insertAdjacentHTML(position, markup)` - a fragment parse at one of four
    // places relative to this element. The parser and the copy are the same
    // ones innerHTML uses; only where the nodes land differs.
    method("insertAdjacentHTML", [this](context & c, std::span<value> args) {
        const node_id self = receiver(c);
        if (!self || atoms_ == nullptr) { return value::undefined(); }
        std::string where = arg_string(c, args, 0);
        ascii_lower_in_place(where);
        const std::string markup = arg_string(c, args, 1);

        // Parsed into a scratch document, as innerHTML does and for the same
        // reason: tree_builder::parse replaces the root it is handed.
        document scratch{*atoms_};
        (void)parse_html(scratch, markup);
        const auto from = scratch.read();
        node_id body{};
        const auto find_body = [&](auto && walk, node_id at) -> void {
            if (!body && from.tag(at).value_or(atom{}) == atoms_->intern_lower("body")) {
                body = at;
            }
            for (const node_id child : from.children(at)) { walk(walk, child); }
        };
        find_body(find_body, from.root());
        if (!body) { return value::undefined(); }

        const auto txn = doc_->read();
        const node_id parent = txn.parent(self);
        // `beforebegin` and `afterend` need a PARENT to be inserted into, and an
        // element that has none simply ignores them - which is what a browser
        // does rather than throwing.
        for (const node_id child : from.children(body)) {
            if (where == "afterbegin") {
                // Reversed, because each new node goes in front of the last -
                // otherwise a two-node fragment arrives back to front.
                const std::span<const node_id> existing = txn.children(self);
                const node_id first = existing.empty() ? node_id{} : existing.front();
                const node_id made = copy_subtree(from, child, self);
                if (first) { (void)doc_->insert_before(self, made, first); }
            } else if (where == "beforebegin" && parent) {
                const node_id made = copy_subtree(from, child, parent);
                (void)doc_->insert_before(parent, made, self);
            } else if (where == "afterend" && parent) {
                (void)copy_subtree(from, child, parent);
            } else {
                // beforeend, and the fallback: append inside.
                (void)copy_subtree(from, child, self);
            }
        }
        mutated();
        return value::undefined();
    });

    // TREE NAVIGATION. appendChild and removeChild could already change the
    // tree; nothing could WALK it, so an element could not reach its own
    // parent. `this.elt.parentNode.removeChild(this.elt)` is the ordinary way
    // to take an element out of the page - it is how p5.js discards the default
    // canvas when a sketch calls createCanvas - and with parentNode undefined
    // the removal threw inside a callback and the discarded canvas stayed in
    // the document, laid out and painted, underneath the real one.
    method("remove", [this](context & c, std::span<value>) {
        const node_id self = receiver(c);
        if (self) {
            (void)doc_->remove_child(self);
            mutated();
        }
        return value::undefined();
    });
    method("insertBefore", [this](context & c, std::span<value> args) {
        const node_id parent = receiver(c);
        const node_id child = handle_of(arg(args, 0));
        const node_id before = handle_of(arg(args, 1));
        if (parent && child) {
            // A null reference node means "at the end", which is what makes
            // `insertBefore(node, null)` a documented spelling of appendChild.
            if (before) {
                (void)doc_->insert_before(parent, child, before);
            } else {
                (void)doc_->append_child(parent, child);
            }
            mutated();
        }
        return arg(args, 0);
    });
    // WHERE THE ELEMENT IS ON SCREEN. A page turns a pointer event's viewport
    // coordinates into coordinates within an element by subtracting this - p5's
    // getMouseInfo does exactly that to compute mouseX/mouseY - so without it
    // every mouse listener throws on its first event. The listeners were
    // installed and the events were dispatched; the conversion in between is
    // what was missing, and it made the whole input surface look absent.
    // `querySelector` ON AN ELEMENT, searching its own subtree. The document
    // had both and an element had neither, so the ordinary "find something
    // inside this" - which is what a library does with a container it owns -
    // threw. p5.js's describe() builds an offscreen tree and queries it.
    method("querySelector", [this](context & c, std::span<value> args) {
        const std::vector<node_id> found = query(arg_string(c, args, 0), receiver(c));
        return found.empty() ? value::null() : wrap(c, found.front());
    });
    method("querySelectorAll", [this](context & c, std::span<value> args) {
        const std::vector<node_id> found = query(arg_string(c, args, 0), receiver(c));
        value out = c.make_array();
        auto * items = static_cast<script::array_object *>(out.as_heap());
        for (const node_id node : found) { items->items.push_back(wrap(c, node)); }
        return out;
    });
    // `element.getElementsByTagName(tag)` - the DOCUMENT had one and an element
    // did not, so a page that scoped its search to a subtree found the method
    // missing. p5's XML module walks a parsed document with exactly this.
    method("getElementsByTagName", [this](context & c, std::span<value> args) {
        const node_id from = receiver(c);
        const std::string want = arg_string(c, args, 0);
        value out = c.make_array();
        auto * items = static_cast<script::array_object *>(out.as_heap());
        if (!from) { return out; }
        const auto txn = doc_->read();
        // `*` is every descendant, which is what a page uses to count a subtree.
        const atom tag = want == "*" ? atom{} : atoms_->intern_lower(want);
        const auto walk = [&](auto && self, node_id at, bool include) -> void {
            if (include && (want == "*" || txn.tag(at).value_or(atom{}) == tag)) {
                items->items.push_back(wrap(c, at));
            }
            for (const node_id child : txn.children(at)) { self(self, child, true); }
        };
        // DESCENDANTS ONLY - the element itself is not one of its own results.
        walk(walk, from, false);
        return out;
    });
    // `element.getElementsByClassName(names)`, scoped to this subtree and LIVE
    // for the same reason the document's is - see make_live_collection. The
    // element is not one of its own results.
    method("getElementsByClassName", [this](context & c, std::span<value> args) {
        const node_id from = receiver(c);
        const std::vector<std::string> tokens = ordered_set(arg_string(c, args, 0));
        return make_live_collection(c, [this, from, tokens] {
            return from ? all_by_class(from, tokens) : std::vector<node_id>{};
        });
    });
    method("getBoundingClientRect", [this](context & c, std::span<value>) {
        const rect box = box_of(receiver(c));
        auto * out = static_cast<script::object_object *>(c.make_object().as_heap());
        const auto set = [&](const char * name, float v) {
            out->set(name, value::number(static_cast<double>(v)));
        };
        set("x", box.x);
        set("y", box.y);
        set("left", box.x);
        set("top", box.y);
        set("width", box.width);
        set("height", box.height);
        // right and bottom are DERIVED, and pages read them directly rather
        // than adding the width themselves.
        set("right", box.x + box.width);
        set("bottom", box.y + box.height);
        return value::object(out);
    });
    method("appendChild", [this](context & c, std::span<value> args) {
        const node_id parent = receiver(c);
        const node_id child = handle_of(arg(args, 0));
        if (parent && child) {
            (void)doc_->append_child(parent, child);
            mutated();
        }
        return arg(args, 0);
    });
    method("removeChild", [this](context &, std::span<value> args) {
        const node_id child = handle_of(arg(args, 0));
        if (child) {
            (void)doc_->remove_child(child);
            mutated();
        }
        return arg(args, 0);
    });
    method("addEventListener", [this](context & c, std::span<value> args) {
        const node_id id = receiver(c);
        if (id) { add_listener(make_listener(c, path_step{id, listen_on::node}, args)); }
        return value::undefined();
    });
    // The other half. The WINDOW could remove a listener and an element could
    // not, so a page that tidied up after itself - which p5's Element does when
    // it is removed - threw instead. (The comment here used to say the document
    // could too. It could not, and that was found the same way, one corpus
    // later: see install_document.)
    // `element.dispatchEvent(event)` - the third of the three EventTarget
    // methods, and the one that was missing. addEventListener and
    // removeEventListener were here; nothing a page constructed could ever be
    // sent anywhere, so a page could only ever RECEIVE events the engine made.
    //
    // It returns whether the event was NOT cancelled, which is the opposite of
    // what the internal dispatch reports and is how a caller learns that a
    // listener refused the default action.
    method("dispatchEvent", [this](context & c, std::span<value> args) {
        const node_id id = receiver(c);
        const value event = arg(args, 0);
        if (!id || !event.is_object()) { return value::boolean(true); }
        return value::boolean(!dispatch_to(event, path_step{id, listen_on::node}));
    });
    method("removeEventListener", [this](context & c, std::span<value> args) {
        const node_id id = receiver(c);
        const std::string type = arg_string(c, args, 0);
        const value callback = arg(args, 1);
        std::erase_if(listeners_, [&](const listener & l) {
            return l.target == id && l.type == type && l.callback.bits() == callback.bits();
        });
        return value::undefined();
    });

    // --- form controls -------------------------------------------------
    method("getValue", [this](context & c, std::span<value>) {
        const node_id id = receiver(c);
        if (!id) { return c.string(std::string{}); }
        const auto txn = doc_->read();
        return c.string(forms_->state_of(txn, *atoms_, id).value);
    });
    method("setValue", [this](context & c, std::span<value> args) {
        const node_id id = receiver(c);
        if (!id) { return value::undefined(); }
        const auto txn = doc_->read();
        control_state & control = forms_->state_of(txn, *atoms_, id);
        control.value = arg_string(c, args, 0);
        control.caret = control.value.size();
        control.selection = control.caret;
        control.value_edited = true;
        mutated();
        return value::undefined();
    });
    method("isChecked", [this](context & c, std::span<value>) {
        const node_id id = receiver(c);
        if (!id) { return value::boolean(false); }
        const auto txn = doc_->read();
        return value::boolean(forms_->state_of(txn, *atoms_, id).checked);
    });
    method("setChecked", [this](context & c, std::span<value> args) {
        const node_id id = receiver(c);
        if (!id) { return value::undefined(); }
        const auto txn = doc_->read();
        forms_->state_of(txn, *atoms_, id).checked = context::truthy(arg(args, 0));
        mutated();
        return value::undefined();
    });
    method("focus", [this](context & c, std::span<value>) {
        if (const node_id id = receiver(c); id && on_focus_) { on_focus_(id); }
        return value::undefined();
    });
    method("blur", [this](context &, std::span<value>) {
        if (on_focus_) { on_focus_(node_id{}); }
        return value::undefined();
    });

    // --- canvas --------------------------------------------------------
    method("getContext", [this](context & c, std::span<value> args) {
        const node_id id = receiver(c);
        const std::string kind = arg_string(c, args, 0);
        // "2d" and "webgl". `webgl2` IS NOT IMPLEMENTED AND RETURNS NULL, and
        // getting that right took two wrong answers.
        //
        // It first threw for the whole WebGL family, on the grounds that a null
        // was the silent-wrong-answer shape: p5 would take it, fall back to its
        // 2D renderer, and a WEBGL sketch would draw nothing 3D while reporting
        // nothing. A blank canvas and a clear conscience.
        //
        // When a real context arrived, `webgl2` kept throwing - and the comment
        // here claimed the throw was what made p5 fall back to `webgl`. That was
        // backwards, and measurably so. p5's RendererGL asks for `webgl2` first
        // and relies on `getContext(...) || getContext('webgl')`, so it needs a
        // FALSY VALUE to fall through. It catches nothing, so the throw escaped
        // the constructor, escaped createCanvas, and left the sketch on the
        // Renderer2D it already had - which is precisely the outcome the throw
        // was supposed to prevent.
        //
        // Null is also simply what the specification says: an unsupported
        // context id returns null, and feature detection is BUILT on that. It is
        // a documented "not supported" signal rather than a plausible wrong
        // answer, which is the distinction the loud-failure rule turns on.
        if (kind == "webgl" || kind == "experimental-webgl") {
            if (!id) { return value::null(); }
            return webgl_context_object(c, id, 1);
        }
        // `webgl2` RETURNS A CONTEXT NOW (2026-08-02). Everything above is the
        // history of it returning null, and every word of it was right at the
        // time; what changed is that the language and the two capabilities
        // behind it exist - see docs/history/webgl2.md stages 1 to 3.
        //
        // p5's RendererGL asks for this FIRST, so from here on every p5 WEBGL
        // sketch takes a path it has never taken in this engine.
        // examples/pages/p5-webgl.html's golden is the tripwire: the same
        // sketch must produce the same pixels through either context, and if
        // that image moves, this path is wrong rather than new.
        if (kind == "webgl2") {
            if (!id) { return value::null(); }
            return webgl_context_object(c, id, 2);
        }
        if (!id || kind != "2d") { return value::null(); }
        return canvas_context_object(c, id);
    });

    // `canvas.toDataURL()` and `canvas.toBlob()` - READING A CANVAS BACK OUT.
    //
    // Both mean PNG: that is what p5's save() asks for, and encode_png writes one
    // with no compression library (see shell/image/images.hpp). A `type` argument
    // naming anything else still gets PNG rather than a lie about the format -
    // the data URL says image/png, so a page that reads it back is not misled.
    const auto canvas_bytes = [this](context & c) -> std::vector<std::byte> {
        const node_id id = receiver(c);
        if (!id || canvases_ == nullptr) { return {}; }
        // context_for, not pixels_of: a canvas nobody asked getContext of has no
        // surface yet, and a browser still gives you a transparent PNG of the
        // right size rather than nothing. An empty answer here would look like a
        // broken encoder.
        const auto number = [&](std::string_view name, int fallback) {
            const auto txn = doc_->read();
            const std::string_view text = txn.attribute_value(id, atoms_->intern(name));
            int out = 0;
            bool any = false;
            for (const char digit : text) {
                if (digit < '0' || digit > '9') { break; }
                out = out * 10 + (digit - '0');
                any = true;
            }
            return any ? out : fallback;
        };
        (void)canvases_->context_for(id, number("width", 300), number("height", 150));
        const std::shared_ptr<const paint::bitmap> pixels = canvases_->pixels_of(id);
        return pixels ? encode_png(*pixels) : std::vector<std::byte>{};
    };
    method("toDataURL", [canvas_bytes](context & c, std::span<value>) {
        const std::vector<std::byte> png = canvas_bytes(c);
        std::string binary;
        binary.reserve(png.size());
        for (const std::byte b : png) { binary += static_cast<char>(b); }
        // Through the standard library's own btoa, so ONE base64 encoder decides
        // what this means here.
        const value encoder = c.global("btoa");
        if (!encoder.is_callable()) { return c.string("data:image/png;base64,"); }
        const value text = c.string(binary);
        const value args[1] = {text};
        return c.string("data:image/png;base64," + c.to_string(c.call(encoder, args)));
    });
    method("toBlob", [this, canvas_bytes](context & c, std::span<value> args) {
        const value callback = arg(args, 0);
        if (!callback.is_callable()) { return value::undefined(); }
        std::vector<std::byte> png = canvas_bytes(c);
        auto * blob = static_cast<script::object_object *>(c.make_object().as_heap());
        value bytes = c.make_array();
        auto * out = static_cast<script::array_object *>(bytes.as_heap());
        out->elements = script::element_kind::u8;
        out->items.reserve(png.size());
        for (const std::byte b : png) {
            out->items.push_back(value::number(static_cast<double>(static_cast<unsigned char>(b))));
        }
        blob->set("size", value::number(static_cast<double>(png.size())));
        blob->set("type", c.string("image/png"));
        blob->set("__bytes", bytes);
        if (blob_prototype_.is_object()) {
            blob->prototype = blob_prototype_; // so `x instanceof Blob` is true
        }
        // QUEUED, not called: toBlob is asynchronous, and a page that wraps it in
        // a promise - which is what p5's p5.Image.toBlob does - depends on the
        // callback landing after the call returns.
        const value blob_value = value::object(blob);
        c.queue_microtask(callback, std::vector<value>{blob_value});
        return value::undefined();
    });
}

std::shared_ptr<const paint::bitmap> dom_bindings::image_argument(value v) {
    if (images_ == nullptr) { return nullptr; }
    if (v.is_number()) { return images_->at(static_cast<int>(context::to_number(v))); }
    if (!v.is_object()) { return nullptr; }
    auto * wrapper = static_cast<script::object_object *>(v.as_heap());
    const value * handle = wrapper->find(handle_property);
    if (handle == nullptr || assets_ == nullptr) { return nullptr; }
    const node_id id = unpack(static_cast<std::uint64_t>(context::to_number(*handle)));
    const auto txn = doc_->read();
    // A CANVAS IS AN IMAGE SOURCE. `drawImage(otherCanvas, ...)` is how a page
    // composites one surface onto another, and it is what p5's `image(g, ...)`
    // does with a createGraphics - so an offscreen buffer drew nothing at all,
    // silently, because a canvas has no `src` to load and this returned null.
    if (canvases_ != nullptr && atoms_->text(txn.tag(id).value_or(atom{})) == "canvas") {
        return canvases_->pixels_of(id);
    }
    const std::string_view src = txn.attribute_value(id, atoms_->intern("src"));
    return src.empty() ? nullptr : images_->load(*assets_, src);
}

// The wrapper for a node IF ONE EXISTS. Deliberately does not create one: this
// is on the dispatch path for every event, and making a wrapper per node per
// event would build the whole document's worth of them for a mousemove.
value dom_bindings::value_of_wrapper(node_id id) const {
    if (!id) { return value::undefined(); }
    const auto it = wrappers_.find(pack(id));
    return it == wrappers_.end() || it->second == nullptr ? value::undefined()
                                                          : value::object(it->second);
}

} // namespace ctbrowser::shell
