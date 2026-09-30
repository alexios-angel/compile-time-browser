#include "helpers.hpp"

namespace ctbrowser::shell {
using namespace detail;
using namespace input_types;

void dom_bindings::install_control_select_options(context & cx) {
    const control_helpers helpers{this};
    // --- the select and option model, HTML 4.10.7 and 4.10.10 -----------------
    //
    // An option's SELECTEDNESS is the private selected_slot on its wrapper when
    // a script or the select has set it, else its `selected` attribute; its
    // DIRTINESS is dirty_slot. The select's list of options is its
    // option descendants (customizable select), skipping a nested select's.
    // The engine paints and submits the select from its store value, so every
    // change of selectedness here is written through to that value - and a
    // store value the chrome or the shadowing `value` setter changed is read
    // back into selectedness when the two disagree (`reconcile`).
    // HTML 4.10.7 "list of options" - the store's walk, so the value the
    // chrome paints and the options this model selects agree.

    // An option's "nearest ancestor select" (HTML 4.10.10) -
    // control_helpers::select_of, which form_writes.cpp asks too.
    const auto select_of = [helpers](const read_txn & txn, node_id option) {
        return helpers.select_of(txn, option);
    };

    // The display size: the `size` attribute, else 4 for multiple and 1
    // otherwise.

    // Every option's selectedness, with the store reconciled - see above.
    // `__syncedValue` on the select is the store value this model last
    // wrote; a store value that differs from it was written by the chrome or
    // the shadowing `value` setter since, and is read back into the slots.

    // The store written from the model, and "ask for a reset" -
    // control_helpers::sync_store and ask_for_reset, which the attribute change
    // steps (form_writes.cpp) run too.
    const auto sync_store = [helpers](context & c, const read_txn & txn, dom_bindings * b,
                                      node_id select, const std::vector<node_id> & options,
                                      const std::vector<bool> & flags) {
        helpers.sync_store(c, txn, b, select, options, flags);
    };
    const auto ask_for_reset = [helpers](context & c, dom_bindings * b, node_id select) {
        helpers.ask_for_reset(c, b, select);
    };
    const auto set_selected = [helpers, select_of, ask_for_reset](context & c, dom_bindings * b,
                                                                  node_id option, bool on,
                                                                  bool dirty) {
        helpers.set_slot(c, b, option, selected_slot, value::boolean(on));
        if (dirty) { helpers.set_slot(c, b, option, dirty_slot, value::boolean(true)); }
        const node_id select = [&] {
            const auto txn = b->doc_->read();
            return select_of(txn, option);
        }();
        if (!select) { return; }
        {
            const auto txn = b->doc_->read();
            if (on && !helpers.is_multiple(txn, b, select)) {
                // Every other option off - through the slots, not the flags,
                // so the store is not consulted against a half-made state.
                for (const node_id other : helpers.options_of(txn, b, select)) {
                    if (other != option && helpers.option_selected(c, txn, b, other)) {
                        helpers.set_slot(c, b, other, selected_slot, value::boolean(false));
                    }
                }
                control_state & held = b->forms_->state_of(txn, *b->atoms_, select);
                held.value = helpers.option_value(txn, b, option);
                helpers.set_slot(c, b, select, "__syncedValue", c.string(held.value));
            }
        }
        ask_for_reset(c, b, select);
        b->mutated();
    };
    const auto selected_index = [helpers](context & c, dom_bindings * b, node_id select) {
        const auto txn = b->doc_->read();
        std::vector<node_id> options;
        const std::vector<bool> flags = helpers.selected_flags(c, txn, b, select, options);
        for (std::size_t i = 0; i < flags.size(); ++i) {
            if (flags[i]) { return static_cast<long long>(i); }
        }
        return -1LL;
    };
    const auto set_selected_index = [helpers, sync_store](context & c, dom_bindings * b,
                                                          node_id select, long long index) {
        const auto txn = b->doc_->read();
        std::vector<node_id> options;
        std::vector<bool> flags = helpers.selected_flags(c, txn, b, select, options);
        for (std::size_t i = 0; i < options.size(); ++i) {
            flags[i] = static_cast<long long>(i) == index;
            helpers.set_slot(c, b, options[i], selected_slot, value::boolean(flags[i]));
            if (flags[i]) { helpers.set_slot(c, b, options[i], dirty_slot, value::boolean(true)); }
        }
        sync_store(c, txn, b, select, options, flags);
        b->mutated();
    };

    // `select.add(element, before)` and HTMLOptionsCollection.add: an option
    // or optgroup, before an element or an index, or at the end.
    const auto select_add = [this, helpers, ask_for_reset](context & c, dom_bindings * b,
                                                           node_id select, std::span<value> a) {
        const node_id element = b->handle_of(arg(a, 0));
        {
            const auto txn = b->doc_->read();
            if (!element ||
                !(helpers.is(txn, element, "option") || helpers.is(txn, element, "optgroup"))) {
                c.throw_error("TypeError", "add: the element is not an option or optgroup");
                return;
            }
            for (node_id up = select; up; up = txn.parent(up)) {
                if (up == element) {
                    throw_dom_exception(c, "HierarchyRequestError",
                                        "add: the element is an ancestor of the select");
                    return;
                }
            }
        }
        const value before_value = arg(a, 1);
        node_id parent = select;
        node_id before;
        if (before_value.is_object_like()) {
            before = b->handle_of(before_value);
            const auto txn = b->doc_->read();
            bool inside = false;
            for (node_id up = before; up; up = txn.parent(up)) {
                if (up == select) { inside = true; }
            }
            if (!before || !inside) {
                throw_dom_exception(c, "NotFoundError", "add: `before` is not in the select");
                return;
            }
            parent = txn.parent(before);
        } else if (!before_value.is_nullish()) {
            const long long index = static_cast<long long>(context::to_int32(before_value));
            const auto txn = b->doc_->read();
            std::vector<node_id> options;
            const auto walk = [&](auto && self, node_id at2) -> void {
                for (const node_id child : txn.children(at2)) {
                    if (helpers.is(txn, child, "option")) { options.push_back(child); }
                    if (helpers.is(txn, child, "select")) { continue; }
                    self(self, child);
                }
            };
            walk(walk, select);
            if (index >= 0 && static_cast<std::size_t>(index) < options.size()) {
                before = options[static_cast<std::size_t>(index)];
                parent = txn.parent(before);
            }
        }
        if (element == before) { return; }
        (void)b->insert_node(parent, element, before);
        // The insertion steps have run (form_writes.cpp: a selected option
        // arriving is the one selected); the selectedness setting algorithm
        // then selects the first enabled option of a select left with none.
        ask_for_reset(c, b, select);
    };

    // --- `Option`, the legacy factory --------------------------------------------
    if (!secondary_) {
        auto * option_ctor = cx.allocate<script::native_object>(
            "Option", [this, helpers](context & c, std::span<value> a) -> value {
                const value made = create_html_element(c, "option");
                const node_id id = handle_of(made);
                if (!id) { return made; }
                if (const value text = arg(a, 0); !text.is_undefined()) {
                    const std::string data = c.to_string(text);
                    if (!data.empty()) {
                        (void)insert_node(id, doc_->create_text(data), node_id{});
                    }
                }
                if (const value given = arg(a, 1); !given.is_undefined()) {
                    (void)doc_->set_attribute(id, atoms_->intern("value"), c.to_string(given));
                }
                const bool default_selected = context::truthy(arg(a, 2));
                if (default_selected) {
                    (void)doc_->set_attribute(id, atoms_->intern("selected"), "");
                }
                // The attribute's change steps first (form_writes.cpp: a clean
                // option's selectedness follows it), THEN the constructor's
                // own step: the selectedness IS `selected` - false when it is
                // absent, even with defaultSelected - and the dirtiness is
                // left alone.
                // Held in the slot only where it differs from what the
                // attribute alone would say.
                mutated();
                if (const bool selected = context::truthy(arg(a, 3));
                    selected != default_selected) {
                    helpers.set_slot(c, this, id, selected_slot, value::boolean(selected));
                }
                return made;
            });
        if (const value proto_value = interface_prototype("HTMLOptionElement");
            proto_value.is_object()) {
            option_ctor->define("prototype", proto_value, script::attr_none);
        }
        option_ctor->define("length", value::number(0), script::attr_configurable);
        cx.define_builtin_global("Option", value::object(option_ctor));
    }

    // --- HTMLOptionElement --------------------------------------------------------
    helpers.accessor(
        cx, "HTMLOptionElement", "selected",
        [helpers, select_of](context & c, std::span<value>) {
            const auto where = helpers.at(c);
            dom_bindings * b = where.first;
            const node_id id = where.second;
            if (!id) { return value::boolean(false); }
            const auto txn = b->doc_->read();
            // THROUGH THE SELECT'S RECONCILIATION FIRST: `select.value = x`
            // writes the store, and the option slots learn of it when the
            // flags are next computed - which a read of ONE option's
            // selectedness must do too, or `slt.value = "2"` left
            // `slt.options[1].selected` false (reset-form.html).
            // AND THE FLAGS ARE THE ANSWER: an untouched display-size-1
            // select selects its first enabled option without a slot saying
            // so (select-validity.html reads the placeholder's `selected`).
            if (const node_id select = select_of(txn, id)) {
                std::vector<node_id> options;
                const std::vector<bool> flags = helpers.selected_flags(c, txn, b, select, options);
                for (std::size_t i = 0; i < options.size(); ++i) {
                    if (options[i] == id) { return value::boolean(flags[i]); }
                }
            }
            return value::boolean(helpers.option_selected(c, txn, b, id));
        },
        [helpers, set_selected](context & c, std::span<value> a) {
            const auto where = helpers.at(c);
            dom_bindings * b = where.first;
            const node_id id = where.second;
            if (id) { set_selected(c, b, id, context::truthy(arg(a, 0)), true); }
            return value::undefined();
        });
    helpers.accessor(
        cx, "HTMLOptionElement", "index", [helpers, select_of](context & c, std::span<value>) {
            const auto where = helpers.at(c);
            dom_bindings * b = where.first;
            const node_id id = where.second;
            if (!id) { return value::number(0); }
            const auto txn = b->doc_->read();
            const node_id select = select_of(txn, id);
            if (!select) { return value::number(0); }
            const std::vector<node_id> options = helpers.options_of(txn, b, select);
            const auto here = std::ranges::find(options, id);
            return value::number(
                here == options.end() ? 0 : static_cast<double>(here - options.begin()));
        });
    helpers.accessor(cx, "HTMLOptionElement", "form",
                     [helpers, select_of](context & c, std::span<value>) {
                         const auto where = helpers.at(c);
                         dom_bindings * b = where.first;
                         const node_id id = where.second;
                         if (!id) { return value::null(); }
                         node_id form;
                         {
                             const auto txn = b->doc_->read();
                             if (const node_id select = select_of(txn, id)) {
                                 form = helpers.form_owner(txn, b, select);
                             }
                         }
                         return form ? b->wrap(c, form) : value::null();
                     });
    // `text`: the descendant text with any <script> and <svg:script> left
    // out, stripped and collapsed; set replaces the children with one Text.
    helpers.accessor(
        cx, "HTMLOptionElement", "text",
        [helpers](context & c, std::span<value>) {
            const auto where = helpers.at(c);
            dom_bindings * b = where.first;
            const node_id id = where.second;
            if (!id) { return c.string(""); }
            std::string text;
            {
                const auto txn = b->doc_->read();
                const auto walk = [&](auto && self, node_id at2) -> void {
                    if (txn.local_name(at2) == "script" && (txn.element_ns(at2) == node_ns::html ||
                                                            txn.element_ns(at2) == node_ns::svg)) {
                        return;
                    }
                    if (txn.kind(at2).value_or(node_kind::element) == node_kind::text ||
                        txn.kind(at2).value_or(node_kind::element) == node_kind::cdata_section) {
                        text += txn.text(at2);
                    }
                    for (const node_id child : txn.children(at2)) { self(self, child); }
                };
                walk(walk, id);
            }
            return c.string(collapse_whitespace(text, html_whitespace));
        },
        [helpers](context & c, std::span<value> a) {
            const auto where = helpers.at(c);
            dom_bindings * b = where.first;
            const node_id id = where.second;
            if (id) { b->set_text(id, arg_string(c, a, 0)); }
            return value::undefined();
        });

    // --- HTMLSelectElement --------------------------------------------------------
    helpers.accessor(cx, "HTMLSelectElement", "type", [helpers](context & c, std::span<value>) {
        const auto where = helpers.at(c);
        dom_bindings * b = where.first;
        const node_id id = where.second;
        if (!id) { return c.string("select-one"); }
        const auto txn = b->doc_->read();
        return c.string(helpers.is_multiple(txn, b, id) ? "select-multiple" : "select-one");
    });
    helpers.accessor(
        cx, "HTMLSelectElement", "selectedIndex",
        [helpers, selected_index](context & c, std::span<value>) {
            const auto where = helpers.at(c);
            dom_bindings * b = where.first;
            const node_id id = where.second;
            return value::number(id ? static_cast<double>(selected_index(c, b, id)) : -1);
        },
        [helpers, set_selected_index](context & c, std::span<value> a) {
            const auto where = helpers.at(c);
            dom_bindings * b = where.first;
            const node_id id = where.second;
            if (id) { set_selected_index(c, b, id, context::to_int32(arg(a, 0))); }
            return value::undefined();
        });
    // `options`, one HTMLOptionsCollection per select, live over its options,
    // with `selectedIndex`, `add`, `remove` and a settable `length` of its own.
    const auto options_collection = [helpers, ask_for_reset](context & c, dom_bindings * b,
                                                             node_id select) {
        const value held = helpers.slot_of(c, b, select, "__options");
        if (held.is_object_like()) { return held; }
        const value list = b->make_live_collection(
            c,
            [b, helpers, select] {
                const auto txn = b->doc_->read();
                return txn.contains(select) ? helpers.options_of(txn, b, select)
                                            : std::vector<node_id>{};
            },
            "HTMLOptionsCollection");
        // THE INDEXED SETTER (HTML 4.10.10.1), which a plain collection does
        // not have: `options[i] = option` replaces the i-th option, or - past
        // the end - pads with blank options and appends; `= null` removes the
        // i-th. Any other key is the collection's own `set` trap's.
        if (list.is_kind(script::heap_kind::proxy)) {
            auto & handler = static_cast<script::proxy_object *>(list.as_heap())->handler;
            auto * traps = handler.is_object()
                               ? static_cast<script::object_object *>(handler.as_heap())
                               : nullptr;
            const value * plain = traps == nullptr ? nullptr : traps->find("set");
            if (plain != nullptr && plain->is_callable()) {
                const value fallback = *plain;
                auto * trap = c.allocate<script::native_object>(
                    "set",
                    [b, select, helpers, ask_for_reset, fallback,
                     handler](context & c2, std::span<value> args) -> value {
                        const std::string key = args.size() > 1 ? c2.to_string(args[1]) : "";
                        const bool index_key =
                            !key.empty() && key.size() <= 10 &&
                            std::ranges::all_of(key,
                                                [](char ch) { return ch >= '0' && ch <= '9'; }) &&
                            (key == "0" || key.front() != '0') && std::stoull(key) < 4294967295ULL;
                        if (args.size() < 3 || !index_key) {
                            return c2.call(fallback, args, handler);
                        }
                        const std::size_t index = std::stoull(key);
                        const value given = args[2];
                        const node_id option = given.is_null() ? node_id{} : b->handle_of(given);
                        std::vector<node_id> options;
                        {
                            const auto txn = b->doc_->read();
                            if (!given.is_null() && !helpers.is(txn, option, "option")) {
                                c2.throw_error("TypeError", "Failed to set an indexed property on "
                                                            "'HTMLOptionsCollection': the value "
                                                            "is not an HTMLOptionElement.");
                                return value::boolean(false);
                            }
                            options = helpers.options_of(txn, b, select);
                        }
                        if (!option) {
                            if (index < options.size()) {
                                (void)b->doc_->remove_child(options[index]);
                                ask_for_reset(c2, b, select);
                                b->mutated();
                            }
                            return value::boolean(true);
                        }
                        if (index >= options.size()) {
                            for (std::size_t i = options.size(); i < index && i < 100000; ++i) {
                                (void)b->insert_node(
                                    select, b->doc_->create_element(b->atoms_->intern("option")),
                                    node_id{});
                            }
                            (void)b->insert_node(select, option, node_id{});
                        } else if (options[index] != option) {
                            const node_id old = options[index];
                            const node_id parent = b->doc_->read().parent(old);
                            (void)b->insert_node(parent, option, old);
                            (void)b->doc_->remove_child(old);
                            b->mutated();
                        }
                        ask_for_reset(c2, b, select);
                        return value::boolean(true);
                    });
                trap->retained.push_back(fallback);
                trap->retained.push_back(handler);
                traps->set("set", value::object(trap));
            }
        }
        c.store_property(list, CTBROWSER_SYMBOL_KEY("sym:ctbrowser:options-owner"),
                         b->wrap(c, select));
        helpers.set_slot(c, b, select, "__options", list);
        return list;
    };
    helpers.accessor(cx, "HTMLSelectElement", "options",
                     [helpers, options_collection](context & c, std::span<value>) {
                         const auto where = helpers.at(c);
                         dom_bindings * b = where.first;
                         const node_id id = where.second;
                         return id ? options_collection(c, b, id) : value::null();
                     });
    helpers.accessor(cx, "HTMLSelectElement", "selectedOptions",
                     [helpers](context & c, std::span<value>) {
                         const auto where = helpers.at(c);
                         dom_bindings * b = where.first;
                         const node_id id = where.second;
                         if (!id) { return value::null(); }
                         // [SameObject]: one live collection per select. It
                         // re-reads its members when the document's version
                         // moves, and a selectedness slot that changes moves
                         // it (control_helpers::set_slot).
                         if (const value held = helpers.slot_of(c, b, id, "__selectedOptions");
                             held.is_object_like()) {
                             return held;
                         }
                         const value list = b->make_live_collection(c, [b, id, helpers] {
                             const auto txn = b->doc_->read();
                             std::vector<node_id> options;
                             std::vector<node_id> out;
                             if (!txn.contains(id) || b->cx_ == nullptr) { return out; }
                             const std::vector<bool> flags =
                                 helpers.selected_flags(*b->cx_, txn, b, id, options);
                             for (std::size_t i = 0; i < options.size(); ++i) {
                                 if (flags[i]) { out.push_back(options[i]); }
                             }
                             return out;
                         });
                         helpers.set_slot(c, b, id, "__selectedOptions", list);
                         return list;
                     });
    // `length`: the number of options; setting it appends blank options or
    // removes the surplus from the end.
    const auto set_option_count = [helpers](context & c, dom_bindings * b, node_id select,
                                            double wanted) {
        if (!(wanted >= 0)) { return; }
        std::vector<node_id> options;
        {
            const auto txn = b->doc_->read();
            options = helpers.options_of(txn, b, select);
        }
        const auto count = static_cast<double>(options.size());
        if (wanted > count) {
            if (wanted - count > 100000) { return; }
            for (double i = count; i < wanted; ++i) {
                (void)b->insert_node(select, b->doc_->create_element(b->atoms_->intern("option")),
                                     node_id{});
            }
        } else {
            for (std::size_t i = static_cast<std::size_t>(wanted); i < options.size(); ++i) {
                (void)b->doc_->remove_child(options[i]);
            }
            b->mutated();
        }
        (void)c;
    };
    helpers.accessor(
        cx, "HTMLSelectElement", "length",
        [helpers](context & c, std::span<value>) {
            const auto where = helpers.at(c);
            dom_bindings * b = where.first;
            const node_id id = where.second;
            if (!id) { return value::number(0); }
            const auto txn = b->doc_->read();
            return value::number(static_cast<double>(helpers.options_of(txn, b, id).size()));
        },
        [helpers, set_option_count](context & c, std::span<value> a) {
            const auto where = helpers.at(c);
            dom_bindings * b = where.first;
            const node_id id = where.second;
            if (id) {
                set_option_count(c, b, id, static_cast<double>(context::to_uint32(arg(a, 0))));
            }
            return value::undefined();
        });
    helpers.operation(cx, "HTMLSelectElement", "item", 1,
                      [this, helpers](context & c, std::span<value> a) {
                          const node_id id = receiver(c);
                          if (!id) { return value::null(); }
                          std::vector<node_id> options;
                          {
                              const auto txn = doc_->read();
                              options = helpers.options_of(txn, this, id);
                          }
                          const double index = static_cast<double>(context::to_uint32(arg(a, 0)));
                          if (index >= static_cast<double>(options.size())) {
                              return value::null();
                          }
                          return wrap(c, options[static_cast<std::size_t>(index)]);
                      });
    helpers.operation(
        cx, "HTMLSelectElement", "namedItem", 1, [this, helpers](context & c, std::span<value> a) {
            const node_id id = receiver(c);
            if (!id) { return value::null(); }
            const std::string want = arg_string(c, a, 0);
            node_id found;
            {
                const auto txn = doc_->read();
                const atom id_attr = atoms_->intern("id");
                const atom name_attr = atoms_->intern("name");
                for (const node_id option : helpers.options_of(txn, this, id)) {
                    if (!want.empty() && (txn.attribute_value(option, id_attr) == want ||
                                          txn.attribute_value(option, name_attr) == want)) {
                        found = option;
                        break;
                    }
                }
            }
            return found ? wrap(c, found) : value::null();
        });
    helpers.operation(cx, "HTMLSelectElement", "add", 1,
                      [this, select_add](context & c, std::span<value> a) {
                          if (const node_id id = receiver(c)) { select_add(c, this, id, a); }
                          return value::undefined();
                      });
    // `remove()` with no argument is ChildNode's; with an index it removes
    // that option.
    helpers.operation(cx, "HTMLSelectElement", "remove", 0,
                      [this, helpers, ask_for_reset](context & c, std::span<value> a) {
                          const node_id id = receiver(c);
                          if (!id) { return value::undefined(); }
                          if (a.empty()) {
                              if (id == doc_->root() && secondary_) {
                                  doc_->remove_document_element();
                              } else {
                                  (void)doc_->remove_child(id);
                              }
                              mutated();
                              return value::undefined();
                          }
                          const long long index = context::to_int32(a[0]);
                          std::vector<node_id> options;
                          {
                              const auto txn = doc_->read();
                              options = helpers.options_of(txn, this, id);
                          }
                          if (index >= 0 && static_cast<std::size_t>(index) < options.size()) {
                              (void)doc_->remove_child(options[static_cast<std::size_t>(index)]);
                              ask_for_reset(c, this, id); // the removal steps run it too
                              mutated();
                          }
                          return value::undefined();
                      });

    // --- HTMLOptionsCollection's own members ----------------------------------------
    if (!secondary_) {
        if (auto * on = helpers.proto("HTMLOptionsCollection")) {
            const auto owner_select = [this](context & c) -> std::pair<dom_bindings *, node_id> {
                const value select = c.lookup_property(
                    c.current_this(), CTBROWSER_SYMBOL_KEY("sym:ctbrowser:options-owner"));
                dom_bindings * b = owner_of(select);
                if (b == nullptr) {
                    return {nullptr, node_id{}};
                }
                return {b, b->handle_of(select)};
            };
            on->define_accessor(
                "selectedIndex",
                native(cx, "selectedIndex",
                       [owner_select, selected_index](context & c, std::span<value>) {
                           const auto where = owner_select(c);
                           dom_bindings * b = where.first;
                           const node_id id = where.second;
                           return value::number(id ? static_cast<double>(selected_index(c, b, id))
                                                   : -1);
                       }),
                native(cx, "selectedIndex",
                       [owner_select, set_selected_index](context & c, std::span<value> a) {
                           const auto where = owner_select(c);
                           dom_bindings * b = where.first;
                           const node_id id = where.second;
                           if (id) { set_selected_index(c, b, id, context::to_int32(arg(a, 0))); }
                           return value::undefined();
                       }));
            // `length` is the HTMLCollection's getter and this setter.
            on->define_accessor(
                "length",
                native(cx, "length",
                       [owner_select, helpers](context & c, std::span<value>) {
                           const auto where = owner_select(c);
                           dom_bindings * b = where.first;
                           const node_id id = where.second;
                           if (!id) { return value::number(0); }
                           const auto txn = b->doc_->read();
                           return value::number(
                               static_cast<double>(helpers.options_of(txn, b, id).size()));
                       }),
                native(cx, "length",
                       [owner_select, set_option_count](context & c, std::span<value> a) {
                           const auto where = owner_select(c);
                           dom_bindings * b = where.first;
                           const node_id id = where.second;
                           if (id) {
                               set_option_count(c, b, id,
                                                static_cast<double>(context::to_uint32(arg(a, 0))));
                           }
                           return value::undefined();
                       }));
            set_method(
                cx, *on, "add",
                [owner_select, select_add](context & c, std::span<value> a) {
                    const auto where = owner_select(c);
                    dom_bindings * b = where.first;
                    const node_id id = where.second;
                    if (id) { select_add(c, b, id, a); }
                    return value::undefined();
                },
                script::attr_builtin);
            set_method(
                cx, *on, "remove",
                [owner_select, helpers, ask_for_reset](context & c, std::span<value> a) {
                    const auto where = owner_select(c);
                    dom_bindings * b = where.first;
                    const node_id id = where.second;
                    if (!id) { return value::undefined(); }
                    const long long index = context::to_int32(arg(a, 0));
                    std::vector<node_id> options;
                    {
                        const auto txn = b->doc_->read();
                        options = helpers.options_of(txn, b, id);
                    }
                    if (index >= 0 && static_cast<std::size_t>(index) < options.size()) {
                        (void)b->doc_->remove_child(options[static_cast<std::size_t>(index)]);
                        ask_for_reset(c, b, id);
                        b->mutated();
                    }
                    return value::undefined();
                },
                script::attr_builtin);
        }
    }
}

} // namespace ctbrowser::shell
