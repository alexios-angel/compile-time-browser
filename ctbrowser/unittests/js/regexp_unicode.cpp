// `\p{...}` and Canonicalize under `iu` (22.2.2.9, 22.2.2.7.3) - the parts of
// the matcher that read lib/Script/regex_properties.inc. Each assertion is
// one test262 built-ins/RegExp/property-escapes shape, reduced to a line.

#include "vm_expect.hpp"

namespace {

// A literal that must be a SyntaxError AT PARSE TIME (22.2.1.1 early errors).
void refused(std::string_view literal) {
    const program prog = compiler::compile(std::string{literal} + ";");
    if (prog.ok || !prog.error.starts_with("parse error:")) {
        std::printf("FAIL     %s must be an early SyntaxError -> %s\n",
                    std::string{literal}.c_str(), prog.ok ? "accepted" : prog.error.c_str());
        ++ctbrowser_test_failures;
    }
}

void test_property_escapes() {
    expect_result("return /^\\p{Lu}+$/u.test('ABC') + ',' + /^\\p{Lu}+$/u.test('AbC');",
                  "true,false");
    expect_result("return /\\P{Lu}/u.test('A') + ',' + /\\P{Lu}/u.test('a');", "false,true");
    // the lone form takes a General_Category value or a binary property,
    // under any of its aliases; the pair form takes gc, sc and scx only
    expect_result("return /^[\\p{Letter}\\p{L}\\p{gc=L}\\p{General_Category=Letter}]{4}$/u"
                  ".test('a\\u00e9\\u0391\\u4e00');",
                  "true");
    expect_result("return /^\\p{Script=Latin}+$/u.test('abc\\u00e9') + ',' + "
                  "/^\\p{sc=Latn}+$/u.test('\\u0391');",
                  "true,false");
    // U+00B7 MIDDLE DOT is Common by Script and Latin (among others) by
    // Script_Extensions
    expect_result("return /\\p{Script=Latin}/u.test('\\u00b7') + ',' + "
                  "/\\p{scx=Latn}/u.test('\\u00b7');",
                  "false,true");
    expect_result("return /^\\p{ASCII_Hex_Digit}+$/u.test('0fA') + ',' + /^\\p{AHex}$/u.test('g');",
                  "true,false");
    expect_result(
        "return /\\p{Any}/u.test('\\u{10FFFF}') + ',' + /\\p{Assigned}/u.test('\\u0378');",
        "true,false");
    expect_result("return /[\\p{Hex}\\P{Hex}]/u.test('\\u{1D306}');", "true");
    // without `u` it is the letter p
    expect_result("return /\\p{Lu}/.test('p{Lu}') + ',' + /\\p{Lu}/.test('A');", "true,false");
    // `\P` is the complement of the set and `[^...]` inverts the match: they
    // differ under `iu`, where Canonicalize runs between the two
    expect_result("return /\\P{Lu}/iu.test('A') + ',' + /[^\\p{Lu}]/iu.test('A');", "true,false");
    // the properties of strings need `v`, and match a whole sequence
    expect_result("return /^\\p{RGI_Emoji_Flag_Sequence}$/v.test('\\u{1F1FA}\\u{1F1F8}') + ',' + "
                  "/^\\p{Basic_Emoji}$/v.test('\\u231A');",
                  "true,true");
    // no loose matching, no unknown names, no properties the table does not
    // list, no strings under `u` or negated (all 22.2.1.1 early errors)
    refused("/\\p{ Lu }/u");
    refused("/\\p{lu}/u");
    refused("/\\p{Nope}/u");
    refused("/\\p{General_Category}/u");
    refused("/\\p{Script=Nope}/u");
    refused("/\\p{Block=Basic_Latin}/u");
    refused("/\\p{ASCII=Yes}/u");
    refused("/\\p{Basic_Emoji}/u");
    refused("/\\pL/u");
    refused("/\\p{}/u");
    expect_result("try { new RegExp('\\\\P{Basic_Emoji}', 'v'); return 'no'; } catch (e) "
                  "{ return e.name; }",
                  "SyntaxError");
    expect_result("try { new RegExp('[^\\\\p{Basic_Emoji}]', 'v'); return 'no'; } catch (e) "
                  "{ return e.name; }",
                  "SyntaxError");
}

void test_canonicalize() {
    // simple case folding under `iu`: K and the KELVIN SIGN share a fold, as
    // do s and LATIN SMALL LETTER LONG S - and only under `u`
    expect_result("return /k/iu.test('\\u212a') + ',' + /\\u212a/iu.test('k') + ',' + "
                  "/k/i.test('\\u212a');",
                  "true,true,false");
    expect_result("return /\\w/iu.test('\\u017f') + ',' + /\\W/iu.test('\\u017f') + ',' + "
                  "/\\w/u.test('\\u017f');",
                  "true,false,false");
    expect_result("return /[\\u03c3]/iu.test('\\u03a3') + ',' + /[\\u03c3]/iu.test('\\u03c2');",
                  "true,true");
    // a pattern character above ASCII is one atom: the quantifier repeats
    // the character, not its last byte
    expect_result(
        "return /^\u00e9+$/.test('\\u00e9\\u00e9') + ',' + /^\u00e9{2}$/.test('\\u00e9');",
        "true,false");
    // \s is WhiteSpace and LineTerminator, not the ASCII five
    expect_result("return /\\s/.test('\\u00a0') + ',' + /\\s/.test('\\u2028') + ',' + "
                  "/\\S/.test('\\u3000');",
                  "true,true,false");
    expect_result("return /[\\D]/.test('5') + ',' + /[\\D]/.test('x');", "false,true");
    // a million repetitions of one class is a loop, not a recursion
    expect_result("let s = ''; for (let i = 0; i < 200; i++) s += 'abcdefghij'.repeat(1000);"
                  "return /^\\p{L}+$/u.test(s) + ',' + /^[a-j]*$/.test(s) + ',' + s.length;",
                  "true,true,2000000");
}

void test_unicode_sets() {
    // ClassSetExpression: union, `--`, `&&`, nested classes, `\q{...}`
    expect_result("return /^[[0-9]--[0-9]]+$/v.test('0') + ',' + /^[[0-9]&&\\d]+$/v.test('5') + "
                  "',' + /^[\\w--\\d]+$/v.test('ab_') + ',' + /^[\\w--\\d]+$/v.test('a1');",
                  "false,true,true,false");
    expect_result("return /^[[a-z]&&[^aeiou]]+$/v.test('bcd') + ',' + "
                  "/^[[a-z]&&[^aeiou]]+$/v.test('e');",
                  "true,false");
    expect_result(
        "return /^[\\q{abc|d}]+$/v.test('abcdabc') + ',' + /^[\\q{abc|d}]+$/v.test('abd') "
        "+ ',' + /^[\\q{}]$/v.test('');",
        "true,false,true");
    expect_result("return /^[\\d--\\q{0|2|4|9\\uFE0F\\u20E3}]+$/v.test('1') + ',' + "
                  "/^[\\d--\\q{0|2|4|9\\uFE0F\\u20E3}]+$/v.test('0') + ',' + "
                  "/^[\\p{Emoji_Keycap_Sequence}&&\\q{0|2|4|9\\uFE0F\\u20E3}]+$/v"
                  ".test('9\\uFE0F\\u20E3');",
                  "true,false,true");
    // under `vi` an operand is folded before it is complemented, so `\P{Lu}`
    // no longer takes "A" - the documented break between u and v
    expect_result("return /\\P{Lu}/ui.test('A') + ',' + /\\P{Lu}/vi.test('A') + ',' + "
                  "/[\\p{Lu}--[a-c]]/vi.test('A') + ',' + /[\\p{Lu}--[a-c]]/vi.test('D');",
                  "true,false,false,true");
    // the syntax characters must be escaped, the double punctuators are
    // reserved, operators do not mix, a range is a union's
    for (const char * bad : {"/[(]/v", "/[-]/v", "/[&&]/v", "/[^^^]/v", "/[_^^]/v", "/[a-z--b]/v",
                             "/[a--b&&c]/v", "/[^\\q{ab}]/v", "/\\P{RGI_Emoji}/v"}) {
        refused(bad);
    }
    expect_result("return /[\\&\\-\\!]/v.test('-') + ',' + /[\\p{ASCII_Hex_Digit}--[0-9]]/v"
                  ".test('a') + ',' + /[\\p{ASCII_Hex_Digit}--[0-9]]/v.test('5');",
                  "true,true,false");
}

// `new RegExp(p, f)` is judged by the same early-error scan a literal gets
// (22.2.3.4), a class range out of order is one of those errors in both
// readers, `\k` outside `u` is Annex B's letter k, and `(?ims-ims:...)` sets
// the modifiers for its body alone (22.2.2.1.1).
void test_constructor_and_modifiers() {
    for (const char * bad :
         {"a**", "??", "+a", "x{1}{1,}", "[b-a]", "(?s-s:a)", "(?x:a)", "(?-:a)", "[\\\\d-x]"}) {
        const std::string flags = std::string_view{bad} == "[\\\\d-x]" ? "u" : "";
        expect_result("try { new RegExp('" + std::string{bad} + "', '" + flags +
                          "'); return 'no'; } catch (e) { return e.name; }",
                      "SyntaxError");
    }
    refused("/[b-ac-e]/");
    refused("/[\\d-x]/u");
    expect_result("return /[\\d-x]/.test('-') + ',' + /[--0]/.test('.') + ',' + "
                  "/\\k</.test('k<') + ',' + /\\k<x>/.test('k<x>');",
                  "true,true,true,true");
    expect_result("return /(?i:a)b/.test('Ab') + ',' + /(?i:a)b/.test('AB') + ',' + "
                  "/(?-i:a)b/i.test('Ab') + ',' + /(?-i:a)b/i.test('aB');",
                  "true,false,false,true");
    expect_result("return /(?s:.)/.test('\\n') + ',' + /(?m:^b)/.test('a\\nb') + ',' + "
                  "/(?i:(?-i:a)b)/.test('aB') + ',' + /(?i:(?-i:a)b)/.test('Ab') + ',' + "
                  "/(?i:a)|b/.test('B') + ',' + /(?i:a)/.ignoreCase;",
                  "true,true,true,false,false,false");
}

} // namespace

int main() {
    test_property_escapes();
    test_canonicalize();
    test_unicode_sets();
    test_constructor_and_modifiers();
    REPORT("regexp_unicode");
}
