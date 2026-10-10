// CTNative/Lowering/Objects/array/overwrite-loop.mlir checks this file too.
function overwritten_loop() {
    var a = [1, 2, 3];
    a[1] = 9;
    var total = 0;
    for (var i = 0; i < a.length; i++) {
        total += a[i];
    }
    return total;
}
var summed = overwritten_loop();
