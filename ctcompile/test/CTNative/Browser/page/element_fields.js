// A closed-shape object whose field holds an element or null, as Collapse's
// config holds `parent` (bootstrap.bundle.js: `parent: null` in its defaults,
// then `t.parent = a(t.parent)`). The field is one std::optional element
// member (Lowering/ObjectValues/VariantFields.cpp), written null, an element
// that querySelector may not find, and an element. element_fields.test runs
// element_fields_report() on the interpreter and natively over
// element_fields.html and compares the answers.
var elementFieldsAnswer = (function () {
    function run() {
        const root = document.documentElement;
        const config = {
            parent: null,
            toggle: true
        };
        let out = (config.parent == null ? 'none' : 'some') + ',';
        config.parent = root.querySelector('#list');
        out += (config.parent ? config.parent.id : 'none') + ',';
        config.parent = root.querySelector('#missing');
        out += (config.parent == null ? 'none' : 'some') + ',';
        if (config.toggle) {
            config.parent = root;
        }
        return out + (config.parent ? config.parent.id : 'none');
    }

    return run();
})();

function element_fields_report() {
    return elementFieldsAnswer + '';
}
