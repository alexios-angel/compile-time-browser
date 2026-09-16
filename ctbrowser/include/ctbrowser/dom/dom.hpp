#pragma once
// The runtime document: a slab of nodes addressed by generation-tagged
// handles, mutated in place on the one thread that owns it.
//
//   node      the node itself - and, deliberately, NOT its layout results.
//             Those belong to the box tree.
//   document  creation, structural and per-node writes
//   read_txn  the read view; what it hands out lasts until the next write
//             to that node
//   html      the WHATWG tokenizer and tree builder, behind document::builder
//
// See :document for what a write invalidates.

#include <ctbrowser/dom/dataset.hpp>
#include <ctbrowser/dom/document.hpp>
#include <ctbrowser/dom/element.hpp>
#include <ctbrowser/dom/html.hpp>
#include <ctbrowser/dom/node.hpp>
#include <ctbrowser/dom/token_list.hpp>
#include <ctbrowser/dom/tokenizer.hpp>
#include <ctbrowser/dom/treebuilder.hpp>
#include <ctbrowser/dom/xml.hpp>
