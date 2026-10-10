// The exporter. Interpreted in every arm, on purpose: the differential holds
// one thing still, and the thing under test is the importing module's top
// level - which is where op::load_import, op::bind_export and
// op::load_namespace are emitted and nowhere else.
//
// `count` is reassigned after the importer has taken its cell, which is the
// only shape that separates a live binding from a copied value. An importer
// holding the box reads 2; one holding the value reads 1, and every other
// assertion in the fixture would still pass.
export let count = 1;

// A second export whose name and value are both distinct from the first, so a
// lowering that read the wrong name index answers "D" where 2 is right, or
// raises, rather than answering a coincidentally equal number.
export const tag = "D";

// The default, whose export name is the synthesised string `default` rather
// than anything in the source - a name the importing side must ask for by that
// spelling and not by the local one it binds to.
export default 7;

export function bump() {
    count = count + 1;
}
