//===- ctbrowser/unittests/unit/crypto_bindings.cpp - Web Crypto tests ----===//
//
// `crypto.getRandomValues` and `crypto.randomUUID` (WebCryptoAPI §10), against
// a live page: the types the first accepts, its 65536-byte quota, the UUID's
// shape, and a statistical smoke test that the bytes are not the seeded
// Math.random's: two pages must not draw the same values.
//
//===----------------------------------------------------------------------===//

#include <ctbrowser.hpp>

#include "check.hpp"
#include "dom_probe.hpp"

#include <string>

namespace {

constexpr const char * page_html = "<!DOCTYPE html><html><body></body></html>";

void is(const std::string & body, const std::string & expected) {
    ctbrowser_test::is_in(page_html, "(function () { " + body + " })()", expected);
}

void test_get_random_values_types() {
    is("var a = new Uint8Array(4); return crypto.getRandomValues(a) === a;", "true");
    is("return [Int8Array, Uint8Array, Uint8ClampedArray, Int16Array, Uint16Array, Int32Array,"
       " Uint32Array, BigInt64Array, BigUint64Array].map(function (T) {"
       "  var a = new T(8); crypto.getRandomValues(a); return a.length; }).join();",
       "8,8,8,8,8,8,8,8,8");
    // A view over part of a buffer writes that part only.
    is("var b = new ArrayBuffer(8), v = new Uint8Array(b, 2, 4);"
       " for (var i = 0; i < 100 && v[0] === 0; ++i) crypto.getRandomValues(v);"
       " var all = new Uint8Array(b); return [all[0], all[1], all[6], all[7]].join();",
       "0,0,0,0");
    is("try { crypto.getRandomValues(new Float32Array(4)); return 'no throw'; }"
       " catch (e) { return e.name; }",
       "TypeMismatchError");
    is("try { crypto.getRandomValues([1, 2]); return 'no throw'; } catch (e) { return e.name; }",
       "TypeError");
    is("try { crypto.getRandomValues(); return 'no throw'; } catch (e) { return e.name; }",
       "TypeError");
    is("return [typeof Crypto, crypto instanceof Crypto,"
       " Object.prototype.toString.call(crypto)].join();",
       "function,true,[object Crypto]");
}

void test_the_quota() {
    is("return crypto.getRandomValues(new Uint8Array(65536)).length;", "65536");
    is("try { crypto.getRandomValues(new Uint8Array(65537)); return 'no throw'; }"
       " catch (e) { return e.name; }",
       "QuotaExceededError");
    is("try { crypto.getRandomValues(new Uint32Array(16385)); return 'no throw'; }"
       " catch (e) { return e.name; }",
       "QuotaExceededError");
}

void test_the_bytes_look_random() {
    // 65536 bytes: every byte value turns up (P(miss) ~ 256 * e^-256), and the
    // mean is 127.5 give or take well under 2.
    is("var a = crypto.getRandomValues(new Uint8Array(65536)), seen = new Array(256).fill(0),"
       " sum = 0; for (var i = 0; i < a.length; ++i) { seen[a[i]]++; sum += a[i]; }"
       " var mean = sum / a.length;"
       " return seen.every(function (n) { return n > 0; }) && Math.abs(mean - 127.5) < 2;",
       "true");
    // Two pages draw differently. Math.random is seeded, so two fresh pages
    // agree on it; they must not agree on this.
    const std::string draw = "crypto.getRandomValues(new Uint32Array(4)).join()";
    const std::string first = ctbrowser_test::answer_in(page_html, draw);
    CHECK(first.find(',') != std::string::npos);
    CHECK(first != ctbrowser_test::answer_in(page_html, draw));
    CHECK_EQ(ctbrowser_test::answer_in(page_html, "Math.random()"),
             ctbrowser_test::answer_in(page_html, "Math.random()"));
}

void test_random_uuid() {
    is("return /^[0-9a-f]{8}-[0-9a-f]{4}-4[0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}$/"
       ".test(crypto.randomUUID());",
       "true");
    is("var seen = {}; for (var i = 0; i < 1000; ++i) seen[crypto.randomUUID()] = 1;"
       " return Object.keys(seen).length;",
       "1000");
}

} // namespace

int main() {
    test_get_random_values_types();
    test_the_quota();
    test_the_bytes_look_random();
    test_random_uuid();
    REPORT("crypto_bindings");
}
