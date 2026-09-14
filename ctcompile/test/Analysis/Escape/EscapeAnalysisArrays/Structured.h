#pragma once

#include "Harness.h"

namespace ctcompile::test::escape::arrays {

inline void checkStructuredContents(mlir::MLIRContext & context) {
    const std::string values =
        "  %zero = ctjs.constant #ctjs.number<0> {storage_test_id = \"zero\"}\n"
        "  %one = ctjs.constant #ctjs.number<4607182418800017408> {storage_test_id = \"one\"}\n"
        "  %x = ctjs.create_object {storage_test_id = \"x\"}\n"
        "  %y = ctjs.create_object {storage_test_id = \"y\"}\n"
        "  %a = ctjs.create_array [%x] {storage_test_id = \"a\"}\n"
        "  %flag = ctjs.truthy %p\n";
    const std::string select = "  %selected = scf.if %flag -> (!ctjs.value) {\n"
                               "    scf.yield %a : !ctjs.value\n"
                               "  } else {\n"
                               "    scf.yield %b : !ctjs.value\n"
                               "  }\n";
    const std::string done = "  ctjs.return %zero\n";
    const std::vector<contents_row> rows = {
        {.what = "structured aliases retain separate strong-update targets",
         .body = values +
                 "  %b = ctjs.create_array [%y] {storage_test_id = \"b\"}\n"
                 "  %c = ctjs.create_array [%a, %b] {storage_test_id = \"c\"}\n" +
                 select + "  ctjs.set_property %selected[%zero], %zero\n  ctjs.return %c\n",
         .arrays = "a:[zero]; b:[y]; c:[a,b] | a:[x]; b:[zero]; c:[a,b]",
         .exit = "c -> {a,b,c,y}; c -> {a,b,c,x}"},
        {.what = "structured yields carry exact original Number facts into CFG operands",
         .body = values + "  %index = scf.if %flag -> (!ctjs.value) {\n"
                          "    %sum = ctjs.binary add %zero, %zero\n"
                          "    scf.yield %sum : !ctjs.value\n"
                          "  } else {\n"
                          "    scf.yield %zero : !ctjs.value\n"
                          "  }\n"
                          "  cf.br ^next(%a, %index : !ctjs.value, !ctjs.value)\n"
                          "^next(%base: !ctjs.value, %key: !ctjs.value):\n"
                          "  %read = ctjs.get_property %base[%key]\n  ctjs.return %read\n",
         .arrays = "a:[x] | a:[x]",
         .reads = "a[0]=x; a[0]=x",
         .exit = "x -> {x}; x -> {x}"},
        {.what = "structured scalar snapshots survive mutation of their source array",
         .body = values + "  %key = ctjs.constant #ctjs.string<\"length\">\n"
                          "  %saved = scf.if %flag -> (!ctjs.value) {\n"
                          "    %length = ctjs.get_property %a[%key]\n"
                          "    ctjs.append %y to %a\n"
                          "    scf.yield %length : !ctjs.value\n"
                          "  } else {\n"
                          "    ctjs.append %y to %a\n"
                          "    scf.yield %one : !ctjs.value\n"
                          "  }\n"
                          "  %read = ctjs.get_property %a[%saved]\n  ctjs.return %read\n",
         .arrays = "a:[x,y] | a:[x,y]",
         .reads = "a[1]=y; a[1]=y",
         .exit = "y -> {y}; y -> {y}"},
        {.what = "all structured results are transported together",
         .body = values + "  %left, %right = scf.if %flag -> (!ctjs.value, !ctjs.value) {\n"
                          "    scf.yield %x, %y : !ctjs.value, !ctjs.value\n"
                          "  } else {\n"
                          "    scf.yield %y, %x : !ctjs.value, !ctjs.value\n"
                          "  }\n"
                          "  ctjs.set_property %a[%zero], %left\n"
                          "  ctjs.append %right to %a\n  ctjs.return %a\n",
         .arrays = "a:[x,y] | a:[y,x]",
         .exit = "a -> {a,x,y}; a -> {a,x,y}"},
        {.what = "an implicit empty else retains the unmodified array",
         .body = values + "  scf.if %flag {\n    ctjs.set_property %a[%zero], %y\n  }\n"
                          "  ctjs.return %a\n",
         .arrays = "a:[y] | a:[x]",
         .exit = "a -> {a,y}; a -> {a,x}"},
        {.what = "nested structured aliases resume the correct enclosing yield",
         .body = values + "  %outer = scf.if %flag -> (!ctjs.value) {\n"
                          "    %inner = scf.if %flag -> (!ctjs.value) {\n"
                          "      scf.yield %x : !ctjs.value\n"
                          "    } else {\n"
                          "      scf.yield %y : !ctjs.value\n"
                          "    }\n    scf.yield %inner : !ctjs.value\n"
                          "  } else {\n    scf.yield %zero : !ctjs.value\n  }\n"
                          "  ctjs.return %outer\n",
         .arrays = "a:[x] | a:[x] | a:[x]",
         .exit = "x -> {x}; y -> {y}; zero -> {}"},
        {.what = "unused opaque structured results do not become retention facts",
         .body = values +
                 "  %unused = scf.if %flag -> (!ctjs.value) {\n"
                 "    scf.yield %a : !ctjs.value\n"
                 "  } else {\n    scf.yield %p : !ctjs.value\n  }\n" +
                 done,
         .arrays = "a:[x] | a:[x]",
         .exit = "zero -> {}; zero -> {}"},
        {.what = "an opaque structured result cannot borrow the other arm's array",
         .body = values + "  %base = scf.if %flag -> (!ctjs.value) {\n"
                          "    scf.yield %a : !ctjs.value\n"
                          "  } else {\n    scf.yield %p : !ctjs.value\n  }\n"
                          "  %read = ctjs.get_property %base[%zero]\n  ctjs.return %read\n",
         .failure = ArrayContentsFailure::UnknownArray},
        {.what = "an unmodified structured arm cannot borrow the other arm's appended slot",
         .body = values + "  scf.if %flag {\n    ctjs.append %y to %a\n  }\n"
                          "  %read = ctjs.get_property %a[%one]\n  ctjs.return %read\n",
         .failure = ArrayContentsFailure::MissingElement},
        {.what = "a constant structured condition does not conceal publication in its other arm",
         .body = values +
                 "  %known = ctjs.truthy %one\n"
                 "  scf.if %known {\n  } else {\n    ctjs.store_global \"held\", %a\n  }\n" +
                 done,
         .failure = ArrayContentsFailure::UnsupportedOperation},
        {.what = "a failure after structured yields discards every earlier path",
         .body = values +
                 "  scf.if %flag {\n    ctjs.set_property %a[%zero], %y\n  }\n"
                 "  ctjs.store_global \"held\", %a\n" +
                 done,
         .failure = ArrayContentsFailure::UnsupportedOperation},
        {.what = "structured loops remain outside the single-pass alias proof",
         .body = values +
                 "  %loop = scf.while (%before = %a) : (!ctjs.value) -> !ctjs.value {\n"
                 "    scf.condition(%flag) %before : !ctjs.value\n"
                 "  } do {\n  ^body(%after: !ctjs.value):\n"
                 "    scf.yield %after : !ctjs.value\n  }\n" +
                 done,
         .failure = ArrayContentsFailure::UnsupportedControlFlow},
    };
    std::size_t budgets = 0;
    for (const auto & expected : rows) {
        auto module = mlir::parseSourceString<mlir::ModuleOp>(
            std::string{kPrologue} + expected.body + "}\n", &context);
        if (!module) {
            fail(row{.what = expected.what, .body = expected.body, .expected = ""},
                 "the structured contents fixture did not parse");
            continue;
        }
        checkArrayContents(*module, expected);
        budgets += computeArrayContents(*module->getOps<ctjs::FuncOp>().begin()).work;
    }
    std::printf("structured contents: %zu rows, %zu contents budget cutoffs\n", rows.size(),
                budgets);
}

} // namespace ctcompile::test::escape::arrays
