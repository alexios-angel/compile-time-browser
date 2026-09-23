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
        reject("table conversion retains exact primitive and own-index bounds",
               replace(convertedTable, "#ctjs.string<\"0\">", primitive));
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
        reject("product latches require exact nonzero primitive conversion",
               replace(invariantProduct, makeUnit, "  %unit = ctjs.constant " + literal + "\n"));
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
        reject("division latches cannot borrow noncanonical primitive conversion",
               replace(source, makeUnit, "  %unit = ctjs.constant #ctjs.string<\"01\">\n"));
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
            reject("invariant Sub refuses noncanonical original Strings",
                   replace(string, "#ctjs.string<\"1\">", "#ctjs.string<\"01\">"));
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
        reject("bitwise latches refuse noncanonical String conversion",
               replace(source, makeUnit, "  %unit = ctjs.constant #ctjs.string<\"01\">\n"));
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
        reject("power latches cannot borrow unsupported primitive conversions",
               replace(invariantPower, makeUnit, "  %unit = ctjs.constant " + literal + "\n"));
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
            reject("a BitNot latch requires exact bounded nonzero literal conversion",
                   replace(source, literal, refused));
        }
    }
}

} // namespace ctcompile::test::escape::arrays::induction_detail
