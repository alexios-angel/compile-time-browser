// THE GC-ROOTS FIXTURE, AND THE ONLY COPY OF IT.
//
// It held just the compiled bodies until the drivers lived in a C++ raw string
// in GCRoots.cpp - two DIFFERENT programs, with a comment asking that they stay
// identical. They could not be, because one had functions the other did not.
//
// That is not cosmetic, and it is the same defect differential.js already had:
// a compiled body bakes the function INDEX of every closure it builds, and the
// ABI row for ct_aot_make_closure says a function index means nothing outside
// the program it was compiled in. `held` builds one. The indices lined up only
// because the extra functions happened to come after it in the C++ copy, and
// inserting anything before `keep` would have made the compiled body close over
// a different function while every check stayed green.
//
// The driver #includes this file now, so the program the pipeline compiles and
// the program the interpreter runs are the same bytes.
//
// TWO SAFEPOINTS WITH A LIVE VALUE BETWEEN THEM, which is the shape that
// matters. `a + b` allocates a string; `k(...)` is a call that runs user
// JavaScript and can collect. The intermediate is live across the call, and it
// also has to be in the call's CONTIGUOUS argument window - so this exercises
// both kinds of rooting the backend does: one slot per produced value, and a
// reserved run per call site.
function f(a, b, c, k) { return k(a + b, c); }

// AND A CLOSURE BUILT IN COMPILED CODE, HELD ACROSS A COLLECTION.
//
// ct_aot_make_closure allocates and is a safepoint, and so is the call below
// it. Three things have to survive: the string `a + b` builds, the CELL holding
// it, and the closure_object itself - none of which is reachable from anything
// but this frame's slots while `k` runs user JavaScript.
function held(a, b, c, k) { var s = a + b; var keep = function () { return s; }; return k(keep(), c); }


function built(a, b, c, k) { var o = new Box(a + b); return k(o.s, c); }

function cat(x, y) { return x + y; }

// A CONSTRUCTOR, so ct_aot_construct's own argument window is covered.
//
// It is a SEPARATE window from ct_aot_call's - a second park loop and a second
// window_pointer in the backend - and nothing above reaches it. The string
// `a + b` builds is handed to `new Box(...)` and then lives ONLY in that
// window while make_instance allocates and the body runs.
function Box(s) { this.s = s; }

function repeat(ch) { var s = ''; for (var i = 0; i < 32; i++) { s = s + ch; } return s; }

var A = '', B = '', R = '';

// ITS valueOf ALLOCATES BEFORE IT ANSWERS, so the collection happens while the
// caller is holding a value it has nowhere rooted.
var c = { valueOf: function () { var j = ''; for (var i = 0; i < 8; i++) { j = j + 'q'; } return 'Z'; } };

function setup() { A = repeat('A'); B = repeat('B'); }
function run() { R = f(A, B, c, cat); }
function runHeld() { R = held(A, B, c, cat); }
function runBuilt() { R = built(A, B, c, cat); }
