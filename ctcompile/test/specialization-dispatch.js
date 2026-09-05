// The generic entry still receives both tuples through boxed dispatch.
// The write keeps the caller and the formula's heap prefix from folding away.
function formula(input) {
    const record = {value: input * 10};
    effect = input;
    return record.value;
}
function drive() {
    return formula(1) * 100 + formula(2);
}
var effect = 0;
var observation = drive();
