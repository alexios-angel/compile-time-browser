#include "Cases.hpp"

namespace ctcompile::test::escape::arrays::structured_detail {

void StructuredCases::signedArithmetic() {
    const auto carriedComplement = replace(carriedNegative, makeNegative,
                                           "  %magnitude = ctjs.unary plus %one\n"
                                           "  %unit = ctjs.unary bitnot %magnitude\n");
    rows.push_back({.what = "BitNot keeps its signed snapshot through reordered structured yields",
                    .body = carriedComplement,
                    .arrays = "a:[x,y]",
                    .reads = "a[0]=x",
                    .exit = "x -> {x}"});
    rows.push_back({.what = "structured BitNot snapshots release only unreturned children",
                    .body = replace(carriedComplement, "ctjs.return %result", "ctjs.return %zero"),
                    .arrays = "a:[x,y]",
                    .reads = "a[0]=x",
                    .exit = "zero -> {}"});
    const auto savedComplement =
        replace(savedNegative, "unary neg %magnitude", "unary bitnot %magnitude");
    rows.push_back({.what = "structured BitNot retains its source length before shrink",
                    .body = savedComplement,
                    .arrays = "a:[x,y]; seed:[]",
                    .reads = "a[0]=x",
                    .exit = "x -> {x}"});
    rows.push_back({.what = "structured BitNot predecessors preserve their own exact magnitude",
                    .body = replace(carriedComplement, "  %unit = ctjs.unary bitnot %magnitude\n",
                                    "  %unit = scf.if %flag -> (!ctjs.value) {\n"
                                    "    %twoStep = ctjs.unary bitnot %magnitude\n"
                                    "    scf.yield %twoStep : !ctjs.value\n"
                                    "  } else {\n"
                                    "    %oneStep = ctjs.unary bitnot %zero\n"
                                    "    scf.yield %oneStep : !ctjs.value\n  }\n"),
                    .arrays = "a:[x,y] | a:[x,y]",
                    .reads = "a[0]=x; a[0]=x; a[1]=y",
                    .exit = "x -> {x}; y -> {y}"});
    reject("a BitNot snapshot cannot change across structured yields",
           replace(carriedComplement, "%base, %step, %read, %d :", "%base, %step, %read, %one :"));
    const auto stringComplement =
        replace(carriedComplement, "  %magnitude = ctjs.unary plus %one\n",
                "  %text = ctjs.constant #ctjs.string<\"1\">\n"
                "  %magnitude = scf.if %flag -> (!ctjs.value) {\n"
                "    scf.yield %one : !ctjs.value\n"
                "  } else {\n    scf.yield %text : !ctjs.value\n  }\n");
    rows.push_back({.what = "BitNot preserves original Number and String predecessor snapshots",
                    .body = stringComplement,
                    .arrays = "a:[x,y] | a:[x,y]",
                    .reads = "a[0]=x; a[0]=x",
                    .exit = "x -> {x}; x -> {x}"});
    rows.push_back({.what = "String BitNot discharges only unreturned structured children",
                    .body = replace(stringComplement, "ctjs.return %result", "ctjs.return %zero"),
                    .arrays = "a:[x,y] | a:[x,y]",
                    .reads = "a[0]=x; a[0]=x",
                    .exit = "zero -> {}; zero -> {}"});
    rows.push_back(
        {.what = "bounded decimal conversion preserves original reads and retained children",
         .body = replace(stringComplement, "#ctjs.string<\"1\">", "#ctjs.string<\"01\">"),
         .arrays = "a:[x,y] | a:[x,y]",
         .reads = "a[0]=x; a[0]=x",
         .exit = "x -> {x}; x -> {x}"});
    reject("a String BitNot snapshot cannot change across structured yields",
           replace(stringComplement, "%base, %step, %read, %d :", "%base, %step, %read, %one :"));
    const auto primitiveComplement =
        replace(carriedComplement, "  %magnitude = ctjs.unary plus %one\n",
                "  %magnitude = scf.if %flag -> (!ctjs.value) {\n"
                "    %boolean = ctjs.constant #ctjs.boolean<true>\n"
                "    scf.yield %boolean : !ctjs.value\n"
                "  } else {\n"
                "    %null = ctjs.constant #ctjs.null\n"
                "    scf.yield %null : !ctjs.value\n  }\n");
    rows.push_back({.what = "Boolean/null unary results retain separate structured snapshots",
                    .body = primitiveComplement,
                    .arrays = "a:[x,y] | a:[x,y]",
                    .reads = "a[0]=x; a[0]=x; a[1]=y",
                    .exit = "x -> {x}; y -> {y}"});
    rows.push_back(
        {.what = "primitive unary snapshots release only unreturned structured children",
         .body = replace(primitiveComplement, "ctjs.return %result", "ctjs.return %zero"),
         .arrays = "a:[x,y] | a:[x,y]",
         .reads = "a[0]=x; a[0]=x; a[1]=y",
         .exit = "zero -> {}; zero -> {}"});
    reject(
        "a primitive unary result cannot change across structured yields",
        replace(primitiveComplement, "%base, %step, %read, %d :", "%base, %step, %read, %one :"));
    reject("an Undefined predecessor cannot borrow the Boolean/null Number proof",
           replace(primitiveComplement, "#ctjs.null", "#ctjs.undefined"));
    for (const std::string kind : {"bitand", "bitor", "bitxor", "shl", "shr"}) {
        const std::string right = kind == "bitand" ? "%operand" : "%zero";
        const std::string operation =
            "  %unit = ctjs.binary_static " + kind + " %operand, " + right + "\n";
        const auto carried = replace(carriedNegative, makeNegative,
                                     "  %operand = ctjs.unary neg %one\n" + operation);
        rows.push_back({.what = "signed bitwise snapshots survive reordered structured yields",
                        .body = carried,
                        .arrays = "a:[x,y]",
                        .reads = "a[0]=x; a[1]=y",
                        .exit = "y -> {y}"});
        const auto saved = replace(savedNegative, "  %unit = ctjs.unary neg %magnitude\n",
                                   "  %operand = ctjs.unary neg %magnitude\n" + operation);
        rows.push_back({.what = "structured bitwise snapshots survive source length shrink",
                        .body = saved,
                        .arrays = "a:[x,y]; seed:[]",
                        .reads = "a[0]=x; a[1]=y",
                        .exit = "y -> {y}"});
        rows.push_back({.what = "structured bitwise snapshots release only unreturned children",
                        .body = replace(saved, "ctjs.return %result", "ctjs.return %zero"),
                        .arrays = "a:[x,y]; seed:[]",
                        .reads = "a[0]=x; a[1]=y",
                        .exit = "zero -> {}"});
        reject("signed bitwise snapshots cannot change across structured yields",
               replace(carried, "%base, %step, %read, %d :", "%base, %step, %read, %one :"));
        const auto stringPredecessor =
            replace(carried, "  %operand = ctjs.unary neg %one\n",
                    "  %negative = ctjs.unary neg %one\n"
                    "  %text = ctjs.constant #ctjs.string<\"-1\">\n"
                    "  %operand = scf.if %flag -> (!ctjs.value) {\n"
                    "    scf.yield %negative : !ctjs.value\n"
                    "  } else {\n    scf.yield %text : !ctjs.value\n  }\n");
        rows.push_back({.what = "bitwise proves each Number and negative String predecessor",
                        .body = stringPredecessor,
                        .arrays = "a:[x,y] | a:[x,y]",
                        .reads = "a[0]=x; a[1]=y; a[0]=x; a[1]=y",
                        .exit = "y -> {y}; y -> {y}"});
        rows.push_back(
            {.what = "bounded decimal conversion preserves original reads and retained children",
             .body = replace(stringPredecessor, "#ctjs.string<\"-1\">", "#ctjs.string<\"-01\">"),
             .arrays = "a:[x,y] | a:[x,y]",
             .reads = "a[0]=x; a[1]=y; a[0]=x; a[1]=y",
             .exit = "y -> {y}; y -> {y}"});
    }
    const std::string primitiveBits = "  %operand = scf.if %flag -> (!ctjs.value) {\n"
                                      "    %truth = ctjs.constant #ctjs.boolean<true>\n"
                                      "    scf.yield %truth : !ctjs.value\n"
                                      "  } else {\n"
                                      "    %nil = ctjs.constant #ctjs.null\n"
                                      "    scf.yield %nil : !ctjs.value\n  }\n"
                                      "  %unit = ctjs.binary_static bitor %operand, %one\n";
    const auto primitiveBitwise = replace(carriedUnit, makeUnit, primitiveBits);
    rows.push_back({.what = "primitive bitwise proves each structured predecessor snapshot",
                    .body = primitiveBitwise,
                    .arrays = "a:[x,y] | a:[x,y]",
                    .reads = "a[0]=x; a[1]=y; a[0]=x; a[1]=y",
                    .exit = "y -> {y}; y -> {y}"});
    rows.push_back({.what = "primitive bitwise releases only unreturned structured children",
                    .body = replace(primitiveBitwise, "ctjs.return %result", "ctjs.return %zero"),
                    .arrays = "a:[x,y] | a:[x,y]",
                    .reads = "a[0]=x; a[1]=y; a[0]=x; a[1]=y",
                    .exit = "zero -> {}; zero -> {}"});
    reject("primitive bitwise snapshots cannot change across structured backedges",
           replace(primitiveBitwise, "%base, %step, %read, %d :", "%base, %step, %read, %zero :"));
    reject("primitive bitwise cannot borrow another predecessor's exact operand",
           replace(primitiveBitwise, "scf.yield %nil :", "scf.yield %p :"),
           ArrayContentsFailure::UnknownValue);
    reject("Undefined cannot borrow the structured primitive bitwise proof",
           replace(primitiveBitwise, "#ctjs.null", "#ctjs.undefined"));
    rows.push_back(
        {.what = "repeated structured bitwise producers preserve each original predecessor",
         .body = replace(replace(primitiveBitwise, "    %step =",
                                 "    %repeated = ctjs.binary_static bitor %operand, %one\n"
                                 "    %step ="),
                         "add %i, %d", "add %i, %repeated"),
         .arrays = "a:[x,y] | a:[x,y]",
         .reads = "a[0]=x; a[1]=y; a[0]=x; a[1]=y",
         .exit = "y -> {y}; y -> {y}"});
    const std::string unsignedOperation = "  %count = ctjs.unary neg %one\n"
                                          "  %unit = ctjs.binary_static ushr %operand, %count\n";
    const auto unsignedCarried =
        replace(carriedUnit, makeUnit, "  %operand = ctjs.unary neg %one\n" + unsignedOperation);
    const auto unsignedSaved = replace(savedUnit, "  %unit = ctjs.get_property %seed[%name]\n",
                                       "  %magnitude = ctjs.get_property %seed[%name]\n"
                                       "  %operand = ctjs.unary neg %magnitude\n" +
                                           unsignedOperation);
    for (const auto & source : {unsignedCarried, unsignedSaved}) {
        const char * arrays = source == unsignedCarried ? "a:[x,y]" : "a:[x,y]; seed:[]";
        rows.push_back({.what = "unsigned shifts preserve signed snapshots through structured flow",
                        .body = source,
                        .arrays = arrays,
                        .reads = "a[0]=x; a[1]=y",
                        .exit = "y -> {y}"});
        rows.push_back({.what = "structured unsigned shifts release only unreturned children",
                        .body = replace(source, "ctjs.return %result", "ctjs.return %zero"),
                        .arrays = arrays,
                        .reads = "a[0]=x; a[1]=y",
                        .exit = "zero -> {}"});
    }
    reject("an unsigned shift snapshot cannot change across structured yields",
           replace(unsignedCarried, "%base, %step, %read, %d :", "%base, %step, %read, %zero :"));
    const auto stringCount = replace(unsignedSaved, "  %count = ctjs.unary neg %one",
                                     "  %count = ctjs.constant #ctjs.string<\"31\">");
    rows.push_back(
        {.what = "String counts preserve signed snapshots after structured source shrink",
         .body = stringCount,
         .arrays = "a:[x,y]; seed:[]",
         .reads = "a[0]=x; a[1]=y",
         .exit = "y -> {y}"});
    rows.push_back({.what = "String shift snapshots release only unreturned structured children",
                    .body = replace(stringCount, "ctjs.return %result", "ctjs.return %zero"),
                    .arrays = "a:[x,y]; seed:[]",
                    .reads = "a[0]=x; a[1]=y",
                    .exit = "zero -> {}"});
    rows.push_back(
        {.what = "bounded decimal conversion preserves original reads and retained children",
         .body = replace(stringCount, "#ctjs.string<\"31\">", "#ctjs.string<\"031\">"),
         .arrays = "a:[x,y]; seed:[]",
         .reads = "a[0]=x; a[1]=y",
         .exit = "y -> {y}"});
    const auto subSnapshot =
        replace(savedNegative, "unary neg %magnitude", "binary sub %zero, %magnitude");
    rows.push_back(
        {.what = "structured Sub snapshots retain their child after source length shrink",
         .body = subSnapshot,
         .arrays = "a:[x,y]; seed:[]",
         .reads = "a[0]=x; a[1]=y",
         .exit = "y -> {y}"});
    rows.push_back({.what = "structured Sub snapshots discharge only unreturned children",
                    .body = replace(subSnapshot, "ctjs.return %result", "ctjs.return %zero"),
                    .arrays = "a:[x,y]; seed:[]",
                    .reads = "a[0]=x; a[1]=y",
                    .exit = "zero -> {}"});
    rows.push_back({.what = "zero-trip structured Sub snapshots preserve the original result",
                    .body = replace(replace(subSnapshot, "  ctjs.append %y to %a\n", ""),
                                    "%index = %zero", "%index = %one"),
                    .arrays = "a:[x]; seed:[]",
                    .exit = "zero -> {}"});
    const auto carriedSub =
        replace(carriedNegative, "unary neg %magnitude", "binary sub %one, %magnitude");
    const std::string primitiveSum = "  %negative = ctjs.unary neg %magnitude\n"
                                     "  %unit = ctjs.binary add %operand, %negative\n";
    const auto primitiveAdd = replace(carriedNegative, "  %unit = ctjs.unary neg %magnitude\n",
                                      "  %operand = scf.if %flag -> (!ctjs.value) {\n"
                                      "    %truth = ctjs.constant #ctjs.boolean<true>\n"
                                      "    scf.yield %truth : !ctjs.value\n"
                                      "  } else {\n    %nil = ctjs.constant #ctjs.null\n"
                                      "    scf.yield %nil : !ctjs.value\n  }\n" +
                                          primitiveSum);
    rows.push_back({.what = "primitive addition proves each structured predecessor snapshot",
                    .body = primitiveAdd,
                    .arrays = "a:[x,y] | a:[x,y]",
                    .reads = "a[0]=x; a[1]=y; a[0]=x",
                    .exit = "y -> {y}; x -> {x}"});
    reject("primitive addition cannot borrow another predecessor's operand",
           replace(primitiveAdd, "scf.yield %nil :", "scf.yield %p :"),
           ArrayContentsFailure::UnsupportedOperation);
    reject("primitive addition excludes String concatenation on every predecessor",
           replace(primitiveAdd, "#ctjs.null", "#ctjs.string<\"0\">"));
    reject("primitive addition snapshots cannot change on structured backedges",
           replace(primitiveAdd, "%base, %step, %read, %d :", "%base, %step, %read, %zero :"));
    const std::string primitiveDifference = "  %unit = ctjs.binary sub %operand, %magnitude\n";
    const auto primitiveSub = replace(carriedNegative, "  %unit = ctjs.unary neg %magnitude\n",
                                      "  %operand = scf.if %flag -> (!ctjs.value) {\n"
                                      "    %truth = ctjs.constant #ctjs.boolean<true>\n"
                                      "    scf.yield %truth : !ctjs.value\n"
                                      "  } else {\n    %nil = ctjs.constant #ctjs.null\n"
                                      "    scf.yield %nil : !ctjs.value\n  }\n" +
                                          primitiveDifference);
    rows.push_back({.what = "primitive subtraction proves separate structured predecessor strides",
                    .body = primitiveSub,
                    .arrays = "a:[x,y] | a:[x,y]",
                    .reads = "a[0]=x; a[1]=y; a[0]=x",
                    .exit = "y -> {y}; x -> {x}"});
    rows.push_back({.what = "primitive subtraction releases only unreturned structured children",
                    .body = replace(primitiveSub, "ctjs.return %result", "ctjs.return %zero"),
                    .arrays = "a:[x,y] | a:[x,y]",
                    .reads = "a[0]=x; a[1]=y; a[0]=x",
                    .exit = "zero -> {}; zero -> {}"});
    reject("primitive subtraction snapshots cannot change on structured backedges",
           replace(primitiveSub, "%base, %step, %read, %d :", "%base, %step, %read, %zero :"));
    reject("primitive subtraction cannot borrow another predecessor's operand",
           replace(primitiveSub, "scf.yield %nil :", "scf.yield %p :"),
           ArrayContentsFailure::UnsupportedOperation);
    reject("Undefined cannot borrow structured primitive subtraction evidence",
           replace(primitiveSub, "#ctjs.null", "#ctjs.undefined"));
    rows.push_back({.what = "repeated structured subtraction proves each original predecessor",
                    .body = replace(replace(primitiveSub, "    %step =",
                                            replace(primitiveDifference, "%unit =", "%repeated =") +
                                                "    %step ="),
                                    "binary sub %i, %d", "binary sub %i, %repeated"),
                    .arrays = "a:[x,y] | a:[x,y]",
                    .reads = "a[0]=x; a[1]=y; a[0]=x",
                    .exit = "y -> {y}; x -> {x}"});
    rows.push_back({.what = "structured Sub snapshots preserve exact reordered transport",
                    .body = carriedSub,
                    .arrays = "a:[x,y]",
                    .reads = "a[0]=x; a[1]=y",
                    .exit = "y -> {y}"});
    reject("structured Sub snapshots cannot change on the backedge",
           replace(carriedSub, "%base, %step, %read, %d :", "%base, %step, %read, %one :"));
    reject("structured Add cannot borrow a negative Sub snapshot",
           replace(carriedSub, "binary sub %i, %d", "binary add %i, %d"));
    for (const std::string producer : {"binary sub %zero, %zero", "binary sub %magnitude, %zero"}) {
        reject("structured Sub snapshots preserve the zero and positive stride refusals",
               replace(subSnapshot, "binary sub %zero, %magnitude", producer));
    }
    const auto stringSub =
        replace(carriedSub, "binary add %one, %one", "constant #ctjs.string<\"2\">");
    rows.push_back({.what = "canonical String offsets survive reordered structured transport",
                    .body = stringSub,
                    .arrays = "a:[x,y]",
                    .reads = "a[0]=x; a[1]=y",
                    .exit = "y -> {y}"});
    const auto leftStringSub = replace(carriedSub, "  %unit = ctjs.binary sub %one, %magnitude",
                                       "  %left = ctjs.constant #ctjs.string<\"1\">\n"
                                       "  %unit = ctjs.binary sub %left, %magnitude");
    rows.push_back({.what = "left String subtraction survives reordered structured transport",
                    .body = leftStringSub,
                    .arrays = "a:[x,y]",
                    .reads = "a[0]=x; a[1]=y",
                    .exit = "y -> {y}"});
    rows.push_back({.what = "structured left String subtraction releases only unreturned children",
                    .body = replace(leftStringSub, "ctjs.return %result", "ctjs.return %zero"),
                    .arrays = "a:[x,y]",
                    .reads = "a[0]=x; a[1]=y",
                    .exit = "zero -> {}"});
    reject("left String subtraction snapshots cannot change across structured yields",
           replace(leftStringSub, "%base, %step, %read, %d :", "%base, %step, %read, %one :"));
    const auto leftPredecessors =
        replace(leftStringSub, "  %left = ctjs.constant #ctjs.string<\"1\">\n",
                "  %left = scf.if %flag -> (!ctjs.value) {\n"
                "    %good = ctjs.constant #ctjs.string<\"1\">\n"
                "    scf.yield %good : !ctjs.value\n"
                "  } else {\n    scf.yield %one : !ctjs.value\n  }\n");
    rows.push_back({.what = "left subtraction proves String and Number predecessors independently",
                    .body = leftPredecessors,
                    .arrays = "a:[x,y] | a:[x,y]",
                    .reads = "a[0]=x; a[1]=y; a[0]=x; a[1]=y",
                    .exit = "y -> {y}; y -> {y}"});
    rows.push_back(
        {.what = "bounded decimal conversion preserves original reads and retained children",
         .body = replace(leftPredecessors, "scf.yield %one : !ctjs.value",
                         "%bad = ctjs.constant #ctjs.string<\"01\">\n"
                         "    scf.yield %bad : !ctjs.value"),
         .arrays = "a:[x,y] | a:[x,y]",
         .reads = "a[0]=x; a[1]=y; a[0]=x; a[1]=y",
         .exit = "y -> {y}; y -> {y}"});
    const auto savedStringSub = replace(subSnapshot, "  %unit = ctjs.binary sub %zero, %magnitude",
                                        "  %offset = ctjs.constant #ctjs.string<\"2\">\n"
                                        "  %unit = ctjs.binary sub %magnitude, %offset");
    rows.push_back({.what = "structured String-offset snapshots survive source length shrink",
                    .body = savedStringSub,
                    .arrays = "a:[x,y]; seed:[]",
                    .reads = "a[0]=x; a[1]=y",
                    .exit = "y -> {y}"});
    rows.push_back({.what = "structured String offsets release only unreturned children",
                    .body = replace(savedStringSub, "ctjs.return %result", "ctjs.return %zero"),
                    .arrays = "a:[x,y]; seed:[]",
                    .reads = "a[0]=x; a[1]=y",
                    .exit = "zero -> {}"});
    reject("a String-offset snapshot cannot change across structured yields",
           replace(stringSub, "%base, %step, %read, %d :", "%base, %step, %read, %one :"));
    rows.push_back(
        {.what = "bounded decimal conversion preserves original reads and retained children",
         .body = replace(stringSub, "  %magnitude = ctjs.constant #ctjs.string<\"2\">\n",
                         "  %magnitude = scf.if %flag -> (!ctjs.value) {\n"
                         "    %good = ctjs.constant #ctjs.string<\"2\">\n"
                         "    scf.yield %good : !ctjs.value\n"
                         "  } else {\n"
                         "    %bad = ctjs.constant #ctjs.string<\"02\">\n"
                         "    scf.yield %bad : !ctjs.value\n  }\n"),
         .arrays = "a:[x,y] | a:[x,y]",
         .reads = "a[0]=x; a[1]=y; a[0]=x; a[1]=y",
         .exit = "y -> {y}; y -> {y}"});
    reject("structured negative Sub snapshots cannot be own indices",
           replace(subSnapshot, "%base[%i]", "%base[%unit]"), ArrayContentsFailure::UnknownIndex);
    rows.push_back(
        {.what = "repeated structured subtraction retains the saved length snapshot",
         .body =
             replace(replace(subSnapshot, "  %unit = ctjs.binary sub %zero, %magnitude\n", ""),
                     "    %step =", "    %unit = ctjs.binary sub %zero, %magnitude\n    %step ="),
         .arrays = "a:[x,y]; seed:[]",
         .reads = "a[0]=x; a[1]=y",
         .exit = "y -> {y}"});
    reject("structured negative Sub snapshots still bound the last exact update",
           replace(replace(replace(carriedSub, "binary sub %one, %magnitude",
                                   "binary sub %zero, %magnitude"),
                           "binary add %one, %one", "constant #ctjs.number<4751297606873776128>"),
                   "%index = %zero", "%index = %one"));
    const std::string signedDifference = "  %negative = ctjs.unary neg %magnitude\n"
                                         "  %other = ctjs.unary neg %one\n"
                                         "  %unit = ctjs.binary sub %negative, %other\n";
    const auto negativeLeft =
        replace(carriedNegative, "  %unit = ctjs.unary neg %magnitude\n", signedDifference);
    for (const auto & source : {negativeLeft, replace(replace(negativeLeft, "sub %negative, %other",
                                                              "sub %other, %negative"),
                                                      "binary sub %i, %d", "binary add %i, %d")}) {
        rows.push_back({.what = "negative-left Sub survives reordered structured transport",
                        .body = source,
                        .arrays = "a:[x,y]",
                        .reads = "a[0]=x; a[1]=y",
                        .exit = "y -> {y}"});
        rows.push_back({.what = "structured negative-left Sub releases unreturned children",
                        .body = replace(source, "ctjs.return %result", "ctjs.return %zero"),
                        .arrays = "a:[x,y]",
                        .reads = "a[0]=x; a[1]=y",
                        .exit = "zero -> {}"});
    }
    reject("negative-left Sub snapshots cannot change across structured yields",
           replace(negativeLeft, "%base, %step, %read, %d :", "%base, %step, %read, %one :"));
    rows.push_back({.what = "negative-left String offsets retain their structured result",
                    .body = replace(negativeLeft, "unary neg %one", "constant #ctjs.string<\"1\">"),
                    .arrays = "a:[x,y]",
                    .reads = "a[0]=x",
                    .exit = "x -> {x}"});
    reject("negative-left Sub needs exact Numbers on every structured predecessor",
           replace(negativeLeft, "  %negative = ctjs.unary neg %magnitude\n",
                   "  %negative = scf.if %flag -> (!ctjs.value) {\n"
                   "    %n = ctjs.unary neg %magnitude\n"
                   "    scf.yield %n : !ctjs.value\n"
                   "  } else {\n    scf.yield %p : !ctjs.value\n  }\n"),
           ArrayContentsFailure::UnsupportedOperation);
    for (const std::string opcode : {"binary", "binary_static"}) {
        const std::string cancellation = "  %negative = ctjs.binary sub %zero, %one\n"
                                         "  %positive = ctjs.binary add %one, %one\n"
                                         "  %unit = ctjs." +
                                         opcode + " add %positive, %negative\n";
        const auto cancelled = replace(carriedUnit, makeUnit, cancellation);
        rows.push_back({.what = "Add cancellation survives reordered structured transport",
                        .body = cancelled,
                        .arrays = "a:[x,y]",
                        .reads = "a[0]=x; a[1]=y",
                        .exit = "y -> {y}"});
        rows.push_back({.what = "cancelled structured strides release unreturned children",
                        .body = replace(cancelled, "ctjs.return %result", "ctjs.return %zero"),
                        .arrays = "a:[x,y]",
                        .reads = "a[0]=x; a[1]=y",
                        .exit = "zero -> {}"});
        const auto zero = replace(cancelled, "add %positive, %negative", "add %one, %negative");
        rows.push_back({.what = "structured cancellation to zero remains a valid start",
                        .body = replace(replace(zero, "%index = %zero", "%index = %unit"),
                                        "add %i, %d", "add %i, %one"),
                        .arrays = "a:[x,y]",
                        .reads = "a[0]=x; a[1]=y",
                        .exit = "y -> {y}"});
        reject("structured cancellation to zero cannot certify progress", zero);
        reject("a cancelled structured stride must remain unchanged on the backedge",
               replace(cancelled, "%base, %step, %read, %d :", "%base, %step, %read, %one :"));
        reject("structured cancellation cannot borrow a coercible String",
               replace(cancelled, "binary sub %zero, %one", "constant #ctjs.string<\"-1\">"));
        reject("structured cancellation cannot borrow another arm's Number",
               replace(cancelled, "  %negative = ctjs.binary sub %zero, %one\n",
                       "  %negative = scf.if %flag -> (!ctjs.value) {\n"
                       "    %n = ctjs.binary sub %zero, %one\n"
                       "    scf.yield %n : !ctjs.value\n"
                       "  } else {\n    scf.yield %p : !ctjs.value\n  }\n"),
               opcode == "binary" ? ArrayContentsFailure::UnsupportedOperation
                                  : ArrayContentsFailure::UnknownValue);
    }
    for (const std::string opcode : {"binary", "binary_static"}) {
        const std::string negativeSum = "  %negative = ctjs.unary neg %magnitude\n"
                                        "  %unit = ctjs." +
                                        opcode + " add %negative, %one\n";
        const auto source =
            replace(carriedNegative, "  %unit = ctjs.unary neg %magnitude\n", negativeSum);
        rows.push_back({.what = "negative Add survives reordered structured transport",
                        .body = source,
                        .arrays = "a:[x,y]",
                        .reads = "a[0]=x; a[1]=y",
                        .exit = "y -> {y}"});
        rows.push_back({.what = "structured negative Add releases unreturned children",
                        .body = replace(source, "ctjs.return %result", "ctjs.return %zero"),
                        .arrays = "a:[x,y]",
                        .reads = "a[0]=x; a[1]=y",
                        .exit = "zero -> {}"});
        reject("negative Add snapshots cannot change across structured yields",
               replace(source, "%base, %step, %read, %d :", "%base, %step, %read, %one :"));
        reject("negative Add needs an exact Number on every structured predecessor",
               replace(source, "  %negative = ctjs.unary neg %magnitude\n",
                       "  %negative = scf.if %flag -> (!ctjs.value) {\n"
                       "    %n = ctjs.unary neg %magnitude\n"
                       "    scf.yield %n : !ctjs.value\n"
                       "  } else {\n    scf.yield %p : !ctjs.value\n  }\n"),
               opcode == "binary" ? ArrayContentsFailure::UnsupportedOperation
                                  : ArrayContentsFailure::UnknownValue);
    }
    for (const std::string operation : {"div", "mod", "pow"}) {
        const std::string makeResult =
            "  %unit = ctjs.binary " + operation +
            (operation != "mod" ? " %negative, %one\n" : " %negative, %two\n");
        const auto source = replace(replace(savedNegative, "  %unit = ctjs.unary neg %magnitude\n",
                                            "  %negative = ctjs.unary neg %magnitude\n"),
                                    "  ctjs.set_property %seed[%name], %zero\n",
                                    "  ctjs.set_property %seed[%name], %zero\n"
                                    "  %two = ctjs.binary add %one, %one\n" +
                                        makeResult);
        rows.push_back(
            {.what = "structured signed division, remainder and power retain pre-shrink Numbers",
             .body = source,
             .arrays = "a:[x,y]; seed:[]",
             .reads = "a[0]=x; a[1]=y",
             .exit = "y -> {y}"});
        rows.push_back(
            {.what = "structured signed division, remainder and power release unreturned children",
             .body = replace(source, "ctjs.return %result", "ctjs.return %zero"),
             .arrays = "a:[x,y]; seed:[]",
             .reads = "a[0]=x; a[1]=y",
             .exit = "zero -> {}"});
        const auto repeated =
            replace(replace(source, makeResult, ""), "    %step =", makeResult + "    %step =");
        rows.push_back({.what = "repeated structured division and power prove invariant operands",
                        .body = repeated,
                        .arrays = "a:[x,y]; seed:[]",
                        .reads = "a[0]=x; a[1]=y",
                        .exit = "y -> {y}"});
    }
    for (const std::string operation : {"div", "mod", "pow"}) {
        const std::string literal = "  %text = ctjs.constant #ctjs.string<\"" +
                                    std::string(operation == "mod" ? "2" : "1") + "\">\n";
        const std::string makeResult = "  %negative = ctjs.unary neg %one\n" + literal +
                                       "  %unit = ctjs.binary " + operation + " %negative, %text\n";
        const auto stringRight =
            replace(carriedNegative, "  %unit = ctjs.unary neg %magnitude\n", makeResult);
        const auto stringLeft = replace(
            replace(replace(stringRight, literal, "  %text = ctjs.constant #ctjs.string<\"1\">\n"),
                    "%negative, %text", operation == "div" ? "%text, %one" : "%text, %magnitude"),
            "binary sub %i, %d", "binary add %i, %d");
        for (const auto & body : {stringRight, stringLeft}) {
            rows.push_back({.what = "String arithmetic survives structured transport",
                            .body = body,
                            .arrays = "a:[x,y]",
                            .reads = "a[0]=x; a[1]=y",
                            .exit = "y -> {y}"});
            rows.push_back({.what = "structured String arithmetic releases unreturned children",
                            .body = replace(body, "ctjs.return %result", "ctjs.return %zero"),
                            .arrays = "a:[x,y]",
                            .reads = "a[0]=x; a[1]=y",
                            .exit = "zero -> {}"});
            reject("a String arithmetic snapshot cannot change on the structured backedge",
                   replace(body, "%base, %step, %read, %d :", "%base, %step, %read, %zero :"));
        }
        if (operation != "mod") {
            rows.push_back(
                {.what =
                     "bounded decimal conversion preserves original reads and retained children",
                 .body = replace(stringRight, literal,
                                 "  %text = scf.if %flag -> (!ctjs.value) {\n" +
                                     replace(literal, "%text =", "%good =") +
                                     "    scf.yield %good : !ctjs.value\n"
                                     "  } else {\n"
                                     "    %bad = ctjs.constant #ctjs.string<\"01\">\n"
                                     "    scf.yield %bad : !ctjs.value\n  }\n"),
                 .arrays = "a:[x,y] | a:[x,y]",
                 .reads = "a[0]=x; a[1]=y; a[0]=x; a[1]=y",
                 .exit = "y -> {y}; y -> {y}"});
        } else {
            reject("String arithmetic cannot borrow a canonical value from another predecessor",
                   replace(stringRight, literal,
                           "  %text = scf.if %flag -> (!ctjs.value) {\n" +
                               replace(literal, "%text =", "%good =") +
                               "    scf.yield %good : !ctjs.value\n"
                               "  } else {\n"
                               "    %bad = ctjs.constant #ctjs.string<\"01\">\n"
                               "    scf.yield %bad : !ctjs.value\n  }\n"));
        }
    }
    for (const std::string operation : {"div", "mod"}) {
        const std::string makeResult =
            "  %unit = ctjs.binary " + operation +
            (operation == "div" ? " %negative, %operand\n" : " %operand, %magnitude\n");
        auto source = replace(carriedNegative, "  %unit = ctjs.unary neg %magnitude\n",
                              "  %negative = ctjs.unary neg %one\n"
                              "  %operand = scf.if %flag -> (!ctjs.value) {\n"
                              "    %truth = ctjs.constant #ctjs.boolean<true>\n"
                              "    scf.yield %truth : !ctjs.value\n"
                              "  } else {\n    scf.yield %one : !ctjs.value\n  }\n" +
                                  makeResult);
        if (operation == "mod") {
            source = replace(source, "binary sub %i, %d", "binary add %i, %d");
        }
        rows.push_back({.what = "primitive division proves each structured predecessor snapshot",
                        .body = source,
                        .arrays = "a:[x,y] | a:[x,y]",
                        .reads = "a[0]=x; a[1]=y; a[0]=x; a[1]=y",
                        .exit = "y -> {y}; y -> {y}"});
        rows.push_back({.what = "primitive division releases only unreturned structured children",
                        .body = replace(source, "ctjs.return %result", "ctjs.return %zero"),
                        .arrays = "a:[x,y] | a:[x,y]",
                        .reads = "a[0]=x; a[1]=y; a[0]=x; a[1]=y",
                        .exit = "zero -> {}; zero -> {}"});
        reject("primitive division snapshots cannot change on structured backedges",
               replace(source, "%base, %step, %read, %d :", "%base, %step, %read, %zero :"));
        reject("primitive division cannot borrow another predecessor's exact operand",
               replace(source, "scf.yield %one :", "scf.yield %p :"),
               ArrayContentsFailure::UnsupportedOperation);
        rows.push_back(
            {.what = "repeated structured primitive division proves each operand snapshot",
             .body = replace(replace(source, "    %step =",
                                     replace(makeResult, "%unit =", "%repeated =") + "    %step ="),
                             operation == "div" ? "binary sub %i, %d" : "binary add %i, %d",
                             operation == "div" ? "binary sub %i, %repeated"
                                                : "binary add %i, %repeated"),
             .arrays = "a:[x,y] | a:[x,y]",
             .reads = "a[0]=x; a[1]=y; a[0]=x; a[1]=y",
             .exit = "y -> {y}; y -> {y}"});
        const auto zero = replace(
            replace(replace(replace(source, "#ctjs.boolean<true>", "#ctjs.boolean<false>"),
                            "scf.yield %one :",
                            "%nil = ctjs.constant #ctjs.null\n    scf.yield %nil :"),
                    makeResult, "  %unit = ctjs.binary " + operation + " %operand, %negative\n"),
            "%index = %zero", "%index = %unit");
        rows.push_back(
            {.what = "false and null division preserve zero starts across structured joins",
             .body = replace(zero, operation == "div" ? "binary sub %i, %d" : "binary add %i, %d",
                             "binary add %i, %one"),
             .arrays = "a:[x,y] | a:[x,y]",
             .reads = "a[0]=x; a[1]=y; a[0]=x; a[1]=y",
             .exit = "y -> {y}; y -> {y}"});
    }
    for (const bool primitiveBase : {false, true}) {
        const std::string primitivePower =
            "  %unit = ctjs.binary pow " +
            std::string(primitiveBase ? "%operand, %negative" : "%negative, %operand") + "\n";
        auto source = replace(carriedNegative, "  %unit = ctjs.unary neg %magnitude\n",
                              "  %negative = ctjs.unary neg %one\n"
                              "  %operand = scf.if %flag -> (!ctjs.value) {\n"
                              "    %truth = ctjs.constant #ctjs.boolean<true>\n"
                              "    scf.yield %truth : !ctjs.value\n"
                              "  } else {\n    scf.yield %one : !ctjs.value\n  }\n" +
                                  primitivePower);
        if (primitiveBase) { source = replace(source, "binary sub %i, %d", "binary add %i, %d"); }
        rows.push_back({.what = "primitive powers prove each structured predecessor snapshot",
                        .body = source,
                        .arrays = "a:[x,y] | a:[x,y]",
                        .reads = "a[0]=x; a[1]=y; a[0]=x; a[1]=y",
                        .exit = "y -> {y}; y -> {y}"});
        rows.push_back({.what = "primitive powers release only unreturned structured children",
                        .body = replace(source, "ctjs.return %result", "ctjs.return %zero"),
                        .arrays = "a:[x,y] | a:[x,y]",
                        .reads = "a[0]=x; a[1]=y; a[0]=x; a[1]=y",
                        .exit = "zero -> {}; zero -> {}"});
        reject("primitive power snapshots cannot change across structured backedges",
               replace(source, "%base, %step, %read, %d :", "%base, %step, %read, %zero :"));
        reject("primitive powers cannot borrow another predecessor's exact operand",
               replace(source, "scf.yield %one :", "scf.yield %p :"),
               ArrayContentsFailure::UnsupportedOperation);
        rows.push_back(
            {.what = "repeated structured primitive powers prove each invariant operand snapshot",
             .body =
                 replace(replace(source, "    %step =",
                                 replace(primitivePower, "%unit =", "%repeated =") + "    %step ="),
                         primitiveBase ? "binary add %i, %d" : "binary sub %i, %d",
                         primitiveBase ? "binary add %i, %repeated" : "binary sub %i, %repeated"),
             .arrays = "a:[x,y] | a:[x,y]",
             .reads = "a[0]=x; a[1]=y; a[0]=x; a[1]=y",
             .exit = "y -> {y}; y -> {y}"});
        auto zero =
            replace(replace(source, "#ctjs.boolean<true>", "#ctjs.boolean<false>"),
                    "scf.yield %one :", "%nil = ctjs.constant #ctjs.null\n    scf.yield %nil :");
        if (primitiveBase) {
            zero = replace(replace(zero, "pow %operand, %negative", "pow %operand, %magnitude"),
                           "%index = %zero", "%index = %unit");
        }
        rows.push_back(
            {.what = "false/null powers retain exact starts and strides across joins",
             .body = replace(zero, primitiveBase ? "binary add %i, %d" : "binary sub %i, %d",
                             primitiveBase ? "binary add %i, %one" : "binary add %i, %d"),
             .arrays = "a:[x,y] | a:[x,y]",
             .reads = "a[0]=x; a[1]=y; a[0]=x; a[1]=y",
             .exit = "y -> {y}; y -> {y}"});
    }
}

} // namespace ctcompile::test::escape::arrays::structured_detail
