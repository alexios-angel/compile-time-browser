// console.log of a Number-or-String: Runtime/Console.hpp has no carrier for
// that union, so the lowering must refuse the function with its reason
// (console-log.test).
function console_log_union(flag) {
    console.log(flag ? 1 : 'one');
}
console_log_union(false);
console_log_union(true);
