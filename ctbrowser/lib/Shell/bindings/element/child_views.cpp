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
    // THE ELEMENT-ONLY HALF OF THE TREE, which this wrapper had none of. Every
    // one of these is a one-assertion test file in `dom/nodes`, and there are
    // eight of them: Element-firstElementChild, -lastElementChild,
    // -nextElementSibling, -previousElementSibling, -childElementCount and
    // three -childElementCount-dynamic-* variants, each reporting `undefined`
    // where a node or a count belongs.
    //
    // ACCESSORS, like `parentNode` above and for the same reason: the three
    // dynamic tests add and remove children and read the count again, so a
    // value captured at wrapping time is wrong by construction.
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
    // A SIBLING WALK NEEDS THE PARENT, because the tree is stored as a child
    // list rather than as sibling links: the element's position among its
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
        // NOT AMONG ITS PARENT'S ELEMENT CHILDREN: a text node asked for its
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
    // `childNodes` is EVERY child, text nodes included; `children` is the
    // elements only. Both exist because they answer different questions, and a
    // page that wants the text nodes has no other way to reach them.
    // A LIVE NodeList, and THE SAME ONE on every read - `el.childNodes ===
    // el.childNodes` is Node-childNodes.html's first assertion. It is kept on
    // the wrapper under a symbol key, which is what roots it and what keeps it
    // out of `for...in` and getOwnPropertyNames.
    define_getter(cx, obj, "childNodes", [this, id, self = &obj](context & c, std::span<value>) {
        constexpr std::string_view key = "@@sym:ctbrowser:childNodes";
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
    // AN HTMLCollection, LIVE - not an Array. `children` is the one of these
    // navigations the DOM gives an interface to, and `ParentNode-children.html`
    // checks liveness by appending and then asks what the thing IS.
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
    // its PIXEL BUFFER rather than of its laid-out box - `canvas.width / 2` is
    // the first line of most canvas pages. Assigning one resizes the surface,
    // which is what the spec means by a canvas being reset by the assignment.
    //
    // An `unsigned long` reflection, HTML 2.6.9: the rules for parsing
    // non-negative integers on the way out, and on the way in ToUint32 with
    // anything past 2^31-1 writing the default - which is why `canvas.width =
    // 2147483648` reads back as 300.
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
                    long long want = to_uint32(arg_number(a, 0));
                    if (want > 2147483647LL) { want = fallback; }
                    (void)doc_->set_attribute(id, atoms_->intern(property), std::to_string(want));
                    // The SURFACE follows, or the canvas keeps drawing into a
                    // buffer of the size it was created at and everything past
                    // that edge is silently discarded.
                    if (canvases_ != nullptr) {
                        const int w = static_cast<int>(read("width", 300));
                        const int h = static_cast<int>(read("height", 150));
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
            // Readonly, as every [SameObject] attribute here is: a page that
            // assigns to `input.files` must not be able to put a string where
            // the next `for (const f of input.files)` looks.
            obj.define("files", files, script::attr_enumerable | script::attr_configurable);
        } else if ((txn.has_attribute(id, atoms_->intern("width")) ||
                    txn.has_attribute(id, atoms_->intern("height"))) &&
                   !interface_reflects_size(tag)) {
            // AND NOT WHERE THE TABLE HAS A ROW. `<td width=50>`, `<marquee
            // width=50>` and `<iframe width=50>` reflect a DOMString - "50",
            // not 50 - and `<input width=50>` an unsigned long with HTML's
            // integer rules rather than the digit loop above. This branch is
            // what is left: an element whose interface says nothing about
            // width, `<div width=50>` and `<svg width=50>` among them, where a
            // number is better than nothing at all.
            reflect_size("width", 0);
            reflect_size("height", 0);
        }
    }

    // --- element.classList, and every other DOMTokenList over an attribute
    //
    // ONE BUILDER, because a DOMTokenList is defined over "an associated
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
            // (DOMTokenList-Iterable.html, -iteration.html). Once.
            install_iterable_declaration(cx, *static_cast<script::object_object *>(proto.as_heap()),
                                         false);
        }
        const std::string attribute_name{attribute};
        const auto attribute_now = [this, id, attribute_name] {
            const auto txn = doc_->read();
            return std::string{txn.attribute_value(id, atoms_->intern(attribute_name))};
        };
        // AN ORDERED SET, DOM 7.1: `class="a a b"` is the two tokens `a` and
        // `b`, so `length` is 2 and `item(1)` is `b` - and it is what every
        // operation edits and then serialises back, which is why `add("a")` on
        // that attribute writes "a b".
        const auto tokens_now = [attribute_now] { return parse_ordered_tokens(attribute_now()); };
        // THE UPDATE STEPS: nothing is written when there is no attribute and
        // nothing to put in one, otherwise the set, space-joined.
        const auto update = [this, id, attribute_name](const std::vector<std::string> & tokens) {
            const auto result = update_tokens(*doc_, id, atoms_->intern(attribute_name), tokens);
            if (!result || *result) { mutated(); }
        };
        // EVERY ARGUMENT IS CHECKED BEFORE ANYTHING CHANGES: "" is a
        // SyntaxError, a token with whitespace in it an InvalidCharacterError,
        // and `add("a", "")` must leave the attribute alone.
        //
        // `add` and `remove` check each token in turn; `replace` checks BOTH
        // for emptiness before either for whitespace (DOM 7.1, steps 1-2),
        // so `replace(" ", "")` is a SyntaxError - hence `all_empty_first`.
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
            // Preserve the existing snapshot/coercion argument evaluation order.
            return value::boolean(contains_token(attribute_now(), arg_string(c, args, 0)));
        });
        // `toggle(token, force)`, DOM 7.1 - and a no-op runs NO update steps:
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
        // `replace(token, newToken)`: "replace within an ordered set" - the
        // FIRST of either becomes the new token and every other instance of
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
        // supported tokens at all - which is `class` - and otherwise an ASCII
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
        // `value` IS the attribute, verbatim in both directions - it is what a
        // `PutForwards=value` assignment writes - and it is the stringifier.
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
        // An ACCESSOR, not a number: the count changes whenever the attribute
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
        // `classList[i]` - the indexed getter, which only a proxy can keep live.
        // The same shape as make_live_collection's, and only `get`: an index is
        // read-only and everything else falls through to the list itself.
        auto * handler = cx.allocate<script::object_object>();
        handler->set("get", value::object(cx.allocate<script::native_object>(
                                "get", [tokens_now](context & c, std::span<value> args) {
                                    if (args.size() < 2) { return value::undefined(); }
                                    const std::string key = c.to_string(args[1]);
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
        // `has` too, or `Array.prototype.forEach` over the list - a HasProperty
        // per index, holes skipped - visits nothing.
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
        return value::object(
            cx.allocate<script::proxy_object>(value::object(list), value::object(handler)));
    };
    // `[SameObject, PutForwards=value] readonly attribute DOMTokenList
    // classList`: ONE list, and a write to the property forwards to its
    // `value` - `el.classList = "a b"` sets the class attribute, which
    // `Element-classlist.html` assigns in its first case (a readonly data
    // property threw there from strict code) and then calls `add`, `contains`
    // and `item` on the list it still expects to find.
    {
        const value list = make_token_list("class", {});
        auto * reader = cx.allocate<script::native_object>(
            "classList", [list](context &, std::span<value>) { return list; });
        // A capture is not a GC edge - see the note on `attributes` above.
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

    // --- element.blocking, HTML 2.5.7 "blocking attributes"
    //
    // `[SameObject, PutForwards=value] readonly attribute DOMTokenList
    // blocking` on HTMLLinkElement, HTMLScriptElement and HTMLStyleElement -
    // the same list as above over `blocking`, whose one supported token is
    // `render`. An ACCESSOR rather than the readonly data property `classList`
    // is, because a write to it has a meaning: `el.blocking = 'render'`
    // forwards to `value` and sets the attribute, which is how
    // `html/dom/render-blocking` marks a script-inserted element.
    //
    // THE ATTRIBUTE ONLY. This engine loads a stylesheet and a classic script
    // synchronously from the asset registry while the page is being built, so
    // every sheet and script has applied before the first frame is laid out,
    // and there is nothing left for `render` to hold back - the ordering the
    // attribute asks for is the only one the engine has.
    //
    // ...AND THE OTHER DOMTokenList ATTRIBUTES HTML REFLECTS THE SAME WAY:
    // `relList` on a/area/link/form (over `rel`), `htmlFor` on output (over
    // `for`), `sandbox` on iframe, `sizes` on link
    // (DOMTokenList-coverage-for-attributes.html).
    {
        const auto txn = doc_->read();
        const std::string_view tag = atoms_->text(txn.tag(id).value_or(atom{}));
        const bool html = txn.element_ns(id) == node_ns::html;
        const auto token_attribute = [&](const char * property, std::string_view attribute,
                                         std::string_view supported) {
            const value list = make_token_list(attribute, supported);
            auto * reader = cx.allocate<script::native_object>(
                property, [list](context &, std::span<value>) { return list; });
            // A capture is not a GC edge - see the note on `attributes` above.
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
    // A live DOMStringMap over this element's `data-*` attributes, and it did
    // not exist at all: `el.dataset.foo` was a TypeError on the first line of
    // every page that uses the ordinary way of hanging state off an element.
    //
    // A PROXY, because the set of properties IS the set of attributes and a
    // page may write a key this element has never carried. What that costs is
    // said here rather than left to be discovered: this VM implements the
    // `get`, `set` and `has` traps and no others, so
    //
    //   * `delete el.dataset.foo` is a silent no-op - `op::delete_prop` skips
    //     anything that is not exactly a plain object, and
    //     `dataset-delete.html` is what measures it.
    //
    // That is a deviation in `lib/Script` and that is where it is fixable. The
    // alternative shape - a plain object refilled on every read, as
    // `attributes` above is - trades it for a `set` that never reaches the
    // document at all, which is the worse half of the trade: a write that
    // silently does nothing is a wrong answer.
    //
    // THE OTHER TWO ARE FIXED HERE, both without a new trap. Enumeration walks
    // the proxy's TARGET, so the target is refilled with the element's data-*
    // names each time `dataset` is read - which is why it is an accessor rather
    // than a property. And `instanceof` follows a proxy to its target and walks
    // THAT object's prototype, so hanging DOMStringMap.prototype off the target
    // answers `el.dataset instanceof DOMStringMap` without the VM knowing what
    // a proxy's prototype would be.
    //
    // NOT ON EVERY ELEMENT. `dataset` belongs to HTMLElement, SVGElement and
    // MathMLElement, and `document.createElementNS("test", "test").dataset` is
    // `undefined` - which `dataset.html` asserts by name, and which is the only
    // reason this is conditional rather than unconditional.
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
