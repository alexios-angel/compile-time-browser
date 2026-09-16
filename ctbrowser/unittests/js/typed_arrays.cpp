// Typed arrays, ArrayBuffer and DataView - clauses 23.2, 25.1 and 25.3 - as
// built by lib/Script/builtins/collections/typed_arrays/. vm_stdlib.cpp keeps
// the older coercion assertions; this file is the rest: the %TypedArray%
// intrinsic and its prototype methods, buffers that resize and detach, a
// DataView in both byte orders, and the Uint8Array base64/hex codecs.
//
// The lines that are NOT here are the ones the VM cannot answer yet (the
// constructors file says which): `ta.buffer === ta.buffer`, a subclass
// instance.

#include "js_expect.hpp"

int main() {
    const auto throws = [](const char * expression, const char * error) {
        js_expect(std::string{"(function(){try{return "} + expression +
                      "}catch(e){return \"THROWS \"+e.name}})()",
                  std::string{"THROWS "} + error);
    };

    // --- %TypedArray% and the prototype chain, 23.2.1 / 23.2.7 --------------
    js_expect("Object.getPrototypeOf(Int8Array) === Object.getPrototypeOf(Float64Array)", "true");
    js_expect("Object.getPrototypeOf(Int8Array).name", "TypedArray");
    js_expect("Object.getPrototypeOf(Uint8Array.prototype) === "
              "Object.getPrototypeOf(Int8Array).prototype",
              "true");
    js_expect("Int8Array.from === Object.getPrototypeOf(Int8Array).from", "true");
    js_expect("Int8Array[Symbol.species] === Int8Array", "true");
    js_expect("Object.prototype.toString.call(new Uint16Array(1))", "[object Uint16Array]");
    throws("Object.getPrototypeOf(Int8Array)()", "TypeError");
    throws("new (Object.getPrototypeOf(Int8Array))()", "TypeError");
    throws("Int8Array(2)", "TypeError");

    // --- the constructor's five shapes, 23.2.5.1 ------------------------------
    js_expect("new Uint8Array().length", "0");
    js_expect("new Float32Array(3).length", "3");
    throws("new Uint8Array(-1)", "RangeError");
    throws("new Uint8Array(2 ** 53)", "RangeError");
    js_expect("new Int16Array(new Uint8Array([1, 300])).join()", "1,44");
    js_expect("new Uint8Array({length: 2, 0: 7, 1: 8}).join()", "7,8");
    js_expect("new Uint8Array(new Set([3, 4])).join()", "3,4");
    js_expect("new Uint8Array(new ArrayBuffer(8), 2, 3).length", "3");
    js_expect("new Uint8Array(new ArrayBuffer(8), 2).length", "6");
    js_expect("new Int32Array(new ArrayBuffer(8), 4).byteOffset", "4");
    throws("new Int32Array(new ArrayBuffer(8), 2)", "RangeError");
    throws("new Int32Array(new ArrayBuffer(6))", "RangeError");
    throws("new Int32Array(new ArrayBuffer(8), 4, 2)", "RangeError");
    throws("new Int32Array(new ArrayBuffer(8), 12)", "RangeError");
    // A view SHARES the bytes, in both directions, at any offset.
    js_expect("(() => { const b = new ArrayBuffer(4); const u8 = new Uint8Array(b); "
              "new Uint16Array(b, 2, 1)[0] = 258; return u8.join(); })()",
              "0,0,2,1");
    js_expect("(() => { const a = new Uint8Array([1, 2, 3, 4]); const s = a.subarray(1, 3); "
              "s[0] = 9; return a.join() + '|' + s.byteOffset; })()",
              "1,9,3,4|1");
    js_expect("(() => { const a = new Uint8Array([1, 2]); const c = a.slice(); c[0] = 9; "
              "return a[0]; })()",
              "1");

    // --- from and of coerce into the KIND, 23.2.2 ------------------------------
    js_expect("Float32Array.from([1.5]).join() + '|' + Uint8Array.from([1.5]).join()", "1.5|1");
    js_expect("Uint8Array.from([1, 2], x => x * 2).join()", "2,4");
    js_expect("Uint8Array.from({length: 2, 0: 5, 1: 6}).join()", "5,6");
    js_expect("Int8Array.of(1, -1, 300).join()", "1,-1,44");
    throws("Int8Array.from([1], 'nope')", "TypeError");

    // --- the prototype methods, 23.2.3 ----------------------------------------
    js_expect("new Uint8Array([1, 2, 3]).at(-1)", "3");
    js_expect("new Uint8Array([1, 2, 3, 4, 5]).copyWithin(0, 3).join()", "4,5,3,4,5");
    js_expect("new Uint8Array([1, 2, 3, 4, 5]).copyWithin(1, 0, 3).join()", "1,1,2,3,5");
    js_expect("[...new Uint8Array([1, 2]).entries()].join('|')", "0,1|1,2");
    js_expect("[...new Uint8Array([1, 2]).keys()].join()", "0,1");
    js_expect("[...new Uint8Array([1, 2]).values()].join()", "1,2");
    js_expect("[...new Uint8Array([5, 6])].join()", "5,6");
    js_expect("Array.from(new Uint8Array([5, 6])).join()", "5,6");
    js_expect("new Uint8Array([2, 4]).every(x => x % 2 === 0)", "true");
    js_expect("new Uint8Array([2, 3]).some(x => x % 2 === 1)", "true");
    js_expect("new Uint8Array(4).fill(7, 1, 3).join()", "0,7,7,0");
    js_expect("new Int8Array([1, -2, 3]).filter(x => x > 0).join()", "1,3");
    js_expect("new Int8Array([1, -2, 3]).filter(x => x > 0) instanceof Int8Array", "true");
    js_expect("new Uint8Array([1, 2, 3]).find(x => x > 1)", "2");
    js_expect("new Uint8Array([1, 2, 3]).findIndex(x => x > 1)", "1");
    js_expect("new Uint8Array([1, 2, 3]).findLast(x => x < 3)", "2");
    js_expect("new Uint8Array([1, 2, 3]).findLastIndex(x => x > 5)", "-1");
    js_expect("(() => { let s = 0; new Uint8Array([1, 2]).forEach(x => s += x); return s; })()",
              "3");
    js_expect("new Float32Array([NaN]).includes(NaN)", "true");
    js_expect("new Float32Array([NaN]).indexOf(NaN)", "-1");
    js_expect("new Uint8Array([1, 2, 1]).lastIndexOf(1)", "2");
    js_expect("new Uint8Array([1, 2, 1]).indexOf(1, 1)", "2");
    js_expect("new Uint8Array([1, 2]).join('-')", "1-2");
    js_expect("new Uint8Array([1, 2]).map(x => x * 200).join()", "200,144");
    js_expect("new Uint8Array([1, 2, 3]).reduce((a, b) => a + b)", "6");
    js_expect("new Uint8Array([1, 2, 3]).reduceRight((a, b) => a + '' + b, '')", "321");
    throws("new Uint8Array(0).reduce((a, b) => a + b)", "TypeError");
    js_expect("new Uint8Array([1, 2, 3]).reverse().join()", "3,2,1");
    js_expect("(() => { const a = new Uint8Array([1, 2, 3]); const r = a.toReversed(); "
              "return a.join() + '|' + r.join(); })()",
              "1,2,3|3,2,1");
    js_expect("(() => { const a = new Uint8Array(4); a.set(new Uint8Array([9, 8]), 2); "
              "return a.join(); })()",
              "0,0,9,8");
    js_expect("(() => { const a = new Uint8Array([1, 2, 3, 4]); a.set(a.subarray(0, 3), 1); "
              "return a.join(); })()",
              "1,1,2,3");
    throws("new Uint8Array(2).set([1, 2, 3])", "RangeError");
    throws("new Uint8Array(2).set([1], -1)", "RangeError");
    js_expect("new Uint8Array([1, 2, 3, 4]).slice(1, -1).join()", "2,3");
    js_expect("new Uint8Array([3, 1, 2]).sort().join()", "1,2,3");
    js_expect("new Float64Array([1, NaN, -0, 0, -1]).sort().join()", "-1,0,0,1,NaN");
    js_expect("1 / new Float64Array([0, -0]).sort()[0]", "-Infinity");
    js_expect("new Uint8Array([3, 1, 2]).sort((a, b) => b - a).join()", "3,2,1");
    js_expect("(() => { const a = new Uint8Array([3, 1]); const s = a.toSorted(); "
              "return a.join() + '|' + s.join(); })()",
              "3,1|1,3");
    throws("new Uint8Array(1).sort(1)", "TypeError");
    js_expect("new Uint8Array([1, 2, 3]).subarray(-2).join()", "2,3");
    js_expect("new Uint8Array([1, 2]).toLocaleString()", "1,2");
    js_expect("new Uint8Array([1, 2]).toString()", "1,2");
    js_expect("new Uint8Array([1, 2, 3]).with(1, 9).join()", "1,9,3");
    throws("new Uint8Array([1, 2, 3]).with(3, 9)", "RangeError");
    js_expect("new Uint8Array([1, 2])[Symbol.iterator] === Uint8Array.prototype.values", "true");
    throws("Uint8Array.prototype.join.call([1, 2])", "TypeError");
    throws("Object.getPrototypeOf(Int8Array).prototype.length", "TypeError");
    js_expect("Object.getPrototypeOf(Int8Array).prototype[Symbol.toStringTag]", "undefined");
    // An accessor on the prototype, not a data property (23.2.3.19).
    js_expect("typeof Object.getOwnPropertyDescriptor(Object.getPrototypeOf(Int8Array).prototype, "
              "'length').get",
              "function");

    // --- ArrayBuffer, 25.1 ----------------------------------------------------
    js_expect("new ArrayBuffer(8).byteLength", "8");
    js_expect("new ArrayBuffer().byteLength", "0");
    throws("ArrayBuffer(8)", "TypeError");
    throws("new ArrayBuffer(-1)", "RangeError");
    js_expect("Object.prototype.toString.call(new ArrayBuffer(1))", "[object ArrayBuffer]");
    js_expect("ArrayBuffer.isView(new Uint8Array(1)) + '|' + ArrayBuffer.isView(new DataView(new "
              "ArrayBuffer(1))) + '|' + ArrayBuffer.isView([])",
              "true|true|false");
    js_expect("Object.keys(new ArrayBuffer(4)).length", "0");
    js_expect("(() => { const b = new ArrayBuffer(4); new Uint8Array(b).set([1, 2, 3, 4]); "
              "return new Uint8Array(b.slice(1, 3)).join(); })()",
              "2,3");
    js_expect("new ArrayBuffer(4).slice(-1).byteLength", "1");
    js_expect("new ArrayBuffer(2, {maxByteLength: 8}).resizable", "true");
    js_expect("new ArrayBuffer(2).resizable + '|' + new ArrayBuffer(2).maxByteLength", "false|2");
    js_expect("new ArrayBuffer(2, {maxByteLength: 8}).maxByteLength", "8");
    throws("new ArrayBuffer(9, {maxByteLength: 8})", "RangeError");
    throws("new ArrayBuffer(2).resize(4)", "TypeError");
    throws("new ArrayBuffer(2, {maxByteLength: 4}).resize(5)", "RangeError");
    // A length-tracking view follows the buffer; a fixed one goes out of
    // bounds when the buffer shrinks under it, and comes back when it grows.
    js_expect("(() => { const b = new ArrayBuffer(2, {maxByteLength: 8}); const t = new "
              "Uint8Array(b); b.resize(6); return t.length + '|' + t.byteLength; })()",
              "6|6");
    js_expect("(() => { const b = new ArrayBuffer(4, {maxByteLength: 8}); const f = new "
              "Uint8Array(b, 0, 4); b.resize(2); const a = f.length; b.resize(4); return a + '|' "
              "+ f.length; })()",
              "0|4");
    throws("(() => { const b = new ArrayBuffer(4, {maxByteLength: 8}); const f = new "
           "Uint8Array(b, 0, 4); b.resize(2); return f.join(); })()",
           "TypeError");
    js_expect("(() => { const b = new ArrayBuffer(4, {maxByteLength: 8}); const t = new "
              "Uint16Array(b, 2); b.resize(8); return t.length; })()",
              "3");
    // transfer moves the bytes and detaches the source.
    js_expect("(() => { const b = new ArrayBuffer(2); new Uint8Array(b)[0] = 7; const c = "
              "b.transfer(4); return b.detached + '|' + b.byteLength + '|' + c.byteLength + '|' + "
              "new Uint8Array(c)[0]; })()",
              "true|0|4|7");
    js_expect("new ArrayBuffer(2, {maxByteLength: 8}).transfer().resizable", "true");
    js_expect("new ArrayBuffer(2, {maxByteLength: 8}).transferToFixedLength().resizable", "false");
    throws("(() => { const b = new ArrayBuffer(2); b.transfer(); return b.slice(0); })()",
           "TypeError");
    js_expect("(() => { const b = new ArrayBuffer(2); const t = new Uint8Array(b); b.transfer(); "
              "return t.length + '|' + t.byteOffset + '|' + t[0]; })()",
              "0|0|undefined");
    throws("(() => { const b = new ArrayBuffer(2); const t = new Uint8Array(b); b.transfer(); "
           "return t.fill(1); })()",
           "TypeError");
    js_expect("(() => { class B extends ArrayBuffer {}; const b = new B(3); return b instanceof "
              "B && b.byteLength === 3; })()",
              "true");

    // An immutable buffer (the immutable-arraybuffer proposal): made by
    // transferToImmutable or sliceToImmutable, refused by every writing method.
    js_expect("new ArrayBuffer(2).transferToImmutable().immutable + '|' + new "
              "ArrayBuffer(2).immutable",
              "true|false");
    js_expect("(() => { const b = new ArrayBuffer(4); new Uint8Array(b).set([1, 2, 3, 4]); "
              "const i = b.sliceToImmutable(1, 3); return new Uint8Array(i).join() + '|' + "
              "i.immutable + '|' + b.detached; })()",
              "2,3|true|false");
    throws("new ArrayBuffer(2).transferToImmutable().transfer()", "TypeError");
    throws("new ArrayBuffer(2).transferToImmutable().resize(1)", "TypeError");
    throws("new Uint8Array(new ArrayBuffer(2).transferToImmutable()).fill(1)", "TypeError");
    throws("new Uint8Array(new ArrayBuffer(2).transferToImmutable()).sort()", "TypeError");
    throws("new DataView(new ArrayBuffer(2).transferToImmutable()).setUint8(0, 1)", "TypeError");
    js_expect("new Uint8Array(new ArrayBuffer(2).transferToImmutable()).toSorted().join()", "0,0");
    // A species constructor that throws: the throw is the page's, once.
    js_expect("(() => { const C = {}; C[Symbol.species] = function() { throw new "
              "RangeError('species'); }; const ta = new Uint8Array(2); ta.constructor = C; "
              "try { ta.slice(); } catch (e) { return e.message; } })()",
              "species");
    // -0 has no place in an integer kind; a float kind keeps it.
    js_expect("1 / Int32Array.of(-0)[0] + '|' + 1 / Float32Array.of(-0)[0]", "Infinity|-Infinity");
    js_expect("1 / new Int8Array(1).fill(-0)[0]", "Infinity");
    js_expect("Object.getOwnPropertyDescriptor(Object.getPrototypeOf(Int8Array).prototype, "
              "Symbol.toStringTag).get.name",
              "get [Symbol.toStringTag]");

    // --- DataView, 25.3 -------------------------------------------------------
    throws("new DataView({})", "TypeError");
    throws("new DataView(new ArrayBuffer(2), 3)", "RangeError");
    throws("new DataView(new ArrayBuffer(2), 1, 2)", "RangeError");
    js_expect("new DataView(new ArrayBuffer(8), 2).byteLength", "6");
    js_expect("new DataView(new ArrayBuffer(8), 2, 3).byteOffset", "2");
    js_expect("Object.prototype.toString.call(new DataView(new ArrayBuffer(1)))",
              "[object DataView]");
    js_expect("(() => { const b = new ArrayBuffer(4); const d = new DataView(b); d.setUint16(0, "
              "258); return new Uint8Array(b).join(); })()",
              "1,2,0,0");
    js_expect("(() => { const d = new DataView(new ArrayBuffer(4)); d.setUint16(0, 258, true); "
              "return d.getUint8(0) + '|' + d.getUint16(0) + '|' + d.getUint16(0, true); })()",
              "2|513|258");
    js_expect("(() => { const d = new DataView(new ArrayBuffer(8)); d.setFloat64(0, 1.5); "
              "return d.getFloat64(0); })()",
              "1.5");
    js_expect("(() => { const d = new DataView(new ArrayBuffer(4)); d.setFloat32(0, 0.1, true); "
              "return d.getFloat32(0, true) === Math.fround(0.1); })()",
              "true");
    js_expect("(() => { const d = new DataView(new ArrayBuffer(4)); d.setInt32(0, -1); "
              "return d.getUint32(0) + '|' + d.getInt8(3); })()",
              "4294967295|-1");
    js_expect("(() => { const d = new DataView(new ArrayBuffer(8)); d.setBigInt64(0, -2n); "
              "return d.getBigInt64(0) + '|' + d.getBigUint64(0); })()",
              "-2|18446744073709551614");
    throws("new DataView(new ArrayBuffer(8)).setBigInt64(0, 1)", "TypeError");
    js_expect("(() => { const d = new DataView(new ArrayBuffer(2)); d.setFloat16(0, 1.5); "
              "return d.getUint16(0).toString(16) + '|' + d.getFloat16(0); })()",
              "3e00|1.5");
    js_expect("(() => { const d = new DataView(new ArrayBuffer(2)); d.setFloat16(0, 65520); "
              "return d.getFloat16(0); })()",
              "Infinity");
    js_expect("(() => { const d = new DataView(new ArrayBuffer(2)); d.setFloat16(0, 2 ** -25); "
              "return d.getFloat16(0); })()",
              "0");
    js_expect("(() => { const d = new DataView(new ArrayBuffer(2)); d.setFloat16(0, 2 ** -24); "
              "return d.getFloat16(0) === 2 ** -24; })()",
              "true");
    throws("new DataView(new ArrayBuffer(2)).getUint32(0)", "RangeError");
    throws("new DataView(new ArrayBuffer(2)).getUint8(-1)", "RangeError");
    throws("DataView.prototype.getUint8.call({}, 0)", "TypeError");
    throws("(() => { const b = new ArrayBuffer(2); const d = new DataView(b); b.transfer(); "
           "return d.getUint8(0); })()",
           "TypeError");
    js_expect("(() => { const b = new ArrayBuffer(2, {maxByteLength: 8}); const d = new "
              "DataView(b); b.resize(5); return d.byteLength; })()",
              "5");

    // --- Uint8Array base64 and hex, ES2025 --------------------------------------
    js_expect("Uint8Array.fromBase64('AQID').join()", "1,2,3");
    js_expect("Uint8Array.fromBase64('AQ==').join() + '|' + Uint8Array.fromBase64('AQ').join()",
              "1|1");
    js_expect("Uint8Array.fromBase64('  AQ ID \\n').join()", "1,2,3");
    js_expect("Uint8Array.fromBase64('-_8', {alphabet: 'base64url'}).join()", "251,255");
    throws("Uint8Array.fromBase64('AQ', {lastChunkHandling: 'strict'})", "SyntaxError");
    throws("Uint8Array.fromBase64('AR==', {lastChunkHandling: 'strict'})", "SyntaxError");
    js_expect("Uint8Array.fromBase64('AR==', {lastChunkHandling: 'loose'}).join()", "1");
    js_expect("Uint8Array.fromBase64('AQIDA', {lastChunkHandling: 'stop-before-partial'}).join()",
              "1,2,3");
    throws("Uint8Array.fromBase64('A')", "SyntaxError");
    throws("Uint8Array.fromBase64('AQ=x')", "SyntaxError");
    throws("Uint8Array.fromBase64('+/', {alphabet: 'base64url'})", "SyntaxError");
    throws("Uint8Array.fromBase64(1)", "TypeError");
    throws("Uint8Array.fromBase64('AQ', {alphabet: 'hex'})", "TypeError");
    js_expect("new Uint8Array([1, 2, 3]).toBase64()", "AQID");
    js_expect("new Uint8Array([1]).toBase64() + '|' + new Uint8Array([1]).toBase64({omitPadding: "
              "true})",
              "AQ==|AQ");
    js_expect("new Uint8Array([251, 255]).toBase64({alphabet: 'base64url'})", "-_8=");
    js_expect("Uint8Array.fromHex('00ff7A').join()", "0,255,122");
    throws("Uint8Array.fromHex('abc')", "SyntaxError");
    throws("Uint8Array.fromHex('zz')", "SyntaxError");
    js_expect("new Uint8Array([0, 255, 122]).toHex()", "00ff7a");
    js_expect("(() => { const a = new Uint8Array(4); const r = a.setFromBase64('AQID'); "
              "return a.join() + '|' + r.read + '|' + r.written; })()",
              "1,2,3,0|4|3");
    // Only WHOLE chunks that fit are decoded: 'BAU' would be two bytes and
    // one slot is left, so the read stops at the chunk before it.
    js_expect("(() => { const a = new Uint8Array(4); const r = a.setFromBase64('AQIDBAU'); "
              "return a.join() + '|' + r.read + '|' + r.written; })()",
              "1,2,3,0|4|3");
    js_expect("(() => { const a = new Uint8Array(5); const r = a.setFromBase64('AQIDBAU'); "
              "return a.join() + '|' + r.read + '|' + r.written; })()",
              "1,2,3,4,5|7|5");
    js_expect("(() => { const a = new Uint8Array(2); const r = a.setFromHex('0102ff'); "
              "return a.join() + '|' + r.read + '|' + r.written; })()",
              "1,2|4|2");
    throws("Uint8Array.prototype.toHex.call(new Int8Array(1))", "TypeError");

    // --- the VM's `ta[i] = v` is TypedArraySetElement: ToNumber first ------------
    js_expect("(() => { const a = new Uint8Array(1); a[0] = {valueOf() { return 7; }}; "
              "return a[0]; })()",
              "7");
    js_expect("(() => { const a = new Int8Array(1); a[0] = -0; return Object.is(a[0], 0); })()",
              "true");
    js_expect("(() => { const a = new Int8Array(new ArrayBuffer(1)); a[0] = '5'; "
              "return a[0]; })()",
              "5");
    throws("(() => { const a = new Uint8Array(1); a[0] = Symbol(); })()", "TypeError");

    // --- BigInt64Array, BigUint64Array (Table 71): the element is a bigint -----
    js_expect("BigInt64Array.BYTES_PER_ELEMENT + '|' + BigUint64Array.BYTES_PER_ELEMENT", "8|8");
    js_expect("Object.getPrototypeOf(BigInt64Array) === Object.getPrototypeOf(Int8Array)", "true");
    js_expect("Object.prototype.toString.call(new BigUint64Array(1))", "[object BigUint64Array]");
    js_expect("typeof new BigInt64Array(2)[0] + '|' + new BigInt64Array(2)[1]", "bigint|0");
    // ToBigInt64 / ToBigUint64 wrap modulo 2^64 - in either storage shape.
    js_expect("(() => { const a = new BigInt64Array(2); a[0] = 2n ** 63n; a[1] = -1n; "
              "return a[0] + '|' + a[1]; })()",
              "-9223372036854775808|-1");
    js_expect("(() => { const a = new BigUint64Array(1); a[0] = -1n; return a[0]; })()",
              "18446744073709551615");
    js_expect("(() => { const a = new BigInt64Array(new ArrayBuffer(16)); a[1] = -2n; "
              "return a[1] + '|' + new Uint8Array(a.buffer).join(); })()",
              "-2|0,0,0,0,0,0,0,0,254,255,255,255,255,255,255,255");
    js_expect("new BigUint64Array(new BigInt64Array([-1n]).buffer)[0]", "18446744073709551615");
    // ToBigInt: a Number is a TypeError, a string parses, a valueOf runs.
    throws("(() => { const a = new BigInt64Array(1); a[0] = 1; })()", "TypeError");
    throws("new BigInt64Array([1])", "TypeError");
    throws("new BigInt64Array(1).fill(1)", "TypeError");
    js_expect("(() => { const a = new BigInt64Array(1); a[0] = '12'; return a[0]; })()", "12");
    js_expect("(() => { const a = new BigInt64Array(1); a[0] = {valueOf() { return 7n; }}; "
              "return a[0]; })()",
              "7");
    js_expect("Object.getOwnPropertyDescriptor(new BigInt64Array([5n]), 0).value", "5");
    js_expect("(() => { const a = new BigInt64Array(1); Object.defineProperty(a, 0, {value: 3n}); "
              "return a[0]; })()",
              "3");
    throws("Object.defineProperty(new BigInt64Array(1), 0, {value: 3})", "TypeError");
    // [[ContentType]]: a BigInt array and a Number array do not mix.
    throws("new BigInt64Array(new Uint8Array(1))", "TypeError");
    throws("new Uint8Array(new BigInt64Array(1))", "TypeError");
    throws("new Uint8Array(1).set(new BigInt64Array(1))", "TypeError");
    throws("new BigInt64Array(1).set([1])", "TypeError");
    throws("(() => { const a = new BigInt64Array(1); a.constructor = {[Symbol.species]: "
           "Uint8Array}; return a.map(x => x); })()",
           "TypeError");
    // The prototype methods, over bigints.
    js_expect("BigInt64Array.from([1n, 2n], x => x * 2n).join('-')", "2-4");
    js_expect("BigUint64Array.of(3n, 1n, 2n).sort().join()", "1,2,3");
    js_expect("new BigInt64Array([3n, -1n, 2n]).sort().join()", "-1,2,3");
    js_expect("new BigInt64Array([3n, -1n, 2n]).sort((a, b) => Number(b - a)).join()", "3,2,-1");
    js_expect("new BigInt64Array([1n, 2n, 3n]).map(x => x + 1n).join()", "2,3,4");
    js_expect("new BigInt64Array([1n, 2n, 3n]).filter(x => x > 1n).join()", "2,3");
    js_expect("new BigInt64Array([1n, 2n, 3n]).reduce((a, b) => a + b)", "6");
    js_expect(
        "new BigInt64Array([1n, 2n]).includes(2n) + '|' + new BigInt64Array([1n]).includes(1)",
        "true|false");
    js_expect("new BigInt64Array([1n, 2n]).indexOf(2n) + '|' + "
              "new BigInt64Array([1n, 2n]).lastIndexOf(1n)",
              "1|0");
    js_expect("new BigInt64Array([1n, 2n]).with(0, 9n).join()", "9,2");
    js_expect("new BigInt64Array([1n, 2n]).toReversed().join() + '|' + "
              "new BigInt64Array([1n, 2n]).reverse().join()",
              "2,1|2,1");
    js_expect("new BigInt64Array([1n, 2n, 3n]).subarray(1)[0] + '|' + "
              "new BigInt64Array([1n, 2n, 3n]).slice(-1)[0]",
              "2|3");
    js_expect("[...new BigInt64Array([4n, 5n])].join() + '|' + "
              "[...new BigInt64Array([4n]).entries()].join()",
              "4,5|0,4");
    js_expect("new BigInt64Array(3).fill(7n, 1).join()", "0,7,7");
    js_expect("new BigUint64Array([1n, 2n]).at(-1)", "2");
    js_expect("new BigInt64Array([1n, 2n]).toString()", "1,2");
    js_expect("(() => { const a = new BigInt64Array([1n, 2n, 3n]); a.copyWithin(0, 1); "
              "return a.join(); })()",
              "2,3,3");
    js_expect("(() => { const a = new BigInt64Array(2); const b = new BigInt64Array(a.buffer); "
              "b[1] = 5n; return a[1]; })()",
              "5");
    js_expect("new DataView(new BigInt64Array([-1n]).buffer).getBigInt64(0)", "-1");

    // --- Float16Array: binary16, ties to even, either storage shape --------------
    js_expect("Float16Array.BYTES_PER_ELEMENT", "2");
    js_expect("new Float16Array([1.5, 65520, 2 ** -25, 0.1]).join()",
              "1.5,Infinity,0,0.0999755859375");
    js_expect("(() => { const a = new Float16Array(new ArrayBuffer(4)); a[1] = -2; "
              "return a[1] + '|' + new Uint16Array(a.buffer)[1].toString(16); })()",
              "-2|c000");
    js_expect("(() => { const a = new Float16Array(1); a[0] = NaN; return a[0] + '|' + "
              "typeof (a[0] - 1); })()",
              "NaN|number");
    js_expect("(() => { const a = new Float16Array(1); a[0] = -0; return Object.is(a[0], -0); })()",
              "true");
    js_expect("new Float16Array([3, 1, 2]).sort().join() + '|' + Math.f16round(0.1)",
              "1,2,3|0.0999755859375");
    js_expect("new Float16Array(new Uint8Array([1, 2])).join()", "1,2");

    REPORT("typed_arrays");
}
