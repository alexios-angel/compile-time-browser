//===- dom.hpp - DOM subsystem aggregate ---------------------- -*- C++ -*-===//
//
// The DOM: a slab of nodes addressed by generation-tagged handles, mutated in
// place on the one thread that owns it. This header includes every DOM header.
//
//   node      the node itself, and deliberately not its layout results, which
//             belong to the box tree
//   document  creation, structural and per-node writes
//   read_txn  the read view; what it hands out lasts until the next write to
//             that node
//   html      the WHATWG tokenizer and tree builder, writing through
//             document::builder
//   xml       the XML parser
//
// See document.hpp for what a write invalidates.
//
//===----------------------------------------------------------------------===//

#pragma once

#include <ctbrowser/dom/dataset.hpp>
#include <ctbrowser/dom/document.hpp>
#include <ctbrowser/dom/element.hpp>
#include <ctbrowser/dom/encoding.hpp>
#include <ctbrowser/dom/html.hpp>
#include <ctbrowser/dom/node.hpp>
#include <ctbrowser/dom/token_list.hpp>
#include <ctbrowser/dom/tokenizer.hpp>
#include <ctbrowser/dom/treebuilder.hpp>
#include <ctbrowser/dom/xml.hpp>
