# Direct loads and candidate provenance

## Inherited accessor correction, 2026-09-11

Runtime commit **552a4ba0**, merged as **ec83e488**, invokes accessors on the
implicit `Object.prototype` chain. Fresh allocation alone therefore proves
neither receiver confinement nor that an assignment creates an own field.
Property reads/writes now expose the receiver as `Passed`. Iteration exposes
its source while retaining the array alias edge; load provenance records both
effects independently, including later publication of a loaded candidate.

The separate contents query refuses ordinary-object assignments until an
independent own-data/prototype proof exists. This prevents an intercepted
assignment followed by deletion from incorrectly discharging the stored child.
Dense arrays keep their existing in-bounds Number-index proof. The earlier
ordinary-object refinement measurements below are historical; those admissions
are withdrawn by this correction, and their original source cases remain tests.

The inherited getter, setter and iteration regressions record receiver/child
retention through globals and report zero oracle violations. Their matching
compiler claims are `Passed` for receivers and `Stored` for the setter argument.
See [HANDOFF.md](HANDOFF.md) for the complete measured gate and remaining native
admission boundaries.

## Detailed sections

The following sections retain the complete original text and measurements.

<a id="direct-candidate-links"></a>
- [Direct candidate links](escape-load-evidence/01-direct-candidate-links.md#direct-candidate-links)
<a id="preceding-direct-census-measurement-2026-09-07"></a>
- [Preceding direct-census measurement, 2026-09-07](escape-load-evidence/01-direct-candidate-links.md#preceding-direct-census-measurement-2026-09-07)
<a id="bounded-candidate-provenance"></a>
- [Bounded candidate provenance](escape-load-evidence/01-direct-candidate-links.md#bounded-candidate-provenance)
<a id="measured-provenance-gate-2026-09-07"></a>
- [Measured provenance gate, 2026-09-07](escape-load-evidence/01-direct-candidate-links.md#measured-provenance-gate-2026-09-07)
<a id="complete-own-elements-for-a-bounded-local-subset"></a>
- [Complete own elements for a bounded local subset](escape-load-evidence/01-direct-candidate-links.md#complete-own-elements-for-a-bounded-local-subset)
<a id="bounded-retention-consumer"></a>
- [Bounded retention consumer](escape-load-evidence/01-direct-candidate-links.md#bounded-retention-consumer)
<a id="imported-frame-and-root-bookkeeping"></a>
- [Imported frame and root bookkeeping](escape-load-evidence/01-direct-candidate-links.md#imported-frame-and-root-bookkeeping)
<a id="acyclic-conditional-paths"></a>
- [Acyclic conditional paths](escape-load-evidence/01-direct-candidate-links.md#acyclic-conditional-paths)
<a id="fixed-own-properties-on-fresh-objects"></a>
- [Fixed own properties on fresh objects](escape-load-evidence/01-direct-candidate-links.md#fixed-own-properties-on-fresh-objects)
<a id="bounded-switch-paths"></a>
- [Bounded switch paths](escape-load-evidence/01-direct-candidate-links.md#bounded-switch-paths)
<a id="fixed-own-field-deletion"></a>
- [Fixed own-field deletion](escape-load-evidence/01-direct-candidate-links.md#fixed-own-field-deletion)
<a id="fresh-own-data-object-copies"></a>
- [Fresh own-data object copies](escape-load-evidence/01-direct-candidate-links.md#fresh-own-data-object-copies)
<a id="executed-copy-paths-and-the-raw-import-boundary"></a>
- [Executed copy paths and the raw-import boundary](escape-load-evidence/01-direct-candidate-links.md#executed-copy-paths-and-the-raw-import-boundary)
<a id="opaque-entry-register-transport"></a>
- [Opaque entry-register transport](escape-load-evidence/01-direct-candidate-links.md#opaque-entry-register-transport)
<a id="noncapturing-source-switch-producers"></a>
- [Noncapturing source-switch producers](escape-load-evidence/01-direct-candidate-links.md#noncapturing-source-switch-producers)
<a id="noncapturing-logical-negation"></a>
- [Noncapturing logical negation](escape-load-evidence/02-noncapturing-logical-negation.md#noncapturing-logical-negation)
<a id="noncapturing-typeof-and-void"></a>
- [Noncapturing typeof and void](escape-load-evidence/02-noncapturing-logical-negation.md#noncapturing-typeof-and-void)
<a id="static-binary-results-after-excluding-bigint"></a>
- [Static binary results after excluding BigInt](escape-load-evidence/02-noncapturing-logical-negation.md#static-binary-results-after-excluding-bigint)
<a id="arithmetic-unary-results-from-proved-primitive-inputs"></a>
- [Arithmetic unary results from proved primitive inputs](escape-load-evidence/02-noncapturing-logical-negation.md#arithmetic-unary-results-from-proved-primitive-inputs)
<a id="loose-equality-from-two-proved-primitive-origins"></a>
- [Loose equality from two proved primitive origins](escape-load-evidence/02-noncapturing-logical-negation.md#loose-equality-from-two-proved-primitive-origins)
<a id="primitive-relational-origins-2026-09-08"></a>
- [Primitive relational origins, 2026-09-08](escape-load-evidence/02-noncapturing-logical-negation.md#primitive-relational-origins-2026-09-08)
<a id="primitive-dynamic-arithmetic-origins-2026-09-08"></a>
- [Primitive dynamic arithmetic origins, 2026-09-08](escape-load-evidence/02-noncapturing-logical-negation.md#primitive-dynamic-arithmetic-origins-2026-09-08)
<a id="primitive-add-and-concat-origins-2026-09-08"></a>
- [Primitive Add and Concat origins, 2026-09-08](escape-load-evidence/02-noncapturing-logical-negation.md#primitive-add-and-concat-origins-2026-09-08)
<a id="exact-bigint-pair-equality-recovery-2026-09-09"></a>
- [Exact BigInt-pair equality recovery, 2026-09-09](escape-load-evidence/03-exact-bigint-pair-equality-recovery-2026-09-09.md#exact-bigint-pair-equality-recovery-2026-09-09)
<a id="exact-bigint-pair-relational-origins-2026-09-09"></a>
- [Exact BigInt-pair relational origins, 2026-09-09](escape-load-evidence/03-exact-bigint-pair-equality-recovery-2026-09-09.md#exact-bigint-pair-relational-origins-2026-09-09)
<a id="computed-bigint-unary-categories-2026-09-09"></a>
- [Computed BigInt unary categories, 2026-09-09](escape-load-evidence/03-exact-bigint-pair-equality-recovery-2026-09-09.md#computed-bigint-unary-categories-2026-09-09)
<a id="computed-bigint-addsubmul-categories-2026-09-09"></a>
- [Computed BigInt Add/Sub/Mul categories, 2026-09-09](escape-load-evidence/03-exact-bigint-pair-equality-recovery-2026-09-09.md#computed-bigint-addsubmul-categories-2026-09-09)
<a id="computed-static-bigint-categories-2026-09-09"></a>
- [Computed static BigInt categories, 2026-09-09](escape-load-evidence/03-exact-bigint-pair-equality-recovery-2026-09-09.md#computed-static-bigint-categories-2026-09-09)
<a id="computed-signed-bigint-shifts-2026-09-09"></a>
- [Computed signed BigInt shifts, 2026-09-09](escape-load-evidence/03-exact-bigint-pair-equality-recovery-2026-09-09.md#computed-signed-bigint-shifts-2026-09-09)
<a id="computed-bigint-division-and-remainder-2026-09-09"></a>
- [Computed BigInt division and remainder, 2026-09-09](escape-load-evidence/03-exact-bigint-pair-equality-recovery-2026-09-09.md#computed-bigint-division-and-remainder-2026-09-09)
<a id="owning-string-gate-and-bigint-pow-review-2026-09-09"></a>
- [Owning String gate and BigInt Pow review, 2026-09-09](escape-load-evidence/04-owning-string-gate-and-bigint-pow-review-2026-09-09.md#owning-string-gate-and-bigint-pow-review-2026-09-09)
<a id="computed-bigint-exponentiation-2026-09-09"></a>
- [Computed BigInt exponentiation, 2026-09-09](escape-load-evidence/04-owning-string-gate-and-bigint-pow-review-2026-09-09.md#computed-bigint-exponentiation-2026-09-09)
<a id="successful-stringbigint-add-and-concat-2026-09-09"></a>
- [Successful String/BigInt Add and Concat, 2026-09-09](escape-load-evidence/04-owning-string-gate-and-bigint-pow-review-2026-09-09.md#successful-stringbigint-add-and-concat-2026-09-09)
<a id="mixed-primitive-bigint-comparison-retention-2026-09-09"></a>
- [Mixed primitive BigInt comparison retention, 2026-09-09](escape-load-evidence/04-owning-string-gate-and-bigint-pow-review-2026-09-09.md#mixed-primitive-bigint-comparison-retention-2026-09-09)
<a id="mixed-primitive-bigint-div-recovery-2026-09-10"></a>
- [Mixed primitive BigInt Div recovery, 2026-09-10](escape-load-evidence/04-owning-string-gate-and-bigint-pow-review-2026-09-09.md#mixed-primitive-bigint-div-recovery-2026-09-10)
<a id="mixed-primitive-bigint-mod-retention-2026-09-10"></a>
- [Mixed primitive BigInt Mod retention, 2026-09-10](escape-load-evidence/05-mixed-primitive-bigint-mod-retention-2026-09-10.md#mixed-primitive-bigint-mod-retention-2026-09-10)
