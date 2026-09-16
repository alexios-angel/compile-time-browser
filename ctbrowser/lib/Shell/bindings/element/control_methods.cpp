// dom_bindings - click, form control value and focus, the canvas methods
// getContext, toDataURL and toBlob, and the table model (rows, cells, sections).

#include "internal.hpp"

namespace ctbrowser::shell {

using namespace detail;

// `click`, `focus` and `blur` are HTMLElement's (focus and blur SVGElement's
// too); the canvas three are HTMLCanvasElement's. The EventTarget trio -
// addEventListener, removeEventListener, dispatchEvent - is NOT here: it lives
// on EventTarget.prototype (bindings/events/interfaces.cpp), which every node
// chains to, and `step_of` there already resolves a wrapper to its node.
void dom_bindings::install_control_methods(context & cx) {
    const std::initializer_list<const char *> html = {"HTMLElement"};
    const std::initializer_list<const char *> focusable = {"HTMLElement", "SVGElement"};
    const std::initializer_list<const char *> canvas = {"HTMLCanvasElement"};
    const auto method = [&](std::initializer_list<const char *> on, const char * name,
                            unsigned length, script::native_fn fn) {
        define_operation(cx, on, name, length, std::move(fn));
    };

    // `element.click()` - the whole of it is in dom_bindings::click, beside the
    // engine's own mouse events, because it IS one of those.
    method(html, "click", 0, [this](context & c, std::span<value>) {
        (void)click(receiver(c));
        return value::undefined();
    });

    // --- form controls -------------------------------------------------
    method(html, "getValue", 0, [this](context & c, std::span<value>) {
        const node_id id = receiver(c);
        if (!id) { return c.string(std::string{}); }
        const auto txn = doc_->read();
        return c.string(forms_->state_of(txn, *atoms_, id).value);
    });
    method(html, "setValue", 1, [this](context & c, std::span<value> args) {
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
    method(html, "isChecked", 0, [this](context & c, std::span<value>) {
        const node_id id = receiver(c);
        if (!id) { return value::boolean(false); }
        const auto txn = doc_->read();
        return value::boolean(forms_->state_of(txn, *atoms_, id).checked);
    });
    method(html, "setChecked", 1, [this](context & c, std::span<value> args) {
        const node_id id = receiver(c);
        if (!id) { return value::undefined(); }
        const auto txn = doc_->read();
        forms_->state_of(txn, *atoms_, id).checked = context::truthy(arg(args, 0));
        mutated();
        return value::undefined();
    });
    method(focusable, "focus", 0, [this](context & c, std::span<value>) {
        if (const node_id id = receiver(c); id && on_focus_) { on_focus_(id); }
        return value::undefined();
    });
    method(focusable, "blur", 0, [this](context &, std::span<value>) {
        if (on_focus_) { on_focus_(node_id{}); }
        return value::undefined();
    });

    // --- canvas --------------------------------------------------------
    method(canvas, "getContext", 1, [this](context & c, std::span<value> args) {
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
        const auto txn = doc_->read();
        (void)canvases_->context_for(id, static_cast<int>(size_attribute(txn, id, "width", 300)),
                                     static_cast<int>(size_attribute(txn, id, "height", 150)));
        const std::shared_ptr<const paint::bitmap> pixels = canvases_->pixels_of(id);
        return pixels ? encode_png(*pixels) : std::vector<std::byte>{};
    };
    method(canvas, "toDataURL", 0, [canvas_bytes](context & c, std::span<value>) {
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
    method(canvas, "toBlob", 1, [this, canvas_bytes](context & c, std::span<value> args) {
        const value callback = arg(args, 0);
        if (!callback.is_callable()) { return value::undefined(); }
        const std::vector<std::byte> png = canvas_bytes(c);
        // QUEUED, not called: toBlob is asynchronous, and a page that wraps it in
        // a promise - which is what p5's p5.Image.toBlob does - depends on the
        // callback landing after the call returns.
        const value blob_value = make_blob(c, make_u8_array(c, png), "image/png");
        c.queue_microtask(callback, std::vector<value>{blob_value});
        return value::undefined();
    });

    // --- THE TABLE MODEL, HTML 4.9.1, 4.9.5, 4.9.8 and 4.9.9 ----------------
    //
    // `table.rows`, `tBodies`, `caption`/`tHead`/`tFoot` and the create/delete
    // pair for each, `insertRow`/`deleteRow` on the table and on a section,
    // `tr.cells`/`rowIndex`/`sectionRowIndex`/`insertCell`/`deleteCell` and
    // `cell.cellIndex`. The collections are LIVE - `table.rows` after a
    // `deleteRow` is the shorter list - and every index rule is the
    // specification's: -1 means "the end", one past the end is an
    // IndexSizeError for an insert and the end itself is one for a delete.
    // getElementsByClassName-20..25.htm reach their cells through it.
    const auto proto = [this](const char * which) {
        return prototype_object(interface_prototype(which));
    };
    const auto operation = [&](const char * which, const char * name, unsigned length,
                               script::native_fn fn) {
        define_operation(cx, {which}, name, length, std::move(fn));
    };
    const auto getter = [&](const char * which, const char * name, script::native_fn get,
                            script::native_fn set = nullptr) {
        auto * on = proto(which);
        if (on == nullptr) { return; }
        on->define_accessor(
            name, value::object(cx.allocate<script::native_object>(name, std::move(get))),
            set == nullptr
                ? value::undefined()
                : value::object(cx.allocate<script::native_object>(name, std::move(set))));
    };
    const auto is = [](const read_txn & txn, node_id id, std::string_view name) {
        return id && txn.element_ns(id) == node_ns::html && txn.local_name(id) == name;
    };
    // The first child called `name`, or none.
    const auto first_child = [this, is](node_id parent, std::string_view name) {
        const auto txn = doc_->read();
        for (const node_id child : txn.children(parent)) {
            if (is(txn, child, name)) { return child; }
        }
        return node_id{};
    };
    // A section's rows: its `tr` children. A table's: the `thead` children's
    // rows, then its own `tr` children and its `tbody` children's rows in
    // tree order, then the `tfoot` children's.
    const auto section_rows = [this, is](node_id section) {
        std::vector<node_id> rows;
        const auto txn = doc_->read();
        for (const node_id child : txn.children(section)) {
            if (is(txn, child, "tr")) { rows.push_back(child); }
        }
        return rows;
    };
    const auto table_rows = [this, is](node_id table) {
        std::vector<node_id> rows;
        const auto txn = doc_->read();
        const auto add_rows_of = [&](node_id section) {
            for (const node_id row : txn.children(section)) {
                if (is(txn, row, "tr")) { rows.push_back(row); }
            }
        };
        for (const node_id child : txn.children(table)) {
            if (is(txn, child, "thead")) { add_rows_of(child); }
        }
        for (const node_id child : txn.children(table)) {
            if (is(txn, child, "tr")) { rows.push_back(child); }
            if (is(txn, child, "tbody")) { add_rows_of(child); }
        }
        for (const node_id child : txn.children(table)) {
            if (is(txn, child, "tfoot")) { add_rows_of(child); }
        }
        return rows;
    };
    const auto row_cells = [this, is](node_id row) {
        std::vector<node_id> cells;
        const auto txn = doc_->read();
        for (const node_id child : txn.children(row)) {
            if (is(txn, child, "td") || is(txn, child, "th")) { cells.push_back(child); }
        }
        return cells;
    };
    const auto make = [this](std::string_view name) {
        return doc_->create_element(atoms_->intern(name));
    };
    // WebIDL `long`: ToInt32 of whatever was passed, -1 when absent.
    const auto index_arg = [](context & c, std::span<value> a) {
        if (a.empty()) { return -1LL; }
        const long long unsigned_value = to_uint32(c.to_number_value(a[0]));
        return unsigned_value >= 2147483648LL ? unsigned_value - 4294967296LL : unsigned_value;
    };
    // "insert a row/cell at index" over a list: -1 or the length appends,
    // anything else goes before the index-th member; out of range is an
    // IndexSizeError, thrown here.
    const auto insert_at = [this, index_arg](context & c, std::span<value> a, node_id parent,
                                             const std::vector<node_id> & members,
                                             std::string_view where, node_id made) -> value {
        const long long index = index_arg(c, a);
        const auto count = static_cast<long long>(members.size());
        if (index < -1 || index > count) {
            throw_dom_exception(c, "IndexSizeError",
                                std::string{where} + ": index " + std::to_string(index) +
                                    " is out of range");
            return value::undefined();
        }
        const node_id before =
            index == -1 || index == count ? node_id{} : members[static_cast<std::size_t>(index)];
        (void)insert_node(parent, made, before);
        return wrap(c, made);
    };
    const auto delete_at = [this, index_arg](context & c, std::span<value> a,
                                             const std::vector<node_id> & members,
                                             std::string_view where) {
        const long long index = index_arg(c, a);
        const auto count = static_cast<long long>(members.size());
        if (index < -1 || index >= count) {
            throw_dom_exception(c, "IndexSizeError",
                                std::string{where} + ": index " + std::to_string(index) +
                                    " is out of range");
            return value::undefined();
        }
        if (index == -1 && count == 0) { return value::undefined(); }
        (void)doc_->remove_child(
            members[static_cast<std::size_t>(index == -1 ? count - 1 : index)]);
        mutated();
        return value::undefined();
    };
    const auto remove_first = [this, first_child](node_id table, std::string_view name) {
        if (const node_id had = first_child(table, name)) {
            (void)doc_->remove_child(had);
            mutated();
        }
    };
    // Where a `thead` goes: before the first child that is neither a caption
    // nor a colgroup, else at the end.
    const auto insert_head = [this, is](node_id table, node_id head) {
        node_id before;
        {
            const auto txn = doc_->read();
            for (const node_id child : txn.children(table)) {
                if (txn.tag(child).has_value() && !is(txn, child, "caption") &&
                    !is(txn, child, "colgroup")) {
                    before = child;
                    break;
                }
            }
        }
        (void)insert_node(table, head, before);
    };

    // --- HTMLTableElement ----------------------------------------------------
    const char * table = "HTMLTableElement";
    getter(table, "rows", [this, table_rows](context & c, std::span<value>) {
        const node_id self = receiver(c);
        return make_live_collection(c, [table_rows, self] { return table_rows(self); });
    });
    getter(table, "tBodies", [this, is](context & c, std::span<value>) {
        const node_id self = receiver(c);
        return make_live_collection(c, [this, is, self] {
            std::vector<node_id> bodies;
            const auto txn = doc_->read();
            for (const node_id child : txn.children(self)) {
                if (is(txn, child, "tbody")) { bodies.push_back(child); }
            }
            return bodies;
        });
    });
    // caption, tHead and tFoot: the first such child, or null. Setting one
    // removes the old and puts the new where the specification says - a
    // caption first, a thead before the body, a tfoot last - and a value of
    // the wrong kind is a HierarchyRequestError (a TypeError for the caption,
    // whose IDL type is not nullable-of-anything-else).
    const auto part = [&](const char * idl, const char * name, auto place) {
        getter(
            table, idl,
            [this, first_child, name](context & c, std::span<value>) {
                const node_id had = first_child(receiver(c), name);
                return had ? wrap(c, had) : value::null();
            },
            [this, remove_first, name, idl, place](context & c, std::span<value> a) {
                const node_id self = receiver(c);
                if (!self) { return value::undefined(); }
                const value given = arg(a, 0);
                const node_id made = handle_of(given);
                if (!given.is_nullish()) {
                    const auto txn = doc_->read();
                    if (!made || txn.element_ns(made) != node_ns::html ||
                        txn.local_name(made) != name) {
                        if (std::string_view{idl} == "caption") {
                            c.throw_error("TypeError", "Failed to set the 'caption' property on "
                                                       "'HTMLTableElement': the value is not of "
                                                       "type 'HTMLTableCaptionElement'.");
                        } else {
                            throw_dom_exception(c, "HierarchyRequestError",
                                                std::string{idl} + " must be a <" + name + ">");
                        }
                        return value::undefined();
                    }
                }
                remove_first(self, name);
                if (made) { place(self, made); }
                return value::undefined();
            });
    };
    part("caption", "caption", [this](node_id self, node_id made) {
        const node_id first = [&] {
            const auto txn = doc_->read();
            const std::span<const node_id> kids = txn.children(self);
            return kids.empty() ? node_id{} : kids.front();
        }();
        (void)insert_node(self, made, first);
    });
    part("tHead", "thead", insert_head);
    part("tFoot", "tfoot",
         [this](node_id self, node_id made) { (void)insert_node(self, made, node_id{}); });
    // createX answers the existing one or makes it; deleteX removes it.
    operation(table, "createCaption", 0, [this, first_child, make](context & c, std::span<value>) {
        const node_id self = receiver(c);
        if (!self) { return value::null(); }
        if (const node_id had = first_child(self, "caption")) { return wrap(c, had); }
        const node_id made = make("caption");
        node_id first;
        {
            const auto txn = doc_->read();
            const std::span<const node_id> kids = txn.children(self);
            first = kids.empty() ? node_id{} : kids.front();
        }
        (void)insert_node(self, made, first);
        return wrap(c, made);
    });
    operation(table, "deleteCaption", 0, [this, remove_first](context & c, std::span<value>) {
        if (const node_id self = receiver(c)) { remove_first(self, "caption"); }
        return value::undefined();
    });
    operation(table, "createTHead", 0,
              [this, first_child, make, insert_head](context & c, std::span<value>) {
                  const node_id self = receiver(c);
                  if (!self) { return value::null(); }
                  if (const node_id had = first_child(self, "thead")) { return wrap(c, had); }
                  const node_id made = make("thead");
                  insert_head(self, made);
                  return wrap(c, made);
              });
    operation(table, "deleteTHead", 0, [this, remove_first](context & c, std::span<value>) {
        if (const node_id self = receiver(c)) { remove_first(self, "thead"); }
        return value::undefined();
    });
    operation(table, "createTFoot", 0, [this, first_child, make](context & c, std::span<value>) {
        const node_id self = receiver(c);
        if (!self) { return value::null(); }
        if (const node_id had = first_child(self, "tfoot")) { return wrap(c, had); }
        const node_id made = make("tfoot");
        (void)insert_node(self, made, node_id{});
        return wrap(c, made);
    });
    operation(table, "deleteTFoot", 0, [this, remove_first](context & c, std::span<value>) {
        if (const node_id self = receiver(c)) { remove_first(self, "tfoot"); }
        return value::undefined();
    });
    // A new tbody goes after the last tbody child, else at the end.
    operation(table, "createTBody", 0, [this, is, make](context & c, std::span<value>) {
        const node_id self = receiver(c);
        if (!self) { return value::null(); }
        const node_id made = make("tbody");
        node_id before;
        {
            const auto txn = doc_->read();
            const std::span<const node_id> kids = txn.children(self);
            for (std::size_t i = kids.size(); i-- > 0;) {
                if (is(txn, kids[i], "tbody")) {
                    before = i + 1 < kids.size() ? kids[i + 1] : node_id{};
                    break;
                }
            }
        }
        (void)insert_node(self, made, before);
        return wrap(c, made);
    });
    // insertRow on the TABLE: with no rows and no tbody a tbody is made to
    // hold the row; with no rows the last tbody takes it; an append goes to
    // the parent of the last row; anything else goes before the index-th row.
    operation(table, "insertRow", 0,
              [this, is, table_rows, make, insert_at, index_arg](context & c, std::span<value> a) {
                  const node_id self = receiver(c);
                  if (!self) { return value::null(); }
                  const std::vector<node_id> rows = table_rows(self);
                  const long long index = index_arg(c, a);
                  if (index < -1 || index > static_cast<long long>(rows.size())) {
                      throw_dom_exception(c, "IndexSizeError",
                                          "insertRow: index " + std::to_string(index) +
                                              " is out of range");
                      return value::undefined();
                  }
                  if (rows.empty()) {
                      node_id last_body;
                      {
                          const auto txn = doc_->read();
                          for (const node_id child : txn.children(self)) {
                              if (is(txn, child, "tbody")) { last_body = child; }
                          }
                      }
                      if (!last_body) {
                          last_body = make("tbody");
                          (void)insert_node(self, last_body, node_id{});
                      }
                      const node_id row = make("tr");
                      (void)insert_node(last_body, row, node_id{});
                      return wrap(c, row);
                  }
                  const node_id parent = doc_->read().parent(
                      index == -1 || index == static_cast<long long>(rows.size())
                          ? rows.back()
                          : rows[static_cast<std::size_t>(index)]);
                  return insert_at(c, a, parent, rows, "insertRow", make("tr"));
              });
    operation(table, "deleteRow", 1,
              [this, table_rows, delete_at](context & c, std::span<value> a) {
                  const node_id self = receiver(c);
                  if (!self) { return value::undefined(); }
                  return delete_at(c, a, table_rows(self), "deleteRow");
              });

    // --- HTMLTableSectionElement ---------------------------------------------
    const char * section = "HTMLTableSectionElement";
    getter(section, "rows", [this, section_rows](context & c, std::span<value>) {
        const node_id self = receiver(c);
        return make_live_collection(c, [section_rows, self] { return section_rows(self); });
    });
    operation(section, "insertRow", 0,
              [this, section_rows, make, insert_at](context & c, std::span<value> a) {
                  const node_id self = receiver(c);
                  if (!self) { return value::null(); }
                  return insert_at(c, a, self, section_rows(self), "insertRow", make("tr"));
              });
    operation(section, "deleteRow", 1,
              [this, section_rows, delete_at](context & c, std::span<value> a) {
                  const node_id self = receiver(c);
                  if (!self) { return value::undefined(); }
                  return delete_at(c, a, section_rows(self), "deleteRow");
              });

    // --- HTMLTableRowElement -------------------------------------------------
    const char * row = "HTMLTableRowElement";
    getter(row, "cells", [this, row_cells](context & c, std::span<value>) {
        const node_id self = receiver(c);
        return make_live_collection(c, [row_cells, self] { return row_cells(self); });
    });
    // rowIndex: this row's place in the rows of the table it is in - as a
    // child of the table or of a section child of it - else -1.
    // sectionRowIndex: its place among its parent's rows, the parent being a
    // table or a section, else -1.
    const auto position = [](const std::vector<node_id> & among, node_id self) {
        for (std::size_t i = 0; i < among.size(); ++i) {
            if (among[i] == self) { return value::number(static_cast<double>(i)); }
        }
        return value::number(-1);
    };
    getter(row, "rowIndex", [this, is, table_rows, position](context & c, std::span<value>) {
        const node_id self = receiver(c);
        if (!self) { return value::number(-1); }
        node_id table_node;
        {
            const auto txn = doc_->read();
            node_id parent = txn.parent(self);
            if (parent && (is(txn, parent, "thead") || is(txn, parent, "tbody") ||
                           is(txn, parent, "tfoot"))) {
                parent = txn.parent(parent);
            }
            if (is(txn, parent, "table")) { table_node = parent; }
        }
        return table_node ? position(table_rows(table_node), self) : value::number(-1);
    });
    getter(row, "sectionRowIndex",
           [this, is, table_rows, section_rows, position](context & c, std::span<value>) {
               const node_id self = receiver(c);
               if (!self) { return value::number(-1); }
               node_id parent;
               bool parent_is_table = false;
               {
                   const auto txn = doc_->read();
                   const node_id up = txn.parent(self);
                   parent_is_table = is(txn, up, "table");
                   if (parent_is_table || is(txn, up, "thead") || is(txn, up, "tbody") ||
                       is(txn, up, "tfoot")) {
                       parent = up;
                   }
               }
               if (!parent) { return value::number(-1); }
               return position(parent_is_table ? table_rows(parent) : section_rows(parent), self);
           });
    operation(row, "insertCell", 0,
              [this, row_cells, make, insert_at](context & c, std::span<value> a) {
                  const node_id self = receiver(c);
                  if (!self) { return value::null(); }
                  return insert_at(c, a, self, row_cells(self), "insertCell", make("td"));
              });
    operation(row, "deleteCell", 1, [this, row_cells, delete_at](context & c, std::span<value> a) {
        const node_id self = receiver(c);
        if (!self) { return value::undefined(); }
        return delete_at(c, a, row_cells(self), "deleteCell");
    });

    // --- HTMLTableCellElement ------------------------------------------------
    getter("HTMLTableCellElement", "cellIndex",
           [this, is, row_cells, position](context & c, std::span<value>) {
               const node_id self = receiver(c);
               if (!self) { return value::number(-1); }
               node_id parent;
               {
                   const auto txn = doc_->read();
                   if (const node_id up = txn.parent(self); is(txn, up, "tr")) { parent = up; }
               }
               return parent ? position(row_cells(parent), self) : value::number(-1);
           });
}

} // namespace ctbrowser::shell
