// Array.prototype, over a receiver that is not an Array - and the seven
// methods that were simply absent.
//
// EVERY Array.prototype method is specified generic: `this` is ToObject'd and
// then read through [[Get]] with string index keys, so
// `Array.prototype.reduce.call({length: 2, 0: 'a', 1: 'b'}, f)` is ordinary
// JavaScript. Every one of them here opened with `detail::this_array`, which
// answers nullptr for anything that is not an array_object, and then returned a
// default - so all of it did nothing, quietly. Counted over test262 at the
// pinned hash (docs/test262.md), that shape is 91 of `reduce`'s 260 files, 89
// of `reduceRight`'s and about 580 across the nine iteration methods.
//
// THIS FILE IS THE REGRESSION NET, not that suite: test262 is opt-in
// (-DCTBROWSER_TEST262=ON), needs a 273 MB corpus that is deliberately not in
// the repository, and takes five minutes. The expected values are node's,
// checked against V8.
//
// Three deviations are asserted here as they ARE rather than as the
// specification has them, because each is a property of this engine's array
// representation rather than a bug in these methods:
//
//   * a real Array is iterated to `items.size()` and not to its `length`
//     property, because array_object records an index it refused to
//     materialise in `sparse` and raises `length` over it - so one assignment
//     can make `length` four billion, and a loop to it is a hang rather than an
//     answer. array_object's own comment names that deviation.
//   * an Array has no HOLES. `delete a[0]` removes nothing (see
//     context::delete_own_property), so nothing here can distinguish a hole
//     from an undefined in a real array. The hole cases below are all written
//     over a plain object, where HasProperty means something.
//   * the copying methods build a plain Array whatever they were called on;
//     there is no ArraySpeciesCreate and no `Symbol.species`.

#include "js_expect.hpp"

