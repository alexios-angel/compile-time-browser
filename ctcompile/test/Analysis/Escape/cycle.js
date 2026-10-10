// Escape-analysis fixture for reference cycles. This file is the only copy of
// the program.
//
// A doubly-linked ring. Every edge lies on a cycle: `next` goes round one way,
// `prev` the other, so there is no "back-reference" a compiler could pick to
// make weak. That is why the shape is a ring and not a tree. Making one edge
// weak would change behaviour: a node reachable only through the weak edge
// could be freed while the program can still reach it. Cycle.cpp shows both
// ways that goes wrong, next to the answer this interpreter gives.
//
// Cycle.cpp #includes this file, so the interpreter, the allocation recorder
// and the ctjs-translate run behind the `ctnative.weak` check all see exactly
// the same program.

var keep;

// Escapes by return, then by the store into a global. The head is
// `escapes:returned` and every other node `escapes:stored`. In the boxed
// (garbage-collected) build, the collector keeps the ring alive while `keep`
// reaches it.
function ring(n) {
    var head = {
        v: 0,
        next: null,
        prev: null
    };
    var cur = head;
    for (var i = 1; i < n; i++) {
        var node = {
            v: i,
            next: null,
            prev: cur
        };
        cur.next = node;
        cur = node;
    }
    cur.next = head;
    head.prev = cur;
    return head;
}

// The same ring, built, walked twice round and dropped inside one call. Its
// nodes never outlive the call (the recorder sees each one freed at return),
// but the analysis reports `escapes:stored` because it treats every store into
// an object's field (`cur.next = node`) as an escape. The claims output lists
// this function as IMPRECISE until a cycle that stays inside one call can be
// given a region owned by that call.
function ringLocal(n) {
    var head = {
        v: 0,
        next: null,
        prev: null
    };
    var cur = head;
    for (var i = 1; i < n; i++) {
        var node = {
            v: i,
            next: null,
            prev: cur
        };
        cur.next = node;
        cur = node;
    }
    cur.next = head;
    head.prev = cur;
    var s = 0,
        c = head;
    for (var i = 0; i < 2 * n; i++) {
        s += c.v;
        c = c.next;
    }
    return s;
}

// k steps round from h. Every call is a safepoint, so under gc_stress each
// walk begins with a collection the ring has to survive.
function walk(h, k) {
    var c = h;
    for (var i = 0; i < k; i++) c = c.next;
    return c;
}

// k confined rings in a row: 4k allocations that die 4 at a time. Under
// gc_stress, memory use must stay bounded.
function churn(k) {
    var t = 0;
    for (var i = 0; i < k; i++) t += ringLocal(4);
    return t;
}

keep = ring(3);
