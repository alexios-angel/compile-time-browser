/* beautify ignore:start */
// Imports as four functions. Host-prefix analysis understands all of startup, but the
// published table still needs a native owner and a proof of every later callee.
var host = {};
(function(factory) {
    host.slot = factory();
})(function() {
    const state = new Map();
    return {
        get() { return state.size; }
    };
});
var trace = host.slot.get();
/* beautify ignore:end */
