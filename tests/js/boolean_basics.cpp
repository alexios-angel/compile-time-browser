// `boolean`, against V8.
//
// Two values and a conversion table, which is most of what makes control flow
// work: every `if`, every `&&`, every `||` runs ToBoolean on something that is
// usually not a boolean. Differentially tested against node (V8); 1 difference.
//
// The rule worth internalising is that the falsy set is CLOSED and short -
// seven values - so everything else is true, including "0", "false", [] and {}.
// Most "why did my guard not fire" bugs are a member of that list.

#include "js_expect.hpp"

int main() {
    // --- the type ------------------------------------------------------------
    js_expect("typeof true", "boolean");
    js_expect("typeof false", "boolean");
    js_expect("typeof Boolean", "function");
    js_expect("typeof (true).constructor", "function");

    // --- the falsy set, which is exactly seven values ------------------------
    for (const char * falsy : {"undefined", "null", "false", "0", "-0", "NaN", "\"\""}) {
        js_expect(std::string{"Boolean("} + falsy + ")", "false");
        js_expect(std::string{"!"} + falsy, "true");
        js_expect(std::string{"("} + falsy + " ? \"T\" : \"F\")", "F");
    }
    // ...and everything else is truthy, including the ones that look falsy.
    for (const char * truthy : {"true", "1", "-1", "\"0\"", "\"false\"", "\" \"", "[]", "({})",
                                "Infinity", "(function(){})"}) {
        js_expect(std::string{"Boolean("} + truthy + ")", "true");
        js_expect(std::string{"!"} + truthy, "false");
    }
    js_expect("!!1", "true");
    js_expect("!!\"\"", "false");
    js_expect("!!!true", "false");

    // --- conversions out of boolean ------------------------------------------
    js_expect("String(true)", "true");
    js_expect("String(false)", "false");
    js_expect("Number(true)", "1");
    js_expect("Number(false)", "0");
    js_expect("true + true", "2"); // ToNumber, so this is 2 and not "truetrue"
    js_expect("true + 1", "2");
    js_expect("true * 2", "2");
    js_expect("[true,false].join(\",\")", "true,false");
    js_expect("JSON.stringify(true)", "true");

    // --- equality and comparison ---------------------------------------------
    // `==` converts a boolean to a NUMBER first, which is why "1" equals true
    // and "true" does not.
    js_expect("true == 1", "true");
    js_expect("true === 1", "false");
    js_expect("false == 0", "true");
    js_expect("false == \"\"", "true");
    js_expect("true == \"1\"", "true");
    js_expect("true == \"true\"", "false");
    js_expect("2 == true", "false"); // 2 is truthy but not equal to true
    js_expect("true < 2", "true");
    js_expect("false < 1", "true");

    // --- the prototype methods -----------------------------------------------
    js_expect("true.toString()", "true");
    js_expect("(true).valueOf()", "true");
    js_expect("typeof Boolean.prototype", "object");
    js_expect("Boolean.prototype.toString.call(true)", "true");

    // --- && and || return an OPERAND, not a boolean --------------------------
    js_expect("true && false", "false");
    js_expect("true || false", "true");
    js_expect("1 && 2", "2");
    js_expect("0 || \"d\"", "d");
    js_expect("null && \"x\"", "null");

    // --- KNOWN WRONG, pinned so a fix trips over it ---------------------------
    // `new Boolean(false)` is an OBJECT, and every object is truthy - which is
    // the standard argument against the wrapper constructors. This engine does
    // not box, so `new Boolean(false)` is the primitive and stays falsy.
    js_expect("Boolean(new Boolean(false))", "false"); // V8: true

    REPORT("boolean_basics");
}
