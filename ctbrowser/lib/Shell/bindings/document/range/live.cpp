#include "helpers.hpp"

namespace ctbrowser::shell {

using namespace detail;
using namespace range_detail;
using range_detail::slot;

// `document.createRange()`: a range collapsed at (this document, 0).
value dom_bindings::create_range(context & cx) {
    const value ctor = cx.global("Range");
    auto * made = cx.allocate<script::object_object>();
    if (ctor.is_callable()) {
        const value proto = cx.lookup_property(ctor, "prototype");
        if (proto.is_object()) { made->prototype = proto; }
    }
    made->define(std::string{start_node_slot}, document_, script::attr_none);
    made->define(std::string{start_offset_slot}, value::number(0), script::attr_none);
    made->define(std::string{end_node_slot}, document_, script::attr_none);
    made->define(std::string{end_offset_slot}, value::number(0), script::attr_none);
    register_live_range(value::object(made));
    return value::object(made);
}

// ============================================================================
// THE LIVE RANGE STEPS
// ============================================================================

void dom_bindings::register_live_range(value range) {
    primary().live_ranges_.push_back(range);
}

void dom_bindings::each_live_boundary(const std::function<void(const live_boundary &)> & fn) {
    if (cx_ == nullptr || doc_ == nullptr) { return; }
    // A copy: a callback may register a range (wrap() does not, but the
    // list is the primary's and `fn` is not audited).
    const std::vector<value> ranges = primary().live_ranges_;
    for (const value & held : ranges) {
        auto * range = static_cast<script::object_object *>(held.as_heap());
        for (const auto & [node_slot, offset_slot] : {std::pair{start_node_slot, start_offset_slot},
                                                      std::pair{end_node_slot, end_offset_slot}}) {
            const value node = slot(range, node_slot);
            live_boundary at{range, node_slot, offset_slot, node_id{},
                             context::to_number(slot(range, offset_slot))};
            // A boundary of THIS document - not an Attr, not another
            // document's node.
            if (is_the_document(node)) {
                at.node = doc_->document_node();
            } else if (const node_id id = handle_of(node)) {
                at.node = id;
            } else {
                continue;
            }
            fn(at);
        }
    }
}

void dom_bindings::move_live_boundary(const live_boundary & at, node_id node, double offset) {
    at.range->set(std::string{at.node_slot},
                  node == doc_->document_node() ? document_ : wrap(*cx_, node));
    at.range->set(std::string{at.offset_slot}, value::number(offset));
}

// The range steps of DOM 4.2.3 "insert" (step 7 - for each live range whose
// start node is parent and start offset is greater than child's index,
// increase its start offset by count; likewise end), "remove" (steps 5-8 -
// a boundary inside the removed node goes to (parent, index); one on the
// parent past it moves down by one) and 4.10.2 "replace data" (steps 8-11 -
// a boundary inside the replaced span goes to its start; one after it moves
// by the difference in length). `this` is the bindings whose document
// logged the edits; the ranges are the realm's.
void dom_bindings::settle_live_ranges(const std::vector<document::write_note> & writes) {
    if (cx_ == nullptr || doc_ == nullptr || primary().live_ranges_.empty()) { return; }
    using edit = document::write_note::edit;
    for (const document::write_note & note : writes) {
        if (note.kind == edit::attribute) { continue; }
        const auto txn = doc_->read();
        each_live_boundary([&](const live_boundary & at) {
            switch (note.kind) {
            case edit::removed: {
                // Inside the removed node - which may since have been put
                // somewhere else, and is still its own subtree's root.
                bool inside = false;
                for (node_id up = at.node; up && txn.contains(up); up = dom_parent(txn, up)) {
                    if (up == note.child) {
                        inside = true;
                        break;
                    }
                }
                if (inside) {
                    move_live_boundary(at, note.node, note.index);
                } else if (at.node == note.node && at.offset > note.index) {
                    move_live_boundary(at, at.node, at.offset - 1);
                }
                break;
            }
            case edit::inserted:
                if (at.node == note.node && at.offset > note.index) {
                    move_live_boundary(at, at.node, at.offset + 1);
                }
                break;
            case edit::data: {
                if (at.node != note.node) { break; }
                const double offset = note.data.offset;
                const double end = offset + note.data.count;
                if (at.offset > offset && at.offset <= end) {
                    move_live_boundary(at, at.node, offset);
                } else if (at.offset > end) {
                    move_live_boundary(at, at.node, at.offset + note.data.added - note.data.count);
                }
                break;
            }
            case edit::attribute: break;
            }
        });
    }
}

void dom_bindings::split_live_ranges(node_id node, node_id made, double offset, node_id parent,
                                     double made_index) {
    each_live_boundary([&](const live_boundary & at) {
        if (at.node == node && at.offset > offset) {
            move_live_boundary(at, made, at.offset - offset);
        } else if (parent && at.node == parent && at.offset == made_index) {
            move_live_boundary(at, at.node, at.offset + 1);
        }
    });
}

void dom_bindings::absorb_live_ranges(node_id current, node_id parent, double index, node_id node,
                                      double length) {
    each_live_boundary([&](const live_boundary & at) {
        if (at.node == current) {
            move_live_boundary(at, node, length + at.offset);
        } else if (at.node == parent && at.offset == index) {
            move_live_boundary(at, node, length);
        }
    });
}

} // namespace ctbrowser::shell
