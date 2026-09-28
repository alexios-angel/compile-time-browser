# Element facts for Bootstrap's element helpers (design, 2026-09-28)

Status: designed, not started. Follows `05-component-data-registration-design.md`.

## Where the DOM path stops

Authentic W + B + Button `U` + the real Data holder, entered as
`function bootstrapDom(element) { ... new U(element, null) ... }` through the
DOM class path (`ctbrowser-dom-v1`, heritage helpers, `Object` and `Error`
declared; generic MLIR is required - see HANDOFF.md):

    native DOM class: class own-key snapshot requires fixed constructor fields

With a real element B's constructor arm `(t = a(t)) && (this._element = t, ...)`
is live. R1a/R1b decide an arm only on primitive actuals (`evaluateHelper`
binds formals to `partial_eval::value::primitive`), so the arm stays
conditional and B's own-key snapshot (`dispose`'s `getOwnPropertyNames` loop)
sees two field shapes. `Map` is also not an accepted DOM initial intrinsic, so
the Data holder cannot be declared in this path yet.

## What deciding `a(element)` needs

`r = t => !(!t || "object" != typeof t) && (void 0 !== t.jquery && (t = t[0]), void 0 !== t.nodeType)`
and `a = t => r(t) ? t.jquery ? t[0] : t : ...`. On an element:

- `!t` false, `typeof t` "object" - facts of any host object;
- `t.nodeType` is 1 - an IDL attribute the VM's element wrapper sets
  (`lib/Shell/bindings/element/wrapper.cpp`);
- `t.jquery` is undefined - the hard one: it is true only while no element or
  prototype in its chain has a `jquery` property.

Then `a(element)` returns its argument: fold the arm taken and replace the
call's result by the actual, erasing the pure call (the helper then has no
running caller and the 2026-09-28 uncalled-helper rule removes it).

## The `jquery` fact, options

1. **Hard-coded duck-typing keys** (`jquery`): small, but it is a per-library
   shim in the compiler; rejected (see the no-shims rule).
2. **"Not a member of the element binding"**: a read of key K on an element is
   undefined when (a) K is not a property the VM's element binding defines -
   the VM is the oracle, so its set is the right one, not WebIDL's - and (b) no
   accepted program writes an expando onto an element. (b) already holds: the
   typed DOM proof models a whitelist of element keys
   (`HostContract/DOM/Entry/BrowserOperations.cpp`) and refuses every other
   write, and the census refuses prototype mutation. (a) needs the binding's
   complete name set as data: extract it from `lib/Shell/bindings/element/*`
   into a public `ctbrowser/include/ctbrowser/dom/` table the binding itself
   installs from (one list, two callers - the extraction rule), then
   `evaluateHelper` consults it. Pin (b) with a control that writes
   `element.jquery` and must refuse.

3. **The same name set, enumerated from the VM**: the element surface is
   installed across ~8.8k lines of `lib/Shell/bindings/element/` (own data
   properties per wrapper, reflection rows, interface prototypes from
   `interfaces.cpp`'s table), so a table-driven binding is a large,
   WPT-sensitive refactor. Instead enumerate, with the reference interpreter,
   every own and prototype-chain property name of an element of each
   interface in `interface_table`, check the union in as data, and add a
   focused test that re-enumerates and fails on any difference. It is derived
   data with a drift gate, not a second implementation.

**Recommendation: option 2's rule with option 3's name set.** The
partial-evaluation side is small: an element-actual marker in
`evaluateHelper` (truthy, `typeof` "object", known IDL constants such as
`nodeType` 1, names outside the set undefined, a result that is the actual
itself). The name set and its drift test are the real work, and no binding
code changes.

After it: `this._config = this._getConfig(i)` runs `_mergeConfigObj`, which
needs the real `H` (Manipulator) in the fixture, and the Data holder's Maps
need the Data-session provider's Map support in the class path.
