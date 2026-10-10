// THE VARIANT VERIFIER, ONE expected-error PER CLAUSE.
//
// `!ctnative.variant` is the only ctnative type with a verifier, and what it
// enforces is a canonical form rather than well-formedness. Every clause below
// rejects a type that is a second spelling of one that already exists: a
// variant nests, `bottom` and `opt` lift out, `json` and `boxed` absorb.
//
// Why that is worth a verifier: `meet` compares types with `==` in three places
// (its own fast path, the union's dedupe, and any caller's fixpoint check), and
// MLIR's `==` on a parameterized type is structural. A lattice with two
// spellings for one element therefore has an `==` that answers false for two
// equal things, and a dataflow analysis over it does not converge. The
// verifier keeps that from being reachable by hand-written IR; `meet` itself
// only ever constructs canonical variants.
//
// A guard nobody has seen fire proves nothing (see test/Lowering/EmitC/
// each names the message it expects rather than matching "error:".



// RUN: ctjs-opt --split-input-file --verify-diagnostics %s

// expected-error @below {{a variant needs at least two alternatives}}
func.func private @one_way(!ctnative.variant<!ctnative.bool>)

// -----

// ON ONE LINE, AND IT HAS TO BE. `expected-error @below` matches a diagnostic
// on the next line, and MLIR reports a type error at the token that failed, so
// a type split across two lines reports on the second one and the expectation
// on the first goes unmatched. That failure reads as "the verifier did not
// fire", which is the opposite of what happened.
// expected-error @below {{a variant alternative may not itself be a variant}}
func.func private @nested(!ctnative.variant<!ctnative.bool, !ctnative.variant<!ctnative.num<f64>, !ctnative.str<utf8>>>)

// -----

// expected-error @below {{a variant alternative may not be `bottom`}}
func.func private @with_bottom(!ctnative.variant<!ctnative.bool, !ctnative.bottom>)

// -----

// Nullability is hoisted. The canonical spelling of "a boolean or a number,
// possibly absent" is `opt<variant<bool, num<f64>>>`; this rejects the same set
// of values written the other way round.
// expected-error @below {{a variant alternative may not be `opt`}}
func.func private @with_opt(!ctnative.variant<!ctnative.bool, !ctnative.opt<!ctnative.num<f64>>>)

// -----

// expected-error @below {{a variant alternative may not be `json` or `boxed`}}
func.func private @with_json(!ctnative.variant<!ctnative.bool, !ctnative.json>)

// -----

// expected-error @below {{a variant alternative may not be `json` or `boxed`}}
func.func private @with_boxed(!ctnative.variant<!ctnative.bool, !ctnative.boxed>)

// -----

// expected-error @below {{a variant may not repeat an alternative}}
func.func private @repeated(!ctnative.variant<!ctnative.bool, !ctnative.bool>)

// -----

// The one that must be accepted, so that the file cannot pass by rejecting
// everything. Four distinct, non-absorbing, non-lifting alternatives is exactly
// the cap and exactly canonical.
func.func private @canonical(
    !ctnative.variant<!ctnative.bool, !ctnative.num<f64>,
                      !ctnative.str<utf8>, !ctnative.vec<!ctnative.bool>>)
