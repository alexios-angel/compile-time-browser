# Design memo: Bootstrap component Data registration (read-only review, 2026-09-27)

Authentic W + B + Button U + real Data holder, `new U(null, null)`, first refuses
with `class own-key snapshot constructor observes its receiver` (Sources.cpp ~864-867):
U's arm is live (alwaysThrows stops at U's own NAME), `this` flows into
`e.set` -> Data.set's `n` -> `s.set(i, n)`. Behind it: the branch join
(`class own-key snapshot branches change ordered fields`, ~808-816).

Why the publication proofs decline: sinkCapturedPublication (Proof.cpp:205,
Publication.cpp:122-123/166/239-243/305-351) needs one capture, entry-block call,
literal/forwarded keys, non-base class; normalizeNestedMaps (CapturedMapHelpers.cpp
107-132) needs the Data Map in the entry block; sinkConstructorPublication
(Publication.cpp:852/884-893) needs one upvalue and a 2-arg Map set in the same
block. Key typing: retainedMapAliases literal String keys (Sources.cpp:1062-1064);
nested routing folds outer keys that are literal Strings, fresh entry-block
objects or exact DOM element inputs, inner keys literal (:528-532, :563-592),
and needs statically known branch conditions (:441-445). There is no native
runtime identity-keyed Map (class-map-optional-key-object stays boxed, refused).
DATA_KEY is inlined only in rewrite(); a fold must happen in examine(U) after
cloneReceiverGetters and normalizeSuper, under throwingRead's exactness guards.

Plan (smallest first):
- R1a: decide constructor arms from exact actuals. New foldDecidedArms(closure,
  instances) right after normalizeSuper in examine. Class not in baseClasses, >=1
  instance; top-level scf.if on ctjs.truthy v; sourceValue(v) a constructor formal;
  every actual a primitive ConstantOp (missing = undefined); partial_eval::truthy
  agrees for all instances. Splice the selected region; erase dropped ops from
  callableCaptures/holderCaptures/mapCaptures/helperCalls/mapOperations; charge
  steps before mutating. No new messages. Fixtures (54.mlir): positive
  constructor-arm-falsy-actual (dispose snapshot + real Data holder,
  `t && (this._element = t, e.set(t, "bs.leaf", this))`, new Leaf(null) and new
  Leaf()); refusals -mixed-actual, -truthy-actual, -extended, -captured-parameter.
- R1b: evaluate an exact pure helper on those actuals (`(t = a(t)) && ...`):
  v may be an undefined-receiver call of a proved capture; evaluateHelper walks
  only the executed path (Constant/Truthy/FromBool, Unary/Compare/Binary via
  partial_eval, scf.if with known condition, LoadUpvalue of a fixed helper cell,
  nested exact calls, primitive return). Positive with authentic r/a; refusals
  -helper-effect, -helper-object, -helper-host, -helper-replaced. Generate
  bootstrap-component-data.js beside bootstrap-base-data; expected frontier then
  "declared intrinsic identity escapes, is reflected or is mutated" (W's
  Object.entries), same as bootstrap-base-data.
- Later, only for real elements: R2 exactStaticString(read) key fold in
  sinkConstructorPublication; R3 element facts so a(el) is el (DOM data session);
  R4 leaf holder publication after super normalization, then re-run
  normalizeCapturedMapHelpers/normalizeNestedMaps (new refusal "leaf holder
  publication lacks completed nested routing").
- Skipped: live undecided arms (sometimes-present fields + runtime identity Map).

Check first: inherited-own-fields-implicit refuses implicit derived constructors
("derived class requires receiver-preserving super normalization"); authentic U
has none of its own. The 2026-09-27 ladder probe (tmp driver) did reach the
snapshot refusal with authentic U, so that path differs - confirm with a fixture.

## Correction, measured after R1a landed (b37bd1d9b)

R1a (decided constructor arms) is in. But on the authentic probe (W + B + Button
U + real Data holder, `new U(null, null)`) the snapshot refusal comes from
**B's own examine**, before U is examined: B is a base class, so neither the
throwing-arm proof nor R1a (both require a non-extended class) applies to B's
arm, and B's `dispose` makes B's own-key snapshot proof walk B's constructor,
which publishes `this` into `e.set`. Removing B's snapshot line moves the probe
to `static method receiver requires an exact own getter read`
(`getOrCreateInstance`'s `new this`). U's implicit constructor is
`gather_rest` + `call_spread` (super(...args)).

So the first component rung is a base-class one: B's snapshot proof for a base
that is constructed only through its leaves' super calls. Leaves consume the
base's result through `snapshotFields[base]` (ownFieldSnapshots), so the base
proof cannot simply be skipped: either compute B's fields per leaf after super
normalization (where R1a/R1b can decide the arm on the leaf's actuals), or
record B's two arm shapes and let each leaf select one. R1b (helper
evaluation) only pays off after that.
