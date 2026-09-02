// WHAT A CYCLE MEANS, PINNED AGAINST THE INTERPRETER - Phase 55C, gate 4 of
// ctcompile-plan/25-escape-analysis.md §5.
//
// The spec's escape analysis asked for "back-references or circular properties
// replaced with weak links". Part 24 Stage 55C refuses that as stated, and this
// file is the evidence, in one executable, for why:
//
//   THE REFERENCE SEMANTICS ARE THE VM's TRACING COLLECTOR. A reachable cycle
//   survives every collection; an unreachable one is freed whole. Whatever the
//   native tier emits for an object graph has to reproduce exactly that, and
//   section A asks the interpreter what "that" is with the collector made
//   hostile, rather than writing the answer down.
//
//   THE NATIVE TIER HAS NO COLLECTOR. Ownership there is RAII, so a cycle it
//   cannot give an RAII lifetime is a COMPILE-TIME DIAGNOSTIC naming the sites
//   - never a silent choice of edge. Section C is the two ways the silent
//   choice fails, as counters: `shared_ptr` both ways destroys NOTHING, and a
//   compiler-chosen `weak_ptr` destroys a node JavaScript can still see.
//
//   THE ONE RAII-SOUND ANSWER FOR A CONFINED CYCLE is a frame-owned region
//   whose members die together at scope exit - 55C option 2, the shape a
//   lowering of `ringLocal` will emit. Section C models it and counts every
//   destructor exactly once.
//
// EVERY VALUE THE HARNESS HOLDS LIVES IN A JAVASCRIPT GLOBAL, as GCRoots.cpp
// insists: the collector walks exactly GCRoots.def, a C++ local is in none of
// its roots, and section B uses precisely that to show the collector is not
// merely inert - the same ring, held only by a C++ local, is freed.
//
// EVERY CLAIM IS A COUNTER. `collections()` must have GROWN before a stressed
// answer means anything (a stress mode that stops collecting looks exactly like
// one that works), and `live_objects()` is asserted around every forced
// collection rather than trusted from output.
#include <ctbrowser/script/builtins.hpp>
#include <ctbrowser/script/compile.hpp>
#include <ctbrowser/script/vm.hpp>

#include <cstddef>
#include <cstdio>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

using ctbrowser::script::context;
using ctbrowser::script::program;
using ctbrowser::script::value;

