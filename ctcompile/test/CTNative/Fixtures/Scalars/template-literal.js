// Template-literal lowering: a `${}` chain becomes one ctnative::format call,
// and literal `{` and `}` reach std::format escaped as `{{` and `}}`. Each
// console.log is a line the VM prints; template-literal.test runs the same
// file through the native pipeline and requires byte-equal stdout. Numbers go
// through the runtime's Number::toString, so 1e21 prints as `1e+21` and 1e-7
// as `1e-7`, the JavaScript spellings.
function template_literal_kinds(flag) {
    console.log(`plain`);
    console.log(`sum ${0.1 + 0.2}`);
    console.log(`neg ${-0}`);
    console.log(`big ${1e21}`);
    console.log(`small ${1e-7}`);
    console.log(`nan ${0 / 0}`);
    console.log(`inf ${1 / 0}`);
    console.log(`nested ${`inner ${1 + 1}`} outer`);
    console.log(`braces {x} and {{y}}: ${flag}`);
    console.log(`multi ${1} ${"two"} ${true} ${null} ${void 0}`);
}
function template_nul_before(value) {
    const text = `pre\0${value}post`;
    console.log(text.length, text === "pre\0" + value + "post");
}
function template_nul_after(value) {
    const text = `${value}\0tail`;
    console.log(text.length, text === value + "\0tail");
}
template_literal_kinds(3);
template_nul_before(3);
template_nul_after(3);
