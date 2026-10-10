//===- child_views.cpp - Element tree and token-list views ------*- C++ -*-===//
//
// dom_bindings::install_element_child_views: the per-wrapper tree accessors
// (children, siblings, childNodes), the width/height reflection, classList and
// the other DOMTokenList attributes, and element.dataset.
//
//===----------------------------------------------------------------------===//

#include "../computed_style/internal.hpp"
#include "internal.hpp"

#include "view_geometry.hpp"
#include <ctbrowser/dom/token_list.hpp>
#include <ctbrowser/layout/overflow.hpp>
#include <ctbrowser/shell/page/canvas.hpp>

namespace ctbrowser::shell {

using namespace detail;

void dom_bindings::install_element_child_views(context & cx, script::object_object & obj,
                                               node_id id) {
    // --- ParentNode and NonDocumentTypeChildNode -----------------------------
    //
    // Accessors, not data properties: children change after the wrapper is
    // made, so each read walks the live tree.
    const auto element_children = [](const read_txn & txn, node_id parent) {
        std::vector<node_id> out;
        for (const node_id child : txn.children(parent)) {
            if (txn.kind(child).value_or(node_kind::text) == node_kind::element) {
                out.push_back(child);
            }
        }
        return out;
    };
    define_getter(cx, obj, "firstElementChild",
                  [this, id, element_children](context & c, std::span<value>) {
                      const auto txn = doc_->read();
                      const std::vector<node_id> kids = element_children(txn, id);
                      return kids.empty() ? value::null() : wrap(c, kids.front());
                  });
    define_getter(cx, obj, "lastElementChild",
                  [this, id, element_children](context & c, std::span<value>) {
                      const auto txn = doc_->read();
                      const std::vector<node_id> kids = element_children(txn, id);
                      return kids.empty() ? value::null() : wrap(c, kids.back());
                  });
    define_getter(cx, obj, "childElementCount",
                  [this, id, element_children](context & c, std::span<value>) {
                      (void)c;
                      const auto txn = doc_->read();
                      return value::number(static_cast<double>(element_children(txn, id).size()));
                  });
    // A sibling walk needs the parent, because the tree is stored as child
    // lists rather than sibling links: the element's position among its
    // parent's children is the only place the answer lives. A node with no
    // parent has no siblings, which is null rather than an empty walk.
    const auto sibling = [this, element_children](context & c, node_id self, bool forward,
                                                  bool elements_only) {
        const auto txn = doc_->read();
        const node_id parent = dom_parent(txn, self);
        if (!parent) { return value::null(); }
        const std::vector<node_id> kids =
            elements_only
                ? element_children(txn, parent)
                : std::vector<node_id>{txn.children(parent).begin(), txn.children(parent).end()};
        for (std::size_t i = 0; i < kids.size(); ++i) {
            if (kids[i] != self) { continue; }
            if (forward) { return i + 1 < kids.size() ? wrap(c, kids[i + 1]) : value::null(); }
            return i > 0 ? wrap(c, kids[i - 1]) : value::null();
        }
        // Not among its parent's element children: a text node asked for its
        // previousElementSibling. Walk the full child list to find where it
        // sits and then scan outward for an element.
        if (!elements_only) { return value::null(); }
        const std::span<const node_id> all = txn.children(parent);
        for (std::size_t i = 0; i < all.size(); ++i) {
            if (all[i] != self) { continue; }
            for (std::size_t step = 1; step <= all.size(); ++step) {
                const std::size_t at = forward ? i + step : i - step;
                if (forward ? at >= all.size() : step > i) { break; }
                if (txn.kind(all[at]).value_or(node_kind::text) == node_kind::element) {
                    return wrap(c, all[at]);
                }
            }
            return value::null();
        }
        return value::null();
    };
    define_getter(cx, obj, "firstChild", [this, id](context & c, std::span<value>) {
        const auto txn = doc_->read();
        const std::span<const node_id> kids = txn.children(id);
        return kids.empty() ? value::null() : wrap(c, kids.front());
    });
    define_getter(cx, obj, "lastChild", [this, id](context & c, std::span<value>) {
        const auto txn = doc_->read();
        const std::span<const node_id> kids = txn.children(id);
        return kids.empty() ? value::null() : wrap(c, kids.back());
    });
    define_getter(cx, obj, "nextSibling", [id, sibling](context & c, std::span<value>) {
        return sibling(c, id, true, false);
    });
    define_getter(cx, obj, "previousSibling", [id, sibling](context & c, std::span<value>) {
        return sibling(c, id, false, false);
    });
    define_getter(cx, obj, "nextElementSibling", [id, sibling](context & c, std::span<value>) {
        return sibling(c, id, true, true);
    });
    define_getter(cx, obj, "previousElementSibling", [id, sibling](context & c, std::span<value>) {
        return sibling(c, id, false, true);
    });
    // `childNodes` is every child, text nodes included; `children` is the
    // elements only.
    // A live NodeList, and the same one on every read (`el.childNodes ===
    // el.childNodes` is Node-childNodes.html's first assertion). It is kept on
    // the wrapper under a symbol key, which roots it and keeps it out of
    // `for...in` and getOwnPropertyNames.
    define_getter(cx, obj, "childNodes", [this, id, self = &obj](context & c, std::span<value>) {
        constexpr std::string_view key = CTBROWSER_SYMBOL_KEY("sym:ctbrowser:childNodes");
        if (const value * held = self->find(key); held != nullptr) { return *held; }
        const value list = make_live_collection(
            c,
            [this, id] {
                const auto txn = doc_->read();
                const std::span<const node_id> kids = txn.children(id);
                return std::vector<node_id>{kids.begin(), kids.end()};
            },
            "NodeList");
        self->define(key, list, script::attr_none);
        return list;
    });
    // A live HTMLCollection, not an Array (ParentNode-children.html checks
    // liveness by appending, then checks the type).
    define_getter(cx, obj, "children", [this, id](context & c, std::span<value>) {
        return make_live_collection(c, [this, id] {
            const auto txn = doc_->read();
            std::vector<node_id> found;
            for (const node_id child : txn.children(id)) {
                if (txn.tag(child).has_value()) { found.push_back(child); }
            }
            return found;
        });
    });

    // `width` and `height` are numbers, and on a <canvas> they are the size of
    // its pixel buffer rather than of its laid-out box. Assigning one resizes
    // the surface, which is what the spec means by a canvas being reset by the
    // assignment.
    //
    // An `unsigned long` reflection, HTML 2.6.9: the rules for parsing
    // non-negative integers on the way out, and on the way in ToUint32 with
    // anything past 2^31-1 writing the default, so `canvas.width = 2147483648`
    // reads back as 300.
    const auto reflect_size = [&](std::string property, long long fallback) {
        const auto read = [this, id](std::string_view name, long long missing) {
            return size_attribute(doc_->read(), id, name, missing);
        };
        obj.define_accessor(
            property,
            value::object(cx.allocate<script::native_object>(
                property,
                [property, fallback, read](context &, std::span<value>) {
                    return value::number(static_cast<double>(read(property, fallback)));
                })),
            value::object(cx.allocate<script::native_object>(
                property, [this, id, property, fallback, read](context &, std::span<value> a) {
                    long long want = number_to_uint32(arg_number(a, 0));
                    if (want > 2147483647LL) { want = fallback; }
                    (void)doc_->set_attribute(id, atoms_->intern(property), std::to_string(want));
                    // Resize the surface too, or the canvas keeps drawing into
                    // a buffer of its original size and everything past that
                    // edge is silently discarded.
                    if (canvases_ != nullptr) {
                        const int w = static_cast<int>(read("width", 300));
                        const int h = static_cast<int>(read("height", 150));
                        canvases_->resize(id, w, h);
                        // And the WebGL context over the same canvas, which held
                        // a pointer into the buffer that resize just replaced.
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
        } else if ((txn.has_attribute(id, atoms_->intern("width")) ||
                    txn.has_attribute(id, atoms_->intern("height"))) &&
                   !interface_reflects_size(tag)) {
            // Only for elements whose interface does not define width/height
            // itself (interface_reflects_size): `<td>`, `<marquee>` and
            // `<iframe>` reflect a DOMString ("50", not 50), and `<input>` an
            // unsigned long with HTML's integer rules. For the rest, e.g.
            // `<div width=50>` or `<svg width=50>`, a number beats nothing.
            reflect_size("width", 0);
            reflect_size("height", 0);
        }
    }

    // --- element.classList, and every other DOMTokenList over an attribute
    //
    // One builder, because a DOMTokenList is defined over "an associated
    // attribute" and nothing about it is specific to `class`: `blocking` below
    // is the same object over another attribute with a set of supported
    // tokens. Every operation reads the attribute, edits the token list and
    // writes it back, so nothing is cached and a token added by the parser, by
    // setAttribute or by the style engine is seen by all of them.
    const auto make_token_list = [this, &cx, id](std::string_view attribute,
                                                 std::string_view supported) {
        auto * list = cx.allocate<script::object_object>();
        if (const value proto = interface_prototype("DOMTokenList"); proto.is_object()) {
            list->prototype = proto;
            // `iterable<DOMString>`: keys/values/entries/forEach and
            // `@@iterator`, live over `length` and the indexed getter
            // (DOMTokenList-Iterable.html, -iteration.html). Installed once.
            install_iterable_declaration(cx, *static_cast<script::object_object *>(proto.as_heap()),
                                         false);
        }
        const std::string attribute_name{attribute};
        const auto attribute_now = [this, id, attribute_name] {
            const auto txn = doc_->read();
            return std::string{txn.attribute_value(id, atoms_->intern(attribute_name))};
        };
        // An ordered set, DOM 7.1: `class="a a b"` is the two tokens `a` and
        // `b`, so `length` is 2 and `item(1)` is `b`. It is what every
        // operation edits and then serialises back, which is why `add("a")` on
        // that attribute writes "a b".
        const auto tokens_now = [attribute_now] { return parse_ordered_tokens(attribute_now()); };
        // The update steps: nothing is written when there is no attribute and
        // nothing to put in one; otherwise the set, space-joined.
        const auto update = [this, id, attribute_name](const std::vector<std::string> & tokens) {
            const auto result = update_tokens(*doc_, id, atoms_->intern(attribute_name), tokens);
            if (!result || *result) { mutated(); }
        };
        // Every argument is checked before anything changes: "" is a
        // SyntaxError, a token with whitespace in it an InvalidCharacterError,
        // and `add("a", "")` must leave the attribute alone.
        //
        // `add` and `remove` check each token in turn; `replace` checks both
        // for emptiness before either for whitespace (DOM 7.1, steps 1-2),
        // so `replace(" ", "")` is a SyntaxError; hence `all_empty_first`.
        const auto report_token_error = [this](context & c, const std::string & token,
                                               token_error error) {
            if (error == token_error::empty) {
                throw_dom_exception(c, "SyntaxError",
                                    "DOMTokenList: the empty string is not a token");
            } else {
                throw_dom_exception(c, "InvalidCharacterError",
                                    "DOMTokenList: '" + token + "' contains whitespace");
            }
        };
        const auto valid_tokens = [report_token_error](context & c, std::span<value> args,
                                                       std::vector<std::string> & out,
                                                       bool all_empty_first = false) {
            for (const value & v : args) { out.push_back(c.to_string(v)); }
            for (const std::string & token : out) {
                const auto error = validate_token(token);
                if (error && (*error == token_error::empty || !all_empty_first)) {
                    report_token_error(c, token, *error);
                    return false;
                }
            }
            for (const std::string & token : out) {
                if (const auto error = validate_token(token)) {
                    report_token_error(c, token, *error);
                    return false;
                }
            }
            return true;
        };
        const auto has = [](const std::vector<std::string> & tokens, const std::string & token) {
            return std::find(tokens.begin(), tokens.end(), token) != tokens.end();
        };
        const auto change_tokens = [this, id, attribute_name, report_token_error](
                                       context & c, std::span<value> args, bool add) {
            std::vector<std::string> given;
            for (const value & v : args) { given.push_back(c.to_string(v)); }
            const auto result = (add ? add_tokens : remove_tokens)(
                *doc_, id, atoms_->intern(attribute_name), given);
            if (!result) {
                report_token_error(c, given[result.error().index], result.error().error);
                return value::undefined();
            }
            if (!*result || **result) { mutated(); }
            return value::undefined();
        };
        set_method(cx, *list, "add", [change_tokens](context & c, std::span<value> args) {
            return change_tokens(c, args, true);
        });
        set_method(cx, *list, "remove", [change_tokens](context & c, std::span<value> args) {
            return change_tokens(c, args, false);
        });
        set_method(cx, *list, "contains", [attribute_now](context & c, std::span<value> args) {
            // C++ leaves unspecified whether the attribute is read before or
            // after the argument is converted, so a `toString` on the argument
            // that edits the attribute can observe either order.
            return value::boolean(contains_token(attribute_now(), arg_string(c, args, 0)));
        });
        // `toggle(token, force)`, DOM 7.1. A no-op runs no update steps:
        // `toggle("c", false)` on `class="a a"` leaves the duplicate in place.
        set_method(
            cx, *list, "toggle",
            [this, id, attribute_name, report_token_error](context & c, std::span<value> args) {
                const std::string token = args.empty() ? "undefined" : c.to_string(args.front());
                const bool forced = args.size() > 1 && !args[1].is_undefined();
                const auto result =
                    toggle_token(*doc_, id, atoms_->intern(attribute_name), token,
                                 forced ? std::optional{context::truthy(args[1])} : std::nullopt);
                if (!result) {
                    report_token_error(c, token, result.error());
                    return value::undefined();
                }
                if (!result->update || *result->update) { mutated(); }
                return value::boolean(result->present);
            });
        // `replace(token, newToken)`: "replace within an ordered set". The
        // first of either becomes the new token and every other instance of
        // either goes, so `class="a b c"` replacing c with a is "a b".
        set_method(cx, *list, "replace",
                   [tokens_now, update, valid_tokens, has](context & c, std::span<value> args) {
                       if (args.size() < 2) {
                           c.throw_error("TypeError", "replace: 2 arguments required");
                           return value::undefined();
                       }
                       std::vector<std::string> given;
                       if (!valid_tokens(c, args.subspan(0, 2), given, true)) {
                           return value::undefined();
                       }
                       std::vector<std::string> tokens = tokens_now();
                       if (!has(tokens, given[0])) { return value::boolean(false); }
                       std::vector<std::string> replaced;
                       bool done = false;
                       for (const std::string & token : tokens) {
                           if (token != given[0] && token != given[1]) {
                               replaced.push_back(token);
                           } else if (!done) {
                               replaced.push_back(given[1]);
                               done = true;
                           }
                       }
                       update(replaced);
                       return value::boolean(true);
                   });
        set_method(cx, *list, "item", [tokens_now](context & c, std::span<value> args) {
            const std::vector<std::string> tokens = tokens_now();
            const auto i = static_cast<std::ptrdiff_t>(
                context::to_number(args.empty() ? value::undefined() : args[0]));
            if (i < 0 || static_cast<std::size_t>(i) >= tokens.size()) { return value::null(); }
            return c.string(tokens[static_cast<std::size_t>(i)]);
        });
        // `supports(token)`, DOM 7.1: a TypeError when the attribute defines no
        // supported tokens at all (as for `class`), and otherwise an ASCII
        // case-insensitive membership test.
        const std::string supported_tokens{supported};
        set_method(cx, *list, "supports", [supported_tokens](context & c, std::span<value> args) {
            if (supported_tokens.empty()) {
                c.throw_error("TypeError", "DOMTokenList has no supported tokens");
                return value::undefined();
            }
            return value::boolean(
                lists_token(supported_tokens, ascii_lower_copy(arg_string(c, args, 0))));
        });
        // `value` is the attribute, verbatim in both directions (it is what a
        // `PutForwards=value` assignment writes), and it is the stringifier.
        set_method(cx, *list, "toString", [attribute_now](context & c, std::span<value>) {
            return c.string(attribute_now());
        });
        const auto write_attribute = [this, id, attribute_name](std::string_view text) {
            (void)doc_->set_attribute(id, atoms_->intern(attribute_name), std::string{text});
            mutated();
        };
        list->define_accessor(
            "value",
            value::object(cx.allocate<script::native_object>(
                "value", [attribute_now](context & c,
                                         std::span<value>) { return c.string(attribute_now()); })),
            value::object(cx.allocate<script::native_object>(
                "value", [write_attribute](context & c, std::span<value> args) {
                    write_attribute(arg_string(c, args, 0));
                    return value::undefined();
                })));
        // An accessor, not a number: the count changes whenever the attribute
        // does, and a data property would report whatever it was when the
        // element was first wrapped.
        list->define_accessor("length",
                              value::object(cx.allocate<script::native_object>(
                                  "length",
                                  [tokens_now](context &, std::span<value>) {
                                      return value::number(
                                          static_cast<double>(tokens_now().size()));
                                  })),
                              value::undefined());
        // `classList[i]`: the indexed getter, which only a proxy can keep live.
        // Indexed keys are read-only; any other key falls through to the list.
        auto * handler = cx.allocate<script::object_object>();
        handler->set("get", value::object(cx.allocate<script::native_object>(
                                "get", [tokens_now](context & c, std::span<value> args) {
                                    if (args.size() < 2) { return value::undefined(); }
                                    const std::string key = c.to_string(args[1]);
                                    // A canonical array index: digits, at most
                                    // nine of them, and no leading zero.
                                    if (!key.empty() && key.size() < 10 &&
                                        key.find_first_not_of("0123456789") == std::string::npos &&
                                        (key == "0" || key[0] != '0')) {
                                        const std::vector<std::string> tokens = tokens_now();
                                        const std::size_t at = std::stoul(key);
                                        return at < tokens.size() ? c.string(tokens[at])
                                                                  : value::undefined();
                                    }
                                    return c.lookup_property(args[0], key);
                                })));
        // `has` too, or `Array.prototype.forEach` over the list (a HasProperty
        // per index, holes skipped) visits nothing.
        handler->set("has",
                     value::object(cx.allocate<script::native_object>(
                         "has", [tokens_now](context & c, std::span<value> args) {
                             if (args.size() < 2) { return value::boolean(false); }
                             const std::string key = c.to_string(args[1]);
                             if (!key.empty() && key.size() < 10 &&
                                 key.find_first_not_of("0123456789") == std::string::npos &&
                                 (key == "0" || key[0] != '0')) {
                                 return value::boolean(std::stoul(key) < tokens_now().size());
                             }
                             return value::boolean(c.has_property(args[0], key));
                         })));
        set_method(
            cx, *handler, "getOwnPropertyDescriptor",
            [tokens_now](context & c, std::span<value> args) {
                if (args.size() < 2) { return value::undefined(); }
                const std::string key = c.to_string(args[1]);
                context::property_descriptor own;
                if (c.own_property(args[0], key, own)) { return c.from_property_descriptor(own); }
                std::uint32_t at = 0;
                if (script::object_object::array_index_key(key, at)) {
                    const auto tokens = tokens_now();
                    return at < tokens.size()
                               ? c.from_property_descriptor(context::property_descriptor::data(
                                     c.string(tokens[at]),
                                     script::attr_enumerable | script::attr_configurable))
                               : value::undefined();
                }
                return value::undefined();
            });
        return value::object(
            cx.allocate<script::proxy_object>(value::object(list), value::object(handler)));
    };
    // `[SameObject, PutForwards=value] readonly attribute DOMTokenList
    // classList`: one list, and a write to the property forwards to its
    // `value`, so `el.classList = "a b"` sets the class attribute
    // (Element-classlist.html assigns it in its first case, from strict code,
    // and then calls `add`, `contains` and `item` on the same list).
    {
        const value list = make_token_list("class", {});
        auto * reader = cx.allocate<script::native_object>(
            "classList", [list](context &, std::span<value>) { return list; });
        // A lambda capture is not traced by the GC; `retained` keeps the list alive.
        reader->retained.push_back(list);
        auto * writer = cx.allocate<script::native_object>(
            "classList", [list](context & c, std::span<value> a) {
                c.store_property(list, "value", a.empty() ? c.string("") : a[0]);
                return value::undefined();
            });
        writer->retained.push_back(list);
        obj.define_accessor("classList", value::object(reader), value::object(writer),
                            script::attr_enumerable | script::attr_configurable);
    }

    // --- DOMTokenList attributes, HTML 2.5.7 and others
    //
    // `blocking` on link/script/style (supported token: `render`), `relList`
    // on a/area/link/form and SVG a (over `rel`), `htmlFor` on output (over
    // `for`), `sandbox` on iframe and `sizes` on link
    // (DOMTokenList-coverage-for-attributes.html). Each is a
    // [SameObject, PutForwards=value] accessor like classList.
    // `blocking` changes nothing: stylesheets and classic scripts load
    // synchronously before the first layout, so there is nothing to hold back.
    {
        const auto txn = doc_->read();
        const std::string_view tag = atoms_->text(txn.tag(id).value_or(atom{}));
        const bool html = txn.element_ns(id) == node_ns::html;
        const auto token_attribute = [&](const char * property, std::string_view attribute,
                                         std::string_view supported) {
            const value list = make_token_list(attribute, supported);
            auto * reader = cx.allocate<script::native_object>(
                property, [list](context &, std::span<value>) { return list; });
            // A lambda capture is not traced by the GC; `retained` keeps the list alive.
            reader->retained.push_back(list);
            auto * writer = cx.allocate<script::native_object>(
                property, [list](context & c, std::span<value> a) {
                    c.store_property(list, "value", a.empty() ? c.string("") : a[0]);
                    return value::undefined();
                });
            writer->retained.push_back(list);
            obj.define_accessor(property, value::object(reader), value::object(writer));
        };
        if (html && (tag == "link" || tag == "script" || tag == "style")) {
            token_attribute("blocking", "blocking", "render");
        }
        if ((html && (tag == "a" || tag == "area" || tag == "link" || tag == "form")) ||
            (txn.element_ns(id) == node_ns::svg && tag == "a")) {
            token_attribute("relList", "rel", {});
        }
        if (html && tag == "output") { token_attribute("htmlFor", "for", {}); }
        if (html && tag == "iframe") { token_attribute("sandbox", "sandbox", {}); }
        if (html && tag == "link") { token_attribute("sizes", "sizes", {}); }
    }

    // --- element.dataset
    //
    // A live DOMStringMap over the element's data-* attributes, built as a proxy
    // (see dataset.cpp) because the property set is the attribute set and a
    // page may write a key the element never had. Only HTML, SVG and MathML
    // elements have it: `document.createElementNS("test", "test").dataset` is
    // undefined (dataset.html).
    {
        const auto txn = doc_->read();
        const std::string ns = namespace_of(id);
        const bool wanted =
            txn.kind(id).value_or(node_kind::text) == node_kind::element &&
            (ns == xhtml_namespace || ns == svg_namespace || ns == mathml_namespace);
        if (wanted) { install_dataset(cx, obj, id); }
    }
}

} // namespace ctbrowser::shell