namespace {

constexpr std::string_view fixture =
#include "escape-cycle.js.inc"
    ;

#if defined(CTCOMPILE_ESCAPE_CYCLE_MODULE)
// THE CTJS MODULE OF THE SAME FILE, printed by ctjs-translate at build time.
// Gate 4(f) reads it as text: it needs no MLIR to link, and a `ctnative.weak`
// appearing in it would be the miscompile CTNativeTypes.td forbids.
constexpr std::string_view printed_module =
#include "escape-cycle.ctjs.inc"
    ;
#endif

int failures = 0;

void report(const char * what, bool ok) {
    std::printf("%-58s %s\n", what, ok ? "ok" : "FAILED");
    if (!ok) { ++failures; }
}

void report(const char * what, bool ok, const std::string & got, const std::string & want) {
    report(what, ok);
    if (!ok) { std::printf("    expected: %s\n    got:      %s\n", want.c_str(), got.c_str()); }
}

// A DRIVER IS A SECOND CLASSIC SCRIPT IN THE SAME CONTEXT. Its top-level `var`s
// are globals, so it can read `keep` and leave its answers where the collector
// can see them and this file can read them back by name.
//
// THE PROGRAM IS THE CALLER'S AND OUTLIVES EVERY CLOSURE IT MADE.
// closure_object::proto points into program::functions and the constant pool
// every property name comes from, so a program compiled inside a helper and
// dropped on return leaves each function it declared dangling - which is how
// the first version of this file segfaulted, two checks after it had read
// "undefined" out of a freed pool.
bool run_kept(context & cx, const program & prog, const char * what) {
    if (!prog.ok) {
        std::printf("%s did not compile: %s\n", what, prog.error.c_str());
        return false;
    }
    const ctbrowser::script::run_result r = cx.run(prog);
    if (!r.ok) { std::printf("%s did not run: %s\n", what, r.error.c_str()); }
    return r.ok;
}

std::string global_text(context & cx, const char * name) {
    return cx.to_string(cx.global(name));
}

// ONE EXPRESSION IN A FRESH CONTEXT, rendered the way JavaScript prints it -
// ctbrowser/test/support/js_expect.hpp's shape, local so this test carries no
// dependency on the engine's own unit-test support tree. A parse or runtime
// fault is "THREW", flattened deliberately.
std::string probe(std::string_view expression) {
    const program prog =
        ctbrowser::script::compiler::compile("return (" + std::string{expression} + ");");
    context cx;
    ctbrowser::script::install_builtins(cx);
    const ctbrowser::script::run_result r = cx.run(prog);
    if (!r.ok) { return "THREW"; }
    return cx.to_string(r.returned);
}

void probe_expect(const char * what, std::string_view expression, std::string_view want) {
    const std::string got = probe(expression);
    report(what, got == want, got, std::string{want});
}

// ---------------------------------------------------------------------------
// SECTION C's MODELS. Three node types, three counters, so the failure modes
// cannot mask one another.
// ---------------------------------------------------------------------------
namespace model {

// (c1) THE SPEC'S DEFAULT, TAKEN LITERALLY: every JavaScript reference is a
// `std::shared_ptr`. Reference counting cannot see a cycle, so a ring whose
// every handle is gone still holds itself.
int strong_destroyed = 0;
struct strong_node {
    int v;
    std::shared_ptr<strong_node> next;
    std::shared_ptr<strong_node> prev;
    explicit strong_node(int value) : v(value) {}
    ~strong_node() { ++strong_destroyed; }
};

// Builds `ring(n)` and DROPS EVERY HANDLE - only raw, non-owning pointers to
// the nodes come back, which is exactly the state a leaked ring is in.
std::vector<strong_node *> strong_ring_dropped(int n) {
    std::vector<std::shared_ptr<strong_node>> handles;
    for (int i = 0; i < n; ++i) { handles.push_back(std::make_shared<strong_node>(i)); }
    for (int i = 0; i < n; ++i) {
        handles[static_cast<std::size_t>(i)]->next = handles[static_cast<std::size_t>((i + 1) % n)];
        handles[static_cast<std::size_t>(i)]->prev =
            handles[static_cast<std::size_t>((i + n - 1) % n)];
    }
    std::vector<strong_node *> raw;
    for (const auto & h : handles) { raw.push_back(h.get()); }
    return raw; // `handles` dies here: every strong handle is gone
}

// (c2) THE SPEC'S PROPOSED FIX: the "back-reference" made weak. The compiler
// has picked `prev`, which in a doubly-linked pair is the only candidate.
int weak_destroyed = 0;
struct weak_node {
    int v;
    std::shared_ptr<weak_node> next;
    std::weak_ptr<weak_node> prev;
    explicit weak_node(int value) : v(value) {}
    ~weak_node() { ++weak_destroyed; }
};

// (c3) 55C OPTION 2, THE RAII-SOUND ANSWER FOR A CONFINED CYCLE: a region the
// FRAME owns. Members point at each other with plain pointers into it, nothing
// counts anything, and the whole graph dies when the frame does - which is
// also what the interpreter does to `ringLocal`'s ring at that frame's end.
int arena_destroyed = 0;
struct arena_node {
    int v = 0;
    arena_node * next = nullptr;
    arena_node * prev = nullptr;
    ~arena_node() { ++arena_destroyed; }
};

struct arena_outcome {
    int sum;
    int destroyed_before_exit;
};

// `ringLocal(n)` as the lowering will spell it. A `std::vector` with a reserve
// is the frame-owned region; when `n` is the literal it is in `churn`, a
// `std::array<arena_node, 4>` on the stack is the same thing with no heap.
arena_outcome ring_local_arena(int n) {
    std::vector<arena_node> region;
    region.reserve(static_cast<std::size_t>(n)); // pointers into it stay valid
    // emplace, never push a temporary: a copied temporary would run the
    // destructor during construction and the "nothing dies early" count
    // below is exactly what would catch it.
    for (int i = 0; i < n; ++i) { region.emplace_back().v = i; }
    for (int i = 0; i < n; ++i) {
        region[static_cast<std::size_t>(i)].next = &region[static_cast<std::size_t>((i + 1) % n)];
        region[static_cast<std::size_t>(i)].prev =
            &region[static_cast<std::size_t>((i + n - 1) % n)];
    }
    int s = 0;
    const arena_node * c = &region[0];
    for (int i = 0; i < 2 * n; ++i) {
        s += c->v;
        c = c->next;
    }
    return {s, arena_destroyed}; // nothing has died yet; all of it dies on `}`
}

} // namespace model

// THE DRIVER: every helper the harness needs, declared ONCE, before any count is
// taken, so its closures are part of the baseline and no later step runs a
// program. A program run leaves its top-level register window behind - the
// collector marks `registers_` in full - so a value that was live at the end of
// a driver stays rooted until the next `run` reassigns the file. Calling these
// from C++ instead has `context::invoke` resize the window away on return.
//
// `.call` IS THE SAFEPOINT. This interpreter collects under stress at exactly
// one place, `context::invoke`'s entry (vm/call.cpp, the `safepoint()` below
// the argument copy) - every C++ entry into JavaScript, which
// `Function.prototype.call` is (builtins/objects.cpp: `c.call(self, rest,
// ...)`). An interpreted JS-to-JS call pushes its frame in the run loop and is
// NOT one. So `walk(keep, 9)` from inside a driver never collects, and
// `walk.call(null, keep, 9)` collects once, at entry, with the ring reachable
// only through the global and the argument window it was just copied into.
constexpr std::string_view driver = R"(
    var A9, A4, A5, AI, AB, AS, keep2, M, MV;
    function probeWalks() {
      A9 = walk(keep, 9).v;
      A4 = walk(keep, 4).v;
      A5 = walk(keep, 5).v;
      AI = (keep.prev.next === keep);
      AB = (walk(keep, 1).prev === keep);
      var c = keep;
      for (var i = 0; i < 9; i++) c = walk(c, 1);
      AS = c.v;
    }
    function probeWalksVia() {
      A9 = walk.call(null, keep, 9).v;
      A4 = walk.call(null, keep, 4).v;
      A5 = walk.call(null, keep, 5).v;
      AI = (keep.prev.next === keep);
      AB = (walk.call(null, keep, 1).prev === keep);
      var c = keep;
      for (var i = 0; i < 9; i++) c = walk.call(null, c, 1);
      AS = c.v;
    }
    function holdMiddle() { keep2 = keep.next; keep = null; }
    function probeMiddle() { M = (keep2.prev.next === keep2); MV = keep2.prev.v; }
    function restoreHead() { keep = keep2.prev; keep2 = null; }
    function churnVia(k) { var t = 0; for (var i = 0; i < k; i++) t += ringLocal.call(null, 4); return t; }
)";

