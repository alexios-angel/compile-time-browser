#include "helpers.hpp"

namespace ctbrowser::shell {
using namespace detail;
using namespace input_types;

void dom_bindings::install_control_tables(context & cx) {
    const control_helpers helpers{this};
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

    // The first child called `name`, or none.

    // A section's rows: its `tr` children. A table's: the `thead` children's
    // rows, then its own `tr` children and its `tbody` children's rows in
    // tree order, then the `tfoot` children's.
    const auto section_rows = [this, helpers](node_id section) {
        std::vector<node_id> rows;
        const auto txn = doc_->read();
        for (const node_id child : txn.children(section)) {
            if (helpers.is(txn, child, "tr")) { rows.push_back(child); }
        }
        return rows;
    };
    const auto table_rows = [this, helpers](node_id table) {
        std::vector<node_id> rows;
        const auto txn = doc_->read();
        const auto add_rows_of = [&](node_id section) {
            for (const node_id row : txn.children(section)) {
                if (helpers.is(txn, row, "tr")) { rows.push_back(row); }
            }
        };
        for (const node_id child : txn.children(table)) {
            if (helpers.is(txn, child, "thead")) { add_rows_of(child); }
        }
        for (const node_id child : txn.children(table)) {
            if (helpers.is(txn, child, "tr")) { rows.push_back(child); }
            if (helpers.is(txn, child, "tbody")) { add_rows_of(child); }
        }
        for (const node_id child : txn.children(table)) {
            if (helpers.is(txn, child, "tfoot")) { add_rows_of(child); }
        }
        return rows;
    };
    const auto row_cells = [this, helpers](node_id row) {
        std::vector<node_id> cells;
        const auto txn = doc_->read();
        for (const node_id child : txn.children(row)) {
            if (helpers.is(txn, child, "td") || helpers.is(txn, child, "th")) {
                cells.push_back(child);
            }
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
    const auto remove_first = [this, helpers](node_id table, std::string_view name) {
        if (const node_id had = helpers.first_child(table, name)) {
            (void)doc_->remove_child(had);
            mutated();
        }
    };
    // Where a `thead` goes: before the first child that is neither a caption
    // nor a colgroup, else at the end.
    const auto insert_head = [this, helpers](node_id table, node_id head) {
        node_id before;
        {
            const auto txn = doc_->read();
            for (const node_id child : txn.children(table)) {
                if (txn.tag(child).has_value() && !helpers.is(txn, child, "caption") &&
                    !helpers.is(txn, child, "colgroup")) {
                    before = child;
                    break;
                }
            }
        }
        (void)insert_node(table, head, before);
    };

    // --- HTMLTableElement ----------------------------------------------------
    const char * table = "HTMLTableElement";
    helpers.getter(cx, table, "rows", [this, table_rows](context & c, std::span<value>) {
        const node_id self = receiver(c);
        return make_live_collection(c, [table_rows, self] { return table_rows(self); });
    });
    helpers.getter(cx, table, "tBodies", [this, helpers](context & c, std::span<value>) {
        const node_id self = receiver(c);
        return make_live_collection(c, [this, helpers, self] {
            std::vector<node_id> bodies;
            const auto txn = doc_->read();
            for (const node_id child : txn.children(self)) {
                if (helpers.is(txn, child, "tbody")) { bodies.push_back(child); }
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
        helpers.getter(
            cx, table, idl,
            [this, helpers, name](context & c, std::span<value>) {
                const node_id had = helpers.first_child(receiver(c), name);
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
    helpers.operation(cx, table, "createCaption", 0,
                      [this, helpers, make](context & c, std::span<value>) {
                          const node_id self = receiver(c);
                          if (!self) { return value::null(); }
                          if (const node_id had = helpers.first_child(self, "caption")) {
                              return wrap(c, had);
                          }
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
    helpers.operation(cx, table, "deleteCaption", 0,
                      [this, remove_first](context & c, std::span<value>) {
                          if (const node_id self = receiver(c)) { remove_first(self, "caption"); }
                          return value::undefined();
                      });
    helpers.operation(cx, table, "createTHead", 0,
                      [this, helpers, make, insert_head](context & c, std::span<value>) {
                          const node_id self = receiver(c);
                          if (!self) { return value::null(); }
                          if (const node_id had = helpers.first_child(self, "thead")) {
                              return wrap(c, had);
                          }
                          const node_id made = make("thead");
                          insert_head(self, made);
                          return wrap(c, made);
                      });
    helpers.operation(cx, table, "deleteTHead", 0,
                      [this, remove_first](context & c, std::span<value>) {
                          if (const node_id self = receiver(c)) { remove_first(self, "thead"); }
                          return value::undefined();
                      });
    helpers.operation(cx, table, "createTFoot", 0,
                      [this, helpers, make](context & c, std::span<value>) {
                          const node_id self = receiver(c);
                          if (!self) { return value::null(); }
                          if (const node_id had = helpers.first_child(self, "tfoot")) {
                              return wrap(c, had);
                          }
                          const node_id made = make("tfoot");
                          (void)insert_node(self, made, node_id{});
                          return wrap(c, made);
                      });
    helpers.operation(cx, table, "deleteTFoot", 0,
                      [this, remove_first](context & c, std::span<value>) {
                          if (const node_id self = receiver(c)) { remove_first(self, "tfoot"); }
                          return value::undefined();
                      });
    // A new tbody goes after the last tbody child, else at the end.
    helpers.operation(cx, table, "createTBody", 0,
                      [this, helpers, make](context & c, std::span<value>) {
                          const node_id self = receiver(c);
                          if (!self) { return value::null(); }
                          const node_id made = make("tbody");
                          node_id before;
                          {
                              const auto txn = doc_->read();
                              const std::span<const node_id> kids = txn.children(self);
                              for (std::size_t i = kids.size(); i-- > 0;) {
                                  if (helpers.is(txn, kids[i], "tbody")) {
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
    helpers.operation(
        cx, table, "insertRow", 0,
        [this, helpers, table_rows, make, insert_at, index_arg](context & c, std::span<value> a) {
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
                        if (helpers.is(txn, child, "tbody")) { last_body = child; }
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
            const node_id parent =
                doc_->read().parent(index == -1 || index == static_cast<long long>(rows.size())
                                        ? rows.back()
                                        : rows[static_cast<std::size_t>(index)]);
            return insert_at(c, a, parent, rows, "insertRow", make("tr"));
        });
    helpers.operation(cx, table, "deleteRow", 1,
                      [this, table_rows, delete_at](context & c, std::span<value> a) {
                          const node_id self = receiver(c);
                          if (!self) { return value::undefined(); }
                          return delete_at(c, a, table_rows(self), "deleteRow");
                      });

    // --- HTMLTableSectionElement ---------------------------------------------
    const char * section = "HTMLTableSectionElement";
    helpers.getter(cx, section, "rows", [this, section_rows](context & c, std::span<value>) {
        const node_id self = receiver(c);
        return make_live_collection(c, [section_rows, self] { return section_rows(self); });
    });
    helpers.operation(cx, section, "insertRow", 0,
                      [this, section_rows, make, insert_at](context & c, std::span<value> a) {
                          const node_id self = receiver(c);
                          if (!self) { return value::null(); }
                          return insert_at(c, a, self, section_rows(self), "insertRow", make("tr"));
                      });
    helpers.operation(cx, section, "deleteRow", 1,
                      [this, section_rows, delete_at](context & c, std::span<value> a) {
                          const node_id self = receiver(c);
                          if (!self) { return value::undefined(); }
                          return delete_at(c, a, section_rows(self), "deleteRow");
                      });

    // --- HTMLTableRowElement -------------------------------------------------
    const char * row = "HTMLTableRowElement";
    helpers.getter(cx, row, "cells", [this, row_cells](context & c, std::span<value>) {
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
    helpers.getter(
        cx, row, "rowIndex", [this, helpers, table_rows, position](context & c, std::span<value>) {
            const node_id self = receiver(c);
            if (!self) { return value::number(-1); }
            node_id table_node;
            {
                const auto txn = doc_->read();
                node_id parent = txn.parent(self);
                if (parent &&
                    (helpers.is(txn, parent, "thead") || helpers.is(txn, parent, "tbody") ||
                     helpers.is(txn, parent, "tfoot"))) {
                    parent = txn.parent(parent);
                }
                if (helpers.is(txn, parent, "table")) { table_node = parent; }
            }
            return table_node ? position(table_rows(table_node), self) : value::number(-1);
        });
    helpers.getter(
        cx, row, "sectionRowIndex",
        [this, helpers, table_rows, section_rows, position](context & c, std::span<value>) {
            const node_id self = receiver(c);
            if (!self) { return value::number(-1); }
            node_id parent;
            bool parent_is_table = false;
            {
                const auto txn = doc_->read();
                const node_id up = txn.parent(self);
                parent_is_table = helpers.is(txn, up, "table");
                if (parent_is_table || helpers.is(txn, up, "thead") ||
                    helpers.is(txn, up, "tbody") || helpers.is(txn, up, "tfoot")) {
                    parent = up;
                }
            }
            if (!parent) { return value::number(-1); }
            return position(parent_is_table ? table_rows(parent) : section_rows(parent), self);
        });
    helpers.operation(cx, row, "insertCell", 0,
                      [this, row_cells, make, insert_at](context & c, std::span<value> a) {
                          const node_id self = receiver(c);
                          if (!self) { return value::null(); }
                          return insert_at(c, a, self, row_cells(self), "insertCell", make("td"));
                      });
    helpers.operation(cx, row, "deleteCell", 1,
                      [this, row_cells, delete_at](context & c, std::span<value> a) {
                          const node_id self = receiver(c);
                          if (!self) { return value::undefined(); }
                          return delete_at(c, a, row_cells(self), "deleteCell");
                      });

    // --- HTMLTableCellElement ------------------------------------------------
    helpers.getter(cx, "HTMLTableCellElement", "cellIndex",
                   [this, helpers, row_cells, position](context & c, std::span<value>) {
                       const node_id self = receiver(c);
                       if (!self) { return value::number(-1); }
                       node_id parent;
                       {
                           const auto txn = doc_->read();
                           if (const node_id up = txn.parent(self); helpers.is(txn, up, "tr")) {
                               parent = up;
                           }
                       }
                       return parent ? position(row_cells(parent), self) : value::number(-1);
                   });

    // =========================================================================
    // THE FORMS, HTML 4.10: the form owner, `form.elements`, the select and
    // option model, the labels, constraint validation, the selection API,
    // the entry list and FormData. Everything here reads the receiver's OWN
    // document - the prototypes are shared by every document of the realm -
    // and the per-element state that is not a content attribute (an option's
    // selectedness, a custom validity message, the selection direction) is a
    // hidden slot on the element's wrapper, which is what the collector traces.
    //
    // WHAT STOPS SHORT, and where: `value` and `checked` on input, select
    // and textarea are OWN accessors the wrapper installs (element/views.cpp),
    // which shadow anything a prototype could say - so the input type states'
    // value sanitisation and the select's value-to-selectedness rule are not
    // here. Submission stops where the network would begin: the entry list
    // is built, `formdata` and `submit` fire, and nothing navigates.
    // =========================================================================

    // The receiver as (owning bindings, node): a prototype's native is called
    // for any document's element.

    // A hidden slot on an element's wrapper.

    // An input's type STATE, HTML 4.10.5: the attribute's keyword, else text.

    // A form control's value as the store holds it.

    // The tree a node is in, from its top.

    // --- the form owner, HTML 4.10.17.3 ---------------------------------------
    //
    // The `form` attribute names a form by id in the element's tree; without
    // one the nearest form ancestor owns the element. (The `form` IDL
    // attribute itself is installed by install_form_owner, reflection.cpp.)

    // The categories, HTML 4.10.2.

    // The listed elements whose form owner is `form`, in tree order, over the
    // form's tree (a control with `form=` may be outside the form).

    // "Disabled", HTML 4.10.18.5: its own attribute, or a disabled fieldset
    // ancestor it is not inside the first legend of.

    // An event fired at a node with the flags given; whether it was cancelled.
}

} // namespace ctbrowser::shell
