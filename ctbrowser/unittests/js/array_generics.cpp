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

    // ================================================================
    // 9. THE METHODS THAT MUTATE, OVER A RECEIVER THAT IS NOT AN ARRAY
    // ================================================================
    // push, pop, shift, unshift, splice, concat, reverse, sort, flat and
    // flatMap are specified exactly as generic as the eighteen that read, and
    // every one of them opened with `detail::this_array` too. The difference is
    // that these WRITE: the algorithm is a sequence of [[Set]] and [[Delete]]
    // calls in a fixed order, ending with Set(O, "length", n, true), and it is
    // the length write-back and the deleted slot that a test can see.
    //
    // Expected values are node's.

    // --- push (23.1.3.23): the elements land, and `length` is written back ---
    js_expect("(function () { var o = {length: 2, 0: 'a', 1: 'b'};"
              " var n = Array.prototype.push.call(o, 'c');"
              " return n + ':' + o.length + ':' + o[2]; })()",
              "3:3:c");
    // AN ABSENT `length` IS ZERO, not "not an array-like": ToLength(undefined)
    // is 0, so the first push lands at index 0 and defines `length` as 1.
    js_expect("(function () { var o = {}; var n = Array.prototype.push.call(o, -1);"
              " return n + ':' + o.length + ':' + o[0]; })()",
              "1:1:-1");
    js_expect("(function () { var o = {length: null}; Array.prototype.push.call(o, -7);"
              " return o.length + ':' + o[0]; })()",
              "1:-7");
    // 2^53-1 IS A TypeError AND `length` ITSELF IS A CLAMP. Both are step 5 of
    // 23.1.3.23 and they disagree on purpose: pushing nothing onto a receiver
    // claiming an impossible length succeeds and repairs the length.
    js_expect("(function () { var o = {length: Infinity}; Array.prototype.push.call(o);"
              " return o.length; })()",
              "9007199254740991");
    js_expect("(function () { var o = {length: 9007199254740991};"
              " try { Array.prototype.push.call(o, 1); return 'no'; }"
              " catch (e) { return e.name; } })()",
              "TypeError");

    // --- pop (23.1.3.22): the slot is DELETED, not left holding its value ---
    js_expect("(function () { var o = {length: 2, 0: 'a', 1: 'b'};"
              " var v = Array.prototype.pop.call(o);"
              " return v + ':' + o.length + ':' + (1 in o); })()",
              "b:1:false");
    // An empty receiver still writes `length` back - step 3a - which is what
    // turns a `length` of NaN into 0.
    js_expect("(function () { var o = {length: NaN};"
              " var v = Array.prototype.pop.call(o);"
              " return (v === undefined) + ':' + o.length; })()",
              "true:0");

    // --- shift (23.1.3.25): everything moves DOWN and the top is deleted -----
    js_expect("(function () { var o = {length: 3, 0: 'a', 1: 'b', 2: 'c'};"
              " var v = Array.prototype.shift.call(o);"
              " return v + ':' + o.length + ':' + o[0] + o[1] + ':' + (2 in o); })()",
              "a:2:bc:false");
    // A HOLE MOVING DOWN DELETES WHAT IT LANDS ON rather than filling it with
    // undefined, which is the whole of step 6c.ii of the algorithm.
    js_expect("(function () { var o = {length: 3, 0: 'a', 2: 'c'};"
              " Array.prototype.shift.call(o); return (0 in o) + ':' + o[1]; })()",
              "false:c");

    // --- unshift (23.1.3.32): everything moves UP, from the top down ---------
    js_expect("(function () { var o = {length: 2, 0: 'a', 1: 'b'};"
              " var n = Array.prototype.unshift.call(o, 'z');"
              " return n + ':' + o.length + ':' + o[0] + o[1] + o[2]; })()",
              "3:3:zab");
    js_expect("(function () { var o = {length: 9007199254740991};"
              " try { Array.prototype.unshift.call(o, 1); return 'no'; }"
              " catch (e) { return e.name; } })()",
              "TypeError");

    // --- splice (23.1.3.29) -------------------------------------------------
    js_expect("(function () { var o = {length: 3, 0: 'a', 1: 'b', 2: 'c'};"
              " var r = Array.prototype.splice.call(o, 1, 1);"
              " return r.join('') + ':' + o.length + ':' + o[0] + o[1] + ':' + (2 in o); })()",
              "b:2:ac:false");
    // GROWING WALKS THE TAIL BACKWARDS, so an overlapping move never overwrites
    // a source before it has been read.
    js_expect("(function () { var o = {length: 2, 0: 'a', 1: 'b'};"
              " Array.prototype.splice.call(o, 1, 0, 'x', 'y');"
              " return o.length + ':' + o[0] + o[1] + o[2] + o[3]; })()",
              "4:axyb");
    // `splice()` WITH NO ARGUMENT AT ALL DELETES NOTHING - step 6 - and only
    // `splice(i)` deletes to the end. Testing the argument count for both read
    // the no-argument call as "delete everything from index 0".
    js_expect("[1, 2, 3].splice().length", "0");
    js_expect("(function () { var a = [1, 2, 3]; a.splice(); return a.join(','); })()", "1,2,3");
    js_expect("[1, 2, 3].splice(1).join(',')", "2,3");
    js_expect("(function () { var a = [1, 2, 3]; a.splice(1); return a.join(','); })()", "1");
    js_expect("(function () { var a = [1, 2, 3]; a.splice(1, Infinity); return a.join(','); })()",
              "1");

    // --- concat (23.1.3.1): the RECEIVER spreads only if it IS an array ------
    // There is no Symbol.isConcatSpreadable here, so `IsArray` is the whole
    // test: an array-like receiver is ONE element of the result.
    js_expect("(function () { var o = {length: 2, 0: 'a', 1: 'b'};"
              " var r = Array.prototype.concat.call(o, 1, [2, 3]);"
              " return r.length + ':' + (r[0] === o) + ':' + r[1] + r[2] + r[3]; })()",
              "4:true:123");
    js_expect("[1].concat([2, 3], {length: 2, 0: 'x'}).length", "4");
    js_expect("[1].concat([2, 3]).join(',')", "1,2,3");

    // --- reverse (23.1.3.26) ------------------------------------------------
    js_expect("(function () { var o = {length: 3, 0: 'a', 1: 'b', 2: 'c'};"
              " Array.prototype.reverse.call(o); return o[0] + o[1] + o[2]; })()",
              "cba");
    // A HOLE OPPOSITE AN ELEMENT DELETES THE FAR SIDE rather than filling it,
    // which is the only thing separating reverse from read-all-write-back.
    js_expect("(function () { var o = {length: 2, 1: 'b'};"
              " Array.prototype.reverse.call(o); return o[0] + ':' + (1 in o); })()",
              "b:false");
    js_expect("(function () { var a = [1, 2, 3]; a.reverse(); return a.join(','); })()", "3,2,1");

    // --- sort (23.1.3.30) ---------------------------------------------------
    js_expect("(function () { var o = {length: 3, 0: 3, 1: 1, 2: 2};"
              " Array.prototype.sort.call(o, function (x, y) { return x - y; });"
              " return o[0] + ',' + o[1] + ',' + o[2]; })()",
              "1,2,3");
    // SortIndexedProperties SKIPS THE HOLES and the vacated tail is deleted, so
    // a hole ends up after everything - including after an undefined.
    js_expect("(function () { var o = {length: 3, 0: 'b', 2: 'a'};"
              " Array.prototype.sort.call(o); return o[0] + o[1] + ':' + (2 in o); })()",
              "ab:false");

    // --- flat and flatMap ---------------------------------------------------
    js_expect("Array.prototype.flat.call({length: 1, 0: [1]}).join(',')", "1");
    js_expect("Array.prototype.flat.call({length: undefined, 0: [1]}).length", "0");
    // The depth goes through ToIntegerOrInfinity: an explicit `undefined` is
    // the DEFAULT of 1, a string or an object is 0.
    js_expect("[1, [2]].flat(undefined).join(',')", "1,2");
    js_expect("[1, [2]].flat('TestString').length", "2");
    js_expect("[1, [2]].flat(0).length", "2");
    js_expect("[1, [2, [3]]].flat(Infinity).join(',')", "1,2,3");
    // Only a real Array flattens - an array-LIKE element is one element.
    js_expect("[1, {length: 1, 0: 2}].flat().length", "2");
    js_expect("[1, 2].flatMap(function (x) { return [x, x * this.n]; }, {n: 10}).join(',')",
              "1,10,2,20");
    js_expect("Array.prototype.flatMap.call({length: 2, 0: 1, 1: 2},"
              " function (x) { return x; }).join(',')",
              "1,2");
    js_expect(
        "(function () { try { [1].flatMap(7); return 'no'; } catch (e) { return e.name; } })()",
        "TypeError");

    // --- A NULLISH RECEIVER IS A TypeError, not a silent default ------------
    js_expect("(function () { try { Array.prototype.push.call(null, 1); return 'no'; }"
              " catch (e) { return e.name; } })()",
              "TypeError");
    js_expect("(function () { try { Array.prototype.pop.call(undefined); return 'no'; }"
              " catch (e) { return e.name; } })()",
              "TypeError");
    js_expect("(function () { try { Array.prototype.shift.call(null); return 'no'; }"
              " catch (e) { return e.name; } })()",
              "TypeError");
    js_expect("(function () { try { Array.prototype.unshift.call(undefined, 1); return 'no'; }"
              " catch (e) { return e.name; } })()",
              "TypeError");
    js_expect("(function () { try { Array.prototype.splice.call(null, 0); return 'no'; }"
              " catch (e) { return e.name; } })()",
              "TypeError");
    js_expect("(function () { try { Array.prototype.concat.call(null); return 'no'; }"
              " catch (e) { return e.name; } })()",
              "TypeError");
    js_expect("(function () { try { Array.prototype.reverse.call(undefined); return 'no'; }"
              " catch (e) { return e.name; } })()",
              "TypeError");
    js_expect("(function () { try { Array.prototype.sort.call(null); return 'no'; }"
              " catch (e) { return e.name; } })()",
              "TypeError");
    js_expect("(function () { try { Array.prototype.flat.call(null); return 'no'; }"
              " catch (e) { return e.name; } })()",
              "TypeError");
    js_expect("(function () { try { Array.prototype.flatMap.call(null, function () {});"
              " return 'no'; } catch (e) { return e.name; } })()",
              "TypeError");

    // A STRING RECEIVER CANNOT BE MUTATED. Every Set in these algorithms
    // carries Throw=true, so this is a TypeError in sloppy mode as well -
    // unlike a bare `s[0] = 'x'`, which is silently discarded.
    js_expect("(function () { try { Array.prototype.push.call('abc', 1); return 'no'; }"
              " catch (e) { return e.name; } })()",
              "TypeError");
    js_expect("(function () { try { Array.prototype.shift.call(''); return 'no'; }"
              " catch (e) { return e.name; } })()",
              "TypeError");
    // ...and a FROZEN array cannot either, for the same reason.
    js_expect("(function () { var a = []; Object.freeze(a);"
              " try { a.push(); return 'no'; } catch (e) { return e.name; } })()",
              "TypeError");
    js_expect("(function () { var a = [1]; Object.freeze(a);"
              " try { a.push(2); return 'no'; } catch (e) { return e.name; } })()",
              "TypeError");
    js_expect("(function () { var a = [1]; Object.freeze(a);"
              " try { a.pop(); return 'no'; } catch (e) { return e.name; } })()",
              "TypeError");

    // --- ORDER OF OPERATIONS, which is what a Proxy or an accessor counts ---
    // `length` is read ONCE, before anything is written, and written ONCE,
    // after everything is. A method that re-read it per iteration would see a
    // second `get` here, and one that wrote each element through `length`
    // would see a second `set`.
    js_expect("(function () { var log = [];"
              " var o = {0: 'a', 1: 'b',"
              "   get length() { log.push('get'); return 2; },"
              "   set length(v) { log.push('set ' + v); } };"
              " Array.prototype.push.call(o, 'c'); return log.join('|'); })()",
              "get|set 3");
    // `splice` with no arguments still reads `length` once and writes it once
    // (steps 2 and 24), and the value written is ToLength of what it read.
    js_expect("(function () { var log = [];"
              " var o = { get length() { log.push('get'); return '0'; },"
              "   set length(v) { log.push('set ' + v); } };"
              " Array.prototype.splice.call(o); return log.join('|'); })()",
              "get|set 0");
    // The element is READ BEFORE THE SLOT IS DELETED, and the length written
    // after both - 23.1.3.22 steps 4c, 4d, 4e in that order.
    js_expect("(function () { var log = [];"
              " var o = {0: 'a',"
              "   get length() { log.push('get length'); return 1; },"
              "   set length(v) { log.push('set length ' + v); } };"
              " var v = Array.prototype.pop.call(o);"
              " return v + ':' + log.join('|') + ':' + (0 in o); })()",
              "a:get length|set length 0:false");

    // --- THE FAST PATH IS STILL THE FAST PATH -------------------------------
    // A real Array takes the vector operation, and the generic walk must not
    // have changed a single answer on one.
    js_expect("(function () { var a = [1, 2, 3]; a.push(4); a.unshift(0); return a.join(','); })()",
              "0,1,2,3,4");
    js_expect("(function () { var a = [1, 2, 3];"
              " return a.pop() + ':' + a.shift() + ':' + a.join(','); })()",
              "3:1:2");
    js_expect("[].pop() === undefined", "true");
    js_expect("[].shift() === undefined", "true");
    js_expect("[1, 2, 3].splice(-1).join(',')", "3");
    js_expect("(function () { var a = [1, 2, 3]; a.splice(1, 1, 'x', 'y');"
              " return a.join(','); })()",
              "1,x,y,3");

    // --- THE OWN `length` AND `name` OF EACH OF THEM ------------------------
    // Clause 17 and 10.2.5: the specified arity as `length`, the property key
    // as `name`, both { writable: false, enumerable: false, configurable: true }.
    js_expect("[].push.length + ',' + [].pop.length + ',' + [].shift.length + ','"
              " + [].unshift.length + ',' + [].splice.length + ',' + [].concat.length + ','"
              " + [].reverse.length + ',' + [].fill.length + ',' + [].flat.length + ','"
              " + [].flatMap.length + ',' + [].sort.length",
              "1,0,0,1,2,1,0,1,0,1,1");
    js_expect("[].push.name + ',' + [].flatMap.name + ',' + [].splice.name", "push,flatMap,splice");
    js_expect("Object.getOwnPropertyDescriptor([].push, 'length').writable + ','"
              " + Object.getOwnPropertyDescriptor([].push, 'length').enumerable + ','"
              " + Object.getOwnPropertyDescriptor([].push, 'length').configurable",
              "false,false,true");

    return ctbrowser_test_failures == 0 ? 0 : 1;
}