value call0(context & cx, const char * name) {
    return cx.call(cx.global(name), std::span<const value>{}, value::undefined());
}

value call1(context & cx, const char * name, double arg) {
    const value args[] = {value::number(arg)};
    return cx.call(cx.global(name), std::span<const value>{args}, value::undefined());
}

// ONE COLLECTION WITH NOTHING STALE: the register file is reassigned by an
// empty program, the dead entry closure that run leaves is then swept with
// everything else, and the count that comes back is the count of what the
// GLOBALS reach.
std::size_t settled_live(context & cx, const program & scrub) {
    if (!run_kept(cx, scrub, "the scrub")) { return static_cast<std::size_t>(-1); }
    (void)cx.collect();
    return cx.live_objects();
}

} // namespace

int main() {
    const program compiled = ctbrowser::script::compiler::compile(std::string(fixture));
    if (!compiled.ok) {
        std::printf("the fixture did not compile: %s\n", compiled.error.c_str());
        return 1;
    }
    context cx;
    ctbrowser::script::install_builtins(cx);
    if (!cx.run(compiled).ok) {
        std::printf("the fixture did not run\n");
        return 1;
    }
    const program driver_prog = ctbrowser::script::compiler::compile(driver);
    if (!run_kept(cx, driver_prog, "the driver")) { return 1; }
    const program scrub = ctbrowser::script::compiler::compile("void 0;");
    if (!scrub.ok) { return 1; }

    // =======================================================================
    // A. THE REFERENCE SEMANTICS: a reachable cycle survives a tracing
    //    collection. Gate 4(a).
    // =======================================================================
    //
    // The unstressed interpreter defines every answer. The fixture is a
    // 3-ring, so 9 steps is home, 4 is the first node, 5 the second, and both
    // identity questions close - asserted so a fixture that drifted to a chain
    // or a 4-ring cannot make the stressed comparison below vacuous.
    (void)call0(cx, "probeWalks");
    const std::string a9 = global_text(cx, "A9");
    const std::string a4 = global_text(cx, "A4");
    const std::string a5 = global_text(cx, "A5");
    const std::string ai = global_text(cx, "AI");
    const std::string ab = global_text(cx, "AB");
    const std::string as = global_text(cx, "AS");
    report("unstressed: a 3-ring answers 0/1/2 and closes",
           a9 == "0" && a4 == "1" && a5 == "2" && ai == "true" && ab == "true" && as == "0",
           a9 + " " + a4 + " " + a5 + " " + ai + " " + ab + " " + as, "0 1 2 true true 0");
    // CLEARED, so the stressed run cannot pass on a stale answer.
    for (const char * name : {"A9", "A4", "A5", "AI", "AB", "AS"}) {
        cx.define_global(name, value::undefined());
    }

    // NOW WITH THE COLLECTOR HOSTILE. probeWalksVia's entry is one collection,
    // its four `.call`s are four more, and the nine single steps are nine -
    // each with the current node reachable only through `keep`'s ring and the
    // window the argument was copied into. The count is asserted before any
    // answer is compared: a stress mode that stops collecting looks exactly
    // like one that works.
    const std::size_t before = cx.collections();
    cx.set_gc_stress(true);
    (void)call0(cx, "probeWalksVia");
    cx.set_gc_stress(false);
    const std::size_t during = cx.collections() - before;
    std::printf("    collections during the stressed walk: %zu\n", during);
    report("stressed walk collected at every entry (>= 14)", during >= 14);
    report("(a) walk(keep, 9).v survives collection", global_text(cx, "A9") == a9,
           global_text(cx, "A9"), a9);
    report("(a) walk(keep, 4).v and walk(keep, 5).v survive",
           global_text(cx, "A4") == a4 && global_text(cx, "A5") == a5);
    report("(a) keep.prev.next === keep survives collection", global_text(cx, "AI") == ai,
           global_text(cx, "AI"), ai);
    report("(a) walk(keep, 1).prev === keep survives collection", global_text(cx, "AB") == ab,
           global_text(cx, "AB"), ab);
    report("(a) nine single steps, a collection before each", global_text(cx, "AS") == as,
           global_text(cx, "AS"), as);
    // WHAT THIS ASSERTION WOULD DO IF A COLLECTION FREED A MEMBER: `walk`
    // dereferences `c.next` through freed storage - a heap-use-after-free
    // under ASan, garbage or a crash without it - and `AI` compares a dangling
    // `prev` against `keep`. Section B shows the same collector freeing the
    // same ring the moment nothing roots it, so survival here is reachability
    // and not an inert collector.

    // =======================================================================
    // B. THE COUNTS: a reachable ring is exactly its nodes, an unreachable
    //    one is freed whole, and churn is what the safepoints make it.
    //    Gate 4(b).
    // =======================================================================
    //
    // THE NATIVE TIER'S ANSWER FOR THIS RING IS A DIAGNOSTIC. `ring` escapes
    // (returned, then stored into a global); with no collector on that tier
    // there is nothing to fall back to, so a cycle the analysis cannot prove
    // confined is refused at compile time with the sites named. What is pinned
    // here is what any lowering must reproduce: the ring is alive while any
    // member is reachable, and gone - all of it, at once - when none is.
    const std::size_t with_ring = settled_live(cx, scrub);
    cx.define_global("keep", value::null());
    const std::size_t baseline = settled_live(cx, scrub);
    std::printf("    live objects: %zu with the ring, %zu without\n", with_ring, baseline);
    // THREE, and not "three plus something": the nodes are the only heap
    // objects `ring(3)` makes. Property names come from the constant pool,
    // numbers and null are immediates, and the frame's registers are resized
    // away on return. A hidden allocation would show up here as a fourth.
    report("(b) a reachable ring is baseline + 3", with_ring == baseline + 3);

    // REBUILT FROM C++, held through the global, and the same count again - so
    // the number is a property of the ring and not of what the fixture's top
    // level left behind.
    cx.define_global("keep", call1(cx, "ring", 3));
    report("(b) rebuilt ring: baseline + 3 again", settled_live(cx, scrub) == baseline + 3);

    // REACHABLE THROUGH A MIDDLE MEMBER ONLY. This is the exact case a weak
    // `prev` gets wrong (section C2): the head is reachable from nothing but
    // the ring itself, and a tracing collector keeps all three.
    (void)call0(cx, "holdMiddle");
    report("(b) held by a middle member: all 3 survive", settled_live(cx, scrub) == baseline + 3);
    (void)call0(cx, "probeMiddle");
    report("(b) keep2.prev is still the head",
           global_text(cx, "M") == "true" && global_text(cx, "MV") == "0");
    (void)call0(cx, "restoreHead");
    report("(b) restored through prev: still baseline + 3",
           settled_live(cx, scrub) == baseline + 3);

    // THE FALSIFICATION OF (a): the same ring, built by the same function, held
    // ONLY by a C++ local this collector cannot see - and it is freed. The
    // survival above is reachability, and only reachability.
    {
        const value stray = call1(cx, "ring", 3);
        (void)stray; // deliberately never rooted, and never read again
        report("(a, negative) an unrooted ring is freed whole",
               settled_live(cx, scrub) == baseline + 3);
    }

    // AND WHEN NOTHING REACHES IT, THE WHOLE RING GOES - not one node, not
    // two: the count returns to the baseline exactly, which is what neither
    // C++ model in section C can do on its own.
    cx.define_global("keep", value::null());
    report("(b) keep = null; collect() returns to baseline", settled_live(cx, scrub) == baseline);

    // CHURN. `churn(1000)` builds a thousand confined 4-rings and drops each.
    //
    // THE DESIGN'S SKETCH ASSUMED A COLLECTION PER CALL and this interpreter
    // has none: `ringLocal(4)` inside `churn` is a JS-to-JS call, which is not
    // a safepoint, so under stress the whole of `churn(1000)` collects exactly
    // ONCE - at its own entry from C++ - and returns with 4,000 dead nodes on
    // the heap. That is pinned as it is. The boxed AOT tier is different:
    // ct_aot_call IS a safepoint (aot_helpers.def), and the driver's `churnVia`
    // below reproduces that cadence through `Function.prototype.call`, where
    // the bound is one dead ring.
    const std::string churn_unstressed = cx.to_string(call1(cx, "churn", 1000));
    const std::size_t after_calm_churn = cx.live_objects();
    report("churn(1000) unstressed: 4000 dead nodes, none swept yet",
           after_calm_churn == baseline + 4000);
    report("(b) one collection frees all 1000 dead rings", settled_live(cx, scrub) == baseline);

    const std::size_t churn_before = cx.collections();
    cx.set_gc_stress(true);
    const std::string churn_stressed = cx.to_string(call1(cx, "churn", 1000));
    cx.set_gc_stress(false);
    const std::size_t churn_collections = cx.collections() - churn_before;
    const std::size_t after_churn = cx.live_objects();
    std::printf("    churn(1000) under stress: %zu collection(s), live %zu (baseline %zu)\n",
                churn_collections, after_churn, baseline);
    // 4 × (0+1+2+3) × 1000. Both runs must agree, and the arithmetic guards
    // the fixture the way A9/A4/A5 do above.
    report("churn(1000) stressed == unstressed == 12000",
           churn_stressed == churn_unstressed && churn_unstressed == "12000", churn_stressed,
           churn_unstressed);
    report("(b) churn(1000) under stress collects once - the interpreter's one safepoint",
           churn_collections == 1);
    report("(b) ... so live is baseline + 4000 until the next collection",
           after_churn == baseline + 4000);
    report("(b) and back to baseline after one collection", settled_live(cx, scrub) == baseline);

    // THE SAME CHURN AT THE AOT TIER'S CADENCE: a collection at every
    // `ringLocal` entry. The previous ring is dead and swept before the next
    // is built, so the bound is the LAST ring - dead at churnVia's return but
    // not yet swept - and exactly baseline after one more collection.
    const std::size_t via_before = cx.collections();
    cx.set_gc_stress(true);
    const std::string churn_via = cx.to_string(call1(cx, "churnVia", 1000));
    cx.set_gc_stress(false);
    const std::size_t via_collections = cx.collections() - via_before;
    const std::size_t after_via = cx.live_objects();
    std::printf("    churnVia(1000) under stress: %zu collections, live %zu (baseline %zu)\n",
                via_collections, after_via, baseline);
    report("churnVia(1000) collected once per ringLocal entry (>= 1001)", via_collections >= 1001);
    report("churnVia(1000) == churn(1000)", churn_via == churn_unstressed, churn_via,
           churn_unstressed);
    report("(b) churnVia(1000) under stress: live is exactly one dead ring",
           after_via == baseline + 4);
    report("(b) and back to baseline after one collection", settled_live(cx, scrub) == baseline);

    // =======================================================================
    // C. THE C++ MODELS: what "replace back-references with weak links" does,
    //    and the one RAII answer that is sound. Gate 4(c).
    // =======================================================================

    // C1. `shared_ptr` BOTH WAYS: every handle dropped, nothing destroyed.
    // This is the leak - three nodes the program can no longer reach and no
    // destructor will ever run for.
    {
        model::strong_destroyed = 0;
        std::vector<model::strong_node *> leaked = model::strong_ring_dropped(3);
        report("(c1) shared_ptr ring: every handle dropped, 0 destroyed",
               model::strong_destroyed == 0);
        report("(c1) ... and every node still holds its neighbours",
               leaked[0]->next.get() == leaked[1] && leaked[1]->prev.get() == leaked[0] &&
                   leaked[2]->next.get() == leaked[0]);
        // FREEING IT NEEDS THE WHOLE GRAPH: cut every `prev`, then one `next`,
        // in that order, from raw pointers gathered while building. That is
        // knowledge of the graph's shape and of which node is the head -
        // neither of which the JavaScript states - and it is the reasoning
        // the tracing collector does by walking from the roots instead.
        for (model::strong_node * n : leaked) { n->prev.reset(); }
        report("(c1) cutting every prev alone frees nothing", model::strong_destroyed == 0);
        // Moved out first so the chain of destructions it triggers - node 1,
        // then 2, then 0 itself - never runs inside a member of node 0.
        std::shared_ptr<model::strong_node> last_edge = std::move(leaked[0]->next);
        last_edge.reset(); // `leaked` is dangling from here on
        report("(c1) cutting one next as well frees all 3 - the counter is live",
               model::strong_destroyed == 3);
    }

    // C2. THE COMPILER-CHOSEN WEAK EDGE. `a.next = b; b.prev = a`, `prev`
    // made weak; JavaScript holds `b` and lets go of `a`. Under the tracing
    // collector (section B, "held by a middle member") `a` is alive and
    // `b.prev.next === b` is true. Here `a` is destroyed while `b` is still
    // held, and `b.prev` has expired: the early destroy.
    {
        model::weak_destroyed = 0;
        std::shared_ptr<model::weak_node> b;
        {
            auto a = std::make_shared<model::weak_node>(0);
            b = std::make_shared<model::weak_node>(1);
            a->next = b;
            b->prev = a;
            report("(c2) while a is held, b.prev is a",
                   !b->prev.expired() && b->prev.lock().get() == a.get());
        } // `a`'s only strong handle is gone; `b.prev` was never one
        report("(c2) weak prev: a destroyed while b is still held",
               model::weak_destroyed == 1 && b.use_count() == 1);
        report("(c2) weak prev: b.prev has expired - JS would still see it", b->prev.expired());
    }

    // C3. THE FRAME-OWNED REGION - 55C option 2, the shape `ringLocal` lowers
    // to. Nothing dies before the frame ends; everything dies when it does,
    // each node exactly once; and the sum is the interpreter's.
    {
        model::arena_destroyed = 0;
        const model::arena_outcome out = model::ring_local_arena(4);
        report("(c3) arena ring: nothing destroyed before scope exit",
               out.destroyed_before_exit == 0);
        report("(c3) arena ring: all 4 destroyed exactly once at scope exit",
               model::arena_destroyed == 4);
        const std::string vm_sum = cx.to_string(call1(cx, "ringLocal", 4));
        report("(c3) arena ringLocal(4) == the VM's ringLocal(4)",
               std::to_string(out.sum) == vm_sum, std::to_string(out.sum), vm_sum);
        report("(c3) and the VM's ringLocal(4) left nothing behind either",
               settled_live(cx, scrub) == baseline);
    }

    // =======================================================================
    // E. ND-2: no weak references exist in this engine. Gate 4(e).
    // =======================================================================
    //
    // Option 3 - weak links only where the SOURCE says `WeakRef` or `WeakMap`
    // - is vacuous here, and the VM is asked rather than told: there is no
    // `WeakRef` global at all, and `WeakMap`/`WeakSet` are the strong `Map`/
    // `Set` under other names (builtins/collections.cpp).
    probe_expect("(e) ND-2: typeof WeakRef === \"undefined\"", "typeof WeakRef === \"undefined\"",
                 "true");
    probe_expect("(e) ND-2: WeakMap === Map", "WeakMap === Map", "true");
    probe_expect("(e) ND-2: WeakSet === Set", "WeakSet === Set", "true");
    probe_expect("(e) ND-2: the conjunction the plan pins",
                 "typeof WeakRef === \"undefined\" && WeakMap === Map && WeakSet === Set", "true");
    probe_expect("(e) ND-2: a WeakMap entry is a strong Map entry",
                 "(function () { var k = {}; var w = new WeakMap(); w.set(k, 1); "
                 "return w instanceof Map && w.get(k); })()",
                 "1");

    // =======================================================================
    // ND-3: a getter on Object.prototype does not fire for a plain object.
    // =======================================================================
    //
    // lookup_property (vm/objects.cpp) walks the chain calling accessors, but a
    // plain literal's chain is null, and the shared Object.prototype table is
    // consulted afterwards with `find` - data properties only. The same getter
    // DOES fire when the table is on an explicit chain, which is what makes
    // this a pin on the shortcut and not on accessors in general. V8 answers
    // 42 for both.
    probe_expect("ND-3: Object.prototype getter on a literal - undefined here, 42 in V8",
                 "(function () { Object.defineProperty(Object.prototype, \"nd3\", "
                 "{get: function () { return 42; }}); return typeof ({}).nd3; })()",
                 "undefined");
    probe_expect("ND-3: the same getter fires through an explicit chain",
                 "(function () { Object.defineProperty(Object.prototype, \"nd3\", "
                 "{get: function () { return 42; }}); "
                 "return Object.create(Object.prototype).nd3; })()",
                 "42");
    probe_expect("ND-3: a data property on Object.prototype IS seen by a literal",
                 "(function () { Object.prototype.nd3d = 7; return ({}).nd3d; })()", "7");

    // =======================================================================
    // F. THE PRINTED MODULE CONTAINS NO `ctnative.weak`. Gate 4(f).
    // =======================================================================
#if defined(CTCOMPILE_ESCAPE_CYCLE_MODULE)
    report("(f) ctjs-translate printed a module for the fixture", !printed_module.empty());
    report("(f) the module carries the fixture's four functions",
           printed_module.find("ring") != std::string_view::npos &&
               printed_module.find("ringLocal") != std::string_view::npos &&
               printed_module.find("walk") != std::string_view::npos &&
               printed_module.find("churn") != std::string_view::npos);
    // AND NOTHING WAS SKIPPED: ctjs-translate records a function it could not
    // import under `ctjs.skipped`, and a module missing a body is a module the
    // absence below says nothing about.
    report("(f) the importer skipped nothing in the fixture",
           printed_module.find("ctjs.skipped") == std::string_view::npos);
    report("(f) the module contains no ctnative.weak",
           printed_module.find("ctnative.weak") == std::string_view::npos);
#else
    std::printf("%-58s %s\n", "(f) printed module", "SKIPPED - no ctjs-translate in this build");
#endif

    if (failures == 0) { std::printf("\nall checks passed\n"); }
    return failures == 0 ? 0 : 1;
}
