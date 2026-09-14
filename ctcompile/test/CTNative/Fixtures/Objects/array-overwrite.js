// The original array.mlir indexed.js body, separately CALLED here. Keep that
// uncalled refusal control intact; this subject measures real native execution.
function stored() {
    var a = [1, 2];
    a[0] = 9;
    return a[0];
}

function widened_store() {
    var a = [1, 2];
    a[1] = 2.5;
    return a[0] + a[1] + a.length;
}

function saved_read() {
    var a = [1, 2, 3];
    a[1] = 7;
    var old = a[1];
    a[1] = 9;
    a[0] = 0;
    return old + a[1] + a[0] + a[2] + a.length;
}

function computed_index() {
    var a = [1, 2, 3];
    var index = 0;
    index = index + 1;
    a[index] = 8;
    return a[1] + a[2];
}

function stored_length() {
    var a = [1, 2];
    var length = a.length;
    a[0] = length;
    return a[0] + a.length;
}

var overwritten = stored();
var widened = widened_store();
var saved = saved_read();
var computed = computed_index();
var length_value = stored_length();
