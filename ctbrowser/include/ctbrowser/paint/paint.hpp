//===- paint.hpp - Paint umbrella header ---------------------- -*- C++ -*-===//
//
// Paint: from placed geometry to a recorded list of drawing operations.
//
//   values    CSS colours, parsed where they are consumed
//   command   the display list, immutable once recorded, which is what lets
//             the compositor re-composite without re-recording
//   layer     the unit the compositor moves; scrolling is a layer offset
//   record    fragment tree in, display list out
//
// Keeping the recorded list lets scrolling, caret blinks and hover
// re-composite without re-running layout.
//
//===----------------------------------------------------------------------===//
#pragma once

#include <ctbrowser/paint/command.hpp>
#include <ctbrowser/paint/layer.hpp>
#include <ctbrowser/paint/record.hpp>
#include <ctbrowser/paint/values.hpp>
