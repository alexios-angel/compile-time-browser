// Four imported functions. The entire startup prefix is understood, but
// publication still needs a native owner and a live proof of future callees.
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
