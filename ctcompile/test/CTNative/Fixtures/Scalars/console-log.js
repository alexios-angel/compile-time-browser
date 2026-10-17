// console.log of each kind native code can print, one line per call, run by
// console-log.test under the VM and natively. Only functions are global, so the
// native program prints nothing but these lines. The two calls keep `flag`
// unknown, so the optional Number and the optional String stay optional.
function console_log_kinds(flag) {
    console.log('text');
    console.log('');
    console.log(1.5);
    console.log(-0);
    console.log(1 / 0);
    console.log(-1 / 0);
    console.log(0 / 0);
    console.log(1e21);
    console.log(0.1 + 0.2);
    console.log(true);
    console.log(flag);
    console.log(null);
    console.log(void 0);
    console.log('a', 2, false);
    console.log(flag ? 3 : null);
    console.log(flag ? 'present' : void 0);
}
console_log_kinds(false);
console_log_kinds(true);
