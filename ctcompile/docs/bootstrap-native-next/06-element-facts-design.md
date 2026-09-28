# Element facts for Bootstrap's element helpers (design, 2026-09-28)

Status: landed (option 2's rule with option 3's name set), see "What landed"
at the end. Follows `05-component-data-registration-design.md`.

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

## What landed

- **Name set.** `ctcompile-test-native-reference --page` runs an HTML file in a
  live `ctbrowser::shell::browser` and prints its console lines.
  `test/CTNative/HostContract/element_names.py` parses `interface_table`'s tags
  out of `interfaces.cpp`, creates one element per tag plus unknown, custom,
  SVG, MathML and other-namespace elements, and unions
  `Object.getOwnPropertyNames` along each element's whole prototype chain; it
  also fails when a `<form>` or `<select>` answers a named or indexed key.
  The result (588 names) is `HostContract/ClassInitialization/ElementNames.inc`;
  `element-names.test` re-enumerates and fails on any difference (`--write`
  regenerates).
- **`evaluateHelper`** takes `partial_eval::value` actuals; a declared DOM
  element parameter is a `reference` to its index (recorded in `prove`). It is
  truthy, `typeof` "object", strictly unequal to primitives, `nodeType` 1, and a
  constant key outside the name set reads undefined. `__` keys stay unknown (the
  binding adds `__selected`, `__cached_...` slots lazily), as does any key in the
  set, a numeric key, and a comparison of two different element parameters.
- **`foldDecidedArms`** decides on those results; a call that returns the same
  argument's element in every construction is replaced by that argument and
  erased. In DOM entries the uncalled-helper rule now applies to a captured
  helper whose only names are its proved capture transport (its closure only
  fills its fixed cell, only proved closures capture the cell, no `cell_get`),
  because those reads are recorded calls; other DOM helpers keep the census.
- **Soundness.** The typed DOM proof admits `set_property` only on fresh local
  objects, so no accepted program writes an element expando (pinned before and
  after construction and in an unused method), and the census refuses
  `Object.prototype` (pinned with `Object` declared). A non-element actual
  (`new Button({})`) stays undecided and keeps `a`'s census.
- **Fixtures** (class-dom): `class_element_arm` (authentic `r`/`a`, lowers and
  runs natively), `element_arm_expando{,_after,_method}`, `_prototype`,
  `_object` with pinned refusals.

**Measured DOM frontier** (`/tmp/bdom.js`, authentic W+B+Button+Data): still
`class own-key snapshot requires fixed constructor fields`, but from the
provider gate in `ownFieldSnapshots` (any snapshot outside closed-source
refuses), not from the arm. Without `dispose`'s own-key loop the next refusal
is `inherited DOM methods require per-leaf body proof` (`examine`, any DOM
class with inherited methods). Both are independent of element facts; the
non-inherited probe with the same helpers now lowers.
