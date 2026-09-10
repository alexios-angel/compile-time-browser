// THE OTHER SIDE OF op::bind_export, and the only thing that can see it.
//
// Interpreted in every arm, like module-dep.js. Reading `mine` from inside
// module-main.js proves only that its local works; what bind_export decides is
// whether the cell in main's RECORD is the same box that local writes through.
// This module takes that box - through op::load_import against main's record -
// and reads it after main has written to it.
//
// A lowering that published the register's own freshly-made cell instead of
// adopting the record's would leave the record holding the empty box
// instantiate_module created, and this answers `undefined` while every
// assertion inside main still passes.
import { mine, raise2 } from "./main.js";

READ_MINE = function () {
    raise2();
    return "" + mine;
};
