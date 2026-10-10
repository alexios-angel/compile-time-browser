#include "Cases.hpp"
#include "llvm/ADT/StringSet.h"

namespace ctcompile::test::escape::arrays::induction_detail {

void InductionCases::invariantReads() {
    const auto tablePrefix =
        replace(prefix, "  %a = ctjs.create_array [%one, %two, %three] {storage_test_id = \"a\"}\n",
                "  %x = ctjs.create_object {storage_test_id = \"x\"}\n"
                "  %keys = ctjs.create_array [%zero, %two] {storage_test_id = \"keys\"}\n"
                "  %a = ctjs.create_array [%x, %zero, %x] {storage_test_id = \"a\"}\n");
    const auto tableLoop = replace(replace(replace(loop, "  %read = ctjs.get_property %base[%i]\n",
                                                   "  %pick = ctjs.binary mod %i, %two\n"
                                                   "  %slot = ctjs.get_property %keys[%pick]\n"
                                                   "  ctjs.set_property %base[%slot], %zero\n"),
                                           "add %s, %read", "add %s, %one"),
                                   "ctjs.return %result", "ctjs.return %a");
    const auto tableIndex = tablePrefix + tableLoop;
    run({.what = "varying reads from an independent table prove actual own writes",
         .body = tableIndex,
         .arrays = "keys:[zero,two]; a:[zero,zero,zero]",
         .reads = "keys[0]=zero; keys[1]=two; keys[0]=zero",
         .exit = "a -> {a}"},
        "x");
    run({.what = "table order remains source order through singleton refinement",
         .body = replace(tableIndex, "[%zero, %two]", "[%two, %zero]"),
         .arrays = "keys:[two,zero]; a:[zero,zero,zero]",
         .reads = "keys[0]=two; keys[1]=zero; keys[0]=two",
         .exit = "a -> {a}"},
        "x");
    run({.what = "table replay retains children at positions never actually overwritten",
         .body = replace(tableIndex, "[%zero, %two]", "[%zero, %zero]"),
         .arrays = "keys:[zero,zero]; a:[zero,zero,x]",
         .reads = "keys[0]=zero; keys[1]=zero; keys[0]=zero",
         .exit = "a -> {a,x}"});
    run({.what = "saved child identity survives subsequent table-directed writes",
         .body = replace(replace(tableIndex, "  cf.br ^header",
                                 "  %saved = ctjs.get_property %a[%zero]\n  cf.br ^header"),
                         "ctjs.return %a", "ctjs.return %saved"),
         .arrays = "keys:[zero,two]; a:[zero,zero,zero]",
         .reads = "a[0]=x; keys[0]=zero; keys[1]=two; keys[0]=zero",
         .exit = "x -> {x}"});
    const auto selectedTable = replace(replace(tableIndex, "[%x, %zero, %x]", "[%x, %keys, %x]"),
                                       "  %slot = ctjs.get_property %keys[%pick]",
                                       "  %table = ctjs.get_property %base[%one]\n"
                                       "  %slot = ctjs.get_property %table[%pick]");
    run({.what = "a selected table receiver needs independent exclusion from every write",
         .body = selectedTable,
         .arrays = "keys:[zero,two]; a:[zero,keys,zero]",
         .reads = "a[1]=keys; keys[0]=zero; a[1]=keys; keys[1]=two; a[1]=keys; keys[0]=zero",
         .exit = "a -> {a,keys}"},
        "x");
    reject("a table receiver cannot reload a position targeted by its own keys",
           replace(selectedTable, "[%zero, %two]", "[%zero, %one]"));
    reject(
        "a later write cannot invalidate an earlier selected table receiver",
        replace(selectedTable, "  %step =", "  ctjs.set_property %base[%one], %zero\n  %step ="));
    reject("a varying table lookup cannot read the mutable guard allocation",
           replace(tableIndex, "%keys[%pick]", "%base[%pick]"));
    for (const std::string mutation :
         {"ctjs.set_property %keys[%one], %zero", "ctjs.set_property %keys[%key], %one",
          "ctjs.append %zero to %keys", "%called = ctjs.call %p(%keys)"}) {
        reject("complete census refuses table mutation or resizing before a lookup",
               replace(tableIndex, "  %pick =", "  " + mutation + "\n  %pick ="));
        reject("complete census also refuses table mutation after a lookup",
               replace(tableIndex, "  %step =", "  " + mutation + "\n  %step ="));
    }
    const auto aliasTable = replace(tableIndex, "  cf.br ^header",
                                    "  %box = ctjs.create_array [%keys]\n"
                                    "  %alias = ctjs.get_property %box[%zero]\n  cf.br ^header");
    reject("saved table aliases remain visible to the complete write census",
           replace(aliasTable, "  %step =", "  ctjs.set_property %alias[%one], %zero\n  %step ="));
    for (const std::string element : {"%three", "%x"}) {
        reject("table elements must supply bounded Numbers within the guard's own bounds",
               replace(tableIndex, "[%zero, %two]", "[%zero, " + element + "]"));
    }
    reject("an opaque table element refuses before loop proof at array initialization",
           replace(tableIndex, "[%zero, %two]", "[%zero, %p]"), ArrayContentsFailure::UnknownValue);
    reject("table index refinement retains fractional intermediate refusal",
           replace(tableIndex, "mod %i, %two", "div %i, %two"));
    reject("table lookup requires every selected own element to exist",
           replace(tableIndex, "[%zero, %two]", "[%zero]"));
    auto carriedTable = tableIndex;
    for (const auto & [before, after] : {
             std::pair{"^header(%a, %zero, %zero : !ctjs.value, !ctjs.value, !ctjs.value)",
                       "^header(%a, %zero, %zero, %keys : !ctjs.value, !ctjs.value, !ctjs.value, "
                       "!ctjs.value)"},
             {"%sum: !ctjs.value):", "%sum: !ctjs.value, %tableInput: !ctjs.value):"},
             {"^body(%array, %index, %sum : !ctjs.value, !ctjs.value, !ctjs.value)",
              "^body(%array, %index, %sum, %tableInput : !ctjs.value, !ctjs.value, !ctjs.value, "
              "!ctjs.value)"},
             {"%s: !ctjs.value):", "%s: !ctjs.value, %tableBody: !ctjs.value):"},
             {"%keys[%pick]", "%tableBody[%pick]"},
             {"^header(%base, %step, %added : !ctjs.value, !ctjs.value, !ctjs.value)",
              "^header(%base, %step, %added, %tableBody : !ctjs.value, !ctjs.value, !ctjs.value, "
              "!ctjs.value)"},
         }) {
        carriedTable = replace(carriedTable, before, after);
    }
    run({.what = "table receivers preserve exact allocation through unchanged CFG transport",
         .body = carriedTable,
         .arrays = "keys:[zero,two]; a:[zero,zero,zero]",
         .reads = "keys[0]=zero; keys[1]=two; keys[0]=zero",
         .exit = "a -> {a}"},
        "x");
    reject("a changing carried table cannot borrow its initial allocation",
           replace(carriedTable, "^header(%base, %step, %added, %tableBody",
                   "^header(%base, %step, %added, %base"));
    const auto stringTable = replace(
        replace(tableIndex, "  %keys =",
                "  %textZero = ctjs.constant #ctjs.string<\"0\"> {storage_test_id = \"textZero\"}\n"
                "  %textTwo = ctjs.constant #ctjs.string<\"2\"> {storage_test_id = \"textTwo\"}\n"
                "  %keys ="),
        "[%zero, %two]", "[%textZero, %textTwo]");
    run({.what = "String table elements name canonical own keys without Number conversion",
         .body = stringTable,
         .arrays = "keys:[textZero,textTwo]; a:[zero,zero,zero]",
         .reads = "keys[0]=textZero; keys[1]=textTwo; keys[0]=textZero",
         .exit = "a -> {a}"},
        "x");
    run({.what = "mixed Number and String table keys preserve their original read identities",
         .body = replace(stringTable, "[%textZero, %textTwo]", "[%zero, %textTwo]"),
         .arrays = "keys:[zero,textTwo]; a:[zero,zero,zero]",
         .reads = "keys[0]=zero; keys[1]=textTwo; keys[0]=zero",
         .exit = "a -> {a}"},
        "x");
    run({.what = "Number negative zero remains own zero alongside a String table key",
         .body = replace(stringTable, "#ctjs.string<\"0\">", "#ctjs.number<9223372036854775808>"),
         .arrays = "keys:[textZero,textTwo]; a:[zero,zero,zero]",
         .reads = "keys[0]=textZero; keys[1]=textTwo; keys[0]=textZero",
         .exit = "a -> {a}"},
        "x");
    run({.what = "String table replay retains children never actually overwritten",
         .body = replace(stringTable, "[%textZero, %textTwo]", "[%textZero, %textZero]"),
         .arrays = "keys:[textZero,textZero]; a:[zero,zero,x]",
         .reads = "keys[0]=textZero; keys[1]=textZero; keys[0]=textZero",
         .exit = "a -> {a,x}"});
    run({.what = "saved child identity survives String table-directed overwrites",
         .body = replace(replace(stringTable, "  cf.br ^header",
                                 "  %saved = ctjs.get_property %a[%zero]\n  cf.br ^header"),
                         "ctjs.return %a", "ctjs.return %saved"),
         .arrays = "keys:[textZero,textTwo]; a:[zero,zero,zero]",
         .reads = "a[0]=x; keys[0]=textZero; keys[1]=textTwo; keys[0]=textZero",
         .exit = "x -> {x}"});
    const auto selectedStringTable =
        replace(replace(stringTable, "[%x, %zero, %x]", "[%x, %keys, %x]"),
                "  %slot = ctjs.get_property %keys[%pick]",
                "  %table = ctjs.get_property %base[%one]\n"
                "  %slot = ctjs.get_property %table[%pick]");
    run({.what = "String table keys exclude a reloaded receiver from every write footprint",
         .body = selectedStringTable,
         .arrays = "keys:[textZero,textTwo]; a:[zero,keys,zero]",
         .reads = "a[1]=keys; keys[0]=textZero; a[1]=keys; keys[1]=textTwo; a[1]=keys; "
                  "keys[0]=textZero",
         .exit = "a -> {a,keys}"},
        "x");
    const auto nestedStringTable =
        replace(replace(stringTable, "  %keys =",
                        "  %textOne = ctjs.constant #ctjs.string<\"1\"> "
                        "{storage_test_id = \"textOne\"}\n"
                        "  %picks = ctjs.create_array [%textZero, %textOne] "
                        "{storage_test_id = \"picks\"}\n"
                        "  %keys ="),
                "  %slot = ctjs.get_property %keys[%pick]",
                "  %lookup = ctjs.get_property %picks[%pick]\n"
                "  %slot = ctjs.get_property %keys[%lookup]");
    run({.what = "a nested table read converts String keys only at each property boundary",
         .body = nestedStringTable,
         .arrays = "picks:[textZero,textOne]; keys:[textZero,textTwo]; a:[zero,zero,zero]",
         .reads = "picks[0]=textZero; keys[0]=textZero; picks[1]=textOne; keys[1]=textTwo; "
                  "picks[0]=textZero; keys[0]=textZero",
         .exit = "a -> {a}"},
        "x");
    for (const std::string key : {"", "00", "-0", "+0", "0.0", " 0", "0x0", "3", "4294967295"}) {
        reject("a String table key must spell an existing canonical own element",
               replace(stringTable, "#ctjs.string<\"0\">", "#ctjs.string<\"" + key + "\">"));
    }
    reject("String table support cannot coerce an object element to a property key",
           replace(stringTable, "[%textZero, %textTwo]", "[%x, %textTwo]"));
    reject("String table support cannot treat the length property as an own element",
           replace(stringTable, "#ctjs.string<\"0\">", "#ctjs.string<\"length\">"));
    for (const std::string expression :
         {"ctjs.binary add %slot, %zero", "ctjs.binary add %zero, %slot",
          "ctjs.binary_static add %slot, %zero"}) {
        reject("String table positions cannot become Number operands inside addition",
               replace(replace(stringTable, "  ctjs.set_property %base[%slot]",
                               "  %sumKey = " + expression + "\n  ctjs.set_property %base[%slot]"),
                       "%base[%slot]", "%base[%sumKey]"));
    }
    reject("nested String lookups preserve canonical spelling at the inner property boundary",
           replace(nestedStringTable, "#ctjs.string<\"1\">", "#ctjs.string<\"01\">"));
    reject("String table writes cannot overlap a selected receiver reload",
           replace(selectedStringTable, "#ctjs.string<\"2\">", "#ctjs.string<\"1\">"));
    reject("a later store cannot hide inside a String table reload gap",
           replace(selectedStringTable,
                   "  %step =", "  ctjs.set_property %base[%one], %zero\n  %step ="));
    for (const std::string mutation :
         {"ctjs.set_property %keys[%one], %textZero", "ctjs.set_property %keys[%key], %one"}) {
        reject("String table mutation before a read remains in the complete census",
               replace(stringTable, "  %pick =", "  " + mutation + "\n  %pick ="));
        reject("String table mutation after a read remains in the complete census",
               replace(stringTable, "  %step =", "  " + mutation + "\n  %step ="));
    }
    const auto stringTableAlias =
        replace(stringTable, "  cf.br ^header",
                "  %box = ctjs.create_array [%keys]\n"
                "  %alias = ctjs.get_property %box[%zero]\n  cf.br ^header");
    reject("String table aliases retain the same exact allocation in the write census",
           replace(stringTableAlias,
                   "  %step =", "  ctjs.set_property %alias[%one], %textZero\n  %step ="));
    const auto convertedTable =
        replace(stringTable, "  ctjs.set_property %base[%slot]",
                "  %converted = ctjs.unary plus %slot\n  ctjs.set_property %base[%converted]");
    for (const auto & source :
         {convertedTable,
          replace(replace(convertedTable, "#ctjs.string<\"2\">", "#ctjs.string<\"-2\">"),
                  "unary plus %slot", "unary neg %slot"),
          replace(replace(replace(convertedTable, "#ctjs.string<\"0\">", "#ctjs.string<\"-1\">"),
                          "#ctjs.string<\"2\">", "#ctjs.string<\"-3\">"),
                  "unary plus %slot", "unary bitnot %slot"),
          replace(convertedTable, "#ctjs.string<\"0\">", "#ctjs.null"),
          replace(convertedTable, "#ctjs.string<\"0\">", "#ctjs.boolean<false>"),
          replace(convertedTable, "  %converted = ctjs.unary plus %slot",
                  "  %number = ctjs.unary plus %slot\n"
                  "  %converted = ctjs.binary add %number, %zero"),
          replace(convertedTable, "  %converted = ctjs.unary plus %slot",
                  "  %number = ctjs.unary plus %slot\n"
                  "  %converted = ctjs.unary plus %number")}) {
        run({.what =
                 "explicit table conversion makes a Number without changing the stored primitive",
             .body = source,
             .arrays = "keys:[textZero,textTwo]; a:[zero,zero,zero]",
             .reads = "keys[0]=textZero; keys[1]=textTwo; keys[0]=textZero",
             .exit = "a -> {a}"},
            "x");
    }
    run({.what = "converted table replay retains a child at an unwritten position",
         .body = replace(convertedTable, "[%textZero, %textTwo]", "[%textZero, %textZero]"),
         .arrays = "keys:[textZero,textZero]; a:[zero,zero,x]",
         .reads = "keys[0]=textZero; keys[1]=textZero; keys[0]=textZero",
         .exit = "a -> {a,x}"});
    run({.what = "a saved child retains its original identity across converted table writes",
         .body = replace(replace(convertedTable, "  cf.br ^header",
                                 "  %saved = ctjs.get_property %a[%zero]\n  cf.br ^header"),
                         "ctjs.return %a", "ctjs.return %saved"),
         .arrays = "keys:[textZero,textTwo]; a:[zero,zero,zero]",
         .reads = "a[0]=x; keys[0]=textZero; keys[1]=textTwo; keys[0]=textZero",
         .exit = "x -> {x}"});
    const auto selectedConvertedTable =
        replace(selectedStringTable, "  ctjs.set_property %base[%slot]",
                "  %converted = ctjs.unary plus %slot\n  ctjs.set_property %base[%converted]");
    run({.what = "converted table keys still prove the receiver's independent reload gap",
         .body = selectedConvertedTable,
         .arrays = "keys:[textZero,textTwo]; a:[zero,keys,zero]",
         .reads = "a[1]=keys; keys[0]=textZero; a[1]=keys; keys[1]=textTwo; a[1]=keys; "
                  "keys[0]=textZero",
         .exit = "a -> {a,keys}"},
        "x");
    for (const std::string primitive :
         {"#ctjs.string<\"00\">", "#ctjs.string<\"-0\">", "#ctjs.string<\"0.0\">",
          "#ctjs.string<\"-2\">", "#ctjs.string<\"3\">", "#ctjs.string<\"4294967296\">",
          "#ctjs.number<4602678819172646912>", "#ctjs.bigint<\"0\">", "#ctjs.undefined"}) {
        const auto source = replace(convertedTable, "#ctjs.string<\"0\">", primitive);
        if (primitive == "#ctjs.string<\"00\">" || primitive == "#ctjs.string<\"-0\">" ||
            primitive == "#ctjs.string<\"0.0\">") {
            run({.what = "decimal conversion preserves original table primitives",
                 .body = source,
                 .arrays = "keys:[textZero,textTwo]; a:[zero,zero,zero]",
                 .reads = "keys[0]=textZero; keys[1]=textTwo; keys[0]=textZero",
                 .exit = "a -> {a}"},
                "x");
        } else {
            reject("table conversion retains exact primitive and own-index bounds", source);
        }
    }
    for (const std::string text : {"", "+00", " 00 ", "00000000000000000000000000000000"}) {
        run({.what =
                 "bounded signed decimal and empty Strings convert without changing their origin",
             .body =
                 replace(convertedTable, "#ctjs.string<\"0\">", "#ctjs.string<\"" + text + "\">"),
             .arrays = "keys:[textZero,textTwo]; a:[zero,zero,zero]",
             .reads = "keys[0]=textZero; keys[1]=textTwo; keys[0]=textZero",
             .exit = "a -> {a}"},
            "x");
    }
    for (const std::string text : {"+", "-", "--0", "+-0", "-+0", "1e-999junk", "0x0", "0.0", "0e0",
                                   "000000000000000000000000000000000"}) {
        const auto source =
            replace(convertedTable, "#ctjs.string<\"0\">", "#ctjs.string<\"" + text + "\">");
        if (text == "0x0" || text == "0.0" || text == "0e0") {
            run({.what = "bounded numeric conversion preserves the original historical String",
                 .body = source,
                 .arrays = "keys:[textZero,textTwo]; a:[zero,zero,zero]",
                 .reads = "keys[0]=textZero; keys[1]=textTwo; keys[0]=textZero",
                 .exit = "a -> {a}"},
                "x");
        } else {
            reject("conversion validates the whole bounded numeric grammar before Core parsing",
                   source);
        }
    }
    const auto radixTable =
        replace(replace(convertedTable, "#ctjs.string<\"0\">", "#ctjs.string<\"0x0\">"),
                "#ctjs.string<\"2\">", "#ctjs.string<\"0X2\">");
    for (const std::string expression :
         {"unary plus %slot", "binary sub %slot, %zero", "binary mul %slot, %one",
          "binary div %slot, %one", "binary mod %slot, %three", "binary pow %slot, %one",
          "binary_static bitand %slot, %two", "binary_static bitor %slot, %zero",
          "binary_static bitxor %slot, %zero", "binary_static shl %slot, %zero",
          "binary_static shr %slot, %zero", "binary_static ushr %slot, %zero"}) {
        run({.what = "radix conversion attaches only to numeric results and keeps table identity",
             .body = replace(radixTable, "unary plus %slot", expression),
             .arrays = "keys:[textZero,textTwo]; a:[zero,zero,zero]",
             .reads = "keys[0]=textZero; keys[1]=textTwo; keys[0]=textZero",
             .exit = "a -> {a}"},
            "x");
    }
    for (const std::string text :
         {"0X00", "0b0", "0B00", "0o0", "0O00", " 0x0 ", "0x000000000000000000000000000000"}) {
        run({.what = "unsigned radix prefixes retain the original bounded String spelling",
             .body = replace(radixTable, "#ctjs.string<\"0x0\">", "#ctjs.string<\"" + text + "\">"),
             .arrays = "keys:[textZero,textTwo]; a:[zero,zero,zero]",
             .reads = "keys[0]=textZero; keys[1]=textTwo; keys[0]=textZero",
             .exit = "a -> {a}"},
            "x");
        reject("unsigned radix Strings remain ordinary properties without conversion",
               replace(stringTable, "#ctjs.string<\"0\">", "#ctjs.string<\"" + text + "\">"));
    }
    run({.what = "the largest bounded radix Number retains exact low-bit conversion",
         .body =
             replace(replace(radixTable, "#ctjs.string<\"0X2\">", "#ctjs.string<\"0xffffffff\">"),
                     "unary plus %slot", "binary_static bitand %slot, %two"),
         .arrays = "keys:[textZero,textTwo]; a:[zero,zero,zero]",
         .reads = "keys[0]=textZero; keys[1]=textTwo; keys[0]=textZero",
         .exit = "a -> {a}"},
        "x");
    for (const std::string text :
         {"+0x0", "-0x0", "+0o0", "-0b0", "0x", "0o", "0b", "0xg", "0o8", "0b2", "0x0.0", "0x0p0",
          "0x0junk", "0x0n", "0x_0", "0x100000000", "0x0000000000000000000000000000000"}) {
        reject("radix conversion validates sign digits complete spelling size and Number bounds",
               replace(radixTable, "#ctjs.string<\"0x0\">", "#ctjs.string<\"" + text + "\">"));
    }
    run({.what = "radix table overwrites preserve a child saved before conversion",
         .body = replace(replace(radixTable, "  cf.br ^header",
                                 "  %saved = ctjs.get_property %a[%zero]\n  cf.br ^header"),
                         "ctjs.return %a", "ctjs.return %saved"),
         .arrays = "keys:[textZero,textTwo]; a:[zero,zero,zero]",
         .reads = "a[0]=x; keys[0]=textZero; keys[1]=textTwo; keys[0]=textZero",
         .exit = "x -> {x}"});
    reject("radix conversion cannot make an earlier String Add numeric",
           replace(radixTable, "  %converted = ctjs.unary plus %slot",
                   "  %text = ctjs.binary add %slot, %zero\n"
                   "  %converted = ctjs.unary plus %text"));
    for (const std::string before : {"  %pick =", "  %step ="}) {
        reject(
            "radix table mutations remain visible before and after each original read",
            replace(radixTable, before, "  ctjs.set_property %keys[%one], %textZero\n" + before));
    }
    const auto exponentTable =
        replace(replace(convertedTable, "#ctjs.string<\"0\">", "#ctjs.string<\"0.0\">"),
                "#ctjs.string<\"2\">", "#ctjs.string<\"20e-1\">");
    for (const std::string expression :
         {"unary plus %slot", "binary sub %slot, %zero", "binary mul %slot, %one",
          "binary div %slot, %one", "binary mod %slot, %three", "binary pow %slot, %one",
          "binary_static bitand %slot, %two", "binary_static bitor %slot, %zero",
          "binary_static bitxor %slot, %zero", "binary_static shl %slot, %zero",
          "binary_static shr %slot, %zero", "binary_static ushr %slot, %zero"}) {
        run({.what = "decimal point and exponent conversion preserves original primitive identity",
             .body = replace(exponentTable, "unary plus %slot", expression),
             .arrays = "keys:[textZero,textTwo]; a:[zero,zero,zero]",
             .reads = "keys[0]=textZero; keys[1]=textTwo; keys[0]=textZero",
             .exit = "a -> {a}"},
            "x");
    }
    for (const std::string text :
         {".0", "0.", "-0.0", "+.0", "0.e+0", "00.000E-99", "1e-999", "-1e-999", "1e-2147483615",
          " 0.0 ", "0.000000000000000000000000000000"}) {
        run({.what = "bounded decimal grammar retains integral results and signed zero origins",
             .body =
                 replace(exponentTable, "#ctjs.string<\"0.0\">", "#ctjs.string<\"" + text + "\">"),
             .arrays = "keys:[textZero,textTwo]; a:[zero,zero,zero]",
             .reads = "keys[0]=textZero; keys[1]=textTwo; keys[0]=textZero",
             .exit = "a -> {a}"},
            "x");
        reject("decimal grammar Strings keep their property spelling without conversion",
               replace(stringTable, "#ctjs.string<\"0\">", "#ctjs.string<\"" + text + "\">"));
    }
    for (const std::string text : {".",
                                   ".e0",
                                   "e0",
                                   "0e",
                                   "0e+",
                                   "0e-",
                                   "0e--0",
                                   "0e+-0",
                                   "0e.0",
                                   "0..0",
                                   "0e0e0",
                                   "0.0junk",
                                   "1e-999junk",
                                   "0.5",
                                   "1e-1",
                                   "1e999",
                                   "4294967296.0",
                                   "0.0000000000000000000000000000000",
                                   "1e9999999999",
                                   "-1e9999999999",
                                   "1e-9999999999",
                                   "1e-2147483616",
                                   "99e2147483647",
                                   "0.01e-2147483648"}) {
        reject("decimal conversion requires complete bounded grammar and an integral result",
               replace(exponentTable, "#ctjs.string<\"0.0\">", "#ctjs.string<\"" + text + "\">"));
    }
    run({.what = "the largest integral decimal conversion preserves exact bitwise conversion",
         .body = replace(
             replace(exponentTable, "#ctjs.string<\"20e-1\">", "#ctjs.string<\"4294967295.0\">"),
             "unary plus %slot", "binary_static bitand %slot, %two"),
         .arrays = "keys:[textZero,textTwo]; a:[zero,zero,zero]",
         .reads = "keys[0]=textZero; keys[1]=textTwo; keys[0]=textZero",
         .exit = "a -> {a}"},
        "x");
    run({.what = "decimal conversion cannot release a child saved before the writes",
         .body = replace(replace(exponentTable, "  cf.br ^header",
                                 "  %saved = ctjs.get_property %a[%zero]\n  cf.br ^header"),
                         "ctjs.return %a", "ctjs.return %saved"),
         .arrays = "keys:[textZero,textTwo]; a:[zero,zero,zero]",
         .reads = "a[0]=x; keys[0]=textZero; keys[1]=textTwo; keys[0]=textZero",
         .exit = "x -> {x}"});
    reject("decimal grammar cannot convert an earlier String Add to Number Add",
           replace(exponentTable, "  %converted = ctjs.unary plus %slot",
                   "  %text = ctjs.binary add %slot, %zero\n"
                   "  %converted = ctjs.unary plus %text"));
    for (const std::string before : {"  %pick =", "  %step ="}) {
        reject("decimal table mutations remain visible before and after every read",
               replace(exponentTable, before,
                       "  ctjs.set_property %keys[%one], %textZero\n" + before));
    }
    reject("decimal conversion keeps the independent receiver reload gap",
           replace(replace(selectedConvertedTable, "#ctjs.string<\"0\">", "#ctjs.string<\"0.0\">"),
                   "#ctjs.string<\"2\">", "#ctjs.string<\"1e0\">"));
    const auto fractionalTable =
        replace(replace(replace(convertedTable, "#ctjs.string<\"0\">", "#ctjs.string<\"0.9\">"),
                        "#ctjs.string<\"2\">", "#ctjs.string<\"2.9\">"),
                "unary plus %slot", "binary_static bitor %slot, %zero");
    for (const std::string expression :
         {"bitand %slot, %two", "bitor %slot, %zero", "bitxor %slot, %zero", "shl %slot, %zero",
          "shr %slot, %zero", "ushr %slot, %zero"}) {
        run({.what = "bitwise table operands truncate fractions without changing stored Strings",
             .body = replace(fractionalTable, "bitor %slot, %zero", expression),
             .arrays = "keys:[textZero,textTwo]; a:[zero,zero,zero]",
             .reads = "keys[0]=textZero; keys[1]=textTwo; keys[0]=textZero",
             .exit = "a -> {a}"},
            "x");
    }
    for (const auto & [first, second] :
         {std::pair{"-0.9", "+2.9"}, std::pair{"5e-1", "29e-1"},
          std::pair{"2147483648.5", "4294967294.9"}, std::pair{"-0.9", "-4294967294.9"}}) {
        run({.what =
                 "fractional bitwise Strings preserve signed zero and wrap at signed boundaries",
             .body = replace(replace(replace(fractionalTable, "#ctjs.string<\"0.9\">",
                                             "#ctjs.string<\"" + std::string{first} + "\">"),
                                     "#ctjs.string<\"2.9\">",
                                     "#ctjs.string<\"" + std::string{second} + "\">"),
                             "bitor %slot, %zero", "bitand %slot, %two"),
             .arrays = "keys:[textZero,textTwo]; a:[zero,zero,zero]",
             .reads = "keys[0]=textZero; keys[1]=textTwo; keys[0]=textZero",
             .exit = "a -> {a}"},
            "x");
    }
    run({.what = "complement truncates original negative fractional String operands",
         .body = replace(
             replace(replace(fractionalTable, "#ctjs.string<\"0.9\">", "#ctjs.string<\"-1.9\">"),
                     "#ctjs.string<\"2.9\">", "#ctjs.string<\"-3.9\">"),
             "binary_static bitor %slot, %zero", "unary bitnot %slot"),
         .arrays = "keys:[textZero,textTwo]; a:[zero,zero,zero]",
         .reads = "keys[0]=textZero; keys[1]=textTwo; keys[0]=textZero",
         .exit = "a -> {a}"},
        "x");
    run({.what = "varying fractional shift counts truncate before the original left shift",
         .body =
             replace(replace(replace(replace(fractionalTable, "[%x, %zero, %x]", "[%zero, %x, %x]"),
                                     "#ctjs.string<\"0.9\">", "#ctjs.string<\".5\">"),
                             "#ctjs.string<\"2.9\">", "#ctjs.string<\"1.9\">"),
                     "bitor %slot, %zero", "shl %one, %slot"),
         .arrays = "keys:[textZero,textTwo]; a:[zero,zero,zero]",
         .reads = "keys[0]=textZero; keys[1]=textTwo; keys[0]=textZero",
         .exit = "a -> {a}"},
        "x");
    for (const std::string expression :
         {"bitor %i, %textZero", "shl %i, %textZero", "shr %i, %textZero", "ushr %i, %textZero"}) {
        run({.what = "invariant fractional masks and shift counts share the scalar bitwise proof",
             .body = replace(fractionalTable, "bitor %slot, %zero", expression),
             .arrays = "keys:[textZero,textTwo]; a:[zero,zero,zero]",
             .reads = "keys[0]=textZero; keys[1]=textTwo; keys[0]=textZero",
             .exit = "a -> {a}"},
            "x");
    }
    const auto selectedFractional = replace(
        replace(replace(selectedConvertedTable, "#ctjs.string<\"0\">", "#ctjs.string<\"0.9\">"),
                "#ctjs.string<\"2\">", "#ctjs.string<\"2.9\">"),
        "unary plus %slot", "binary_static bitor %slot, %zero");
    run({.what = "fractional table conversion preserves an independent receiver reload gap",
         .body = selectedFractional,
         .arrays = "keys:[textZero,textTwo]; a:[zero,keys,zero]",
         .reads =
             "a[1]=keys; keys[0]=textZero; a[1]=keys; keys[1]=textTwo; a[1]=keys; keys[0]=textZero",
         .exit = "a -> {a,keys}"},
        "x");
    reject("truncated fractional keys cannot overwrite their selected receiver",
           replace(selectedFractional, "#ctjs.string<\"2.9\">", "#ctjs.string<\"1.9\">"));
    run({.what = "a child saved before fractional writes retains its original identity",
         .body = replace(replace(fractionalTable, "  cf.br ^header",
                                 "  %saved = ctjs.get_property %a[%zero]\n  cf.br ^header"),
                         "ctjs.return %a", "ctjs.return %saved"),
         .arrays = "keys:[textZero,textTwo]; a:[zero,zero,zero]",
         .reads = "a[0]=x; keys[0]=textZero; keys[1]=textTwo; keys[0]=textZero",
         .exit = "x -> {x}"});
    for (const std::string text :
         {".", "0.9junk", "1e-999junk", "1e9999999999", "1e-2147483616", "Infinity", "NaN",
          "4294967295.5", "-4294967295.5", "0.0000000000000000000000000000000"}) {
        if (text == "Infinity" || text == "NaN") {
            run({.what = "exact nonfinite token keeps the historical fractional table body",
                 .body = replace(fractionalTable, "#ctjs.string<\"0.9\">",
                                 "#ctjs.string<\"" + text + "\">"),
                 .arrays = "keys:[textZero,textTwo]; a:[zero,zero,zero]",
                 .reads = "keys[0]=textZero; keys[1]=textTwo; keys[0]=textZero",
                 .exit = "a -> {a}"},
                "x");
            continue;
        }
        if (text == "-4294967295.5") {
            run({.what = "wide fractional conversion preserves the original retained child",
                 .body = replace(fractionalTable, "#ctjs.string<\"0.9\">",
                                 "#ctjs.string<\"" + text + "\">"),
                 .arrays = "keys:[textZero,textTwo]; a:[x,zero,zero]",
                 .reads = "keys[0]=textZero; keys[1]=textTwo; keys[0]=textZero",
                 .exit = "a -> {a,x}"});
            continue;
        }
        reject("bitwise conversion keeps grammar exponent source-size and own-index bounds",
               replace(fractionalTable, "#ctjs.string<\"0.9\">", "#ctjs.string<\"" + text + "\">"));
    }
    for (const std::string expression :
         {"unary plus %slot", "binary sub %slot, %zero", "binary mul %slot, %one",
          "binary div %slot, %one", "binary add %slot, %zero"}) {
        reject("fractional arithmetic does not borrow bitwise truncation",
               replace(fractionalTable, "binary_static bitor %slot, %zero", expression));
        const auto converted =
            replace(fractionalTable, "  %converted = ctjs.binary_static bitor %slot, %zero",
                    "  %number = ctjs." + expression +
                        "\n  %converted = ctjs.binary_static bitor %number, %zero");
        if (expression == "unary plus %slot" || expression == "binary sub %slot, %zero") {
            run({.what = "original table identity arithmetic supplies its bitwise conversion",
                 .body = converted,
                 .arrays = "keys:[textZero,textTwo]; a:[zero,zero,zero]",
                 .reads = "keys[0]=textZero; keys[1]=textTwo; keys[0]=textZero",
                 .exit = "a -> {a}"},
                "x");
        } else {
            reject("an outer bitwise operation cannot truncate an unproved arithmetic intermediate",
                   converted);
        }
    }
    reject("fractional String properties preserve their original spelling",
           replace(fractionalTable, "%base[%converted]", "%base[%slot]"));
    run({.what =
             "fractional Number literals share bitwise conversion without changing their identity",
         .body =
             replace(fractionalTable, "#ctjs.string<\"0.9\">", "#ctjs.number<4602678819172646912>"),
         .arrays = "keys:[textZero,textTwo]; a:[zero,zero,zero]",
         .reads = "keys[0]=textZero; keys[1]=textTwo; keys[0]=textZero",
         .exit = "a -> {a}"},
        "x");
    for (const std::string before : {"  %pick =", "  %step ="}) {
        reject("fractional table mutation remains visible before and after reads",
               replace(fractionalTable, before,
                       "  ctjs.set_property %keys[%one], %textZero\n" + before));
    }
    const auto numberFractionalTable = replace(
        replace(fractionalTable, "#ctjs.string<\"0.9\">", "#ctjs.number<4606281698874543309>"),
        "#ctjs.string<\"2.9\">", "#ctjs.number<4613712638259704627>");
    for (const std::string expression :
         {"bitand %slot, %two", "bitor %slot, %zero", "bitxor %slot, %zero", "shl %slot, %zero",
          "shr %slot, %zero", "ushr %slot, %zero", "bitor %i, %textZero", "shl %i, %textZero",
          "shr %i, %textZero", "ushr %i, %textZero"}) {
        run({.what = "Number table operands and invariant bitwise inputs retain original fractions",
             .body = replace(numberFractionalTable, "bitor %slot, %zero", expression),
             .arrays = "keys:[textZero,textTwo]; a:[zero,zero,zero]",
             .reads = "keys[0]=textZero; keys[1]=textTwo; keys[0]=textZero",
             .exit = "a -> {a}"},
            "x");
    }
    const auto numberComplementTable =
        replace(replace(replace(numberFractionalTable, "#ctjs.number<4606281698874543309>",
                                "#ctjs.number<13834607695319426662>"),
                        "#ctjs.number<4613712638259704627>", "#ctjs.number<13839336474928165683>"),
                "binary_static bitor %slot, %zero", "unary bitnot %slot");
    const auto negatedNumberTable = replace(
        replace(replace(numberComplementTable, "  %textZero =",
                        "  %absZero = ctjs.constant #ctjs.number<4611235658464650854>\n"
                        "  %absTwo = ctjs.constant #ctjs.number<4615964438073389875>\n"
                        "  %textZero ="),
                "ctjs.constant #ctjs.number<13834607695319426662>", "ctjs.unary neg %absZero"),
        "ctjs.constant #ctjs.number<13839336474928165683>", "ctjs.unary neg %absTwo");
    for (const auto & source : {numberComplementTable, negatedNumberTable}) {
        run({.what = "Number complement accepts a literal or its original single source Neg",
             .body = source,
             .arrays = "keys:[textZero,textTwo]; a:[zero,zero,zero]",
             .reads = "keys[0]=textZero; keys[1]=textTwo; keys[0]=textZero",
             .exit = "a -> {a}"},
            "x");
    }
    run({.what = "varying fractional Number shift counts truncate only at their consumer",
         .body = replace(
             replace(replace(replace(numberFractionalTable, "[%x, %zero, %x]", "[%zero, %x, %x]"),
                             "#ctjs.number<4606281698874543309>",
                             "#ctjs.number<4602678819172646912>"),
                     "#ctjs.number<4613712638259704627>", "#ctjs.number<4611235658464650854>"),
             "bitor %slot, %zero", "shl %one, %slot"),
         .arrays = "keys:[textZero,textTwo]; a:[zero,zero,zero]",
         .reads = "keys[0]=textZero; keys[1]=textTwo; keys[0]=textZero",
         .exit = "a -> {a}"},
        "x");
    run({.what = "a child saved before Number fractional writes keeps its identity",
         .body = replace(replace(numberFractionalTable, "  cf.br ^header",
                                 "  %saved = ctjs.get_property %a[%zero]\n  cf.br ^header"),
                         "ctjs.return %a", "ctjs.return %saved"),
         .arrays = "keys:[textZero,textTwo]; a:[zero,zero,zero]",
         .reads = "a[0]=x; keys[0]=textZero; keys[1]=textTwo; keys[0]=textZero",
         .exit = "x -> {x}"});
    const auto selectedNumberFractional = replace(
        replace(selectedFractional, "#ctjs.string<\"0.9\">", "#ctjs.number<4606281698874543309>"),
        "#ctjs.string<\"2.9\">", "#ctjs.number<4613712638259704627>");
    run({.what = "Number truncation preserves the independent receiver reload gap",
         .body = selectedNumberFractional,
         .arrays = "keys:[textZero,textTwo]; a:[zero,keys,zero]",
         .reads =
             "a[1]=keys; keys[0]=textZero; a[1]=keys; keys[1]=textTwo; a[1]=keys; keys[0]=textZero",
         .exit = "a -> {a,keys}"},
        "x");
    reject("truncated Number keys cannot overwrite their reloaded table receiver",
           replace(selectedNumberFractional, "#ctjs.number<4613712638259704627>",
                   "#ctjs.number<4611235658464650854>"));
    for (const std::string bits :
         {"4751297606874824704", "13974669643729600512", "4751297606876816998",
          "9218868437227405312", "18442240474082181120", "9221120237041090560"}) {
        if (bits != "4751297606874824704") {
            const bool retained = bits == "13974669643729600512";
            run({.what = "wide Number conversion preserves original reads and retained children",
                 .body = replace(numberFractionalTable, "#ctjs.number<4606281698874543309>",
                                 "#ctjs.number<" + bits + ">"),
                 .arrays = retained ? "keys:[textZero,textTwo]; a:[x,zero,zero]"
                                    : "keys:[textZero,textTwo]; a:[zero,zero,zero]",
                 .reads = "keys[0]=textZero; keys[1]=textTwo; keys[0]=textZero",
                 .exit = retained ? "a -> {a,x}" : "a -> {a}"},
                retained ? "" : "x");
            continue;
        }
        reject("Number bitwise conversion requires valid own-index results",
               replace(numberFractionalTable, "#ctjs.number<4606281698874543309>",
                       "#ctjs.number<" + bits + ">"));
    }
    for (const std::string expression :
         {"unary plus %slot", "binary sub %slot, %zero", "binary mul %slot, %one",
          "binary div %slot, %one", "binary add %slot, %zero"}) {
        reject("Number arithmetic cannot borrow bitwise truncation",
               replace(numberFractionalTable, "binary_static bitor %slot, %zero", expression));
        const auto converted =
            replace(numberFractionalTable, "  %converted = ctjs.binary_static bitor %slot, %zero",
                    "  %number = ctjs." + expression +
                        "\n  %converted = ctjs.binary_static bitor %number, %zero");
        if (expression == "unary plus %slot" || expression == "binary sub %slot, %zero" ||
            expression == "binary add %slot, %zero" || expression == "binary mul %slot, %one" ||
            expression == "binary div %slot, %one") {
            run({.what = "Number identity conversion after a read keeps its bitwise snapshot",
                 .body = converted,
                 .arrays = "keys:[textZero,textTwo]; a:[zero,zero,zero]",
                 .reads = "keys[0]=textZero; keys[1]=textTwo; keys[0]=textZero",
                 .exit = "a -> {a}"},
                "x");
        } else {
            reject("Number bitwise conversion cannot borrow unproved arithmetic provenance",
                   converted);
        }
    }
    reject("Number fractional properties retain their exact original key",
           replace(numberFractionalTable, "%base[%converted]", "%base[%slot]"));
    const auto afterReadTable =
        replace(numberFractionalTable, "  %converted = ctjs.binary_static bitor %slot, %zero",
                "  %number = ctjs.unary plus %slot\n"
                "  %converted = ctjs.binary_static bitor %number, %zero");
    const auto afterSubZero =
        replace(afterReadTable, "unary plus %slot", "binary sub %slot, %zero");
    run({.what = "subtracting zero after a String read preserves its numeric conversion",
         .body =
             replace(afterSubZero, "#ctjs.number<4606281698874543309>", "#ctjs.string<\"0.9\">"),
         .arrays = "keys:[textZero,textTwo]; a:[zero,zero,zero]",
         .reads = "keys[0]=textZero; keys[1]=textTwo; keys[0]=textZero",
         .exit = "a -> {a}"},
        "x");
    reject("subtract-zero bits cannot become a fractional property key",
           replace(afterSubZero, "%base[%converted]", "%base[%number]"));
    reject("subtract-zero bits cannot become an arithmetic value",
           replace(afterSubZero, "binary_static bitor %number, %zero", "binary add %number, %one"));
    run({.what = "nonzero literal subtraction preserves a retained child",
         .body = replace(afterSubZero, "binary sub %slot, %zero", "binary sub %slot, %one"),
         .arrays = "keys:[textZero,textTwo]; a:[zero,zero,x]",
         .reads = "keys[0]=textZero; keys[1]=textTwo; keys[0]=textZero",
         .exit = "a -> {a,x}"});
    const auto afterSubHalf =
        replace(replace(afterSubZero, "  %textZero =",
                        "  %half = ctjs.constant #ctjs.number<4602678819172646912>\n  %textZero ="),
                "binary sub %slot, %zero", "binary sub %slot, %half");
    run({.what = "literal fractional subtraction snapshots only its bitwise result",
         .body = afterSubHalf,
         .arrays = "keys:[textZero,textTwo]; a:[zero,zero,zero]",
         .reads = "keys[0]=textZero; keys[1]=textTwo; keys[0]=textZero",
         .exit = "a -> {a}"},
        "x");
    run({.what = "a saved child still escapes after fractional subtraction overwrites",
         .body = replace(replace(afterSubHalf, "  cf.br ^header",
                                 "  %saved = ctjs.get_property %a[%zero]\n  cf.br ^header"),
                         "ctjs.return %a", "ctjs.return %saved"),
         .arrays = "keys:[textZero,textTwo]; a:[zero,zero,zero]",
         .reads = "a[0]=x; keys[0]=textZero; keys[1]=textTwo; keys[0]=textZero",
         .exit = "x -> {x}"});
    reject("fractional subtraction bits do not prove its original property key",
           replace(afterSubHalf, "%base[%converted]", "%base[%number]"));
    reject("fractional subtraction bits do not prove an exact arithmetic Number",
           replace(afterSubHalf, "binary_static bitor %number, %zero", "binary add %number, %one"));
    const auto arithmeticOwnIndex =
        replace(replace(replace(replace(afterSubHalf, "4602678819172646912", "4598175219545276416"),
                                "4606281698874543309", "4598175219545276416"),
                        "4613712638259704627", "4612248968380809216"),
                "%base[%converted]", "%base[%number]");
    run({.what = "exact integral arithmetic Number results name own array indices",
         .body = arithmeticOwnIndex,
         .arrays = "keys:[textZero,textTwo]; a:[zero,zero,zero]",
         .reads = "keys[0]=textZero; keys[1]=textTwo; keys[0]=textZero",
         .exit = "a -> {a}"},
        "x");
    run({.what = "integral Number indices preserve a previously saved child",
         .body = replace(replace(arithmeticOwnIndex, "  cf.br ^header",
                                 "  %saved = ctjs.get_property %a[%zero]\n  cf.br ^header"),
                         "ctjs.return %a", "ctjs.return %saved"),
         .arrays = "keys:[textZero,textTwo]; a:[zero,zero,zero]",
         .reads = "a[0]=x; keys[0]=textZero; keys[1]=textTwo; keys[0]=textZero",
         .exit = "x -> {x}"});
    run({.what = "integral Number indices retain children outside the actual write image",
         .body = replace(arithmeticOwnIndex, "[%textZero, %textTwo]", "[%textZero, %textZero]"),
         .arrays = "keys:[textZero,textZero]; a:[zero,zero,x]",
         .reads = "keys[0]=textZero; keys[1]=textZero; keys[0]=textZero",
         .exit = "a -> {a,x}"});
    for (const std::string before : {"  %pick =", "  %step ="}) {
        reject("integral Number indices retain the complete table mutation census",
               replace(arithmeticOwnIndex, before,
                       "  ctjs.set_property %keys[%one], %textZero\n" + before));
    }
    for (const std::string value :
         {"#ctjs.number<4606281698874543309>", "#ctjs.number<13833932155375321088>",
          "#ctjs.number<4751297606874300416>", "#ctjs.number<4751297606876135424>",
          "#ctjs.number<9218868437227405312>", "#ctjs.number<9221120237041090560>",
          "#ctjs.string<\"0.25\">", "#ctjs.bigint<\"0\">"}) {
        reject("own indices cannot borrow truncated wrapped nonfinite or coercing Number bits",
               replace(arithmeticOwnIndex,
                       "%textZero = ctjs.constant #ctjs.number<4598175219545276416>",
                       "%textZero = ctjs.constant " + value));
    }
    for (const std::string sign : {"plus", "neg"}) {
        auto signedOwnIndex =
            replace(replace(arithmeticOwnIndex, "  %converted =",
                            "  %signed = ctjs.unary " + sign + " %number\n  %converted ="),
                    "%base[%number]", "%base[%signed]");
        if (sign == "neg") {
            signedOwnIndex = replace(signedOwnIndex, "4612248968380809216", "13833932155375321088");
        }
        run({.what = "unary signs preserve exact integral arithmetic own indices",
             .body = signedOwnIndex,
             .arrays = "keys:[textZero,textTwo]; a:[zero,zero,zero]",
             .reads = "keys[0]=textZero; keys[1]=textTwo; keys[0]=textZero",
             .exit = "a -> {a}"},
            "x");
        run({.what = "unary arithmetic own indices preserve saved child identity",
             .body = replace(replace(signedOwnIndex, "  cf.br ^header",
                                     "  %saved = ctjs.get_property %a[%zero]\n  cf.br ^header"),
                             "ctjs.return %a", "ctjs.return %saved"),
             .arrays = "keys:[textZero,textTwo]; a:[zero,zero,zero]",
             .reads = "a[0]=x; keys[0]=textZero; keys[1]=textTwo; keys[0]=textZero",
             .exit = "x -> {x}"});
        run({.what = "unary arithmetic own indices retain the actual write image",
             .body = replace(signedOwnIndex, "[%textZero, %textTwo]", "[%textZero, %textZero]"),
             .arrays = "keys:[textZero,textZero]; a:[zero,zero,x]",
             .reads = "keys[0]=textZero; keys[1]=textZero; keys[0]=textZero",
             .exit = "a -> {a,x}"});
        for (const std::string before : {"  %pick =", "  %step ="}) {
            reject("unary arithmetic own indices retain the complete mutation census",
                   replace(signedOwnIndex, before,
                           "  ctjs.set_property %keys[%one], %textZero\n" + before));
        }
        for (const std::string value :
             {"#ctjs.number<4606281698874543309>", "#ctjs.number<9218868437227405312>",
              "#ctjs.number<9221120237041090560>", "#ctjs.string<\"0.25\">",
              "#ctjs.bigint<\"0\">"}) {
            reject("unary own indices cannot borrow fractional nonfinite or coerced Number bits",
                   replace(signedOwnIndex,
                           "%textZero = ctjs.constant #ctjs.number<4598175219545276416>",
                           "%textZero = ctjs.constant " + value));
        }
        reject("unary arithmetic own indices reject negative results",
               replace(
                   signedOwnIndex, "%textZero = ctjs.constant #ctjs.number<4598175219545276416>",
                   "%textZero = ctjs.constant #ctjs.number<" +
                       std::string(sign == "neg" ? "4612248968380809216" : "13833932155375321088") +
                       ">"));
    }
    reject("nonzero subtraction keeps String conversion separate from literal Number proof",
           replace(afterSubHalf, "#ctjs.number<4606281698874543309>", "#ctjs.string<\"0.9\">"));
    run({.what = "unary Plus retains independent original Number evidence",
         .body = replace(replace(afterSubHalf, "  %textZero =",
                                 "  %original = ctjs.constant #ctjs.number<4606281698874543309>\n"
                                 "  %textZero ="),
                         "%textZero = ctjs.constant #ctjs.number<4606281698874543309>",
                         "%textZero = ctjs.unary plus %original"),
         .arrays = "keys:[textZero,textTwo]; a:[zero,zero,zero]",
         .reads = "keys[0]=textZero; keys[1]=textTwo; keys[0]=textZero",
         .exit = "a -> {a}"},
        "x");
    for (const std::string sign : {"plus", "neg"}) {
        const auto signedTable =
            replace(afterSubHalf, "  %converted = ctjs.binary_static bitor %number, %zero",
                    "  %signed = ctjs.unary " + sign +
                        " %number\n"
                        "  %combined = ctjs.binary add %signed, " +
                        (sign == "neg" ? "%three" : "%zero") +
                        "\n  %converted = ctjs.binary_static bitor %combined, %zero");
        run({.what = "unary signs preserve computed Number snapshots before later arithmetic",
             .body = signedTable,
             .arrays = "keys:[textZero,textTwo]; a:[zero,zero,zero]",
             .reads = "keys[0]=textZero; keys[1]=textTwo; keys[0]=textZero",
             .exit = "a -> {a}"},
            "x");
        run({.what = "unary Number snapshots retain a child saved before overwrite",
             .body = replace(replace(signedTable, "  cf.br ^header",
                                     "  %saved = ctjs.get_property %a[%zero]\n  cf.br ^header"),
                             "ctjs.return %a", "ctjs.return %saved"),
             .arrays = "keys:[textZero,textTwo]; a:[zero,zero,zero]",
             .reads = "a[0]=x; keys[0]=textZero; keys[1]=textTwo; keys[0]=textZero",
             .exit = "x -> {x}"});
        reject("unary Number snapshots retain the table mutation census",
               replace(signedTable,
                       "  %step =", "  ctjs.set_property %keys[%one], %textZero\n  %step ="));
        reject("unary Number snapshots do not become original property keys",
               replace(signedTable, "%base[%converted]", "%base[%combined]"));
        reject("unary arithmetic still needs original Number rather than String evidence",
               replace(signedTable, "#ctjs.number<4606281698874543309>", "#ctjs.string<\"0.9\">"));
    }
    for (const std::string before : {"  %pick =", "  %step ="}) {
        reject(
            "fractional subtraction preserves the complete table mutation census",
            replace(afterSubHalf, before, "  ctjs.set_property %keys[%one], %textZero\n" + before));
    }
    auto pairedTable =
        replace(replace(afterSubHalf, "  cf.br ^header",
                        "  %quarter = ctjs.constant #ctjs.number<4598175219545276416> "
                        "{storage_test_id = \"quarter\"}\n"
                        "  %offsets = ctjs.create_array [%quarter, %half] {storage_test_id = "
                        "\"offsets\"}\n  cf.br ^header"),
                "  %number = ctjs.binary sub %slot, %half",
                "  %offset = ctjs.get_property %offsets[%pick]\n"
                "  %number = ctjs.binary sub %slot, %offset");
    pairedTable = replace(pairedTable, "#ctjs.number<4602678819172646912>\n",
                          "#ctjs.number<4602678819172646912> {storage_test_id = \"half\"}\n");
    const std::string pairedReads =
        "keys[0]=textZero; offsets[0]=quarter; keys[1]=textTwo; offsets[1]=half; "
        "keys[0]=textZero; offsets[0]=quarter";
    run({.what = "independently varying arithmetic tables retain exact binary64 operands",
         .body = pairedTable,
         .arrays = "keys:[textZero,textTwo]; a:[zero,zero,zero]; offsets:[quarter,half]",
         .reads = pairedReads.c_str(),
         .exit = "a -> {a}"},
        "x");
    run({.what = "binary add keeps both varying Number table operands",
         .body =
             replace(replace(replace(pairedTable, "4598175219545276416", "13821547256400052224"),
                             "4602678819172646912", "13826050856027422720"),
                     "binary sub %slot, %offset", "binary add %slot, %offset"),
         .arrays = "keys:[textZero,textTwo]; a:[zero,zero,zero]; offsets:[quarter,half]",
         .reads = pairedReads.c_str(),
         .exit = "a -> {a}"},
        "x");
    run({.what = "binary_static add keeps both varying Number table operands",
         .body =
             replace(replace(replace(pairedTable, "4598175219545276416", "13821547256400052224"),
                             "4602678819172646912", "13826050856027422720"),
                     "binary sub %slot, %offset", "binary_static add %slot, %offset"),
         .arrays = "keys:[textZero,textTwo]; a:[zero,zero,zero]; offsets:[quarter,half]",
         .reads = pairedReads.c_str(),
         .exit = "a -> {a}"},
        "x");
    run({.what = "binary mul keeps both varying Number table operands",
         .body = replace(replace(replace(pairedTable, "4598175219545276416", "4602678819172646912"),
                                 "4602678819172646912", "4607182418800017408"),
                         "binary sub %slot, %offset", "binary mul %slot, %offset"),
         .arrays = "keys:[textZero,textTwo]; a:[zero,zero,zero]; offsets:[quarter,half]",
         .reads = pairedReads.c_str(),
         .exit = "a -> {a}"},
        "x");
    run({.what = "binary div keeps both varying Number table operands",
         .body = replace(replace(replace(pairedTable, "4598175219545276416", "4611686018427387904"),
                                 "4602678819172646912", "4607182418800017408"),
                         "binary sub %slot, %offset", "binary div %slot, %offset"),
         .arrays = "keys:[textZero,textTwo]; a:[zero,zero,zero]; offsets:[quarter,half]",
         .reads = pairedReads.c_str(),
         .exit = "a -> {a}"},
        "x");
    run({.what = "binary mod keeps both varying Number table operands",
         .body = replace(replace(replace(pairedTable, "4598175219545276416", "4607182418800017408"),
                                 "4602678819172646912", "4613937818241073152"),
                         "binary sub %slot, %offset", "binary mod %slot, %offset"),
         .arrays = "keys:[textZero,textTwo]; a:[zero,zero,zero]; offsets:[quarter,half]",
         .reads = pairedReads.c_str(),
         .exit = "a -> {a}"},
        "x");
    run({.what = "paired arithmetic table overwrites retain a previously saved child",
         .body = replace(replace(pairedTable, "  cf.br ^header",
                                 "  %saved = ctjs.get_property %a[%zero]\n  cf.br ^header"),
                         "ctjs.return %a", "ctjs.return %saved"),
         .arrays = "keys:[textZero,textTwo]; a:[zero,zero,zero]; offsets:[quarter,half]",
         .reads = ("a[0]=x; " + pairedReads).c_str(),
         .exit = "x -> {x}"});
    for (const std::string before : {"  %pick =", "  %step ="}) {
        reject("right arithmetic tables retain their complete mutation census",
               replace(pairedTable, before,
                       "  ctjs.set_property %offsets[%one], %quarter\n" + before));
    }
    reject("paired arithmetic snapshots do not become original property keys",
           replace(pairedTable, "%base[%converted]", "%base[%number]"));
    reject("a changing induction value is not an independent arithmetic singleton",
           replace(pairedTable, "sub %slot, %offset", "sub %slot, %i"));
    reject("String operands cannot borrow the independent Number snapshot",
           replace(pairedTable, "#ctjs.number<4598175219545276416>", "#ctjs.string<\"0.25\">"));
    const auto afterSubQuarters =
        replace(replace(afterSubHalf, "4602678819172646912", "4598175219545276416"),
                "  %number = ctjs.binary sub %slot, %half",
                "  %first = ctjs.binary sub %slot, %half\n"
                "  %number = ctjs.binary sub %first, %half");
    run({.what = "chained subtraction keeps each binary64 result before bitwise conversion",
         .body = afterSubQuarters,
         .arrays = "keys:[textZero,textTwo]; a:[zero,zero,zero]",
         .reads = "keys[0]=textZero; keys[1]=textTwo; keys[0]=textZero",
         .exit = "a -> {a}"},
        "x");
    run({.what = "a saved child survives chained subtraction overwrites",
         .body = replace(replace(afterSubQuarters, "  cf.br ^header",
                                 "  %saved = ctjs.get_property %a[%zero]\n  cf.br ^header"),
                         "ctjs.return %a", "ctjs.return %saved"),
         .arrays = "keys:[textZero,textTwo]; a:[zero,zero,zero]",
         .reads = "a[0]=x; keys[0]=textZero; keys[1]=textTwo; keys[0]=textZero",
         .exit = "x -> {x}"});
    run({.what = "chained subtraction retains a child at an unwritten position",
         .body = replace(afterSubQuarters, "[%textZero, %textTwo]", "[%textZero, %textZero]"),
         .arrays = "keys:[textZero,textZero]; a:[zero,zero,x]",
         .reads = "keys[0]=textZero; keys[1]=textZero; keys[0]=textZero",
         .exit = "a -> {a,x}"});
    for (const std::string key : {"%slot", "%first", "%number"}) {
        reject("chained subtraction never lends converted bits to a property key",
               replace(afterSubQuarters, "%base[%converted]", "%base[" + key + "]"));
    }
    reject("chained subtraction keeps other arithmetic's independent Number proof",
           replace(afterSubQuarters, "binary_static bitor %number, %zero",
                   "binary add %number, %one"));
    for (const std::string expression : {"unary plus %slot", "binary_static bitor %slot, %zero"}) {
        if (expression == "unary plus %slot") {
            run({.what = "subtraction consumes an independently proved unary Number",
                 .body = replace(afterSubQuarters, "binary sub %slot, %half", expression),
                 .arrays = "keys:[textZero,textTwo]; a:[zero,zero,zero]",
                 .reads = "keys[0]=textZero; keys[1]=textTwo; keys[0]=textZero",
                 .exit = "a -> {a}"},
                "x");
            continue;
        }
        reject("subtraction cannot reconstruct binary64 from another result's converted bits",
               replace(afterSubQuarters, "binary sub %slot, %half", expression));
    }
    reject("chained subtraction retains the original String conversion boundary",
           replace(afterSubQuarters, "#ctjs.number<4606281698874543309>", "#ctjs.string<\"0.9\">"));
    for (const std::string before : {"  %pick =", "  %step ="}) {
        reject("chained subtraction retains the complete table mutation census",
               replace(afterSubQuarters, before,
                       "  ctjs.set_property %keys[%one], %textZero\n" + before));
    }
    const auto afterSubAdd =
        replace(afterSubQuarters, "binary sub %first, %half", "binary add %first, %half");
    for (const std::string operation : {"binary add", "binary_static add"}) {
        const auto source = replace(afterSubAdd, "binary add", operation);
        run({.what = "Number addition keeps the preceding binary64 subtraction result",
             .body = source,
             .arrays = "keys:[textZero,textTwo]; a:[zero,zero,zero]",
             .reads = "keys[0]=textZero; keys[1]=textTwo; keys[0]=textZero",
             .exit = "a -> {a}"},
            "x");
        run({.what = "a saved child still escapes after computed Number addition overwrites",
             .body = replace(replace(source, "  cf.br ^header",
                                     "  %saved = ctjs.get_property %a[%zero]\n  cf.br ^header"),
                             "ctjs.return %a", "ctjs.return %saved"),
             .arrays = "keys:[textZero,textTwo]; a:[zero,zero,zero]",
             .reads = "a[0]=x; keys[0]=textZero; keys[1]=textTwo; keys[0]=textZero",
             .exit = "x -> {x}"});
        reject("computed Number addition does not lend converted bits to property keys",
               replace(source, "%base[%converted]", "%base[%number]"));
        reject("String addition cannot borrow a Number snapshot",
               replace(source, "  %number = ctjs." + operation + " %first, %half",
                       "  %text = ctjs.constant #ctjs.string<\"0.25\">\n"
                       "  %number = ctjs." +
                           operation + " %first, %text"));
    }
    for (const std::string before : {"  %pick =", "  %step ="}) {
        reject(
            "computed Number addition retains the complete table mutation census",
            replace(afterSubAdd, before, "  ctjs.set_property %keys[%one], %textZero\n" + before));
    }
    run({.what = "subtraction consumes an independently computed Number addition",
         .body = replace(afterSubQuarters, "binary sub %slot, %half", "binary add %slot, %half"),
         .arrays = "keys:[textZero,textTwo]; a:[zero,zero,zero]",
         .reads = "keys[0]=textZero; keys[1]=textTwo; keys[0]=textZero",
         .exit = "a -> {a}"},
        "x");
    const auto afterSubAddMul =
        replace(afterSubAdd, "  %converted = ctjs.binary_static bitor %number, %zero",
                "  %product = ctjs.binary mul %number, %one\n"
                "  %converted = ctjs.binary_static bitor %product, %zero");
    run({.what = "multiplication consumes an independent Add/Sub Number snapshot",
         .body = afterSubAddMul,
         .arrays = "keys:[textZero,textTwo]; a:[zero,zero,zero]",
         .reads = "keys[0]=textZero; keys[1]=textTwo; keys[0]=textZero",
         .exit = "a -> {a}"},
        "x");
    run({.what = "a saved child survives computed multiplication overwrites",
         .body = replace(replace(afterSubAddMul, "  cf.br ^header",
                                 "  %saved = ctjs.get_property %a[%zero]\n  cf.br ^header"),
                         "ctjs.return %a", "ctjs.return %saved"),
         .arrays = "keys:[textZero,textTwo]; a:[zero,zero,zero]",
         .reads = "a[0]=x; keys[0]=textZero; keys[1]=textTwo; keys[0]=textZero",
         .exit = "x -> {x}"});
    run({.what = "multiplication retains fractional binary64 operands before truncation",
         .body =
             replace(replace(replace(afterSubAddMul, "4613712638259704627", "4611235658464650854"),
                             "[%x, %zero, %x]", "[%zero, %x, %zero, %x]"),
                     "binary mul %number, %one", "binary mul %number, %two"),
         .arrays = "keys:[textZero,textTwo]; a:[zero,zero,zero,zero]",
         .reads = "keys[0]=textZero; keys[1]=textTwo; keys[0]=textZero; keys[1]=textTwo",
         .exit = "a -> {a}"},
        "x");
    run({.what = "a following addition consumes the read-time multiplication snapshot",
         .body = replace(afterSubAddMul, "  %converted = ctjs.binary_static bitor %product, %zero",
                         "  %productSum = ctjs.binary add %product, %zero\n"
                         "  %converted = ctjs.binary_static bitor %productSum, %zero"),
         .arrays = "keys:[textZero,textTwo]; a:[zero,zero,zero]",
         .reads = "keys[0]=textZero; keys[1]=textTwo; keys[0]=textZero",
         .exit = "a -> {a}"},
        "x");
    reject("multiplication cannot use converted bits as its fractional property key",
           replace(afterSubAddMul, "%base[%converted]", "%base[%product]"));
    reject("multiplication needs original Number evidence for both operands",
           replace(afterSubAddMul, "binary mul %number, %one", "binary mul %number, %p"));
    for (const std::string before : {"  %pick =", "  %step ="}) {
        reject("computed multiplication retains the complete table mutation census",
               replace(afterSubAddMul, before,
                       "  ctjs.set_property %keys[%one], %textZero\n" + before));
    }
    const auto afterSubAddDiv =
        replace(afterSubAdd, "  %converted = ctjs.binary_static bitor %number, %zero",
                "  %quotient = ctjs.binary div %number, %one\n"
                "  %converted = ctjs.binary_static bitor %quotient, %zero");
    run({.what = "division consumes an independent Add/Sub Number snapshot",
         .body = afterSubAddDiv,
         .arrays = "keys:[textZero,textTwo]; a:[zero,zero,zero]",
         .reads = "keys[0]=textZero; keys[1]=textTwo; keys[0]=textZero",
         .exit = "a -> {a}"},
        "x");
    run({.what = "a saved child survives computed division overwrites",
         .body = replace(replace(afterSubAddDiv, "  cf.br ^header",
                                 "  %saved = ctjs.get_property %a[%zero]\n  cf.br ^header"),
                         "ctjs.return %a", "ctjs.return %saved"),
         .arrays = "keys:[textZero,textTwo]; a:[zero,zero,zero]",
         .reads = "a[0]=x; keys[0]=textZero; keys[1]=textTwo; keys[0]=textZero",
         .exit = "x -> {x}"});
    run({.what = "division retains fractional binary64 operands before truncation",
         .body = replace(
             replace(replace(replace(afterSubAddDiv, "4598175219545276416", "4602678819172646912"),
                             "4613712638259704627", "4611235658464650854"),
                     "[%x, %zero, %x]", "[%zero, %x, %zero, %x]"),
             "binary div %number, %one", "binary div %number, %half"),
         .arrays = "keys:[textZero,textTwo]; a:[zero,zero,zero,zero]",
         .reads = "keys[0]=textZero; keys[1]=textTwo; keys[0]=textZero; keys[1]=textTwo",
         .exit = "a -> {a}"},
        "x");
    run({.what = "a following addition consumes the read-time division snapshot",
         .body = replace(afterSubAddDiv, "  %converted = ctjs.binary_static bitor %quotient, %zero",
                         "  %quotientSum = ctjs.binary add %quotient, %zero\n"
                         "  %converted = ctjs.binary_static bitor %quotientSum, %zero"),
         .arrays = "keys:[textZero,textTwo]; a:[zero,zero,zero]",
         .reads = "keys[0]=textZero; keys[1]=textTwo; keys[0]=textZero",
         .exit = "a -> {a}"},
        "x");
    reject("division cannot use converted bits as its fractional property key",
           replace(afterSubAddDiv, "%base[%converted]", "%base[%quotient]"));
    reject("division needs original Number evidence for both operands",
           replace(afterSubAddDiv, "binary div %number, %one", "binary div %number, %p"));
    for (const std::string before : {"  %pick =", "  %step ="}) {
        reject("computed division retains the complete table mutation census",
               replace(afterSubAddDiv, before,
                       "  ctjs.set_property %keys[%one], %textZero\n" + before));
    }
    const auto afterSubAddMod =
        replace(afterSubAdd, "  %converted = ctjs.binary_static bitor %number, %zero",
                "  %remainder = ctjs.binary mod %number, %three\n"
                "  %converted = ctjs.binary_static bitor %remainder, %zero");
    run({.what = "remainder consumes an independent Add/Sub Number snapshot",
         .body = afterSubAddMod,
         .arrays = "keys:[textZero,textTwo]; a:[zero,zero,zero]",
         .reads = "keys[0]=textZero; keys[1]=textTwo; keys[0]=textZero",
         .exit = "a -> {a}"},
        "x");
    run({.what = "a saved child survives computed remainder overwrites",
         .body = replace(replace(afterSubAddMod, "  cf.br ^header",
                                 "  %saved = ctjs.get_property %a[%zero]\n  cf.br ^header"),
                         "ctjs.return %a", "ctjs.return %saved"),
         .arrays = "keys:[textZero,textTwo]; a:[zero,zero,zero]",
         .reads = "a[0]=x; keys[0]=textZero; keys[1]=textTwo; keys[0]=textZero",
         .exit = "x -> {x}"});
    run({.what = "fractional remainder preserves a child outside the actual write image",
         .body = replace(afterSubAddMod, "binary mod %number, %three", "binary mod %number, %half"),
         .arrays = "keys:[textZero,textTwo]; a:[zero,zero,x]",
         .reads = "keys[0]=textZero; keys[1]=textTwo; keys[0]=textZero",
         .exit = "a -> {a,x}"});
    run({.what = "remainder uses dividend sign even with a negative divisor",
         .body = replace(replace(afterSubAddMod, "  %pick =",
                                 "  %negativeDivisor = ctjs.constant "
                                 "#ctjs.number<13837309855095848960>\n  %pick ="),
                         "binary mod %number, %three", "binary mod %number, %negativeDivisor"),
         .arrays = "keys:[textZero,textTwo]; a:[zero,zero,zero]",
         .reads = "keys[0]=textZero; keys[1]=textTwo; keys[0]=textZero",
         .exit = "a -> {a}"},
        "x");
    run({.what = "a following addition consumes the read-time remainder snapshot",
         .body =
             replace(afterSubAddMod, "  %converted = ctjs.binary_static bitor %remainder, %zero",
                     "  %remainderSum = ctjs.binary add %remainder, %zero\n"
                     "  %converted = ctjs.binary_static bitor %remainderSum, %zero"),
         .arrays = "keys:[textZero,textTwo]; a:[zero,zero,zero]",
         .reads = "keys[0]=textZero; keys[1]=textTwo; keys[0]=textZero",
         .exit = "a -> {a}"},
        "x");
    reject("remainder cannot use converted bits as its fractional property key",
           replace(afterSubAddMod, "%base[%converted]", "%base[%remainder]"));
    reject("remainder needs original Number evidence for both operands",
           replace(afterSubAddMod, "binary mod %number, %three", "binary mod %number, %p"));
    for (const std::string before : {"  %pick =", "  %step ="}) {
        reject("computed remainder retains the complete table mutation census",
               replace(afterSubAddMod, before,
                       "  ctjs.set_property %keys[%one], %textZero\n" + before));
    }
    const auto afterSubAddModPow =
        replace(afterSubAddMod, "  %converted = ctjs.binary_static bitor %remainder, %zero",
                "  %power = ctjs.binary pow %remainder, %one\n"
                "  %converted = ctjs.binary_static bitor %power, %zero");
    run({.what = "unit power preserves an independent computed remainder Number",
         .body = afterSubAddModPow,
         .arrays = "keys:[textZero,textTwo]; a:[zero,zero,zero]",
         .reads = "keys[0]=textZero; keys[1]=textTwo; keys[0]=textZero",
         .exit = "a -> {a}"},
        "x");
    run({.what = "a saved child survives computed unit-power overwrites",
         .body = replace(replace(afterSubAddModPow, "  cf.br ^header",
                                 "  %saved = ctjs.get_property %a[%zero]\n  cf.br ^header"),
                         "ctjs.return %a", "ctjs.return %saved"),
         .arrays = "keys:[textZero,textTwo]; a:[zero,zero,zero]",
         .reads = "a[0]=x; keys[0]=textZero; keys[1]=textTwo; keys[0]=textZero",
         .exit = "x -> {x}"});
    run({.what = "unit power preserves a child outside the actual remainder image",
         .body =
             replace(afterSubAddModPow, "binary mod %number, %three", "binary mod %number, %half"),
         .arrays = "keys:[textZero,textTwo]; a:[zero,zero,x]",
         .reads = "keys[0]=textZero; keys[1]=textTwo; keys[0]=textZero",
         .exit = "a -> {a,x}"});
    run({.what = "following arithmetic consumes the Number behind unit-power bits",
         .body = replace(afterSubAddModPow, "  %converted = ctjs.binary_static bitor %power, %zero",
                         "  %powerSum = ctjs.binary add %power, %zero\n"
                         "  %converted = ctjs.binary_static bitor %powerSum, %zero"),
         .arrays = "keys:[textZero,textTwo]; a:[zero,zero,zero]",
         .reads = "keys[0]=textZero; keys[1]=textTwo; keys[0]=textZero",
         .exit = "a -> {a}"},
        "x");
    run({.what = "unit power accepts a separately proved computed Number exponent",
         .body = replace(afterSubAddModPow, "  %power = ctjs.binary pow %remainder, %one",
                         "  %exponent = ctjs.binary add %one, %zero\n"
                         "  %power = ctjs.binary pow %remainder, %exponent"),
         .arrays = "keys:[textZero,textTwo]; a:[zero,zero,zero]",
         .reads = "keys[0]=textZero; keys[1]=textTwo; keys[0]=textZero",
         .exit = "a -> {a}"},
        "x");
    reject("unit power cannot turn fractional result bits into property authority",
           replace(afterSubAddModPow, "%base[%converted]", "%base[%power]"));
    for (const std::string exponent : {"%p", "%two", "%half"}) {
        reject("computed power needs its own exact unit Number exponent",
               replace(afterSubAddModPow, "binary pow %remainder, %one",
                       "binary pow %remainder, " + exponent));
    }
    for (const std::string value : {"#ctjs.string<\"1\">", "#ctjs.number<4611235658464650854>"}) {
        reject("unit-power Number evidence cannot borrow String conversion or exponent bits",
               replace(afterSubAddModPow, "  %power = ctjs.binary pow %remainder, %one",
                       "  %exponent = ctjs.constant " + value +
                           "\n  %power = ctjs.binary pow %remainder, %exponent"));
    }
    for (const std::string before : {"  %pick =", "  %step ="}) {
        reject("computed unit power retains the complete table mutation census",
               replace(afterSubAddModPow, before,
                       "  ctjs.set_property %keys[%one], %textZero\n" + before));
    }
    const auto afterReadPower =
        replace(numberFractionalTable, "  %converted = ctjs.binary_static bitor %slot, %zero",
                "  %power = ctjs.binary pow %slot, %one\n"
                "  %converted = ctjs.binary_static bitor %power, %zero");
    for (const std::string bits :
         {"9223372036854775808", "9218868437227405312", "18442240474082181120",
          "9221120237041090560", "4751297606876816998", "13829653735729319117"}) {
        run({.what = "unit power preserves signed zero nonfinite wide and negative Numbers",
             .body = replace(afterReadPower, "4606281698874543309", bits),
             .arrays = "keys:[textZero,textTwo]; a:[zero,zero,zero]",
             .reads = "keys[0]=textZero; keys[1]=textTwo; keys[0]=textZero",
             .exit = "a -> {a}"},
            "x");
    }
    reject("unit power preserves a negative Number rather than its unsigned bits",
           replace(afterReadPower, "4613712638259704627", "13837084675114480435"));
    run({.what = "unit power consumes an independently proved unary Number",
         .body = replace(afterReadPower, "  %power = ctjs.binary pow %slot, %one",
                         "  %unknownNumber = ctjs.unary plus %slot\n"
                         "  %power = ctjs.binary pow %unknownNumber, %one"),
         .arrays = "keys:[textZero,textTwo]; a:[zero,zero,zero]",
         .reads = "keys[0]=textZero; keys[1]=textTwo; keys[0]=textZero",
         .exit = "a -> {a}"},
        "x");
    const auto zeroPower =
        replace(replace(afterSubAddModPow, "[%x, %zero, %x]", "[%zero, %x, %zero]"),
                "binary pow %remainder, %one", "binary pow %remainder, %zero");
    for (const auto & body :
         {zeroPower, replace(zeroPower, "  %power = ctjs.binary pow %remainder, %zero",
                             "  %exponent = ctjs.binary sub %one, %one\n"
                             "  %power = ctjs.binary pow %remainder, %exponent")}) {
        run({.what = "computed Number to the zero power overwrites only index one",
             .body = body,
             .arrays = "keys:[textZero,textTwo]; a:[zero,zero,zero]",
             .reads = "keys[0]=textZero; keys[1]=textTwo; keys[0]=textZero",
             .exit = "a -> {a}"},
            "x");
    }
    run({.what = "zero power preserves children outside index one",
         .body = replace(zeroPower, "[%zero, %x, %zero]", "[%x, %x, %zero]"),
         .arrays = "keys:[textZero,textTwo]; a:[x,zero,zero]",
         .reads = "keys[0]=textZero; keys[1]=textTwo; keys[0]=textZero",
         .exit = "a -> {a,x}"});
    for (const std::string bits : {"9223372036854775808", "9218868437227405312",
                                   "18442240474082181120", "9221120237041090560"}) {
        run({.what = "zero power of signed zero or nonfinite Number is one",
             .body =
                 replace(replace(replace(afterReadPower, "[%x, %zero, %x]", "[%zero, %x, %zero]"),
                                 "binary pow %slot, %one", "binary pow %slot, %zero"),
                         "4606281698874543309", bits),
             .arrays = "keys:[textZero,textTwo]; a:[zero,zero,zero]",
             .reads = "keys[0]=textZero; keys[1]=textTwo; keys[0]=textZero",
             .exit = "a -> {a}"},
            "x");
    }
    reject("zero-power Number evidence still requires a Number base",
           replace(zeroPower, "binary pow %remainder, %zero", "binary pow %p, %zero"));
    reject(
        "zero power does not authorize a stale mutated table snapshot",
        replace(zeroPower, "  %pick =", "  ctjs.set_property %keys[%one], %textZero\n  %pick ="));
    const auto unitBase =
        replace(zeroPower, "binary pow %remainder, %zero", "binary pow %one, %remainder");
    for (const auto & body :
         {unitBase,
          replace(unitBase, "  %power = ctjs.binary pow %one, %remainder",
                  "  %unit = ctjs.binary add %one, %zero\n"
                  "  %power = ctjs.binary pow %unit, %remainder"),
          replace(unitBase, "  %converted = ctjs.binary_static bitor %power, %zero",
                  "  %powerSum = ctjs.binary add %power, %zero\n"
                  "  %converted = ctjs.binary_static bitor %powerSum, %zero")}) {
        run({.what = "unit base with independently finite Number exponent overwrites index one",
             .body = body,
             .arrays = "keys:[textZero,textTwo]; a:[zero,zero,zero]",
             .reads = "keys[0]=textZero; keys[1]=textTwo; keys[0]=textZero",
             .exit = "a -> {a}"},
            "x");
    }
    run({.what = "a saved child survives computed unit-base overwrites",
         .body = replace(replace(unitBase, "  cf.br ^header",
                                 "  %saved = ctjs.get_property %a[%one]\n  cf.br ^header"),
                         "ctjs.return %a", "ctjs.return %saved"),
         .arrays = "keys:[textZero,textTwo]; a:[zero,zero,zero]",
         .reads = "a[1]=x; keys[0]=textZero; keys[1]=textTwo; keys[0]=textZero",
         .exit = "x -> {x}"});
    run({.what = "unit-base powers preserve children outside index one",
         .body = replace(unitBase, "[%zero, %x, %zero]", "[%x, %x, %zero]"),
         .arrays = "keys:[textZero,textTwo]; a:[x,zero,zero]",
         .reads = "keys[0]=textZero; keys[1]=textTwo; keys[0]=textZero",
         .exit = "a -> {a,x}"});
    run({.what = "independently integral unit-base results name own indices",
         .body = replace(unitBase, "%base[%converted]", "%base[%power]"),
         .arrays = "keys:[textZero,textTwo]; a:[zero,zero,zero]",
         .reads = "keys[0]=textZero; keys[1]=textTwo; keys[0]=textZero",
         .exit = "a -> {a}"},
        "x");
    for (const std::string operands : {"%p, %remainder", "%two, %remainder", "%one, %p"}) {
        reject("computed unit-base powers need independently proved Number operands",
               replace(unitBase, "binary pow %one, %remainder", "binary pow " + operands));
    }
    for (const std::string before : {"  %pick =", "  %step ="}) {
        reject("unit-base powers retain the complete table mutation census",
               replace(unitBase, before, "  ctjs.set_property %keys[%one], %textZero\n" + before));
    }
    const auto unitBaseRead =
        replace(replace(afterReadPower, "[%x, %zero, %x]", "[%zero, %x, %zero]"),
                "binary pow %slot, %one", "binary pow %one, %slot");
    for (const std::string bits :
         {"9223372036854775808", "13829653735729319117", "4751297606876816998"}) {
        run({.what = "unit base accepts signed zero negative fractional and wide finite exponents",
             .body = replace(unitBaseRead, "4606281698874543309", bits),
             .arrays = "keys:[textZero,textTwo]; a:[zero,zero,zero]",
             .reads = "keys[0]=textZero; keys[1]=textTwo; keys[0]=textZero",
             .exit = "a -> {a}"},
            "x");
    }
    for (const std::string bits :
         {"9218868437227405312", "18442240474082181120", "9221120237041090560"}) {
        reject("unit base with a nonfinite exponent is not one",
               replace(unitBaseRead, "4606281698874543309", bits));
    }
    reject("unit base cannot use a computed nonfinite exponent as finite evidence",
           replace(unitBase, "binary mod %number, %three", "binary mod %number, %zero"));
    run({.what = "unit base consumes an independently proved unary Number exponent",
         .body = replace(unitBaseRead, "  %power = ctjs.binary pow %one, %slot",
                         "  %exponent = ctjs.unary plus %slot\n"
                         "  %power = ctjs.binary pow %one, %exponent"),
         .arrays = "keys:[textZero,textTwo]; a:[zero,zero,zero]",
         .reads = "keys[0]=textZero; keys[1]=textTwo; keys[0]=textZero",
         .exit = "a -> {a}"},
        "x");
    reject("unit base cannot use String bits as independent Number exponent evidence",
           replace(unitBaseRead, "#ctjs.number<4606281698874543309>", "#ctjs.string<\"0.9\">"));
    const auto negativeUnitBase = replace(
        replace(replace(numberFractionalTable, "4606281698874543309", "4608308318706860032"),
                "4613712638259704627", "4612248968380809216"),
        "  %converted = ctjs.binary_static bitor %slot, %zero",
        "  %quarter = ctjs.constant #ctjs.number<4598175219545276416>\n"
        "  %negativeUnit = ctjs.unary neg %one\n"
        "  %exponent = ctjs.binary sub %slot, %quarter\n"
        "  %power = ctjs.binary pow %negativeUnit, %exponent\n"
        "  %powerIndex = ctjs.binary add %power, %one\n"
        "  %converted = ctjs.binary_static bitor %powerIndex, %zero");
    run({.what = "negative unit powers retain computed Number exponent parity and result identity",
         .body = negativeUnitBase,
         .arrays = "keys:[textZero,textTwo]; a:[zero,zero,zero]",
         .reads = "keys[0]=textZero; keys[1]=textTwo; keys[0]=textZero",
         .exit = "a -> {a}"},
        "x");
    for (const std::string bits :
         {"0", "9223372036854775808", "13837309855095848960", "4613937818241073152",
          "4845873199050653695", "4845873199050653696", "9218868437227405311"}) {
        const bool odd = bits == "13837309855095848960" || bits == "4613937818241073152" ||
                         bits == "4845873199050653695";
        run({.what =
                 "negative unit parity respects signed zero negative and wide integer exponents",
             .body = replace(replace(replace(negativeUnitBase, "4608308318706860032", bits),
                                     "4612248968380809216", "4611686018427387904"),
                             "binary sub %slot, %quarter", "binary add %slot, %zero"),
             .arrays = odd ? "keys:[textZero,textTwo]; a:[zero,zero,zero]"
                           : "keys:[textZero,textTwo]; a:[x,zero,zero]",
             .reads = "keys[0]=textZero; keys[1]=textTwo; keys[0]=textZero",
             .exit = odd ? "a -> {a}" : "a -> {a,x}"},
            odd ? "x" : "");
    }
    for (const std::string value :
         {"#ctjs.number<4602678819172646912>", "#ctjs.number<9218868437227405312>",
          "#ctjs.number<18442240474082181120>", "#ctjs.number<9221120237041090560>",
          "#ctjs.string<\"1.25\">"}) {
        reject("negative unit parity requires independent finite integral Number evidence",
               replace(negativeUnitBase, "#ctjs.number<4608308318706860032>", value));
    }
    run({.what = "independently integral negative-unit arithmetic names own indices",
         .body = replace(negativeUnitBase, "%base[%converted]", "%base[%powerIndex]"),
         .arrays = "keys:[textZero,textTwo]; a:[zero,zero,zero]",
         .reads = "keys[0]=textZero; keys[1]=textTwo; keys[0]=textZero",
         .exit = "a -> {a}"},
        "x");
    reject("negative unit snapshots retain table mutation checks",
           replace(negativeUnitBase,
                   "  %pick =", "  ctjs.set_property %keys[%one], %textZero\n  %pick ="));
    const auto negatedAdd =
        replace(replace(replace(numberFractionalTable, "4606281698874543309", "0"),
                        "4613712638259704627", "4611686018427387904"),
                "  %converted = ctjs.binary_static bitor %slot, %zero",
                "  %positiveSum = ctjs.binary add %slot, %one\n"
                "  %negative = ctjs.unary neg %positiveSum\n"
                "  %number = ctjs.binary add %negative, %three\n"
                "  %converted = ctjs.binary_static bitor %number, %zero");
    run({.what = "negation cannot reuse a preceding sum's positive Number snapshot",
         .body = negatedAdd,
         .arrays = "keys:[textZero,textTwo]; a:[zero,zero,zero]",
         .reads = "keys[0]=textZero; keys[1]=textTwo; keys[0]=textZero",
         .exit = "a -> {a}"},
        "x");
    reject("negated sum snapshots cannot invent nonnegative property writes",
           replace(negatedAdd, "binary add %negative, %three", "binary add %negative, %zero"));
    std::string subtractions = "  %original = ctjs.constant #ctjs.number<4606281698874543309>\n";
    std::string additions = subtractions;
    std::string multiplications = subtractions;
    std::string divisions = subtractions;
    std::string remainders = subtractions;
    std::string powers = subtractions;
    std::string priorSubtraction = "%original";
    for (unsigned depth = 1; depth <= 64; ++depth) {
        const auto next = "%difference" + std::to_string(depth);
        const auto body =
            replace(afterSubHalf, "  %textZero = ctjs.constant #ctjs.number<4606281698874543309>",
                    subtractions + "  %textZero = ctjs.binary sub " + priorSubtraction + ", %zero");
        if (depth == 63) {
            run({.what = "64 subtraction snapshots include the final after-read operation",
                 .body = body,
                 .arrays = "keys:[textZero,textTwo]; a:[zero,zero,zero]",
                 .reads = "keys[0]=textZero; keys[1]=textTwo; keys[0]=textZero",
                 .exit = "a -> {a}"},
                "x");
        } else if (depth == 64) {
            reject("computed binary64 subtraction snapshots stop after 64 operations", body);
        }
        const auto additionBody =
            replace(afterSubHalf, "  %textZero = ctjs.constant #ctjs.number<4606281698874543309>",
                    additions + "  %textZero = ctjs.binary add " + priorSubtraction + ", %zero");
        if (depth == 63) {
            run({.what = "64 mixed Add/Sub snapshots include the final after-read operation",
                 .body = additionBody,
                 .arrays = "keys:[textZero,textTwo]; a:[zero,zero,zero]",
                 .reads = "keys[0]=textZero; keys[1]=textTwo; keys[0]=textZero",
                 .exit = "a -> {a}"},
                "x");
        } else if (depth == 64) {
            reject("computed binary64 Add/Sub snapshots stop after 64 operations", additionBody);
        }
        if (depth >= 63) {
            const auto productBody = replace(
                afterSubHalf, "  %textZero = ctjs.constant #ctjs.number<4606281698874543309>",
                multiplications + "  %textZero = ctjs.binary mul " + priorSubtraction + ", %one");
            if (depth == 63) {
                run({.what = "64 Mul/Sub snapshots include the final after-read operation",
                     .body = productBody,
                     .arrays = "keys:[textZero,textTwo]; a:[zero,zero,zero]",
                     .reads = "keys[0]=textZero; keys[1]=textTwo; keys[0]=textZero",
                     .exit = "a -> {a}"},
                    "x");
            } else {
                reject("computed multiplication snapshots stop after 64 operations", productBody);
            }
        }
        if (depth >= 63) {
            const auto quotientBody = replace(
                afterSubHalf, "  %textZero = ctjs.constant #ctjs.number<4606281698874543309>",
                divisions + "  %textZero = ctjs.binary div " + priorSubtraction + ", %one");
            if (depth == 63) {
                run({.what = "64 Div/Sub snapshots include the final after-read operation",
                     .body = quotientBody,
                     .arrays = "keys:[textZero,textTwo]; a:[zero,zero,zero]",
                     .reads = "keys[0]=textZero; keys[1]=textTwo; keys[0]=textZero",
                     .exit = "a -> {a}"},
                    "x");
            } else {
                reject("computed division snapshots stop after 64 operations", quotientBody);
            }
        }
        if (depth >= 63) {
            const auto remainderBody = replace(
                afterSubHalf, "  %textZero = ctjs.constant #ctjs.number<4606281698874543309>",
                remainders + "  %textZero = ctjs.binary mod " + priorSubtraction + ", %three");
            if (depth == 63) {
                run({.what = "64 Mod/Sub snapshots include the final after-read operation",
                     .body = remainderBody,
                     .arrays = "keys:[textZero,textTwo]; a:[zero,zero,zero]",
                     .reads = "keys[0]=textZero; keys[1]=textTwo; keys[0]=textZero",
                     .exit = "a -> {a}"},
                    "x");
            } else {
                reject("computed remainder snapshots stop after 64 operations", remainderBody);
            }
        }
        if (depth >= 63) {
            const auto powerBody = replace(
                afterSubHalf, "  %textZero = ctjs.constant #ctjs.number<4606281698874543309>",
                powers + "  %textZero = ctjs.binary pow " + priorSubtraction + ", %one");
            if (depth == 63) {
                run({.what = "64 unit-Pow/Sub snapshots include the final after-read operation",
                     .body = powerBody,
                     .arrays = "keys:[textZero,textTwo]; a:[zero,zero,zero]",
                     .reads = "keys[0]=textZero; keys[1]=textTwo; keys[0]=textZero",
                     .exit = "a -> {a}"},
                    "x");
            } else {
                reject("computed unit-power snapshots stop after 64 operations", powerBody);
            }
            const auto unitBaseBody = replace(
                unitBaseRead, "  %textZero = ctjs.constant #ctjs.number<4606281698874543309>",
                subtractions + "  %textZero = ctjs.binary sub " + priorSubtraction + ", %zero");
            if (depth == 63) {
                run({.what = "unit-base powers include the independently proved exponent depth",
                     .body = unitBaseBody,
                     .arrays = "keys:[textZero,textTwo]; a:[zero,zero,zero]",
                     .reads = "keys[0]=textZero; keys[1]=textTwo; keys[0]=textZero",
                     .exit = "a -> {a}"},
                    "x");
            } else {
                reject("unit-base powers cannot reset the 64-operation Number limit", unitBaseBody);
            }
        }
        powers += "  " + next + " = ctjs.binary pow " + priorSubtraction + ", %one\n";
        remainders += "  " + next + " = ctjs.binary mod " + priorSubtraction + ", %three\n";
        divisions += "  " + next + " = ctjs.binary div " + priorSubtraction + ", %one\n";
        multiplications += "  " + next + " = ctjs.binary mul " + priorSubtraction + ", %one\n";
        subtractions += "  " + next + " = ctjs.binary sub " + priorSubtraction + ", %zero\n";
        additions += "  " + next + " = ctjs.binary add " + priorSubtraction + ", %zero\n";
        priorSubtraction = next;
    }
    for (const std::string before : {"  %pick =", "  %step ="}) {
        reject(
            "subtract-zero conversion retains the complete table mutation census",
            replace(afterSubZero, before, "  ctjs.set_property %keys[%one], %textZero\n" + before));
    }
    for (const std::string kind : {"plus", "neg"}) {
        auto source = replace(afterReadTable, "unary plus %slot", "unary " + kind + " %slot");
        if (kind == "neg") {
            source = replace(replace(source, "4606281698874543309", "13829653735729319117"),
                             "4613712638259704627", "13837084675114480435");
        }
        run({.what = "unary table-read conversion preserves signed fractional bitwise inputs",
             .body = source,
             .arrays = "keys:[textZero,textTwo]; a:[zero,zero,zero]",
             .reads = "keys[0]=textZero; keys[1]=textTwo; keys[0]=textZero",
             .exit = "a -> {a}"},
            "x");
        reject("a converted-bit snapshot is not a fractional own property key",
               replace(source, "%base[%converted]", "%base[%number]"));
        reject("a converted-bit snapshot is not an exact arithmetic Number",
               replace(source, "binary_static bitor %number, %zero", "binary sub %number, %zero"));
        for (const std::string before : {"  %pick =", "  %step ="}) {
            reject(
                "unary table snapshots cannot hide a mutation around the read",
                replace(source, before, "  ctjs.set_property %keys[%one], %textZero\n" + before));
        }
    }
    run({.what = "unary conversion after a String table read preserves bounded parsing",
         .body =
             replace(afterReadTable, "#ctjs.number<4606281698874543309>", "#ctjs.string<\"0.9\">"),
         .arrays = "keys:[textZero,textTwo]; a:[zero,zero,zero]",
         .reads = "keys[0]=textZero; keys[1]=textTwo; keys[0]=textZero",
         .exit = "a -> {a}"},
        "x");
    run({.what = "a saved unary read keeps its conversion after the source table changes",
         .body = replace(
             replace(
                 numberFractionalTable, "  cf.br ^header",
                 "  %savedRead = ctjs.get_property %keys[%zero]\n"
                 "  %savedNumber = ctjs.unary plus %savedRead {storage_test_id = \"savedNumber\"}\n"
                 "  %snapshots = ctjs.create_array [%savedNumber, %textTwo] {storage_test_id = "
                 "\"snapshots\"}\n"
                 "  ctjs.set_property %keys[%zero], %textTwo\n  cf.br ^header"),
             "%keys[%pick]", "%snapshots[%pick]"),
         .arrays = "keys:[textTwo,textTwo]; a:[zero,zero,zero]; snapshots:[savedNumber,textTwo]",
         .reads = "keys[0]=textZero; snapshots[0]=savedNumber; snapshots[1]=textTwo; "
                  "snapshots[0]=savedNumber",
         .exit = "a -> {a}"},
        "x");
    for (const std::string before : {"  %pick =", "  %step ="}) {
        reject("Number table mutations remain visible before and after a read",
               replace(numberFractionalTable, before,
                       "  ctjs.set_property %keys[%one], %textZero\n" + before));
    }
    const auto plusNumberTable =
        replace(replace(numberFractionalTable,
                        "  %textZero = ctjs.constant #ctjs.number<4606281698874543309>",
                        "  %rawZero = ctjs.constant #ctjs.number<4606281698874543309>\n"
                        "  %textZero = ctjs.unary plus %rawZero"),
                "  %textTwo = ctjs.constant #ctjs.number<4613712638259704627>",
                "  %rawTwo = ctjs.constant #ctjs.number<4613712638259704627>\n"
                "  %textTwo = ctjs.unary plus %rawTwo");
    for (const std::string expression :
         {"bitand %slot, %two", "bitor %slot, %zero", "bitxor %slot, %zero", "shl %slot, %zero",
          "shr %slot, %zero", "ushr %slot, %zero", "bitor %i, %textZero", "shl %i, %textZero",
          "shr %i, %textZero", "ushr %i, %textZero"}) {
        run({.what = "one source Number Plus preserves table operands masks and shift counts",
             .body = replace(plusNumberTable, "bitor %slot, %zero", expression),
             .arrays = "keys:[textZero,textTwo]; a:[zero,zero,zero]",
             .reads = "keys[0]=textZero; keys[1]=textTwo; keys[0]=textZero",
             .exit = "a -> {a}"},
            "x");
    }
    for (const std::string bits :
         {"9223372036854775808", "4751297606876816998", "9218868437227405311",
          "9218868437227405312", "18442240474082181120", "9221120237041090560"}) {
        run({.what = "source Plus preserves zero wide and nonfinite Number bitwise conversion",
             .body = replace(plusNumberTable, "4606281698874543309", bits),
             .arrays = "keys:[textZero,textTwo]; a:[zero,zero,zero]",
             .reads = "keys[0]=textZero; keys[1]=textTwo; keys[0]=textZero",
             .exit = "a -> {a}"},
            "x");
    }
    run({.what = "source Plus preserves signed fractional Number complement",
         .body = replace(
             replace(replace(plusNumberTable, "4606281698874543309", "13834607695319426662"),
                     "4613712638259704627", "13839336474928165683"),
             "binary_static bitor %slot, %zero", "unary bitnot %slot"),
         .arrays = "keys:[textZero,textTwo]; a:[zero,zero,zero]",
         .reads = "keys[0]=textZero; keys[1]=textTwo; keys[0]=textZero",
         .exit = "a -> {a}"},
        "x");
    run({.what = "source Plus cannot release a previously saved child",
         .body = replace(replace(plusNumberTable, "  cf.br ^header",
                                 "  %saved = ctjs.get_property %a[%zero]\n  cf.br ^header"),
                         "ctjs.return %a", "ctjs.return %saved"),
         .arrays = "keys:[textZero,textTwo]; a:[zero,zero,zero]",
         .reads = "a[0]=x; keys[0]=textZero; keys[1]=textTwo; keys[0]=textZero",
         .exit = "x -> {x}"});
    reject("source Plus does not lend bitwise truncation to original property keys",
           replace(plusNumberTable, "%base[%converted]", "%base[%slot]"));
    reject("source Plus does not lend bitwise truncation to arithmetic",
           replace(plusNumberTable, "binary_static bitor %slot, %zero", "binary sub %slot, %zero"));
    reject("source Plus requires an immediate original Number",
           replace(plusNumberTable, "unary plus %rawZero", "unary plus %p"),
           ArrayContentsFailure::UnsupportedOperation);
    run({.what = "source Plus preserves bounded original String conversion",
         .body =
             replace(plusNumberTable, "#ctjs.number<4606281698874543309>", "#ctjs.string<\"0.9\">"),
         .arrays = "keys:[textZero,textTwo]; a:[zero,zero,zero]",
         .reads = "keys[0]=textZero; keys[1]=textTwo; keys[0]=textZero",
         .exit = "a -> {a}"},
        "x");
    run({.what = "two source Plus operations preserve the original Number provenance",
         .body = replace(plusNumberTable, "  %textZero = ctjs.unary plus %rawZero",
                         "  %inner = ctjs.unary plus %rawZero\n"
                         "  %textZero = ctjs.unary plus %inner"),
         .arrays = "keys:[textZero,textTwo]; a:[zero,zero,zero]",
         .reads = "keys[0]=textZero; keys[1]=textTwo; keys[0]=textZero",
         .exit = "a -> {a}"},
        "x");
    for (const std::string before : {"  %pick =", "  %step ="}) {
        reject("source Plus preserves mutations before and after table reads",
               replace(plusNumberTable, before,
                       "  ctjs.set_property %keys[%one], %textZero\n" + before));
    }
    const auto nestedNumberTable =
        replace(replace(plusNumberTable, "  %textZero = ctjs.unary plus %rawZero",
                        "  %innerZero = ctjs.unary plus %rawZero\n"
                        "  %textZero = ctjs.unary plus %innerZero"),
                "  %textTwo = ctjs.unary plus %rawTwo",
                "  %innerTwo = ctjs.unary plus %rawTwo\n"
                "  %textTwo = ctjs.unary plus %innerTwo");
    for (const std::string inner : {"plus", "neg"}) {
        for (const std::string outer : {"plus", "neg"}) {
            auto source =
                replace(replace(replace(replace(nestedNumberTable, "unary plus %rawZero",
                                                "unary " + inner + " %rawZero"),
                                        "unary plus %rawTwo", "unary " + inner + " %rawTwo"),
                                "unary plus %innerZero", "unary " + outer + " %innerZero"),
                        "unary plus %innerTwo", "unary " + outer + " %innerTwo");
            if (inner != outer) {
                source = replace(replace(source, "4606281698874543309", "13829653735729319117"),
                                 "4613712638259704627", "13837084675114480435");
            }
            run({.what = "two original Number unary operations retain sign parity before bitwise",
                 .body = source,
                 .arrays = "keys:[textZero,textTwo]; a:[zero,zero,zero]",
                 .reads = "keys[0]=textZero; keys[1]=textTwo; keys[0]=textZero",
                 .exit = "a -> {a}"},
                "x");
        }
    }
    for (const std::string bits : {"9223372036854775808", "9218868437227405312",
                                   "18442240474082181120", "9221120237041090560"}) {
        run({.what = "nested unary Number zero and nonfinite inputs keep Core bitwise conversion",
             .body = replace(nestedNumberTable, "4606281698874543309", bits),
             .arrays = "keys:[textZero,textTwo]; a:[zero,zero,zero]",
             .reads = "keys[0]=textZero; keys[1]=textTwo; keys[0]=textZero",
             .exit = "a -> {a}"},
            "x");
    }
    run({.what = "three original Number unary operations preserve literal provenance",
         .body = replace(nestedNumberTable, "  %innerZero = ctjs.unary plus %rawZero",
                         "  %third = ctjs.unary plus %rawZero\n"
                         "  %innerZero = ctjs.unary plus %third"),
         .arrays = "keys:[textZero,textTwo]; a:[zero,zero,zero]",
         .reads = "keys[0]=textZero; keys[1]=textTwo; keys[0]=textZero",
         .exit = "a -> {a}"},
        "x");
    run({.what = "nested unary Number proof preserves bounded String origins",
         .body = replace(nestedNumberTable, "#ctjs.number<4606281698874543309>",
                         "#ctjs.string<\"0.9\">"),
         .arrays = "keys:[textZero,textTwo]; a:[zero,zero,zero]",
         .reads = "keys[0]=textZero; keys[1]=textTwo; keys[0]=textZero",
         .exit = "a -> {a}"},
        "x");
    reject("nested unary Number proof cannot borrow an unknown operand",
           replace(nestedNumberTable, "unary plus %rawZero", "unary plus %p"),
           ArrayContentsFailure::UnsupportedOperation);
    reject("nested unary Number bitwise proof does not change original property keys",
           replace(nestedNumberTable, "%base[%converted]", "%base[%slot]"));
    reject(
        "nested unary Number bitwise proof does not supply fractional arithmetic facts",
        replace(nestedNumberTable, "binary_static bitor %slot, %zero", "binary sub %slot, %zero"));
    const auto unaryChainTable = [&](unsigned length, const std::string & kind) {
        auto source = plusNumberTable;
        for (const std::string suffix : {"Zero", "Two"}) {
            std::string chain;
            std::string operand = "%raw" + suffix;
            for (unsigned i = 1; i < length; ++i) {
                const std::string result = "%chain" + suffix + std::to_string(i);
                chain += "  " + result + " = ctjs.unary " + kind + " " + operand + "\n";
                operand = result;
            }
            chain += "  %text" + suffix + " = ctjs.unary " + kind + " " + operand;
            source =
                replace(source, "  %text" + suffix + " = ctjs.unary plus %raw" + suffix, chain);
        }
        if (kind == "neg" && length % 2 != 0) {
            source = replace(replace(source, "4606281698874543309", "13829653735729319117"),
                             "4613712638259704627", "13837084675114480435");
        }
        return source;
    };
    for (const unsigned length : {7U, 64U}) {
        for (const std::string kind : {"plus", "neg"}) {
            run({.what = "bounded original Number unary chains preserve sign parity",
                 .body = unaryChainTable(length, kind),
                 .arrays = "keys:[textZero,textTwo]; a:[zero,zero,zero]",
                 .reads = "keys[0]=textZero; keys[1]=textTwo; keys[0]=textZero",
                 .exit = "a -> {a}"},
                "x");
        }
    }
    const auto longNumberTable = unaryChainTable(64, "plus");
    reject("Number unary provenance stops after 64 source operations", unaryChainTable(65, "plus"));
    run({.what = "64 unary operations preserve bounded original String provenance",
         .body =
             replace(longNumberTable, "#ctjs.number<4606281698874543309>", "#ctjs.string<\"0.9\">"),
         .arrays = "keys:[textZero,textTwo]; a:[zero,zero,zero]",
         .reads = "keys[0]=textZero; keys[1]=textTwo; keys[0]=textZero",
         .exit = "a -> {a}"},
        "x");
    reject("long unary Number provenance cannot cross an unknown operand",
           replace(longNumberTable, "unary plus %rawZero", "unary plus %p"),
           ArrayContentsFailure::UnsupportedOperation);
    reject("long unary Number bitwise proof keeps fractional property keys unchanged",
           replace(longNumberTable, "%base[%converted]", "%base[%slot]"));
    reject("long unary Number bitwise proof cannot supply fractional arithmetic facts",
           replace(longNumberTable, "binary_static bitor %slot, %zero", "binary sub %slot, %zero"));
    for (const std::string before : {"  %pick =", "  %step ="}) {
        reject("long unary Number table mutations remain visible before and after reads",
               replace(longNumberTable, before,
                       "  ctjs.set_property %keys[%one], %textZero\n" + before));
    }
    const auto unaryStringTable = replace(
        replace(plusNumberTable, "#ctjs.number<4606281698874543309>", "#ctjs.string<\"0.9\">"),
        "#ctjs.number<4613712638259704627>", "#ctjs.string<\"2.9\">");
    const auto unaryUndefinedTable =
        replace(plusNumberTable, "#ctjs.number<4606281698874543309>", "#ctjs.undefined");
    for (const std::string kind : {"plus", "neg"}) {
        const auto source =
            replace(unaryUndefinedTable, "unary plus %rawZero", "unary " + kind + " %rawZero");
        for (const std::string expression :
             {"bitand %slot, %two", "bitor %slot, %zero", "bitxor %slot, %zero", "shl %slot, %zero",
              "shr %slot, %zero", "ushr %slot, %zero", "bitor %i, %textZero", "shl %i, %textZero",
              "shr %i, %textZero", "ushr %i, %textZero"}) {
            run({.what = "Undefined unary Number operands masks and counts retain NaN conversion",
                 .body = replace(source, "bitor %slot, %zero", expression),
                 .arrays = "keys:[textZero,textTwo]; a:[zero,zero,zero]",
                 .reads = "keys[0]=textZero; keys[1]=textTwo; keys[0]=textZero",
                 .exit = "a -> {a}"},
                "x");
        }
    }
    run({.what = "64 unary operations retain original Undefined provenance",
         .body = replace(longNumberTable, "#ctjs.number<4606281698874543309>", "#ctjs.undefined"),
         .arrays = "keys:[textZero,textTwo]; a:[zero,zero,zero]",
         .reads = "keys[0]=textZero; keys[1]=textTwo; keys[0]=textZero",
         .exit = "a -> {a}"},
        "x");
    reject("Undefined unary provenance stops after 64 source operations",
           replace(unaryChainTable(65, "plus"), "#ctjs.number<4606281698874543309>",
                   "#ctjs.undefined"));
    reject("Undefined bitwise proof requires an explicit unary Number conversion",
           replace(unaryUndefinedTable, "unary plus %rawZero", "constant #ctjs.undefined"));
    reject("Undefined unary Number proof cannot borrow an unknown operand",
           replace(unaryUndefinedTable, "unary plus %rawZero", "unary plus %p"),
           ArrayContentsFailure::UnsupportedOperation);
    reject("Undefined unary Number property keys retain NaN spelling",
           replace(unaryUndefinedTable, "%base[%converted]", "%base[%slot]"));
    reject("Undefined unary Number arithmetic cannot borrow bitwise zero",
           replace(unaryUndefinedTable, "binary_static bitor %slot, %zero",
                   "binary sub %slot, %zero"));
    for (const std::string before : {"  %pick =", "  %step ="}) {
        reject("Undefined unary Number table mutations remain visible before and after reads",
               replace(unaryUndefinedTable, before,
                       "  ctjs.set_property %keys[%one], %textZero\n" + before));
    }
    for (const std::string kind : {"plus", "neg"}) {
        auto source = unaryStringTable;
        if (kind == "neg") {
            source = replace(replace(replace(source, "unary plus %rawZero", "unary neg %rawZero"),
                                     "#ctjs.string<\"0.9\">", "#ctjs.string<\"-0.9\">"),
                             "#ctjs.string<\"2.9\">", "#ctjs.string<\"-2.9\">");
            reject("an unnegated negative String operand cannot authorize an own array index",
                   source);
            source = replace(source, "unary plus %rawTwo", "unary neg %rawTwo");
        }
        for (const std::string expression :
             {"bitand %slot, %two", "bitor %slot, %zero", "bitxor %slot, %zero", "shl %slot, %zero",
              "shr %slot, %zero", "ushr %slot, %zero", "bitor %i, %textZero", "shl %i, %textZero",
              "shr %i, %textZero", "ushr %i, %textZero"}) {
            run({.what = "String unary Number operands masks and counts keep Core conversion",
                 .body = replace(source, "bitor %slot, %zero", expression),
                 .arrays = "keys:[textZero,textTwo]; a:[zero,zero,zero]",
                 .reads = "keys[0]=textZero; keys[1]=textTwo; keys[0]=textZero",
                 .exit = "a -> {a}"},
                "x");
        }
    }
    for (const std::string text :
         {"-0", "4294967296.9", "Infinity", "-Infinity", "NaN", "1e999", "0x100000000"}) {
        run({.what = "String unary Number zero wide and nonfinite inputs keep bounded grammar",
             .body = replace(unaryStringTable, "#ctjs.string<\"0.9\">",
                             "#ctjs.string<\"" + text + "\">"),
             .arrays = "keys:[textZero,textTwo]; a:[zero,zero,zero]",
             .reads = "keys[0]=textZero; keys[1]=textTwo; keys[0]=textZero",
             .exit = "a -> {a}"},
            "x");
    }
    run({.what = "String unary Number complement keeps signed fractional values",
         .body = replace(
             replace(replace(unaryStringTable, "#ctjs.string<\"0.9\">", "#ctjs.string<\"-1.9\">"),
                     "#ctjs.string<\"2.9\">", "#ctjs.string<\"-3.9\">"),
             "binary_static bitor %slot, %zero", "unary bitnot %slot"),
         .arrays = "keys:[textZero,textTwo]; a:[zero,zero,zero]",
         .reads = "keys[0]=textZero; keys[1]=textTwo; keys[0]=textZero",
         .exit = "a -> {a}"},
        "x");
    for (const std::string text :
         {"1e2147483647", "0x", "1e", "nan", "000000000000000000000000000000000"}) {
        reject(
            "String unary provenance preserves grammar exponent and source-size bounds",
            replace(unaryStringTable, "#ctjs.string<\"0.9\">", "#ctjs.string<\"" + text + "\">"));
    }
    reject("String unary provenance stops after 64 source operations",
           replace(unaryChainTable(65, "plus"), "#ctjs.number<4606281698874543309>",
                   "#ctjs.string<\"0.9\">"));
    reject("String unary Number bitwise conversion does not truncate property keys",
           replace(unaryStringTable, "%base[%converted]", "%base[%slot]"));
    reject(
        "String unary Number bitwise conversion does not truncate arithmetic",
        replace(unaryStringTable, "binary_static bitor %slot, %zero", "binary sub %slot, %zero"));
    for (const std::string before : {"  %pick =", "  %step ="}) {
        reject("String unary Number conversion retains mutations before and after table reads",
               replace(unaryStringTable, before,
                       "  ctjs.set_property %keys[%one], %textZero\n" + before));
    }
    const auto wideNumberTable =
        replace(replace(numberFractionalTable, "#ctjs.number<4606281698874543309>",
                        "#ctjs.number<4751297606876816998>"),
                "#ctjs.number<4613712638259704627>", "#ctjs.number<4751297606878914150>");
    for (const std::string expression :
         {"bitand %slot, %two", "bitor %slot, %zero", "bitxor %slot, %zero", "shl %slot, %zero",
          "shr %slot, %zero", "ushr %slot, %zero", "bitor %i, %textZero", "shl %i, %textZero",
          "shr %i, %textZero", "ushr %i, %textZero"}) {
        run({.what = "wide finite Number operands masks and counts wrap only at bitwise consumers",
             .body = replace(wideNumberTable, "bitor %slot, %zero", expression),
             .arrays = "keys:[textZero,textTwo]; a:[zero,zero,zero]",
             .reads = "keys[0]=textZero; keys[1]=textTwo; keys[0]=textZero",
             .exit = "a -> {a}"},
            "x");
    }
    // 2^53 and its next representable Number still have distinct uint32 images.
    run({.what = "wide Number conversion uses represented doubles across the precision boundary",
         .body = replace(replace(wideNumberTable, "4751297606876816998", "4845873199050653696"),
                         "4751297606878914150", "4845873199050653697"),
         .arrays = "keys:[textZero,textTwo]; a:[zero,zero,zero]",
         .reads = "keys[0]=textZero; keys[1]=textTwo; keys[0]=textZero",
         .exit = "a -> {a}"},
        "x");
    for (const std::string bits : {"9094988921128908188", "18318360957983683996",
                                   "9218868437227405311", "9223372036854775808"}) {
        run({.what = "huge finite Numbers and negative zero reduce before integer conversion",
             .body = replace(wideNumberTable, "4751297606876816998", bits),
             .arrays = "keys:[textZero,textTwo]; a:[zero,zero,zero]",
             .reads = "keys[0]=textZero; keys[1]=textTwo; keys[0]=textZero",
             .exit = "a -> {a}"},
            "x");
    }
    const auto wideNumberComplement =
        replace(replace(numberComplementTable, "13834607695319426662", "13974669643732641382"),
                "13839336474928165683", "13974669643734738534");
    const auto wideNumberNegated =
        replace(replace(negatedNumberTable, "4611235658464650854", "4751297606877865574"),
                "4615964438073389875", "4751297606879962726");
    for (const auto & source : {wideNumberComplement, wideNumberNegated}) {
        run({.what = "wide negative literals and their original source Neg retain signed bits",
             .body = source,
             .arrays = "keys:[textZero,textTwo]; a:[zero,zero,zero]",
             .reads = "keys[0]=textZero; keys[1]=textTwo; keys[0]=textZero",
             .exit = "a -> {a}"},
            "x");
    }
    run({.what = "wide varying Number shift counts retain their original five-bit images",
         .body =
             replace(replace(replace(replace(wideNumberTable, "[%x, %zero, %x]", "[%zero, %x, %x]"),
                                     "4751297606876816998", "4751297606876397568"),
                             "4751297606878914150", "4751297606877865574"),
                     "bitor %slot, %zero", "shl %one, %slot"),
         .arrays = "keys:[textZero,textTwo]; a:[zero,zero,zero]",
         .reads = "keys[0]=textZero; keys[1]=textTwo; keys[0]=textZero",
         .exit = "a -> {a}"},
        "x");
    run({.what = "wide Number conversion does not release a saved child alias",
         .body = replace(replace(wideNumberTable, "  cf.br ^header",
                                 "  %saved = ctjs.get_property %a[%zero]\n  cf.br ^header"),
                         "ctjs.return %a", "ctjs.return %saved"),
         .arrays = "keys:[textZero,textTwo]; a:[zero,zero,zero]",
         .reads = "a[0]=x; keys[0]=textZero; keys[1]=textTwo; keys[0]=textZero",
         .exit = "x -> {x}"});
    const auto selectedWideNumber =
        replace(replace(selectedNumberFractional, "4606281698874543309", "4751297606876816998"),
                "4613712638259704627", "4751297606878914150");
    run({.what = "wide Number conversion retains the independent receiver reload gap",
         .body = selectedWideNumber,
         .arrays = "keys:[textZero,textTwo]; a:[zero,keys,zero]",
         .reads =
             "a[1]=keys; keys[0]=textZero; a[1]=keys; keys[1]=textTwo; a[1]=keys; keys[0]=textZero",
         .exit = "a -> {a,keys}"},
        "x");
    reject("wide Number conversion cannot overwrite its reloaded receiver",
           replace(selectedWideNumber, "4751297606878914150", "4751297606877865574"));
    reject("the representable Number below 2^53 keeps its negative signed bitwise result",
           replace(wideNumberTable, "4751297606876816998", "4845873199050653695"));
    for (const std::string expression :
         {"unary plus %slot", "binary sub %slot, %zero", "binary mul %slot, %one",
          "binary div %slot, %one", "binary add %slot, %zero"}) {
        reject("wide Number arithmetic cannot borrow bitwise wrapping",
               replace(wideNumberTable, "binary_static bitor %slot, %zero", expression));
        const auto converted =
            replace(wideNumberTable, "  %converted = ctjs.binary_static bitor %slot, %zero",
                    "  %number = ctjs." + expression +
                        "\n  %converted = ctjs.binary_static bitor %number, %zero");
        if (expression == "unary plus %slot" || expression == "binary sub %slot, %zero" ||
            expression == "binary add %slot, %zero" || expression == "binary mul %slot, %one" ||
            expression == "binary div %slot, %one") {
            run({.what = "original table identity arithmetic supplies its bitwise conversion",
                 .body = converted,
                 .arrays = "keys:[textZero,textTwo]; a:[zero,zero,zero]",
                 .reads = "keys[0]=textZero; keys[1]=textTwo; keys[0]=textZero",
                 .exit = "a -> {a}"},
                "x");
        } else {
            reject("wide computed Number values still need independent provenance", converted);
        }
    }
    reject("wide original Number keys do not become wrapped properties",
           replace(wideNumberTable, "%base[%converted]", "%base[%slot]"));
    for (const std::string before : {"  %pick =", "  %step ="}) {
        reject("wide Number table mutations remain visible before and after reads",
               replace(wideNumberTable, before,
                       "  ctjs.set_property %keys[%one], %textZero\n" + before));
    }
    for (const std::string bits :
         {"9218868437227405312", "18442240474082181120", "9221120237041090560"}) {
        run({.what = "nonfinite Numbers convert to zero bits while keeping original table values",
             .body = replace(wideNumberTable, "4751297606876816998", bits),
             .arrays = "keys:[textZero,textTwo]; a:[zero,zero,zero]",
             .reads = "keys[0]=textZero; keys[1]=textTwo; keys[0]=textZero",
             .exit = "a -> {a}"},
            "x");
    }
    const auto nonfiniteNumberTable =
        replace(numberFractionalTable, "4606281698874543309", "9218868437227405312");
    for (const std::string expression :
         {"bitand %slot, %two", "bitor %slot, %zero", "bitxor %slot, %zero", "shl %slot, %zero",
          "shr %slot, %zero", "ushr %slot, %zero", "bitor %i, %textZero", "shl %i, %textZero",
          "shr %i, %textZero", "ushr %i, %textZero"}) {
        run({.what = "nonfinite Number table operands masks and counts share Core conversion",
             .body = replace(nonfiniteNumberTable, "bitor %slot, %zero", expression),
             .arrays = "keys:[textZero,textTwo]; a:[zero,zero,zero]",
             .reads = "keys[0]=textZero; keys[1]=textTwo; keys[0]=textZero",
             .exit = "a -> {a}"},
            "x");
    }
    const auto negatedNonfiniteNumber = replace(
        nonfiniteNumberTable, "  %textZero = ctjs.constant #ctjs.number<9218868437227405312>",
        "  %infinity = ctjs.constant #ctjs.number<9218868437227405312>\n"
        "  %textZero = ctjs.unary neg %infinity");
    const auto complementedNonfiniteNumber = replace(
        nonfiniteNumberTable, "  %converted = ctjs.binary_static bitor %slot, %zero",
        "  %complement = ctjs.unary bitnot %slot\n  %converted = ctjs.unary bitnot %complement");
    for (const auto & source : {negatedNonfiniteNumber, complementedNonfiniteNumber}) {
        run({.what = "source Neg and signed complement keep the original nonfinite identity",
             .body = source,
             .arrays = "keys:[textZero,textTwo]; a:[zero,zero,zero]",
             .reads = "keys[0]=textZero; keys[1]=textTwo; keys[0]=textZero",
             .exit = "a -> {a}"},
            "x");
    }
    run({.what = "nonfinite varying shift counts convert without changing their table",
         .body =
             replace(replace(replace(nonfiniteNumberTable, "[%x, %zero, %x]", "[%zero, %x, %x]"),
                             "4613712638259704627", "4611235658464650854"),
                     "bitor %slot, %zero", "shl %one, %slot"),
         .arrays = "keys:[textZero,textTwo]; a:[zero,zero,zero]",
         .reads = "keys[0]=textZero; keys[1]=textTwo; keys[0]=textZero",
         .exit = "a -> {a}"},
        "x");
    run({.what = "nonfinite bitwise writes cannot release a previously saved child",
         .body = replace(replace(nonfiniteNumberTable, "  cf.br ^header",
                                 "  %saved = ctjs.get_property %a[%zero]\n  cf.br ^header"),
                         "ctjs.return %a", "ctjs.return %saved"),
         .arrays = "keys:[textZero,textTwo]; a:[zero,zero,zero]",
         .reads = "a[0]=x; keys[0]=textZero; keys[1]=textTwo; keys[0]=textZero",
         .exit = "x -> {x}"});
    const auto selectedNonfiniteNumber =
        replace(selectedNumberFractional, "4606281698874543309", "9218868437227405312");
    run({.what = "nonfinite table conversion retains its independent receiver reload gap",
         .body = selectedNonfiniteNumber,
         .arrays = "keys:[textZero,textTwo]; a:[zero,keys,zero]",
         .reads =
             "a[1]=keys; keys[0]=textZero; a[1]=keys; keys[1]=textTwo; a[1]=keys; keys[0]=textZero",
         .exit = "a -> {a,keys}"},
        "x");
    reject("nonfinite conversion cannot invalidate a reloaded table receiver",
           replace(selectedNonfiniteNumber, "4613712638259704627", "4611235658464650854"));
    for (const std::string expression :
         {"unary plus %slot", "binary sub %slot, %zero", "binary mul %slot, %one",
          "binary div %slot, %one", "binary add %slot, %zero"}) {
        reject("nonfinite arithmetic cannot borrow zero bitwise facts",
               replace(nonfiniteNumberTable, "binary_static bitor %slot, %zero", expression));
        const auto converted =
            replace(nonfiniteNumberTable, "  %converted = ctjs.binary_static bitor %slot, %zero",
                    "  %number = ctjs." + expression +
                        "\n  %converted = ctjs.binary_static bitor %number, %zero");
        if (expression == "unary plus %slot" || expression == "binary sub %slot, %zero" ||
            expression == "binary add %slot, %zero" || expression == "binary mul %slot, %one" ||
            expression == "binary div %slot, %one") {
            run({.what = "original table identity arithmetic supplies its bitwise conversion",
                 .body = converted,
                 .arrays = "keys:[textZero,textTwo]; a:[zero,zero,zero]",
                 .reads = "keys[0]=textZero; keys[1]=textTwo; keys[0]=textZero",
                 .exit = "a -> {a}"},
                "x");
        } else {
            reject("nonfinite computed results retain separate provenance requirements", converted);
        }
    }
    reject("nonfinite original Number properties retain their spelling",
           replace(nonfiniteNumberTable, "%base[%converted]", "%base[%slot]"));
    for (const std::string before : {"  %pick =", "  %step ="}) {
        reject("nonfinite conversion preserves table mutations before and after reads",
               replace(nonfiniteNumberTable, before,
                       "  ctjs.set_property %keys[%one], %textZero\n" + before));
    }
    const auto overflowStringTable =
        replace(fractionalTable, "#ctjs.string<\"0.9\">", "#ctjs.string<\"1e999\">");
    for (const std::string text : {"1e999", "-1e999", "+1e999", ".1e999", "1.e999", "1e2147483615",
                                   " 1e999 ", "100000000000000000000000000e9999"}) {
        run({.what = "validated decimal overflow converts to zero bits without changing Strings",
             .body = replace(overflowStringTable, "#ctjs.string<\"1e999\">",
                             "#ctjs.string<\"" + text + "\">"),
             .arrays = "keys:[textZero,textTwo]; a:[zero,zero,zero]",
             .reads = "keys[0]=textZero; keys[1]=textTwo; keys[0]=textZero",
             .exit = "a -> {a}"},
            "x");
    }
    for (const std::string expression :
         {"bitand %slot, %two", "bitor %slot, %zero", "bitxor %slot, %zero", "shl %slot, %zero",
          "shr %slot, %zero", "ushr %slot, %zero", "bitor %i, %textZero", "shl %i, %textZero",
          "shr %i, %textZero", "ushr %i, %textZero"}) {
        run({.what = "overflow String operands masks and counts share Core bitwise conversion",
             .body = replace(overflowStringTable, "bitor %slot, %zero", expression),
             .arrays = "keys:[textZero,textTwo]; a:[zero,zero,zero]",
             .reads = "keys[0]=textZero; keys[1]=textTwo; keys[0]=textZero",
             .exit = "a -> {a}"},
            "x");
    }
    run({.what = "complements preserve signed bitwise intermediates from decimal overflow",
         .body =
             replace(overflowStringTable, "  %converted = ctjs.binary_static bitor %slot, %zero",
                     "  %complement = ctjs.unary bitnot %slot\n"
                     "  %converted = ctjs.unary bitnot %complement"),
         .arrays = "keys:[textZero,textTwo]; a:[zero,zero,zero]",
         .reads = "keys[0]=textZero; keys[1]=textTwo; keys[0]=textZero",
         .exit = "a -> {a}"},
        "x");
    run({.what = "overflow String shift counts convert while preserving the table",
         .body = replace(replace(replace(overflowStringTable, "[%x, %zero, %x]", "[%zero, %x, %x]"),
                                 "#ctjs.string<\"2.9\">", "#ctjs.string<\"1.9\">"),
                         "bitor %slot, %zero", "shl %one, %slot"),
         .arrays = "keys:[textZero,textTwo]; a:[zero,zero,zero]",
         .reads = "keys[0]=textZero; keys[1]=textTwo; keys[0]=textZero",
         .exit = "a -> {a}"},
        "x");
    run({.what = "decimal overflow conversion does not release a saved child",
         .body = replace(replace(overflowStringTable, "  cf.br ^header",
                                 "  %saved = ctjs.get_property %a[%zero]\n  cf.br ^header"),
                         "ctjs.return %a", "ctjs.return %saved"),
         .arrays = "keys:[textZero,textTwo]; a:[zero,zero,zero]",
         .reads = "a[0]=x; keys[0]=textZero; keys[1]=textTwo; keys[0]=textZero",
         .exit = "x -> {x}"});
    const auto selectedOverflowString =
        replace(selectedFractional, "#ctjs.string<\"0.9\">", "#ctjs.string<\"1e999\">");
    run({.what = "decimal overflow conversion retains the receiver reload gap",
         .body = selectedOverflowString,
         .arrays = "keys:[textZero,textTwo]; a:[zero,keys,zero]",
         .reads =
             "a[1]=keys; keys[0]=textZero; a[1]=keys; keys[1]=textTwo; a[1]=keys; keys[0]=textZero",
         .exit = "a -> {a,keys}"},
        "x");
    reject("overflow String keys cannot overwrite a reloaded table receiver",
           replace(selectedOverflowString, "#ctjs.string<\"2.9\">", "#ctjs.string<\"1.9\">"));
    for (const std::string text :
         {"1e999junk", "1e2147483616", "1e-2147483616", "Infinity", "NaN", "0x10000000000000801",
          "4294967296.9", "1000000000000000000000000000e9999"}) {
        if (text == "Infinity" || text == "NaN" || text == "4294967296.9") {
            run({.what = "numeric String conversion keeps the historical overflow table body",
                 .body = replace(overflowStringTable, "#ctjs.string<\"1e999\">",
                                 "#ctjs.string<\"" + text + "\">"),
                 .arrays = "keys:[textZero,textTwo]; a:[zero,zero,zero]",
                 .reads = "keys[0]=textZero; keys[1]=textTwo; keys[0]=textZero",
                 .exit = "a -> {a}"},
                "x");
            continue;
        }
        reject("bitwise overflow proof keeps grammar exponent radix and source-size guards",
               replace(overflowStringTable, "#ctjs.string<\"1e999\">",
                       "#ctjs.string<\"" + text + "\">"));
    }
    for (const std::string expression :
         {"unary plus %slot", "unary neg %slot", "binary sub %slot, %zero",
          "binary mul %slot, %one", "binary div %slot, %one", "binary add %slot, %zero"}) {
        reject("decimal overflow arithmetic cannot borrow zero bitwise facts",
               replace(overflowStringTable, "binary_static bitor %slot, %zero", expression));
        const auto converted =
            replace(overflowStringTable, "  %converted = ctjs.binary_static bitor %slot, %zero",
                    "  %number = ctjs." + expression +
                        "\n  %converted = ctjs.binary_static bitor %number, %zero");
        if (expression == "unary plus %slot" || expression == "binary sub %slot, %zero") {
            run({.what = "original table identity arithmetic supplies its bitwise conversion",
                 .body = converted,
                 .arrays = "keys:[textZero,textTwo]; a:[zero,zero,zero]",
                 .reads = "keys[0]=textZero; keys[1]=textTwo; keys[0]=textZero",
                 .exit = "a -> {a}"},
                "x");
        } else {
            reject("computed overflow still requires its own provenance proof", converted);
        }
    }
    reject("overflow String properties retain their original spelling",
           replace(overflowStringTable, "%base[%converted]", "%base[%slot]"));
    for (const std::string before : {"  %pick =", "  %step ="}) {
        reject("overflow conversion preserves table mutations before and after reads",
               replace(overflowStringTable, before,
                       "  ctjs.set_property %keys[%one], %textZero\n" + before));
    }
    const auto nonfiniteStringTable =
        replace(overflowStringTable, "#ctjs.string<\"1e999\">", "#ctjs.string<\"Infinity\">");
    for (const std::string text : {"Infinity", "+Infinity", "-Infinity", "NaN", " Infinity ",
                                   " +Infinity ", " -Infinity ", " NaN "}) {
        run({.what = "exact nonfinite tokens convert to zero bits without changing String values",
             .body = replace(nonfiniteStringTable, "#ctjs.string<\"Infinity\">",
                             "#ctjs.string<\"" + text + "\">"),
             .arrays = "keys:[textZero,textTwo]; a:[zero,zero,zero]",
             .reads = "keys[0]=textZero; keys[1]=textTwo; keys[0]=textZero",
             .exit = "a -> {a}"},
            "x");
    }
    for (const std::string expression :
         {"bitand %slot, %two", "bitxor %slot, %zero", "shl %slot, %zero", "shr %slot, %zero",
          "ushr %slot, %zero", "bitor %i, %textZero", "shl %i, %textZero", "shr %i, %textZero",
          "ushr %i, %textZero"}) {
        run({.what = "nonfinite String operands masks and counts share Core conversion",
             .body = replace(nonfiniteStringTable, "bitor %slot, %zero", expression),
             .arrays = "keys:[textZero,textTwo]; a:[zero,zero,zero]",
             .reads = "keys[0]=textZero; keys[1]=textTwo; keys[0]=textZero",
             .exit = "a -> {a}"},
            "x");
    }
    run({.what = "nonfinite String complement preserves its signed intermediate",
         .body =
             replace(nonfiniteStringTable, "  %converted = ctjs.binary_static bitor %slot, %zero",
                     "  %complement = ctjs.unary bitnot %slot\n"
                     "  %converted = ctjs.unary bitnot %complement"),
         .arrays = "keys:[textZero,textTwo]; a:[zero,zero,zero]",
         .reads = "keys[0]=textZero; keys[1]=textTwo; keys[0]=textZero",
         .exit = "a -> {a}"},
        "x");
    run({.what = "nonfinite String conversion preserves a saved child",
         .body = replace(replace(nonfiniteStringTable, "  cf.br ^header",
                                 "  %saved = ctjs.get_property %a[%zero]\n  cf.br ^header"),
                         "ctjs.return %a", "ctjs.return %saved"),
         .arrays = "keys:[textZero,textTwo]; a:[zero,zero,zero]",
         .reads = "a[0]=x; keys[0]=textZero; keys[1]=textTwo; keys[0]=textZero",
         .exit = "x -> {x}"});
    for (const std::string text : {"infinity", "INFINITY", "nan", "+NaN", "-NaN", "Infinityx",
                                   "NaNx", "Inf", "Infinity                         "}) {
        reject("nonfinite String proof requires exact tokens within the source-byte bound",
               replace(nonfiniteStringTable, "#ctjs.string<\"Infinity\">",
                       "#ctjs.string<\"" + text + "\">"));
    }
    for (const std::string expression :
         {"unary plus %slot", "unary neg %slot", "binary sub %slot, %zero",
          "binary mul %slot, %one", "binary div %slot, %one", "binary add %slot, %zero"}) {
        reject("nonfinite String arithmetic cannot borrow zero bitwise facts",
               replace(nonfiniteStringTable, "binary_static bitor %slot, %zero", expression));
        const auto converted =
            replace(nonfiniteStringTable, "  %converted = ctjs.binary_static bitor %slot, %zero",
                    "  %number = ctjs." + expression +
                        "\n  %converted = ctjs.binary_static bitor %number, %zero");
        if (expression == "unary plus %slot" || expression == "binary sub %slot, %zero") {
            run({.what = "original table identity arithmetic supplies its bitwise conversion",
                 .body = converted,
                 .arrays = "keys:[textZero,textTwo]; a:[zero,zero,zero]",
                 .reads = "keys[0]=textZero; keys[1]=textTwo; keys[0]=textZero",
                 .exit = "a -> {a}"},
                "x");
        } else {
            reject("computed nonfinite String conversions need independent provenance", converted);
        }
    }
    reject("nonfinite String properties retain their original spelling",
           replace(nonfiniteStringTable, "%base[%converted]", "%base[%slot]"));
    for (const std::string before : {"  %pick =", "  %step ="}) {
        reject("nonfinite String conversion preserves table mutations before and after reads",
               replace(nonfiniteStringTable, before,
                       "  ctjs.set_property %keys[%one], %textZero\n" + before));
    }
    const auto wideStringTable =
        replace(replace(fractionalTable, "#ctjs.string<\"0.9\">", "#ctjs.string<\"4294967296.9\">"),
                "#ctjs.string<\"2.9\">", "#ctjs.string<\"4294967298.9\">");
    for (const std::string expression :
         {"bitand %slot, %two", "bitor %slot, %zero", "bitxor %slot, %zero", "shl %slot, %zero",
          "shr %slot, %zero", "ushr %slot, %zero", "bitor %i, %textZero", "shl %i, %textZero",
          "shr %i, %textZero", "ushr %i, %textZero"}) {
        run({.what = "wide finite String operands masks and counts share Core bitwise conversion",
             .body = replace(wideStringTable, "bitor %slot, %zero", expression),
             .arrays = "keys:[textZero,textTwo]; a:[zero,zero,zero]",
             .reads = "keys[0]=textZero; keys[1]=textTwo; keys[0]=textZero",
             .exit = "a -> {a}"},
            "x");
    }
    for (const std::string text : {"-4294967296.9", "9007199254740993", "1e100",
                                   "1.7976931348623157e308", "0x100000000", "0xffffffffffffffff"}) {
        run({.what = "bounded source Strings wrap the represented Number before integer casts",
             .body = replace(wideStringTable, "#ctjs.string<\"4294967296.9\">",
                             "#ctjs.string<\"" + text + "\">"),
             .arrays = "keys:[textZero,textTwo]; a:[zero,zero,zero]",
             .reads = "keys[0]=textZero; keys[1]=textTwo; keys[0]=textZero",
             .exit = "a -> {a}"},
            "x");
    }
    run({.what = "wide negative String complement retains signed conversion",
         .body =
             replace(replace(replace(wideStringTable, "#ctjs.string<\"4294967296.9\">",
                                     "#ctjs.string<\"-4294967297.9\">"),
                             "#ctjs.string<\"4294967298.9\">", "#ctjs.string<\"-4294967299.9\">"),
                     "binary_static bitor %slot, %zero", "unary bitnot %slot"),
         .arrays = "keys:[textZero,textTwo]; a:[zero,zero,zero]",
         .reads = "keys[0]=textZero; keys[1]=textTwo; keys[0]=textZero",
         .exit = "a -> {a}"},
        "x");
    for (const std::string expression :
         {"unary plus %slot", "unary neg %slot", "binary sub %slot, %zero",
          "binary mul %slot, %one", "binary div %slot, %one", "binary mod %slot, %three",
          "binary pow %slot, %zero", "binary add %slot, %zero"}) {
        reject("wide String arithmetic retains its bounded exact Number proof",
               replace(wideStringTable, "binary_static bitor %slot, %zero", expression));
        const auto converted =
            replace(wideStringTable, "  %converted = ctjs.binary_static bitor %slot, %zero",
                    "  %number = ctjs." + expression +
                        "\n  %converted = ctjs.binary_static bitor %number, %zero");
        if (expression == "unary plus %slot" || expression == "binary sub %slot, %zero") {
            run({.what = "original table identity arithmetic supplies its bitwise conversion",
                 .body = converted,
                 .arrays = "keys:[textZero,textTwo]; a:[zero,zero,zero]",
                 .reads = "keys[0]=textZero; keys[1]=textTwo; keys[0]=textZero",
                 .exit = "a -> {a}"},
                "x");
        } else {
            reject("computed wide String arithmetic cannot borrow literal bitwise provenance",
                   converted);
        }
    }
    reject("wide String properties retain their original spelling",
           replace(wideStringTable, "%base[%converted]", "%base[%slot]"));
    for (const std::string before : {"  %pick =", "  %step ="}) {
        reject("wide String conversion keeps every table mutation in its census",
               replace(wideStringTable, before,
                       "  ctjs.set_property %keys[%one], %textZero\n" + before));
    }
    reject("table conversion cannot invoke object coercion",
           replace(convertedTable, "[%textZero, %textTwo]", "[%x, %textTwo]"));
    reject("table conversion cannot read a missing own element",
           replace(convertedTable, "[%textZero, %textTwo]", "[%textZero]"));
    for (const std::string expression :
         {"ctjs.binary add %slot, %zero", "ctjs.binary add %zero, %slot",
          "ctjs.binary_static add %slot, %zero"}) {
        reject("an outer conversion does not make earlier String addition numeric",
               replace(convertedTable, "  %converted = ctjs.unary plus %slot",
                       "  %text = " + expression + "\n  %converted = ctjs.unary plus %text"));
    }
    reject("converted table keys cannot overlap their selected receiver reload",
           replace(selectedConvertedTable, "#ctjs.string<\"2\">", "#ctjs.string<\"1\">"));
    reject("a later write cannot invalidate a converted table receiver",
           replace(selectedConvertedTable,
                   "  %step =", "  ctjs.set_property %base[%one], %zero\n  %step ="));
    reject("conversion cannot turn the mutable guard array into an invariant table",
           replace(convertedTable, "%keys[%pick]", "%base[%pick]"));
    for (const std::string mutation :
         {"ctjs.set_property %keys[%one], %textZero", "ctjs.set_property %keys[%key], %one"}) {
        reject("table conversion retains mutations before reads in the complete census",
               replace(convertedTable, "  %pick =", "  " + mutation + "\n  %pick ="));
        reject("table conversion retains mutations after reads in the complete census",
               replace(convertedTable, "  %step =", "  " + mutation + "\n  %step ="));
    }
    const auto convertedTableAlias =
        replace(stringTableAlias, "  ctjs.set_property %base[%slot]",
                "  %converted = ctjs.unary plus %slot\n  ctjs.set_property %base[%converted]");
    reject("converted table aliases keep their exact allocation in the write census",
           replace(convertedTableAlias,
                   "  %step =", "  ctjs.set_property %alias[%one], %textZero\n  %step ="));
    const auto binaryTable = replace(convertedTable, "unary plus %slot", "binary sub %slot, %zero");
    for (const std::string expression :
         {"binary sub %slot, %zero", "binary sub %two, %slot", "binary mul %slot, %one",
          "binary mul %one, %slot", "binary div %slot, %one", "binary mod %slot, %three",
          "binary pow %slot, %one", "binary_static bitand %slot, %two",
          "binary_static bitor %slot, %zero", "binary_static bitxor %slot, %zero",
          "binary_static shl %slot, %zero", "binary_static shr %slot, %zero",
          "binary_static ushr %slot, %zero"}) {
        run({.what = "numeric binary table operands convert without changing original read values",
             .body = replace(binaryTable, "binary sub %slot, %zero", expression),
             .arrays = "keys:[textZero,textTwo]; a:[zero,zero,zero]",
             .reads = "keys[0]=textZero; keys[1]=textTwo; keys[0]=textZero",
             .exit = "a -> {a}"},
            "x");
    }
    for (const std::string primitive : {"#ctjs.null", "#ctjs.boolean<false>"}) {
        run({.what = "numeric binary operations retain original null and Boolean table values",
             .body = replace(binaryTable, "#ctjs.string<\"0\">", primitive),
             .arrays = "keys:[textZero,textTwo]; a:[zero,zero,zero]",
             .reads = "keys[0]=textZero; keys[1]=textTwo; keys[0]=textZero",
             .exit = "a -> {a}"},
            "x");
    }
    for (const std::string expression :
         {"binary div %two, %slot", "binary mod %two, %slot", "binary_static shr %two, %slot",
          "binary pow %two, %slot"}) {
        const bool divide = expression == "binary div %two, %slot";
        const bool remainder = expression == "binary mod %two, %slot";
        auto source = replace(binaryTable, "binary sub %slot, %zero", expression);
        source = replace(source, "#ctjs.string<\"0\">",
                         divide || remainder ? "#ctjs.string<\"1\">" : "#ctjs.string<\"0\">");
        source = replace(source, "#ctjs.string<\"2\">",
                         remainder ? "#ctjs.string<\"3\">"
                         : divide  ? "#ctjs.string<\"2\">"
                                   : "#ctjs.string<\"1\">");
        if (!remainder) { source = replace(source, "[%x, %zero, %x]", "[%zero, %x, %x]"); }
        run({.what =
                 "varying binary right operands preserve original division shift and power order",
             .body = source,
             .arrays = "keys:[textZero,textTwo]; a:[zero,zero,zero]",
             .reads = "keys[0]=textZero; keys[1]=textTwo; keys[0]=textZero",
             .exit = "a -> {a}"},
            "x");
    }
    run({.what = "binary table replay retains children never actually overwritten",
         .body = replace(binaryTable, "[%textZero, %textTwo]", "[%textZero, %textZero]"),
         .arrays = "keys:[textZero,textZero]; a:[zero,zero,x]",
         .reads = "keys[0]=textZero; keys[1]=textZero; keys[0]=textZero",
         .exit = "a -> {a,x}"});
    run({.what = "a child saved before binary table writes retains its original identity",
         .body = replace(replace(binaryTable, "  cf.br ^header",
                                 "  %saved = ctjs.get_property %a[%zero]\n  cf.br ^header"),
                         "ctjs.return %a", "ctjs.return %saved"),
         .arrays = "keys:[textZero,textTwo]; a:[zero,zero,zero]",
         .reads = "a[0]=x; keys[0]=textZero; keys[1]=textTwo; keys[0]=textZero",
         .exit = "x -> {x}"});
    run({.what = "two varying binary operands retain their independent original reads",
         .body = replace(binaryTable, "  %converted = ctjs.binary sub %slot, %zero",
                         "  %other = ctjs.get_property %keys[%pick]\n"
                         "  %converted = ctjs.binary sub %slot, %other"),
         .arrays = "keys:[textZero,textTwo]; a:[zero,zero,x]",
         .reads = "keys[0]=textZero; keys[0]=textZero; keys[1]=textTwo; keys[1]=textTwo; "
                  "keys[0]=textZero; keys[0]=textZero",
         .exit = "a -> {a,x}"});
    const auto selectedBinaryTable =
        replace(selectedConvertedTable, "unary plus %slot", "binary mul %slot, %one");
    run({.what = "binary table conversions still prove independent receiver reload gaps",
         .body = selectedBinaryTable,
         .arrays = "keys:[textZero,textTwo]; a:[zero,keys,zero]",
         .reads = "a[1]=keys; keys[0]=textZero; a[1]=keys; keys[1]=textTwo; a[1]=keys; "
                  "keys[0]=textZero",
         .exit = "a -> {a,keys}"},
        "x");
    for (const std::string primitive :
         {"#ctjs.string<\"00\">", "#ctjs.string<\"-0\">", "#ctjs.string<\"0.0\">",
          "#ctjs.string<\"-2\">", "#ctjs.string<\"3\">", "#ctjs.string<\"4294967296\">",
          "#ctjs.number<4602678819172646912>", "#ctjs.bigint<\"0\">", "#ctjs.undefined"}) {
        const auto source = replace(binaryTable, "#ctjs.string<\"0\">", primitive);
        if (primitive == "#ctjs.string<\"00\">" || primitive == "#ctjs.string<\"-0\">" ||
            primitive == "#ctjs.string<\"0.0\">") {
            run({.what = "decimal conversion preserves original table primitives",
                 .body = source,
                 .arrays = "keys:[textZero,textTwo]; a:[zero,zero,zero]",
                 .reads = "keys[0]=textZero; keys[1]=textTwo; keys[0]=textZero",
                 .exit = "a -> {a}"},
                "x");
        } else {
            reject("binary table conversion retains exact primitive and own-index bounds", source);
        }
    }
    reject("binary table conversion cannot invoke object coercion",
           replace(binaryTable, "[%textZero, %textTwo]", "[%x, %textTwo]"));
    reject("binary table conversion cannot read a missing own element",
           replace(binaryTable, "[%textZero, %textTwo]", "[%textZero]"));
    for (const std::string expression :
         {"binary div %slot, %zero", "binary mod %slot, %zero", "binary div %slot, %three",
          "binary pow %slot, %two", "binary add %slot, %zero", "binary add %zero, %slot",
          "binary_static add %slot, %zero"}) {
        reject("numeric table conversions retain poles fractional values general powers and String "
               "Add",
               replace(binaryTable, "binary sub %slot, %zero", expression));
    }
    reject("a numeric outer operation cannot convert an earlier String addition into Number Add",
           replace(binaryTable, "  %converted = ctjs.binary sub %slot, %zero",
                   "  %text = ctjs.binary add %slot, %zero\n"
                   "  %converted = ctjs.binary sub %text, %zero"));
    reject("binary table conversions cannot overlap a selected receiver reload",
           replace(selectedBinaryTable, "#ctjs.string<\"2\">", "#ctjs.string<\"1\">"));
    reject("a later write cannot invalidate a binary table receiver",
           replace(selectedBinaryTable,
                   "  %step =", "  ctjs.set_property %base[%one], %zero\n  %step ="));
    reject("binary conversion cannot make the mutable guard array an invariant table",
           replace(binaryTable, "%keys[%pick]", "%base[%pick]"));
    for (const std::string mutation :
         {"ctjs.set_property %keys[%one], %textZero", "ctjs.set_property %keys[%key], %one"}) {
        reject("binary table conversion retains mutations before reads in the complete census",
               replace(binaryTable, "  %pick =", "  " + mutation + "\n  %pick ="));
        reject("binary table conversion retains mutations after reads in the complete census",
               replace(binaryTable, "  %step =", "  " + mutation + "\n  %step ="));
    }
    reject("binary table aliases retain exact allocation in the complete write census",
           replace(replace(convertedTableAlias, "unary plus %slot", "binary sub %slot, %zero"),
                   "  %step =", "  ctjs.set_property %alias[%one], %textZero\n  %step ="));
    const auto bigintTable =
        replace(replace(stringTable, "#ctjs.string<\"0\">", "#ctjs.bigint<\"0\">"),
                "#ctjs.string<\"2\">", "#ctjs.bigint<\"2\">");
    for (const auto & source :
         {bigintTable,
          replace(replace(bigintTable, "#ctjs.bigint<\"0\">", "#ctjs.bigint<\"0x0\">"),
                  "#ctjs.bigint<\"2\">", "#ctjs.bigint<\"0b10\">"),
          replace(bigintTable, "#ctjs.bigint<\"2\">", "#ctjs.string<\"2\">"),
          replace(bigintTable, "#ctjs.bigint<\"2\">", "#ctjs.number<4611686018427387904>")}) {
        run({.what = "BigInt table property keys preserve the selected primitive identities",
             .body = source,
             .arrays = "keys:[textZero,textTwo]; a:[zero,zero,zero]",
             .reads = "keys[0]=textZero; keys[1]=textTwo; keys[0]=textZero",
             .exit = "a -> {a}"},
            "x");
    }
    run({.what = "BigInt table replay retains children at unwritten positions",
         .body = replace(bigintTable, "[%textZero, %textTwo]", "[%textZero, %textZero]"),
         .arrays = "keys:[textZero,textZero]; a:[zero,zero,x]",
         .reads = "keys[0]=textZero; keys[1]=textZero; keys[0]=textZero",
         .exit = "a -> {a,x}"});
    run({.what = "a saved child survives BigInt table-directed overwrites",
         .body = replace(replace(bigintTable, "  cf.br ^header",
                                 "  %saved = ctjs.get_property %a[%zero]\n  cf.br ^header"),
                         "ctjs.return %a", "ctjs.return %saved"),
         .arrays = "keys:[textZero,textTwo]; a:[zero,zero,zero]",
         .reads = "a[0]=x; keys[0]=textZero; keys[1]=textTwo; keys[0]=textZero",
         .exit = "x -> {x}"});
    const auto nestedBigintTable =
        replace(replace(replace(nestedStringTable, "#ctjs.string<\"0\">", "#ctjs.bigint<\"0\">"),
                        "#ctjs.string<\"1\">", "#ctjs.bigint<\"1\">"),
                "#ctjs.string<\"2\">", "#ctjs.bigint<\"2\">");
    run({.what = "nested BigInt table reads convert only at each property boundary",
         .body = nestedBigintTable,
         .arrays = "picks:[textZero,textOne]; keys:[textZero,textTwo]; a:[zero,zero,zero]",
         .reads = "picks[0]=textZero; keys[0]=textZero; picks[1]=textOne; keys[1]=textTwo; "
                  "picks[0]=textZero; keys[0]=textZero",
         .exit = "a -> {a}"},
        "x");
    const auto selectedBigintTable =
        replace(replace(selectedStringTable, "#ctjs.string<\"0\">", "#ctjs.bigint<\"0\">"),
                "#ctjs.string<\"2\">", "#ctjs.bigint<\"2\">");
    run({.what = "BigInt table keys preserve the selected receiver's independent reload gap",
         .body = selectedBigintTable,
         .arrays = "keys:[textZero,textTwo]; a:[zero,keys,zero]",
         .reads = "a[1]=keys; keys[0]=textZero; a[1]=keys; keys[1]=textTwo; a[1]=keys; "
                  "keys[0]=textZero",
         .exit = "a -> {a,keys}"},
        "x");
    reject("BigInt table writes cannot overlap a selected receiver reload",
           replace(selectedBigintTable, "#ctjs.bigint<\"2\">", "#ctjs.bigint<\"1\">"));
    for (const std::string key : {"-1", "3", "4294967295", "4294967296"}) {
        reject("BigInt table property keys must name an existing bounded own element",
               replace(bigintTable, "#ctjs.bigint<\"0\">", "#ctjs.bigint<\"" + key + "\">"));
    }
    for (const std::string expression :
         {"unary plus %slot", "binary add %slot, %zero", "binary sub %slot, %zero",
          "binary_static shl %slot, %zero"}) {
        reject("BigInt property keys cannot lend Number facts to numeric operations",
               replace(bigintTable, "  ctjs.set_property %base[%slot]",
                       "  %converted = ctjs." + expression +
                           "\n  ctjs.set_property %base[%converted]"));
    }
    for (const std::string point : {"  %pick =", "  %step ="}) {
        reject("BigInt table mutation remains visible before and after lookup",
               replace(bigintTable, point, "  ctjs.set_property %keys[%one], %textZero\n" + point));
    }
    const auto reloaded = replace(
        replace(savedChild, "  %step =", "  %unit = ctjs.get_property %base[%zero]\n  %step ="),
        "add %i, %one", "add %i, %unit");
    run({.what = "dense own-element reloads retain the original array and key",
         .body = reloaded,
         .arrays = "a:[one,x]",
         .reads = "a[0]=one; a[0]=one; a[1]=x; a[0]=one",
         .exit = "x -> {x}"});
    run({.what = "reloaded invariant stride leaves unreturned children confined",
         .body = replace(reloaded, "ctjs.return %result", "ctjs.return %zero"),
         .arrays = "a:[one,x]",
         .reads = "a[0]=one; a[0]=one; a[1]=x; a[0]=one",
         .exit = "zero -> {}"},
        "x");
    const auto disjointReload =
        replace(replace(replace(reloaded, "  cf.br ^header",
                                "  %seed = ctjs.create_array [%one] {storage_test_id = \"seed\"}\n"
                                "  cf.br ^header"),
                        "  %unit =", "  ctjs.set_property %base[%i], %zero\n  %unit ="),
                "%unit = ctjs.get_property %base[%zero]", "%unit = ctjs.get_property %seed[%zero]");
    run({.what = "a disjoint reloaded stride survives current own-element overwrites",
         .body = disjointReload,
         .arrays = "a:[zero,zero]; seed:[one]",
         .reads = "a[0]=one; seed[0]=one; a[1]=x; seed[0]=one",
         .exit = "x -> {x}"});
    run({.what = "disjoint stride reads release overwritten children of the returned array",
         .body = replace(disjointReload, "ctjs.return %result", "ctjs.return %a"),
         .arrays = "a:[zero,zero]; seed:[one]",
         .reads = "a[0]=one; seed[0]=one; a[1]=x; seed[0]=one",
         .exit = "a -> {a}"},
        "x");
    const auto nestedReload = replace(
        replace(replace(disjointReload, "  cf.br ^header",
                        "  %box = ctjs.create_array [%seed, %a] {storage_test_id = \"box\"}\n"
                        "  cf.br ^header"),
                "  %unit =", "  %input = ctjs.get_property %box[%zero]\n  %unit ="),
        "%seed[%zero]", "%input[%zero]");
    run({.what = "nested stride reads use selected allocation origins, not whole-graph aliases",
         .body = replace(nestedReload, "ctjs.return %result", "ctjs.return %a"),
         .arrays = "a:[zero,zero]; seed:[one]; box:[seed,a]",
         .reads = "a[0]=one; box[0]=seed; seed[0]=one; a[1]=x; box[0]=seed; seed[0]=one",
         .exit = "a -> {a}"},
        "x,seed");
    reject("a nested stride read cannot hide the overwritten guard allocation",
           replace(nestedReload, "[%seed, %a]", "[%a, %seed]"));
    reject("a recursive contents graph cannot hide a selected guard allocation",
           replace(replace(replace(nestedReload, "[%seed, %a]", "[%zero, %a]"), "  cf.br ^header",
                           "  ctjs.set_property %box[%zero], %box\n  cf.br ^header"),
                   "%input = ctjs.get_property %box[%zero]",
                   "%link = ctjs.get_property %box[%zero]\n"
                   "  %input = ctjs.get_property %link[%one]"));
    reject("a saved array alias is not disjoint from the overwritten guard allocation",
           replace(replace(disjointReload, "  cf.br ^header",
                           "  %box = ctjs.create_array [%a]\n"
                           "  %alias = ctjs.get_property %box[%zero]\n  cf.br ^header"),
                   "%seed[%zero]", "%alias[%zero]"));
    run({.what = "a nested guard length stays invariant across own-element overwrites",
         .body = replace(replace(replace(nestedReload, "[%seed, %a]", "[%a, %seed]"),
                                 "%input[%zero]", "%input[%key]"),
                         "ctjs.return %result", "ctjs.return %a"),
         .arrays = "a:[zero,x]; seed:[one]; box:[a,seed]",
         .reads = "a[0]=one; box[0]=a",
         .exit = "a -> {a,x}"},
        "seed");
    for (const std::string key : {"#ctjs.string<\"00\">", "#ctjs.string<\"-0\">",
                                  "#ctjs.boolean<false>", "#ctjs.number<4613937818241073152>"}) {
        reject(
            "invariant reload cannot coerce or inherit an absent own key",
            replace(replace(reloaded, "  %unit =", "  %bad = ctjs.constant " + key + "\n  %unit ="),
                    "%base[%zero]", "%base[%bad]"));
    }
    reject("a reloaded stride cannot borrow a changing index",
           replace(reloaded, "%base[%zero]", "%base[%i]"));
    reject("an invariant reload cannot overlook loop mutation",
           replace(reloaded, "  %unit =", "  ctjs.set_property %base[%zero], %two\n  %unit ="));
    const auto lengthReload = replace(replace(savedChild, "  %step =",
                                              "  %name = ctjs.constant #ctjs.string<\"length\">\n"
                                              "  %size = ctjs.get_property %base[%name]\n"
                                              "  %unit = ctjs.binary sub %size, %one\n  %step ="),
                                      "add %i, %one", "add %i, %unit");
    run({.what = "an invariant own-length reload preserves the returned child",
         .body = lengthReload,
         .arrays = "a:[one,x]",
         .reads = "a[0]=one; a[1]=x",
         .exit = "x -> {x}"});
    run({.what = "an invariant own-length reload discharges only discarded children",
         .body = replace(lengthReload, "ctjs.return %result", "ctjs.return %zero"),
         .arrays = "a:[one,x]",
         .reads = "a[0]=one; a[1]=x",
         .exit = "zero -> {}"},
        "x");
    run({.what = "the original header length can independently supply the latch stride",
         .body = replace(savedChild, "add %i, %one", "add %i, %length"),
         .arrays = "a:[one,x]",
         .reads = "a[0]=one",
         .exit = "one -> {}"},
        "x");
    for (const std::string mutation :
         {"ctjs.set_property %base[%name], %one", "ctjs.set_property %base[%zero], %one",
          "ctjs.append %one to %base"}) {
        const auto body = replace(lengthReload, "  %size =", "  " + mutation + "\n  %size =");
        if (mutation == "ctjs.set_property %base[%zero], %one") {
            run({.what = "own-length strides remain invariant across existing-element writes",
                 .body = body,
                 .arrays = "a:[one,x]",
                 .reads = "a[0]=one; a[1]=x",
                 .exit = "x -> {x}"});
        } else {
            reject("an invariant length cannot bypass the complete loop mutation census", body);
        }
    }
    reject("a misspelled length is not an own length",
           replace(lengthReload, "%name = ctjs.constant #ctjs.string<\"length\">",
                   "%name = ctjs.constant #ctjs.string<\"Length\">"));
    reject("an own-length result cannot certify a zero stride",
           replace(lengthReload, "binary sub %size, %one", "binary sub %size, %two"));
    reject("an own-length reload must preserve the final-index bound",
           replace(lengthReload, "  %unit = ctjs.binary sub %size, %one",
                   "  %max = ctjs.constant #ctjs.number<4751297606873776128>\n"
                   "  %unit = ctjs.binary add %size, %max"));
    const auto stringLength =
        replace(replace(savedChild, "  %step =",
                        "  %text = ctjs.constant #ctjs.string<\"a\">\n"
                        "  %name = ctjs.constant #ctjs.string<\"length\">\n"
                        "  %unit = ctjs.get_property %text[%name]\n  %step ="),
                "add %i, %one", "add %i, %unit");
    run({.what = "original ASCII String length is an invariant Number stride",
         .body = stringLength,
         .arrays = "a:[one,x]",
         .reads = "a[0]=one; a[1]=x",
         .exit = "x -> {x}"});
    run({.what = "an ASCII length stride preserves discarded child confinement",
         .body = replace(stringLength, "ctjs.return %result", "ctjs.return %zero"),
         .arrays = "a:[one,x]",
         .reads = "a[0]=one; a[1]=x",
         .exit = "zero -> {}"},
        "x");
    for (const std::string & text : {std::string{}, std::string{"é"}, std::string(257, 'a')}) {
        reject("String length refuses zero strides, Unicode and its scan ceiling",
               replace(stringLength, "#ctjs.string<\"a\">", "#ctjs.string<\"" + text + "\">"));
    }
    const auto stringIndex =
        replace(replace(stringLength, "#ctjs.string<\"a\">", "#ctjs.string<\"1\">"),
                "  %unit = ctjs.get_property %text[%name]",
                "  %character = ctjs.get_property %text[%zero]\n"
                "  %unit = ctjs.unary plus %character");
    run({.what = "original ASCII Number-key reads supply invariant digit strides",
         .body = stringIndex,
         .arrays = "a:[one,x]",
         .reads = "a[0]=one; a[1]=x",
         .exit = "x -> {x}"});
    run({.what = "String index strides discharge only discarded children",
         .body = replace(stringIndex, "ctjs.return %result", "ctjs.return %zero"),
         .arrays = "a:[one,x]",
         .reads = "a[0]=one; a[1]=x",
         .exit = "zero -> {}"},
        "x");
    run({.what = "indexed String snapshots retain their own length",
         .body = replace(stringIndex, "ctjs.unary plus %character",
                         "ctjs.get_property %character[%name]"),
         .arrays = "a:[one,x]",
         .reads = "a[0]=one; a[1]=x",
         .exit = "x -> {x}"});
    const auto stringArrayReload =
        replace(replace(stringIndex, "#ctjs.string<\"1\">", "#ctjs.string<\"0\">"),
                "ctjs.unary plus %character", "ctjs.get_property %base[%character]");
    run({.what = "indexed digit snapshots are canonical own-array keys in invariant reloads",
         .body = stringArrayReload,
         .arrays = "a:[one,x]",
         .reads = "a[0]=one; a[0]=one; a[1]=x; a[0]=one",
         .exit = "x -> {x}"});
    run({.what = "digit-key invariant reloads discharge only discarded children",
         .body = replace(stringArrayReload, "ctjs.return %result", "ctjs.return %zero"),
         .arrays = "a:[one,x]",
         .reads = "a[0]=one; a[0]=one; a[1]=x; a[0]=one",
         .exit = "zero -> {}"},
        "x");
    for (const std::string text : {" ", "x", "9"}) {
        reject("indexed String keys require an existing canonical own-array element",
               replace(stringArrayReload, "#ctjs.string<\"0\">", "#ctjs.string<\"" + text + "\">"));
    }
    reject("digit-key reloads still require an invariant original String index",
           replace(stringArrayReload, "%text[%zero]", "%text[%i]"));
    reject("digit-key reloads cannot bypass the read-only loop census",
           replace(stringArrayReload,
                   "  %unit =", "  ctjs.set_property %base[%character], %one\n  %unit ="));
    reject(
        "an indexed digit String cannot become a Number key for another String",
        replace(stringIndex, "ctjs.unary plus %character", "ctjs.get_property %text[%character]"));
    const std::string digitKey = prefix + "  %x = ctjs.create_object {storage_test_id = \"x\"}\n"
                                          "  ctjs.set_property %a[%zero], %x\n"
                                          "  %text = ctjs.constant #ctjs.string<\"0\">\n"
                                          "  %character = ctjs.get_property %text[%zero]\n"
                                          "  %saved = ctjs.get_property %a[%character]\n"
                                          "  ctjs.set_property %a[%character], %zero\n"
                                          "  ctjs.return %saved\n";
    run({.what = "saved digit keys preserve child identity across own-array overwrite",
         .body = digitKey,
         .arrays = "a:[zero,two,three]",
         .reads = "a[0]=x",
         .exit = "x -> {x}"});
    run({.what = "digit-key overwrites discharge an unreturned saved child",
         .body = replace(digitKey, "ctjs.return %saved", "ctjs.return %zero"),
         .arrays = "a:[zero,two,three]",
         .reads = "a[0]=x",
         .exit = "zero -> {}"},
        "x");
    reject("digit-key replay cannot use a numeric whitespace conversion",
           replace(digitKey, "#ctjs.string<\"0\">", "#ctjs.string<\" \">"),
           ArrayContentsFailure::UnknownIndex);
    reject("digit-key replay retains the ordinary own-element bound",
           replace(digitKey, "#ctjs.string<\"0\">", "#ctjs.string<\"9\">"),
           ArrayContentsFailure::MissingElement);
    for (const std::string key : {"#ctjs.string<\"0\">", "#ctjs.bigint<\"0\">",
                                  "#ctjs.boolean<false>", "#ctjs.number<4607182418800017408>"}) {
        reject("String reads require an in-range original Number key",
               replace(replace(stringIndex, "  %character =",
                               "  %bad = ctjs.constant " + key + "\n  %character ="),
                       "%text[%zero]", "%text[%bad]"));
    }
    for (const std::string & text : {std::string{}, std::string{"é"}, std::string(257, '1')}) {
        reject("String indexed snapshots retain ASCII and size ceilings",
               replace(stringIndex, "#ctjs.string<\"1\">", "#ctjs.string<\"" + text + "\">"));
    }
    reject("String indices cannot borrow a changing induction value",
           replace(stringIndex, "%text[%zero]", "%text[%i]"));
    reject("String character conversion cannot turn String Add into Number Add",
           replace(stringIndex, "add %i, %unit", "add %i, %character"));
    run({.what = "String index strides survive invariant own-element overwrites",
         .body = replace(stringIndex, "  %character =",
                         "  ctjs.set_property %base[%zero], %one\n  %character ="),
         .arrays = "a:[one,x]",
         .reads = "a[0]=one; a[1]=x",
         .exit = "x -> {x}"});
    reject("String length requires its original exact key",
           replace(stringLength, "%text[%name]", "%text[%zero]"));
    reject("computed Strings cannot borrow literal length provenance",
           replace(stringLength, "ctjs.constant #ctjs.string<\"a\">", "ctjs.unary typeof %one"));
    run({.what = "String length strides survive invariant own-element overwrites",
         .body = replace(stringLength,
                         "  %unit =", "  ctjs.set_property %base[%zero], %one\n  %unit ="),
         .arrays = "a:[one,x]",
         .reads = "a[0]=one; a[1]=x",
         .exit = "x -> {x}"});
    const auto heldStringLength =
        replace(replace(computedUnitChild, makeUnit,
                        "  %text = ctjs.constant #ctjs.string<\"a\">\n"
                        "  %name = ctjs.constant #ctjs.string<\"length\">\n"
                        "  %unit = ctjs.get_property %text[%name]\n"),
                "ctjs.return %result", "ctjs.return %zero");
    run({.what = "saved ASCII length snapshots retain original Number identity",
         .body = heldStringLength,
         .arrays = "a:[one,x]",
         .reads = "a[0]=one; a[1]=x",
         .exit = "zero -> {}"},
        "x");
    reject("a negative computed start has no bounded Number certificate",
           replace(computed, "binary sub %one, %one", "binary sub %zero, %one"));
    reject("even an untaken predecessor must independently supply a bounded Number",
           replace(alternateStart, "binary sub %one, %one", "binary sub %zero, %one"));
    for (const std::string constant :
         {"#ctjs.string<\"0\">", "#ctjs.bigint<\"0\">", "#ctjs.number<4602678819172646912>"}) {
        reject("coercible and fractional starts do not become bounded integral Numbers",
               replace(computed, "ctjs.binary sub %one, %one", "ctjs.constant " + constant));
    }
    reject("a computed start leaves every indexed read subject to the original own bound",
           replace(computed, "%base[%i]", "%base[%three]"), ArrayContentsFailure::MissingElement);
    reject("a computed zero stride cannot borrow a previous positive Number fact",
           replace(computedUnit, "binary sub %two, %one", "binary sub %two, %two"));
    for (const std::string constant :
         {"#ctjs.string<\"1\">", "#ctjs.bigint<\"1\">", "#ctjs.number<4602678819172646912>"}) {
        reject("held coercible or fractional values cannot supply a Number-one step",
               replace(computedUnit, "ctjs.binary sub %two, %one", "ctjs.constant " + constant));
    }
    reject("an opaque held unit step cannot borrow any source Number fact",
           replace(computedUnit, "add %i, %unit", "add %i, %p"));
    reject("a changing carried step cannot reuse its initial Number-one fact",
           replace(carriedUnit, "^header(%base, %step, %added, %d",
                   "^header(%base, %step, %added, %zero"));
    reject("a swapping carried step cannot borrow another register's initial Number one",
           replace(carriedUnit, "^header(%base, %step, %added, %d",
                   "^header(%base, %step, %d, %added"));
    run({.what = "a body-computed subtraction proves its invariant unit step",
         .body = replace(replace(computedUnit, makeUnit, ""), "  %step =", makeUnit + "  %step ="),
         .arrays = "a:[one,two,three]",
         .reads = "a[0]=one; a[1]=two; a[2]=three",
         .exit = "added -> {}"});
    run({.what = "a header-computed subtraction proves its invariant unit step",
         .body = replace(replace(computedUnit, makeUnit, ""), "  %key =", makeUnit + "  %key ="),
         .arrays = "a:[one,two,three]",
         .reads = "a[0]=one; a[1]=two; a[2]=three",
         .exit = "added -> {}"});
    run({.what = "a held unit step permits current own-element overwrites",
         .body =
             replace(computedUnit, "  %read =", "  ctjs.set_property %base[%i], %zero\n  %read ="),
         .arrays = "a:[zero,zero,zero]",
         .reads = "a[0]=zero; a[1]=zero; a[2]=zero",
         .exit = "added -> {}"});
    reject("an element-dependent stride cannot borrow pre-overwrite contents",
           replace(reloaded, "  %unit =", "  ctjs.set_property %base[%i], %zero\n  %unit ="));
    reject(
        "an overwrite in the header is not guarded by the own-length comparison",
        replace(savedChild, "  %less =", "  ctjs.set_property %array[%index], %zero\n  %less ="));
    reject("a next-index overwrite may extend past the last own element",
           replace(savedChild, "  cf.br ^header(%base, %step",
                   "  ctjs.set_property %base[%step], %zero\n  cf.br ^header(%base, %step"));
    reject("an untaken predecessor must independently supply a Number-one step",
           replace(alternateUnit, "^entry(%one :", "^entry(%zero :"));
    reject("a positive carried stride cannot change even to another positive Number",
           replace(carriedStride, "^header(%base, %step, %added, %d",
                   "^header(%base, %step, %added, %one"));
    reject("positive strides do not certify their final overshoot as an own element",
           replace(replace(stride, "^exit(%sum :", "^exit(%index :"), "  ctjs.return %result",
                   "  %after = ctjs.get_property %a[%result]\n  ctjs.return %after"),
           ArrayContentsFailure::MissingElement);
    reject("nonunit induction still checks every visited offset against the own array",
           replace(stride, "  %read = ctjs.get_property %base[%i]",
                   "  %offset = ctjs.binary_static add %i, %one\n"
                   "  %read = ctjs.get_property %base[%offset]"),
           ArrayContentsFailure::MissingElement);
    for (const std::string constant :
         {"#ctjs.number<4751297606875873280>", "#ctjs.number<4845873199050653696>",
          "#ctjs.number<9218868437227405312>", "#ctjs.number<9221120237041090560>"}) {
        reject("out-of-range and nonfinite strides cannot supply bounded Number induction",
               replace(maxStride, "#ctjs.number<4751297606873776128>", constant));
        reject("out-of-range and nonfinite starts cannot borrow a bounded Number",
               replace(computed, "ctjs.binary sub %one, %one", "ctjs.constant " + constant));
    }
    reject("a nonzero start cannot overflow on its final stride update",
           replace(maxStride, "^header(%a, %zero, %zero", "^header(%a, %one, %zero"));
    reject("nonzero starts retain own bounds on every visited offset",
           replace(nonzeroStride, "%base[%i]", "%base[%three]"),
           ArrayContentsFailure::MissingElement);
    reject("a stride computation that exceeds the bounded range supplies no wrapped fact",
           replace(replace(maxStride, "  cf.br ^header",
                           "  %overflow = ctjs.binary_static add %max, %one\n  cf.br ^header"),
                   "add %i, %max", "add %i, %overflow"));
    reject("inclusive guards do not prove an own index",
           replace(original, "compare lt", "compare le"));
    reject("reversed inclusive guards do not prove an own index",
           replace(reversed, "compare gt", "compare ge"));
    reject("negated greater-than still includes the inherited length index",
           replace(negated, "compare ge", "compare gt"));
    reject("negated reversed less-than still includes the inherited length index",
           replace(negatedReversed, "compare le", "compare lt"));
    reject("negated guards cannot use a different bound",
           replace(negated, "compare ge %index, %length", "compare ge %index, %two"));
    reject("negated guards still reject a dynamic bound",
           replace(negated, "compare ge %index, %length", "compare ge %index, %p"),
           ArrayContentsFailure::UnsupportedOperation);
    reject("negated guards cannot conceal an array mutation",
           replace(negated, "  %read =", "  ctjs.set_property %base[%key], %zero\n  %read ="));
    reject("negated guards check every own index before releasing children",
           replace(negated, "%base[%i]", "%base[%two]"), ArrayContentsFailure::MissingElement);
    reject("logical negation cannot borrow a NaN initialization",
           replace(negated, "#ctjs.number<0>", "#ctjs.number<9221120237041090560>"));
    reject("logical negation cannot borrow a String initialization",
           replace(negated, "#ctjs.number<0>", "#ctjs.string<\"0\">"));
    reject("typeof does not complement the comparison",
           replace(negated, "unary not %less", "unary typeof %less"));
    reject("a second logical negation does not certify the strict guard",
           replace(negated, "  %flag = ctjs.truthy %negated",
                   "  %twice = ctjs.unary not %negated\n  %flag = ctjs.truthy %twice"));
    reject("greater-than guards still require length on the left",
           replace(reversed, "compare gt %length, %index", "compare gt %index, %length"));
    reject("reversed strict guards cannot use a different bound",
           replace(reversed, "compare gt %length, %index", "compare gt %two, %index"));
    reject("reversed strict guards cannot conceal a length mutation",
           replace(reversed, "  %read =", "  ctjs.set_property %base[%key], %zero\n  %read ="));
    reject("reversed strict guards still check every indexed read",
           replace(reversed, "%base[%i]", "%base[%two]"), ArrayContentsFailure::MissingElement);
    reject("inverted guards do not borrow strict induction",
           replace(original, "compare lt %index, %length", "compare lt %length, %index"));
    reject("an unknown entry index does not borrow literal zero",
           replace(original, "^header(%a, %zero, %zero", "^header(%a, %p, %zero"),
           ArrayContentsFailure::UnsupportedOperation);
    reject("a zero step is not a finite induction",
           replace(original, "add %i, %one", "add %i, %zero"));
    reject("a fractional step is not integer induction",
           replace(replace(original, "  %step =",
                           "  %half = ctjs.constant "
                           "#ctjs.number<4602678819172646912>\n  %step ="),
                   "add %i, %one", "add %i, %half"));
    reject("a negative step is not increasing induction",
           replace(replace(original, "  %step =",
                           "  %minus = ctjs.constant "
                           "#ctjs.number<13830554455654793216>\n  %step ="),
                   "add %i, %one", "add %i, %minus"));
    reject("a mixed BigInt update does not establish Number induction",
           replace(replace(original,
                           "  %step =", "  %big = ctjs.constant #ctjs.bigint<\"1\">\n  %step ="),
                   "add %i, %one", "add %i, %big"));
    reject("an unknown update cannot reuse the previous iteration's exact index",
           replace(original, "^header(%base, %step, %added", "^header(%base, %p, %added"));
    for (const std::string & source : {savedChild, reversed, negated, negatedReversed}) {
        const auto dynamic = replace(source, "binary_static add %i, %one", "binary add %i, %one");
        run({.what = "dynamic Add retains a returned child under exact Number induction",
             .body = dynamic,
             .arrays = "a:[one,x]",
             .reads = "a[0]=one; a[1]=x",
             .exit = "x -> {x}"});
        run({.what = "dynamic Add releases children only after complete bounded replay",
             .body = replace(dynamic, "ctjs.return %result", "ctjs.return %zero"),
             .arrays = "a:[one,x]",
             .reads = "a[0]=one; a[1]=x",
             .exit = "zero -> {}"},
            "x");
    }
    const auto dynamic = replace(original, "binary_static add %i, %one", "binary add %i, %one");
    for (const std::string constant : {"#ctjs.string<\"1\">", "#ctjs.bigint<\"1\">",
                                       "#ctjs.number<0>", "#ctjs.number<9221120237041090560>"}) {
        reject("dynamic Add requires a bounded positive Number stride",
               replace(dynamic, "#ctjs.number<4607182418800017408>", constant));
    }
    reject("dynamic Add cannot concatenate its String initialization",
           replace(dynamic, "#ctjs.number<0>", "#ctjs.string<\"0\">"));
    reject("a dynamic subtract latch cannot borrow the Add proof",
           replace(dynamic, "binary add %i, %one", "binary sub %i, %one"));
    const auto invariantProduct = replace(carriedUnit, "  %step = ctjs.binary_static add %i, %d",
                                          "  %product = ctjs.binary mul %d, %one\n"
                                          "  %step = ctjs.binary add %i, %product");
    for (const auto & source :
         {invariantProduct, replace(invariantProduct, "mul %d, %one", "mul %one, %d"),
          replace(invariantProduct, "mul %d, %one", "mul %d, %delta"),
          replace(replace(invariantProduct, "  %product = ctjs.binary mul %d, %one\n", ""),
                  "  %key =", "  %product = ctjs.binary mul %delta, %one\n  %key =")}) {
        run({.what = "one repeated product proves both invariant CFG operands",
             .body = source,
             .arrays = "a:[one,two,three]",
             .reads = "a[0]=one; a[1]=two; a[2]=three",
             .exit = "added -> {}"});
    }
    reject("a product operand must retain identity even when the replacement is equal",
           replace(invariantProduct, "^header(%base, %step, %added, %d",
                   "^header(%base, %step, %added, %one"));
    for (const std::string operands : {"%i, %one", "%one, %i", "%p, %one", "%one, %p"}) {
        reject("both product operands need independently invariant bounded values",
               replace(invariantProduct, "mul %d, %one", "mul " + operands));
    }
    run({.what = "a nested product proves each original invariant operand",
         .body = replace(invariantProduct, "  %product = ctjs.binary mul %d, %one",
                         "  %nested = ctjs.binary mul %d, %one\n"
                         "  %product = ctjs.binary mul %nested, %one"),
         .arrays = "a:[one,two,three]",
         .reads = "a[0]=one; a[1]=two; a[2]=three",
         .exit = "added -> {}"});
    for (const std::string literal : {"#ctjs.number<0>", "#ctjs.string<\"01\">",
                                      "#ctjs.bigint<\"1\">", "#ctjs.number<4602678819172646912>"}) {
        if (literal == "#ctjs.string<\"01\">") {
            run({.what =
                     "bounded decimal conversion preserves original reads and retained children",
                 .body = replace(invariantProduct, makeUnit,
                                 "  %unit = ctjs.constant " + literal + "\n"),
                 .arrays = "a:[one,two,three]",
                 .reads = "a[0]=one; a[1]=two; a[2]=three",
                 .exit = "added -> {}"});
        } else {
            reject(
                "product latches require exact nonzero primitive conversion",
                replace(invariantProduct, makeUnit, "  %unit = ctjs.constant " + literal + "\n"));
        }
    }
    reject("an invariant product must still bound the final index update",
           replace(replace(invariantProduct, makeUnit,
                           "  %unit = ctjs.constant #ctjs.number<4751297606873776128>\n"),
                   "^header(%a, %zero, %zero", "^header(%a, %one, %zero"));
    reject("two invariant operands cannot certify an overflowing product",
           replace(replace(invariantProduct, makeUnit,
                           "  %unit = ctjs.constant #ctjs.number<4751297606873776128>\n"),
                   "mul %d, %one", "mul %d, %two"));
    for (const std::string operation : {"div", "mod"}) {
        const auto source = replace(invariantProduct, "mul %d, %one",
                                    operation + " %d, " + (operation == "div" ? "%one" : "%two"));
        for (const auto & body :
             {source,
              replace(replace(source, makeUnit, "  %unit = ctjs.constant #ctjs.string<\"-1\">\n"),
                      "binary add %i, %product", "binary sub %i, %product")}) {
            run({.what = "invariant quotient and remainder preserve signed CFG transport",
                 .body = body,
                 .arrays = "a:[one,two,three]",
                 .reads = "a[0]=one; a[1]=two; a[2]=three",
                 .exit = "added -> {}"});
        }
        reject("division operands retain their original backedge identity",
               replace(source, "^header(%base, %step, %added, %d",
                       "^header(%base, %step, %added, %one"));
        for (const std::string operands : {"%d, %zero", "%p, %one", "%one, %i"}) {
            reject("division latches need invariant operands and a nonzero divisor",
                   replace(source, operation + " %d, " + (operation == "div" ? "%one" : "%two"),
                           operation + " " + operands));
        }
        run({.what = "bounded decimal conversion preserves original reads and retained children",
             .body = replace(source, makeUnit, "  %unit = ctjs.constant #ctjs.string<\"01\">\n"),
             .arrays = "a:[one,two,three]",
             .reads = "a[0]=one; a[1]=two; a[2]=three",
             .exit = "added -> {}"});
    }
    reject("a fractional quotient cannot certify integer induction",
           replace(invariantProduct, "mul %d, %one", "div %d, %two"));
    reject("a zero remainder cannot certify termination",
           replace(invariantProduct, "mul %d, %one", "mod %d, %one"));
    reject("an invariant quotient still bounds the final index update",
           replace(replace(replace(invariantProduct, "mul %d, %one", "div %d, %one"), makeUnit,
                           "  %unit = ctjs.constant #ctjs.number<4751297606873776128>\n"),
                   "^header(%a, %zero, %zero", "^header(%a, %one, %zero"));
    for (const std::string expression :
         {"add %d, %zero", "add %zero, %d", "sub %d, %zero", "sub %two, %d", "sub %zero, %d"}) {
        auto source = replace(invariantProduct, "mul %d, %one", expression);
        if (expression == "sub %zero, %d") {
            source = replace(source, "binary add %i, %product", "binary sub %i, %product");
        }
        run({.what = "invariant Add/Sub preserves original signed operands and transport",
             .body = source,
             .arrays = "a:[one,two,three]",
             .reads = "a[0]=one; a[1]=two; a[2]=three",
             .exit = "added -> {}"});
        reject("Add/Sub operands retain their original backedge identity",
               replace(source, "^header(%base, %step, %added, %d",
                       "^header(%base, %step, %added, %one"));
        reject("Add/Sub cannot borrow a changing operand",
               replace(source, expression, replace(expression, "%d", "%i")));
        reject("Add/Sub cannot borrow an unknown operand",
               replace(source, expression, replace(expression, "%d", "%p")));
        const auto string =
            replace(source, makeUnit, "  %unit = ctjs.constant #ctjs.string<\"1\">\n");
        if (expression.starts_with("add")) {
            reject("invariant Add cannot borrow numeric String conversion", string);
        } else {
            run({.what = "invariant Sub converts each original canonical String",
                 .body = string,
                 .arrays = "a:[one,two,three]",
                 .reads = "a[0]=one; a[1]=two; a[2]=three",
                 .exit = "added -> {}"});
            run({.what =
                     "bounded decimal conversion preserves original reads and retained children",
                 .body = replace(string, "#ctjs.string<\"1\">", "#ctjs.string<\"01\">"),
                 .arrays = "a:[one,two,three]",
                 .reads = "a[0]=one; a[1]=two; a[2]=three",
                 .exit = "added -> {}"});
        }
    }
    reject("invariant addition cannot exceed the exact magnitude bound",
           replace(replace(invariantProduct, "mul %d, %one", "add %d, %one"), makeUnit,
                   "  %unit = ctjs.constant #ctjs.number<4751297606873776128>\n"));
    reject("invariant subtraction cannot certify a zero stride",
           replace(invariantProduct, "mul %d, %one", "sub %d, %d"));
    const auto invariantBits =
        replace(invariantProduct, "binary mul %d, %one", "binary_static bitand %d, %one");
    for (const std::string expression : {"bitand %d, %one", "bitor %d, %zero", "bitxor %d, %zero",
                                         "shl %d, %zero", "shr %d, %zero", "ushr %d, %zero"}) {
        const auto source = replace(invariantBits, "bitand %d, %one", expression);
        for (const auto & body :
             {source, replace(source, makeUnit, "  %unit = ctjs.constant #ctjs.string<\"1\">\n")}) {
            run({.what = "bitwise latches preserve invariant Number and canonical String operands",
                 .body = body,
                 .arrays = "a:[one,two,three]",
                 .reads = "a[0]=one; a[1]=two; a[2]=three",
                 .exit = "added -> {}"});
        }
        reject("bitwise operands retain original backedge identity even at an equal value",
               replace(source, "^header(%base, %step, %added, %d",
                       "^header(%base, %step, %added, %one"));
        reject("bitwise latches cannot borrow a changing induction operand",
               replace(source, expression, replace(expression, "%d", "%i")));
        run({.what = "bounded decimal conversion preserves original reads and retained children",
             .body = replace(source, makeUnit, "  %unit = ctjs.constant #ctjs.string<\"01\">\n"),
             .arrays = "a:[one,two,three]",
             .reads = "a[0]=one; a[1]=two; a[2]=three",
             .exit = "added -> {}"});
    }
    run({.what = "header-local bitwise latches retain original header transport",
         .body = replace(
             replace(invariantBits, "  %product = ctjs.binary_static bitand %d, %one\n", ""),
             "  %key =", "  %product = ctjs.binary_static bitand %delta, %one\n  %key ="),
         .arrays = "a:[one,two,three]",
         .reads = "a[0]=one; a[1]=two; a[2]=three",
         .exit = "added -> {}"});
    const auto maskedBits =
        replace(replace(invariantBits, makeUnit,
                        "  %count = ctjs.constant #ctjs.number<4629700416936869888>\n" + makeUnit),
                "bitand %d, %one", "shl %d, %count");
    run({.what = "repeated left shifts mask the original count to five bits",
         .body = maskedBits,
         .arrays = "a:[one,two,three]",
         .reads = "a[0]=one; a[1]=two; a[2]=three",
         .exit = "added -> {}"});
    const auto negativeBits =
        replace(invariantBits, makeUnit, "  %unit = ctjs.constant #ctjs.string<\"-1\">\n");
    for (const std::string kind : {"shr", "ushr"}) {
        auto source = replace(negativeBits, "bitand %d, %one", kind + " %d, %d");
        if (kind == "shr") {
            source = replace(source, "binary add %i, %product", "binary sub %i, %product");
        }
        run({.what = "right shifts preserve signed String operands and mask negative counts",
             .body = source,
             .arrays = "a:[one,two,three]",
             .reads = "a[0]=one; a[1]=two; a[2]=three",
             .exit = "added -> {}"});
    }
    const auto wrappedBits =
        replace(replace(replace(invariantBits, makeUnit,
                                "  %unit = ctjs.constant #ctjs.number<4751297606873776128>\n"),
                        "bitand %d, %one", "shl %d, %one"),
                "binary add %i, %product", "binary sub %i, %product");
    run({.what = "left shifts wrap to an exact negative stride before subtraction",
         .body = wrappedBits,
         .arrays = "a:[one,two,three]",
         .reads = "a[0]=one; a[2]=three",
         .exit = "added -> {}"});
    const auto maximumBits = replace(negativeBits, "bitand %d, %one", "ushr %d, %zero");
    run({.what = "unsigned shifts permit the largest bounded final index",
         .body = maximumBits,
         .arrays = "a:[one,two,three]",
         .reads = "a[0]=one",
         .exit = "added -> {}"});
    reject("unsigned shift induction still bounds the final index update",
           replace(maximumBits, "^header(%a, %zero, %zero", "^header(%a, %one, %zero"));
    const auto nestedBits =
        replace(invariantBits, "  %product = ctjs.binary_static bitand %d, %one",
                "  %inner = ctjs.binary sub %d, %zero\n"
                "  %product = ctjs.binary_static bitand %inner, %one");
    run({.what = "bitwise latches reuse the second exact invariant operation layer",
         .body = nestedBits,
         .arrays = "a:[one,two,three]",
         .reads = "a[0]=one; a[1]=two; a[2]=three",
         .exit = "added -> {}"});
    run({.what = "deeper bitwise latches retain the original invariant operand",
         .body = replace(nestedBits, "  %inner = ctjs.binary sub %d, %zero",
                         "  %deep = ctjs.unary plus %d\n  %inner = ctjs.binary sub %deep, %zero"),
         .arrays = "a:[one,two,three]",
         .reads = "a[0]=one; a[1]=two; a[2]=three",
         .exit = "added -> {}"});
    run({.what = "a repeated bitwise operand reads its unchanged own element",
         .body = replace(nestedBits, "ctjs.binary sub %d, %zero", "ctjs.get_property %base[%zero]"),
         .arrays = "a:[one,two,three]",
         .reads = "a[0]=one; a[0]=one; a[1]=two; a[0]=one; a[2]=three; a[0]=one",
         .exit = "added -> {}"});
    reject("a zero bitwise stride does not certify termination",
           replace(invariantBits, "bitand %d, %one", "bitxor %d, %d"));
    reject("a negative bitwise stride cannot make Add induction increase",
           replace(negativeBits, "bitand %d, %one", "bitor %d, %zero"));
    reject(
        "bitwise conversion cannot change an original String property's key",
        replace(replace(invariantBits, makeUnit, "  %unit = ctjs.constant #ctjs.string<\"-1\">\n"),
                "%base[%i]", "%base[%d]"),
        ArrayContentsFailure::UnknownIndex);
    const auto invariantPower = replace(invariantProduct, "mul %d, %one", "pow %d, %one");
    for (const auto & source :
         {invariantPower, replace(invariantPower, "pow %d, %one", "pow %one, %d"),
          replace(invariantPower, "pow %d, %one", "pow %d, %delta"),
          replace(replace(invariantPower, "  %product = ctjs.binary pow %d, %one\n", ""),
                  "  %key =", "  %product = ctjs.binary pow %delta, %one\n  %key ="),
          replace(
              replace(invariantPower, makeUnit, "  %unit = ctjs.constant #ctjs.string<\"-1\">\n"),
              "binary add %i, %product", "binary sub %i, %product")}) {
        run({.what = "one power latch preserves original invariant CFG operands",
             .body = source,
             .arrays = "a:[one,two,three]",
             .reads = "a[0]=one; a[1]=two; a[2]=three",
             .exit = "added -> {}"});
    }
    reject("a power operand cannot change even to the same Number value",
           replace(invariantPower, "^header(%base, %step, %added, %d",
                   "^header(%base, %step, %added, %one"));
    for (const std::string operands :
         {"%i, %one", "%one, %i", "%p, %one", "%one, %p", "%two, %two", "%zero, %one"}) {
        reject("power latches require invariant exact operands and a positive stride",
               replace(invariantPower, "pow %d, %one", "pow " + operands));
    }
    run({.what = "a nested power proves its original unchanged base",
         .body = replace(invariantPower, "  %product = ctjs.binary pow %d, %one",
                         "  %nested = ctjs.binary pow %d, %one\n"
                         "  %product = ctjs.binary pow %nested, %one"),
         .arrays = "a:[one,two,three]",
         .reads = "a[0]=one; a[1]=two; a[2]=three",
         .exit = "added -> {}"});
    for (const std::string literal :
         {"#ctjs.string<\"01\">", "#ctjs.bigint<\"1\">", "#ctjs.undefined",
          "#ctjs.number<4602678819172646912>", "#ctjs.number<4751297606875873280>"}) {
        if (literal == "#ctjs.string<\"01\">") {
            run({.what =
                     "bounded decimal conversion preserves original reads and retained children",
                 .body =
                     replace(invariantPower, makeUnit, "  %unit = ctjs.constant " + literal + "\n"),
                 .arrays = "a:[one,two,three]",
                 .reads = "a[0]=one; a[1]=two; a[2]=three",
                 .exit = "added -> {}"});
        } else {
            reject("power latches cannot borrow unsupported primitive conversions",
                   replace(invariantPower, makeUnit, "  %unit = ctjs.constant " + literal + "\n"));
        }
    }
    reject("an invariant power still bounds the final index update",
           replace(replace(invariantPower, makeUnit,
                           "  %unit = ctjs.constant #ctjs.number<4751297606873776128>\n"),
                   "^header(%a, %zero, %zero", "^header(%a, %one, %zero"));
    for (const std::string unary : {"plus", "neg", "bitnot"}) {
        const auto source =
            replace(carriedUnit, "  %step = ctjs.binary_static add %i, %d",
                    "  %converted = ctjs.unary " + unary + " %d\n  %step = ctjs.binary " +
                        (unary == "plus" ? "add" : "sub") + " %i, %converted");
        for (const auto & body : {source, replace(source, unary + " %d", unary + " %delta")}) {
            run({.what = "unary latches preserve unchanged CFG operand transport",
                 .body = body,
                 .arrays = "a:[one,two,three]",
                 .reads =
                     unary == "bitnot" ? "a[0]=one; a[2]=three" : "a[0]=one; a[1]=two; a[2]=three",
                 .exit = "added -> {}"});
        }
        reject("a unary latch operand must retain its identity even at the same Number value",
               replace(source, "^header(%base, %step, %added, %d",
                       "^header(%base, %step, %added, %one"));
        reject("a unary latch cannot borrow a changing induction operand",
               replace(source, unary + " %d", unary + " %i"));
        run({.what = "nested unary conversions prove the original saved primitive",
             .body = replace(source, "  %converted = ctjs.unary " + unary + " %d",
                             "  %repeated = ctjs.unary plus %d\n  %converted = ctjs.unary " +
                                 unary + " %repeated"),
             .arrays = "a:[one,two,three]",
             .reads = unary == "bitnot" ? "a[0]=one; a[2]=three" : "a[0]=one; a[1]=two; a[2]=three",
             .exit = "added -> {}"});
    }
    const auto nested = replace(savedChild, "  %step = ctjs.binary_static add %i, %one",
                                "  %inner = ctjs.unary plus %one\n"
                                "  %stride = ctjs.binary mul %inner, %one\n"
                                "  %step = ctjs.binary add %i, %stride");
    for (const std::string expression :
         {"ctjs.unary plus %one", "ctjs.unary neg %one", "ctjs.unary bitnot %zero",
          "ctjs.binary mul %one, %one", "ctjs.binary div %one, %one", "ctjs.binary mod %one, %two",
          "ctjs.binary pow %one, %two", "ctjs.binary add %one, %zero",
          "ctjs.binary sub %one, %zero"}) {
        auto source = replace(nested, "ctjs.unary plus %one", expression);
        if (expression == "ctjs.unary neg %one" || expression == "ctjs.unary bitnot %zero") {
            source = replace(source, "binary add %i, %stride", "binary sub %i, %stride");
        }
        for (const auto & body :
             {source, replace(source, "binary mul %inner, %one", "binary mul %one, %inner")}) {
            run({.what = "mixed nested invariant operations preserve returned CFG children",
                 .body = body,
                 .arrays = "a:[one,x]",
                 .reads = "a[0]=one; a[1]=x",
                 .exit = "x -> {x}"});
            run({.what = "nested invariant operations discharge only unreturned CFG children",
                 .body = replace(body, "ctjs.return %result", "ctjs.return %zero"),
                 .arrays = "a:[one,x]",
                 .reads = "a[0]=one; a[1]=x",
                 .exit = "zero -> {}"},
                "x");
        }
    }
    for (const std::string expression :
         {"ctjs.unary plus %i", "ctjs.unary plus %p", "ctjs.get_property %base[%zero]",
          "ctjs.binary div %one, %two", "ctjs.binary div %one, %zero", "ctjs.binary pow %two, %two",
          "ctjs.binary mul %one, %zero"}) {
        const auto body = replace(nested, "ctjs.unary plus %one", expression);
        if (expression == "ctjs.get_property %base[%zero]") {
            run({.what = "nested induction rechecks an unchanged own element",
                 .body = body,
                 .arrays = "a:[one,x]",
                 .reads = "a[0]=one; a[0]=one; a[1]=x; a[0]=one",
                 .exit = "x -> {x}"});
        } else {
            reject("nested induction requires exact invariant operations and a positive stride",
                   body);
        }
    }
    run({.what = "deeper invariant induction retains the returned child",
         .body = replace(nested, "  %inner = ctjs.unary plus %one",
                         "  %deeper = ctjs.unary plus %one\n  %inner = ctjs.unary plus %deeper"),
         .arrays = "a:[one,x]",
         .reads = "a[0]=one; a[1]=x",
         .exit = "x -> {x}"});
    std::string chain, previous = "%one";
    for (unsigned depth = 1; depth <= 63; ++depth) {
        const auto next = "%deep" + std::to_string(depth);
        chain += "  " + next + " = ctjs.unary plus " + previous + "\n";
        previous = next;
        if (depth != 62 && depth != 63) { continue; }
        const auto body = replace(nested, "  %inner = ctjs.unary plus %one\n",
                                  chain + "  %inner = ctjs.unary plus " + previous + "\n");
        // The inner conversion and outer multiply add two more layers.
        if (depth == 63) {
            reject("deep invariant induction stops at its stack ceiling", body);
        } else {
            run({.what = "the last permitted invariant layer preserves child ownership",
                 .body = body,
                 .arrays = "a:[one,x]",
                 .reads = "a[0]=one; a[1]=x",
                 .exit = "x -> {x}"});
        }
    }
    for (const std::string literal : {"#ctjs.boolean<true>", "#ctjs.string<\"1\">"}) {
        for (const std::string unary : {"plus", "neg"}) {
            const auto source =
                replace(savedChild, "  %step = ctjs.binary_static add %i, %one",
                        "  %literal = ctjs.constant " + literal + "\n  %converted = ctjs.unary " +
                            unary + " %literal\n  %step = ctjs.binary " +
                            (unary == "neg" ? "sub" : "add") + " %i, %converted");
            run({.what = "original unary primitive latches preserve the returned child",
                 .body = source,
                 .arrays = "a:[one,x]",
                 .reads = "a[0]=one; a[1]=x",
                 .exit = "x -> {x}"});
            run({.what = "original unary primitive latches release only unreturned children",
                 .body = replace(source, "ctjs.return %result", "ctjs.return %zero"),
                 .arrays = "a:[one,x]",
                 .reads = "a[0]=one; a[1]=x",
                 .exit = "zero -> {}"},
                "x");
            reject("a repeated unary parameter cannot borrow literal conversion",
                   replace(source, unary + " %literal", unary + " %p"));
            reject("unary primitive latches retain final own-index bounds",
                   replace(source, "%base[%i]", "%base[%two]"),
                   ArrayContentsFailure::MissingElement);
        }
    }
    for (const std::string literal :
         {"#ctjs.number<0>", "#ctjs.boolean<false>", "#ctjs.null", "#ctjs.string<\"0\">",
          "#ctjs.number<13835058055282163712>", "#ctjs.string<\"-2\">",
          "#ctjs.number<4751297606871678976>", "#ctjs.string<\"4294967294\">"}) {
        static const llvm::StringSet<> zeroLiterals{"#ctjs.number<0>", "#ctjs.boolean<false>",
                                                    "#ctjs.null", "#ctjs.string<\"0\">"};
        const bool subtract = zeroLiterals.contains(literal);
        const std::string update = subtract ? "sub" : "add";
        const auto source =
            replace(savedChild, "  %step = ctjs.binary_static add %i, %one",
                    "  %literal = ctjs.constant " + literal +
                        "\n  %converted = ctjs.unary bitnot %literal\n  %step = ctjs.binary " +
                        update + " %i, %converted");
        run({.what = "original BitNot latches preserve the returned CFG child",
             .body = source,
             .arrays = "a:[one,x]",
             .reads = "a[0]=one; a[1]=x",
             .exit = "x -> {x}"});
        run({.what = "original BitNot latches discharge only unreturned CFG children",
             .body = replace(source, "ctjs.return %result", "ctjs.return %zero"),
             .arrays = "a:[one,x]",
             .reads = "a[0]=one; a[1]=x",
             .exit = "zero -> {}"},
            "x");
        reject("a BitNot latch cannot borrow an unknown operand's conversion",
               replace(source, "bitnot %literal", "bitnot %p"));
        run({.what = "nested BitNot proves its original literal conversion",
             .body = replace(source, "  %converted = ctjs.unary bitnot %literal",
                             "  %computed = ctjs.unary plus %literal\n"
                             "  %converted = ctjs.unary bitnot %computed"),
             .arrays = "a:[one,x]",
             .reads = "a[0]=one; a[1]=x",
             .exit = "x -> {x}"});
        reject("a BitNot latch still requires a positive exact update",
               replace(source, "binary " + update + " %i, %converted",
                       "binary " + std::string{subtract ? "add" : "sub"} + " %i, %converted"));
        reject("a BitNot latch retains own-element bounds",
               replace(source, "%base[%i]", "%base[%two]"), ArrayContentsFailure::MissingElement);
        for (const std::string refused : {"#ctjs.string<\"-1\">", "#ctjs.string<\"4294967295\">",
                                          "#ctjs.string<\"00\">", "#ctjs.string<\"4294967296\">",
                                          "#ctjs.number<4602678819172646912>", "#ctjs.undefined"}) {
            if (subtract && literal != "#ctjs.number<0>" &&
                (refused == "#ctjs.string<\"00\">" || refused == "#ctjs.string<\"4294967296\">" ||
                 refused == "#ctjs.number<4602678819172646912>")) {
                run({.what = "bounded decimal conversion preserves original reads and retained "
                             "children",
                     .body = replace(source, literal, refused),
                     .arrays = "a:[one,x]",
                     .reads = "a[0]=one; a[1]=x",
                     .exit = "x -> {x}"});
            } else {
                reject("a BitNot latch requires exact bounded nonzero literal conversion",
                       replace(source, literal, refused));
            }
        }
    }
}

} // namespace ctcompile::test::escape::arrays::induction_detail
