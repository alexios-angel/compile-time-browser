// drive() calls formula() with two different constants; the generic formula must still serve
// both calls through boxed dispatch. The write to `effect` keeps both functions from folding.
function formula(input) {
    const record = {
        value: input * 10
    };
    effect = input;
    return record.value;
}

function drive() {
    return formula(1) * 100 + formula(2);
}
var effect = 0;
var observation = drive();
