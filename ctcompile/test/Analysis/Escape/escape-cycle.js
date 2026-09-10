// THE 55C FIXTURE, AND THE ONLY COPY OF IT - ctcompile-plan/25-escape-analysis.md
// §5 gate 4, written out exactly as that section sketches it.
//
// A doubly-linked RING. Every edge of it lies on a cycle: `next` round one way,
// `prev` round the other, so there is no "back-reference" a compiler could pick
// to make weak - which is the whole reason this shape and not a tree. Part 24
// Stage 55C says why "replace back-references with weak links" is not
// semantics-preserving; EscapeCycle.cpp shows both ways it fails, beside the
// answer this interpreter gives.
//
// The driver #includes this file, so the program the interpreter runs, the
// program the recorder observes (gate 4(d), another agent's) and the module
// ctjs-translate prints for the `ctnative.weak` check are the same bytes.

var keep;

// Escapes by return, and then by the store into a global. Both sites are
// `escapes` - `returned` for the head, `stored` for every other node - and the
// boxed tier's tracing collector owns the ring for as long as `keep` reaches
// it.
function ring(n) {
  var head = {v: 0, next: null, prev: null};
  var cur = head;
  for (var i = 1; i < n; i++) {
    var node = {v: i, next: null, prev: cur};
    cur.next = node;
    cur = node;
  }
  cur.next = head;
  head.prev = cur;
  return head;
}

// The same ring, built, walked twice round and dropped inside one frame. Its
// sites are CONFINED in fact - the recorder sees every node freed at the frame
// end - and claimed `escapes:stored`, because each `cur.next = node` is a sink
// under the analysis's contents cut (§6 item 1). That is option 2 measured, not
// taken: the IMPRECISE line names this function, and stays non-empty until a
// confined cycle can be given one frame-owned region.
function ringLocal(n) {
  var head = {v: 0, next: null, prev: null};
  var cur = head;
  for (var i = 1; i < n; i++) {
    var node = {v: i, next: null, prev: cur};
    cur.next = node;
    cur = node;
  }
  cur.next = head;
  head.prev = cur;
  var s = 0, c = head;
  for (var i = 0; i < 2 * n; i++) { s += c.v; c = c.next; }
  return s;
}

// k steps round from h. Every call is a safepoint, so under gc_stress each
// walk begins with a collection the ring has to survive.
function walk(h, k) { var c = h; for (var i = 0; i < k; i++) c = c.next; return c; }

// k confined rings in a row: 4k allocations that die 4 at a time, which is the
// "bounded under stress" half of gate 4(b).
function churn(k) { var t = 0; for (var i = 0; i < k; i++) t += ringLocal(4); return t; }

keep = ring(3);
