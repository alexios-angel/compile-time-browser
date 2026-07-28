export module ctbrowser.paint;

// Paint: from placed geometry to a recorded list of drawing operations.
//
//   values    CSS colours, parsed where they are consumed
//   command   the DISPLAY LIST - immutable once recorded, which is what lets
//             the compositor re-composite without re-recording
//   layer     the unit the compositor moves; scrolling is a layer offset
//   record    fragment tree in, display list out
//
// the previous engine had no object here at all: layout returned a paint_cmd vector that the
// shell drew and discarded, so a scroll, a caret blink or a hover re-ran the
// entire layout to produce a new one.

export import :values;
export import :command;
export import :layer;
export import :record;
