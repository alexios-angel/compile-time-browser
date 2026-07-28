export module ctbrowser.dom;

// The runtime document: a slab of nodes addressed by generation-tagged
// handles, with lock-free reads and RCU-published payloads.
//
//   node      the node itself - and, deliberately, NOT its layout results.
//             the previous engine stored rects, text-line caches, widget and selection state
//             on the node, which is exactly why the previous engine's layout could not run
//             concurrently. Those belong to the box tree.
//   document  creation, structural and per-node writes, reclamation
//   read_txn  a pinned, lock-free read view
//   html      the WHATWG tokenizer and tree builder, behind document::builder
//
// See :document for the locking policy and for precisely which atomicity
// guarantees this does and does not make.

export import :node;
export import :document;
export import :tokenizer;
export import :treebuilder;
export import :html;
