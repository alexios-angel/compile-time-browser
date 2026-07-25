module;
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include <boost/container/small_vector.hpp>

export module ctbrowser.dom:node;

import ctbrowser.core;

// The DOM node, and the reason reads take no locks.
//
// A generation-tagged handle stops a reader resolving a FREED node. It does
// nothing about a reader iterating a child vector while a writer pushes onto
// it - that is a plain data race on the vector, and no amount of handle
// checking fixes it. So the mutable parts of a node are not mutable:
//
//   children, attributes and text are IMMUTABLE blocks behind atomic
//   pointers. A writer builds a whole new block and publishes the pointer
//   with one release store; a reader loads it with one acquire load and then
//   reads a block nobody will ever touch again. The old block is handed to
//   the epoch domain, which destroys it once no reader can still be holding
//   it.
//
// That is RCU, and it is why a reader needs no lock: it never observes a
// half-updated anything. It sees the old block or the new one.
//
// What this deliberately does NOT store, and v1's node did: layout rects,
// text-line caches, widget state, selection ranges, caret positions, blink
// phase. Those are outputs of style and layout, and keeping them on the node
// is what made v1's layout unable to run concurrently. They belong to the box
// tree, which is not the DOM tree.

export namespace ctbrowser {

struct node_tag {};
using node_id = handle<node_tag>;

enum class node_kind : std::uint8_t { document, element, text, comment };

struct attribute {
	atom name;
	std::string value; // "" for a boolean attribute, per HTML
};

// Immutable once published. Small-vector because the overwhelming majority of
// elements have a handful of children and one or two attributes, and a heap
// allocation each would dominate document construction.
struct child_list {
	boost::container::small_vector<node_id, 4> items;
};
struct attr_list {
	boost::container::small_vector<attribute, 2> items;
};
struct text_block {
	std::string value;
};

// Shared empties, so a leaf element costs no allocation at all. Never
// published into the epoch domain - `retire_payload` skips them.
inline const child_list empty_children{};
inline const attr_list empty_attributes{};
inline const text_block empty_text{};

struct node {
	node_kind kind = node_kind::element;
	atom tag; // elements only

	// 8 bytes and lock-free on every target we care about; a reader that
	// races a reparent sees the old parent or the new one, never a mix.
	std::atomic<node_id> parent{node_id{}};

	std::atomic<const child_list *> children{&empty_children};
	std::atomic<const attr_list *> attributes{&empty_attributes};
	std::atomic<const text_block *> text{&empty_text};

	node() = default;
	explicit node(node_kind k, atom t = {}) noexcept : kind(k), tag(t) {}

	// The slab stores these in place; atomics make them immovable anyway.
	node(const node &) = delete;
	node & operator=(const node &) = delete;

	~node() {
		// Only the shared empties survive a document teardown untouched;
		// anything else was allocated by a publish and is ours to free.
		destroy_payload(children.load(std::memory_order_relaxed));
		destroy_payload(attributes.load(std::memory_order_relaxed));
		destroy_payload(text.load(std::memory_order_relaxed));
	}

	static void destroy_payload(const child_list * p) {
		if (p != &empty_children) { delete p; }
	}
	static void destroy_payload(const attr_list * p) {
		if (p != &empty_attributes) { delete p; }
	}
	static void destroy_payload(const text_block * p) {
		if (p != &empty_text) { delete p; }
	}
};

// Hand a replaced payload block to the epoch domain. The shared empties are
// never retired - they outlive every document.
inline void retire_payload(epoch_domain & domain, const child_list * p) {
	if (p == &empty_children) { return; }
	domain.retire(const_cast<child_list *>(p),
	              [](void * q) { delete static_cast<child_list *>(q); });
}
inline void retire_payload(epoch_domain & domain, const attr_list * p) {
	if (p == &empty_attributes) { return; }
	domain.retire(const_cast<attr_list *>(p), [](void * q) { delete static_cast<attr_list *>(q); });
}
inline void retire_payload(epoch_domain & domain, const text_block * p) {
	if (p == &empty_text) { return; }
	domain.retire(const_cast<text_block *>(p),
	              [](void * q) { delete static_cast<text_block *>(q); });
}

} // namespace ctbrowser