int main() {
    // ================================================================
    // 1. A PLAIN OBJECT AS THE RECEIVER
    // ================================================================
    js_expect("Array.prototype.join.call({length: 3, 0: 'a', 1: 'b', 2: 'c'}, '-')", "a-b-c");
    js_expect("Array.prototype.indexOf.call({length: 3, 0: 'a', 1: 'b', 2: 'c'}, 'b')", "1");
    js_expect("Array.prototype.lastIndexOf.call({length: 3, 0: 'a', 1: 'b', 2: 'a'}, 'a')", "2");
    js_expect("Array.prototype.includes.call({length: 2, 0: 1, 1: 2}, 2)", "true");
    js_expect("Array.prototype.slice.call({length: 3, 0: 'a', 1: 'b', 2: 'c'}, 1).join('')", "bc");
    js_expect("Array.prototype.map.call({length: 2, 0: 1, 1: 2}, function (x) { return x * 2; })"
              ".join(',')",
              "2,4");
    js_expect("Array.prototype.filter.call({length: 3, 0: 1, 1: 2, 2: 3},"
              " function (x) { return x > 1; }).join(',')",
              "2,3");
    js_expect("Array.prototype.reduce.call({length: 3, 0: 1, 1: 2, 2: 3},"
              " function (a, b) { return a + b; })",
              "6");
    js_expect("Array.prototype.reduceRight.call({length: 3, 0: 'a', 1: 'b', 2: 'c'},"
              " function (a, b) { return a + b; })",
              "cba");
    js_expect("Array.prototype.some.call({length: 2, 0: 1, 1: 9}, function (x) { return x > 5; })",
              "true");
    js_expect("Array.prototype.every.call({length: 2, 0: 1, 1: 9}, function (x) { return x > 5; })",
              "false");
    js_expect("(function () { var seen = ''; Array.prototype.forEach.call("
              "{length: 2, 0: 'a', 1: 'b'}, function (x) { seen += x; }); return seen; })()",
              "ab");
    // ...and `arguments`, which is the array-like every page has to hand. It
    // is a real Array in this engine (docs/script.md names that deviation), so
    // this asserts the answer rather than the mechanism.
    js_expect("(function () { return Array.prototype.join.call(arguments, '-'); })(1, 2, 3)",
              "1-2-3");

    // A HOLE IS SKIPPED by the iteration methods and READ AS undefined by
    // `find` and `includes` - the one difference between 23.1.3.15 and
    // 23.1.3.9 that a hole can show.
    js_expect("(function () { var n = 0; Array.prototype.forEach.call({length: 3, 1: 'x'},"
              " function () { n++; }); return n; })()",
              "1");
    js_expect("(function () { var n = 0; Array.prototype.find.call({length: 3, 1: 'x'},"
              " function () { n++; return false; }); return n; })()",
              "3");
    js_expect("Array.prototype.includes.call({length: 2, 0: 1}, undefined)", "true");
    js_expect("Array.prototype.indexOf.call({length: 2, 0: 1}, undefined)", "-1");

    // ================================================================
    // 2. `thisArg`, WHICH WAS ACCEPTED AND DROPPED
    // ================================================================
    js_expect("[1, 2].map(function (x) { return x + this.n; }, {n: 10}).join(',')", "11,12");
    js_expect("[1, 2].filter(function (x) { return x === this.n; }, {n: 2}).join(',')", "2");
    js_expect("[1, 2].some(function (x) { return x === this.n; }, {n: 2})", "true");
    js_expect("[1, 2].every(function (x) { return x < this.n; }, {n: 9})", "true");
    js_expect("[1, 2].find(function (x) { return x === this.n; }, {n: 2})", "2");
    js_expect("[1, 2].findIndex(function (x) { return x === this.n; }, {n: 2})", "1");
    js_expect("[1, 2].findLast(function (x) { return x === this.n; }, {n: 1})", "1");
    js_expect("(function () { var seen = 0;"
              " [1, 2].forEach(function (x) { seen += this.n; }, {n: 5}); return seen; })()",
              "10");

    // ================================================================
    // 3. THE TypeErrors THAT WERE SILENCE
    // ================================================================
    // A non-callable callback, and a `this` of null or undefined. Each used to
    // return a default and do nothing; 141 of built-ins/Array's failures were
    // "Expected a TypeError to be thrown but no exception was thrown at all".
    js_expect("(function () { try { [1].forEach(undefined); return 'no'; }"
              " catch (e) { return e.name; } })()",
              "TypeError");
    js_expect("(function () { try { [1].map(7); return 'no'; } catch (e) { return e.name; } })()",
              "TypeError");
    js_expect("(function () { try { Array.prototype.forEach.call(null, function () {});"
              " return 'no'; } catch (e) { return e.name; } })()",
              "TypeError");
    js_expect("(function () { try { Array.prototype.join.call(undefined); return 'no'; }"
              " catch (e) { return e.name; } })()",
              "TypeError");
    // Reduce of an empty array with no initial value. It answered `undefined`,
    // which is the one thing a fold is written to rely on not happening.
    js_expect("(function () { try { [].reduce(function (a, b) { return a + b; }); return 'no'; }"
              " catch (e) { return e.name; } })()",
              "TypeError");
    js_expect("(function () { try { [].reduceRight(function (a, b) { return a; }); return 'no'; }"
              " catch (e) { return e.name; } })()",
              "TypeError");
    js_expect("[].reduce(function (a, b) { return a + b; }, 7)", "7");
    // A comparator that is neither a function nor undefined, checked BEFORE
    // the receiver is read (23.1.3.30 step 1).
    js_expect("(function () { try { [3, 1].sort(null); return 'no'; }"
              " catch (e) { return e.name; } })()",
              "TypeError");
    js_expect("[3, 1].sort(undefined).join(',')", "1,3");

    // ================================================================
    // 4. `fromIndex`, WHICH BOTH SEARCHES ACCEPTED AND NEITHER READ
    // ================================================================
    js_expect("[1, 2, 1].indexOf(1, 1)", "2");
    js_expect("[1, 2, 1].indexOf(1, -1)", "2");
    js_expect("[1, 2, 1].indexOf(1, 5)", "-1");
    js_expect("[1, 2, 1].lastIndexOf(1)", "2");
    js_expect("[1, 2, 1].lastIndexOf(1, 1)", "0");
    js_expect("[1, 2, 1].lastIndexOf(1, -2)", "0");
    js_expect("[1, 2, 1].lastIndexOf(3)", "-1");
    js_expect("[1, 2, 3].includes(1, 1)", "false");
    js_expect("[1, 2, 3].includes(3, -1)", "true");
    // SameValueZero, which is the whole reason `includes` exists beside
    // `indexOf`: it finds a NaN and `indexOf` cannot.
    js_expect("[NaN].includes(NaN)", "true");
    js_expect("[NaN].indexOf(NaN)", "-1");

    // ================================================================
    // 5. SEPARATORS AND ENDS THAT ARE `undefined`
    // ================================================================
    // An absent argument and an explicit `undefined` are the same thing, and
    // testing the argument COUNT gets that wrong.
    js_expect("[1, 2].join(undefined)", "1,2");
    js_expect("[1, 2].join(null)", "1null2");
    js_expect("[1, 2, 3].fill(0, 1, undefined).join(',')", "1,0,0");
    js_expect("[1, 2, 3].slice(1, undefined).join(',')", "2,3");
    js_expect("[1, 2, 3].fill(0).join(',')", "0,0,0");
    js_expect("[1, 2, 3].fill(0, -2).join(',')", "1,0,0");

    // ================================================================
    // 6. THE SEVEN METHODS THAT DID NOT EXIST
    // ================================================================
    // Each read "TypeError: X is undefined, not a function", which is the
    // largest failure bucket test262 reports for this engine after the early
    // errors it cannot check at all.
    js_expect("typeof [].lastIndexOf + ',' + typeof [].reduceRight + ',' + typeof [].copyWithin"
              " + ',' + typeof []['with'] + ',' + typeof [].toReversed + ',' + typeof [].toSorted"
              " + ',' + typeof [].toSpliced + ',' + typeof [].toLocaleString",
              "function,function,function,function,function,function,function,function");
    js_expect("[1, 2, 3, 4, 5].copyWithin(0, 3).join(',')", "4,5,3,4,5");
    js_expect("[1, 2, 3, 4, 5].copyWithin(1, 3, 4).join(',')", "1,4,3,4,5");
    js_expect("[1, 2, 3, 4, 5].copyWithin(-2, -3, -1).join(',')", "1,2,3,3,4");
    // The overlapping case, which is the one a forwards loop gets wrong: the
    // source is read after the destination has already been written over it.
    js_expect("[1, 2, 3, 4, 5].copyWithin(1, 0).join(',')", "1,1,2,3,4");
    js_expect("[1, 2, 3].copyWithin(0, 1) === undefined", "false");

    // SPELLED AS A KEY. `with` is a reserved word, and `[].with(...)` is legal
    // member access only if the parser lets a keyword be a property name -
    // which is a question about ctjs, not about whether the method is here.
    js_expect("[1, 2, 3]['with'](1, 9).join(',')", "1,9,3");
    js_expect("[1, 2, 3]['with'](-1, 9).join(',')", "1,2,9");
    js_expect("(function () { var a = [1, 2, 3]; a['with'](0, 9); return a.join(','); })()",
              "1,2,3");
    js_expect("(function () { try { [1, 2]['with'](5, 0); return 'no'; }"
              " catch (e) { return e.name; } })()",
              "RangeError");
    js_expect("(function () { try { [1, 2]['with'](-5, 0); return 'no'; }"
              " catch (e) { return e.name; } })()",
              "RangeError");

    js_expect("[1, 2, 3].toReversed().join(',')", "3,2,1");
    js_expect("(function () { var a = [1, 2, 3]; a.toReversed(); return a.join(','); })()",
              "1,2,3");

    js_expect("[3, 1, 2].toSorted().join(',')", "1,2,3");
    js_expect("[10, 9].toSorted().join(',')", "10,9");
    js_expect("[10, 9].toSorted(function (a, b) { return a - b; }).join(',')", "9,10");
    js_expect("(function () { var a = [3, 1]; a.toSorted(); return a.join(','); })()", "3,1");
    js_expect("(function () { try { [1].toSorted(null); return 'no'; }"
              " catch (e) { return e.name; } })()",
              "TypeError");

    js_expect("[1, 2, 3].toSpliced(1, 1).join(',')", "1,3");
    js_expect("[1, 2, 3].toSpliced(1, 1, 'a', 'b').join(',')", "1,a,b,3");
    js_expect("[1, 2, 3].toSpliced(-1).join(',')", "1,2");
    js_expect("[1, 2, 3].toSpliced(1).join(',')", "1");
    js_expect("[1, 2, 3].toSpliced().length", "3");
    js_expect("(function () { var a = [1, 2, 3]; a.toSpliced(0, 3); return a.length; })()", "3");

    js_expect("[1, 2].toLocaleString()", "1,2");
    js_expect("[null, 1, undefined].toLocaleString()", ",1,");
    js_expect("(function () { var o = {toLocaleString: function () { return 'X'; }};"
              " return [o, 1].toLocaleString(); })()",
              "X,1");

    // ================================================================
    // 7. SORT: undefined LAST, AND NEVER COMPARED
    // ================================================================
    // 23.1.3.30 moves every undefined to the end without asking the comparator
    // about one. The default path used to run it through ToString, where
    // "undefined" sorts between "u" and "v".
    js_expect("['z', undefined, 'a'].sort().join(',')", "a,z,");
    // The COUNT of comparator calls is a merge sort's business; what the
    // specification fixes is that none of them is handed an undefined.
    js_expect("(function () { var saw = false; [1, undefined, 2].sort(function (a, b) {"
              " if (a === undefined || b === undefined) { saw = true; } return a - b; });"
              " return saw; })()",
              "false");
    js_expect("[1, undefined, 2].sort(function (a, b) { return a - b; }).join(',')", "1,2,");
    js_expect("[1, undefined, 2].sort(function (a, b) { return a - b; }).length", "3");
    js_expect("[3, 1, 2].sort(function (a, b) { return a - b; }).join(',')", "1,2,3");

    // ================================================================
    // 8. String.raw AND THE String STATICS
    // ================================================================
    js_expect("String.raw({raw: ['a', 'b', 'c']}, 1, 2)", "a1b2c");
    js_expect("String.raw({raw: ['a', 'b', 'c']}, 1)", "a1bc");
    js_expect("String.raw({raw: []})", "");
    js_expect("String.raw({raw: ['only']}, 'ignored')", "only");
    js_expect("String.raw.length", "1");
    // 22.1.2.2 step 2c: a code point must be an integer in [0, 0x10FFFF].
    // to_uint32 wrapped instead, so -1 produced the encoding of 0xFFFFFFFF.
    js_expect("(function () { try { String.fromCodePoint(-1); return 'no'; }"
              " catch (e) { return e.name; } })()",
              "RangeError");
    js_expect("(function () { try { String.fromCodePoint(1.5); return 'no'; }"
              " catch (e) { return e.name; } })()",
              "RangeError");
    js_expect("String.fromCodePoint(65, 66)", "AB");

    return ctbrowser_test_failures == 0 ? 0 : 1;
}
