#pragma once

//===- DOMObservedState.hpp - Frozen pre-normalization IR fixture ---------===//
//
// Holds a frozen copy of imported IR used as input by DOMURI.cpp.
//
//===----------------------------------------------------------------------===//

namespace ctcompile::test::exception_recovery {

/// A frozen copy of the customElements iterator getter as it looked before
/// completion normalization ran: an invoke of `next` whose saved state still
/// holds ub.poison slots. DOMURI.cpp parses it to mutate those inactive slots
/// and check that normalization's poison and budget checks still reject the
/// result. Do not regenerate it from current output, which is already
/// normalized.
inline constexpr char observedIteratorState[] = R"MLIR(
ctjs.func private @customElements$1(%arg0: !ctjs.value, %arg1: !ctjs.value, %arg2: !ctjs.value, %arg3: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
  %0 = ctjs.constant #ctjs.boolean<false>
  %1 = ub.poison : !ctjs.value
  %2 = ctjs.frame_enter 15
  %3 = ctjs.constant #ctjs.undefined
  %4 = ctjs.constant #ctjs.undefined
  %5 = ctjs.constant #ctjs.undefined
  %6 = ctjs.constant #ctjs.undefined
  %7 = ctjs.constant #ctjs.undefined
  %8 = ctjs.constant #ctjs.undefined
  %9 = ctjs.constant #ctjs.undefined
  %10 = ctjs.constant #ctjs.undefined
  %11 = ctjs.constant #ctjs.undefined
  %12 = ctjs.constant #ctjs.undefined
  %13 = ctjs.constant #ctjs.undefined
  %14 = ctjs.constant #ctjs.undefined
  %15 = ctjs.constant #ctjs.undefined
  %16 = ctjs.constant #ctjs.undefined
  %17 = ctjs.create_cell %arg3
  %18 = ctjs.constant #ctjs.undefined
  %19 = ctjs.create_object
  %20 = ctjs.constant #ctjs.undefined
  %21 = ctjs.load_global "Symbol"
  %22 = ctjs.constant #ctjs.string<"iterator">
  %23 = ctjs.get_property %21[%22]
  %24 = ctjs.constant #ctjs.undefined
  %25 = ctjs.create_closure %arg2[3] this %24 captures %17
  %26 = ctjs.constant #ctjs.string<"next">
  ctjs.set_property %19[%26], %25
  %27 = ctjs.constant #ctjs.undefined
  %28 = ctjs.create_closure %arg2[4] this %27 captures %17
  %29 = ctjs.constant #ctjs.undefined
  %30 = ctjs.constant #ctjs.string<"return">
  ctjs.set_property %19[%30], %28
  %31 = ctjs.try {
    %c2_i32 = arith.constant 2 : i32
    %32 = ub.poison : i32
    %c1_i32 = arith.constant 1 : i32
    %33 = ub.poison : !ctjs.value
    %34 = ub.poison : i1
    %c0_i32 = arith.constant 0 : i32
    %35 = ctjs.load_global "__ctbrowser_for_of_open"
    %36 = ctjs.constant #ctjs.undefined
    %37 = ctjs.constant #ctjs.boolean<false>
    %38 = ub.poison : !ctjs.value
    %false = arith.constant false
    %39 = ub.poison : !ctjs.value
    %40 = ub.poison : !ctjs.value
    %41 = ctjs.constant #ctjs.undefined
    %42 = ctjs.constant #ctjs.boolean<false>
    %false_0 = arith.constant false
    %43 = ctjs.constant #ctjs.number<0>
    %44 = ctjs.constant #ctjs.number<4607182418800017408>
    %45 = ctjs.constant #ctjs.undefined
    %46 = ctjs.constant #ctjs.boolean<false>
    %false_1 = arith.constant false
    %47 = ub.poison : i1
    %48 = ub.poison : !ctjs.value
    %49 = ub.poison : !ctjs.value
    %50 = ub.poison : !ctjs.value
    %51 = ctjs.load_global "__ctbrowser_iter_next"
    %52 = ctjs.constant #ctjs.undefined
    %53 = ctjs.constant #ctjs.boolean<true>
    %54 = ctjs.constant #ctjs.string<"next">
    %55 = ctjs.get_property %19[%54]
    %56:19 = ctjs.invoke {
      %85 = ctjs.call %55(%19)
      ctjs.invoke_exit %85 state(%17, %19, %19, %19, %1, %1, %43, %44, %10, %11, %51, %1, %14, %15, %16)
    } normal {
    ^bb0(%arg4: !ctjs.value):
      %85 = ctjs.constant #ctjs.string<"done">
      %86 = ctjs.get_property %arg4[%85]
      %87 = ctjs.constant #ctjs.string<"value">
      %88 = ctjs.get_property %arg4[%87]
      %89 = ctjs.constant #ctjs.boolean<false>
      %90 = ub.poison : !ctjs.value
      ctjs.invoke_yield(%89, %88, %90, %90, %90, %90, %90, %90, %90, %90, %90, %90, %90, %90, %90, %90, %90, %90, %86)
    } unwind {
    ^bb0(%arg4: !ctjs.value, %arg5: !ctjs.value, %arg6: !ctjs.value, %arg7: !ctjs.value, %arg8: !ctjs.value, %arg9: !ctjs.value, %arg10: !ctjs.value, %arg11: !ctjs.value, %arg12: !ctjs.value, %arg13: !ctjs.value, %arg14: !ctjs.value, %arg15: !ctjs.value, %arg16: !ctjs.value, %arg17: !ctjs.value, %arg18: !ctjs.value, %arg19: !ctjs.value):
      %85 = ctjs.constant #ctjs.boolean<true>
      %86 = ub.poison : !ctjs.value
      ctjs.invoke_yield(%85, %86, %arg4, %arg5, %arg6, %arg7, %arg8, %arg9, %arg10, %arg11, %arg12, %arg13, %arg14, %arg15, %arg16, %arg17, %arg18, %arg19, %53)
    } : !ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value
    %57 = ctjs.truthy %56#0
    %58 = ub.poison : i1
    %59 = ub.poison : !ctjs.value
    %60 = ub.poison : !ctjs.value
    %61 = ub.poison : !ctjs.value
    %62 = ub.poison : !ctjs.value
    %63 = ub.poison : !ctjs.value
    %64 = ub.poison : !ctjs.value
    %65 = ub.poison : !ctjs.value
    %66 = ub.poison : !ctjs.value
    %67 = ub.poison : !ctjs.value
    %68 = ub.poison : !ctjs.value
    %69 = ub.poison : !ctjs.value
    %70 = ub.poison : !ctjs.value
    %71 = ub.poison : !ctjs.value
    %72 = ub.poison : !ctjs.value
    %73 = ub.poison : !ctjs.value
    %74 = ub.poison : !ctjs.value
    %75 = ub.poison : !ctjs.value
    %76:28 = scf.if %57 -> (!ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value, i32, i32, !ctjs.value) {
      scf.yield %33, %33, %33, %33, %33, %33, %33, %33, %33, %33, %33, %33, %33, %33, %33, %33, %33, %33, %33, %33, %33, %33, %33, %56#2, %56#3, %c0_i32, %c2_i32, %56#18 : !ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value, i32, i32, !ctjs.value
    } else {
      %85 = ctjs.constant #ctjs.string<"done">
      %86 = ctjs.truthy %56#18
      %87 = ub.poison : !ctjs.value
      %88 = ub.poison : !ctjs.value
      %89 = ub.poison : !ctjs.value
      %90 = ub.poison : !ctjs.value
      %91 = ub.poison : !ctjs.value
      %92 = ub.poison : !ctjs.value
      %93 = ub.poison : !ctjs.value
      %94 = ub.poison : !ctjs.value
      %95 = ub.poison : !ctjs.value
      %96 = ub.poison : !ctjs.value
      %97 = ub.poison : !ctjs.value
      %98 = ub.poison : !ctjs.value
      %99 = ub.poison : !ctjs.value
      %100 = ub.poison : !ctjs.value
      %101 = ub.poison : !ctjs.value
      %102 = ub.poison : !ctjs.value
      %103 = ub.poison : i1
      %104 = ub.poison : !ctjs.value
      %105 = ub.poison : !ctjs.value
      %106 = ub.poison : !ctjs.value
      %107 = ub.poison : !ctjs.value
      %108 = ub.poison : !ctjs.value
      %109 = ub.poison : !ctjs.value
      %110 = ub.poison : !ctjs.value
      %111 = ub.poison : !ctjs.value
      %112 = ub.poison : !ctjs.value
      %113 = ub.poison : !ctjs.value
      %114 = ub.poison : !ctjs.value
      %115 = ub.poison : !ctjs.value
      %116 = ub.poison : !ctjs.value
      %117 = ub.poison : !ctjs.value
      %118 = ub.poison : !ctjs.value
      %119 = ub.poison : !ctjs.value
      %120 = ub.poison : !ctjs.value
      %121 = ub.poison : i32
      %122:25 = scf.if %86 -> (!ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value, i32, !ctjs.value) {
        %123 = ctjs.constant #ctjs.boolean<true>
        scf.yield %17, %19, %19, %19, %1, %1, %43, %44, %56#1, %56#18, %15, %16, %33, %33, %33, %33, %33, %33, %33, %33, %33, %33, %33, %c0_i32, %123 : !ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value, i32, !ctjs.value
      } else {
        %123 = ctjs.constant #ctjs.boolean<false>
        scf.yield %33, %33, %33, %33, %33, %33, %33, %33, %33, %33, %33, %33, %17, %19, %19, %19, %1, %1, %43, %44, %56#1, %56#18, %1, %c1_i32, %123 : !ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value, i32, !ctjs.value
      }
      scf.yield %122#0, %122#1, %122#2, %122#3, %122#4, %122#5, %122#6, %122#7, %122#8, %122#9, %122#10, %122#11, %122#12, %122#13, %122#14, %122#15, %122#16, %122#17, %122#18, %122#19, %122#20, %122#21, %122#22, %87, %88, %121, %122#23, %122#24 : !ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value, i32, i32, !ctjs.value
    }
    %77 = arith.index_castui %76#26 : i32 to index
    %c0 = arith.constant 0 : index
    %78 = arith.cmpi eq, %77, %c0 : index
    %79 = ub.poison : !ctjs.value
    %80 = ub.poison : !ctjs.value
    %81:6 = scf.if %78 -> (!ctjs.value, !ctjs.value, i1, !ctjs.value, i32, !ctjs.value) {
      %85 = ctjs.load_global "__ctbrowser_iter_close"
      %86 = ctjs.constant #ctjs.boolean<false>
      %87 = ctjs.constant #ctjs.undefined
      %88 = ctjs.constant #ctjs.boolean<true>
      %89 = ctjs.truthy %76#27
      %90:18 = scf.if %89 -> (!ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value) {
        %109 = ctjs.constant #ctjs.undefined
        %110 = ctjs.constant #ctjs.boolean<false>
        %111 = ub.poison : !ctjs.value
        scf.yield %110, %109, %111, %111, %111, %111, %111, %111, %111, %111, %111, %111, %111, %111, %111, %111, %111, %111 : !ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value
      } else {
        %109 = ctjs.constant #ctjs.string<"return">
        %110 = ctjs.get_property %19[%109]
        %111:18 = ctjs.invoke {
          %112 = ctjs.call %110(%19)
          ctjs.invoke_exit %112 state(%76#0, %76#1, %76#2, %76#3, %76#4, %76#5, %76#6, %76#7, %76#8, %76#9, %85, %76#4, %86, %76#10, %76#11)
        } normal {
        ^bb0(%arg4: !ctjs.value):
          %112 = ctjs.constant #ctjs.boolean<true>
          %113 = ub.poison : !ctjs.value
          ctjs.invoke_yield(%112, %113, %arg4, %76#0, %76#1, %76#2, %76#3, %76#4, %76#5, %76#6, %76#7, %76#8, %76#9, %85, %76#4, %86, %76#10, %76#11)
        } unwind {
        ^bb0(%arg4: !ctjs.value, %arg5: !ctjs.value, %arg6: !ctjs.value, %arg7: !ctjs.value, %arg8: !ctjs.value, %arg9: !ctjs.value, %arg10: !ctjs.value, %arg11: !ctjs.value, %arg12: !ctjs.value, %arg13: !ctjs.value, %arg14: !ctjs.value, %arg15: !ctjs.value, %arg16: !ctjs.value, %arg17: !ctjs.value, %arg18: !ctjs.value, %arg19: !ctjs.value):
          %112 = ctjs.constant #ctjs.boolean<true>
          %113 = ub.poison : !ctjs.value
          ctjs.invoke_yield(%112, %113, %arg4, %arg5, %arg6, %arg7, %arg8, %arg9, %arg10, %arg11, %arg12, %arg13, %arg14, %arg15, %arg16, %arg17, %arg18, %arg19)
        } : !ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value
        scf.yield %111#0, %111#1, %111#2, %111#3, %111#4, %111#5, %111#6, %111#7, %111#8, %111#9, %111#10, %111#11, %111#12, %111#13, %111#14, %111#15, %111#16, %111#17 : !ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value
      }
      %91 = ctjs.truthy %90#0
      %92 = ub.poison : !ctjs.value
      %93 = ub.poison : !ctjs.value
      %94 = ub.poison : !ctjs.value
      %95 = ub.poison : !ctjs.value
      %96 = ub.poison : !ctjs.value
      %97 = ub.poison : !ctjs.value
      %98 = ub.poison : !ctjs.value
      %99 = ub.poison : !ctjs.value
      %100 = ub.poison : !ctjs.value
      %101 = ub.poison : !ctjs.value
      %102 = ub.poison : !ctjs.value
      %103 = ub.poison : !ctjs.value
      %104 = ub.poison : !ctjs.value
      %105 = ub.poison : !ctjs.value
      %106 = ub.poison : !ctjs.value
      %107 = ub.poison : !ctjs.value
      %108:6 = scf.if %91 -> (!ctjs.value, !ctjs.value, i1, !ctjs.value, i32, !ctjs.value) {
        scf.yield %90#2, %90#3, %34, %33, %c0_i32, %88 : !ctjs.value, !ctjs.value, i1, !ctjs.value, i32, !ctjs.value
      } else {
        %109 = ctjs.cell_get %76#0
        %110 = ctjs.constant #ctjs.string<"hasAttribute">
        %111 = ctjs.get_property %109[%110]
        %112 = ctjs.constant #ctjs.string<"data-visited">
        %113 = ctjs.call %111(%109, %112)
        %false_3 = arith.constant false
        %114 = ub.poison : !ctjs.value
        scf.yield %33, %33, %false_3, %113, %c1_i32, %88 : !ctjs.value, !ctjs.value, i1, !ctjs.value, i32, !ctjs.value
      }
      scf.yield %108#0, %108#1, %108#2, %108#3, %108#4, %108#5 : !ctjs.value, !ctjs.value, i1, !ctjs.value, i32, !ctjs.value
    } else {
      %c1 = arith.constant 1 : index
      %85 = arith.cmpi eq, %77, %c1 : index
      %86 = ub.poison : !ctjs.value
      %87 = ub.poison : !ctjs.value
      %88:6 = scf.if %85 -> (!ctjs.value, !ctjs.value, i1, !ctjs.value, i32, !ctjs.value) {
        %89 = ctjs.constant #ctjs.number<4607182418800017408>
        %90 = ctjs.load_global "__ctbrowser_iter_close"
        %91 = ctjs.constant #ctjs.boolean<false>
        %92 = ctjs.constant #ctjs.undefined
        %93 = ctjs.constant #ctjs.boolean<true>
        %94 = ctjs.truthy %76#27
        %95:18 = scf.if %94 -> (!ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value) {
          %114 = ctjs.constant #ctjs.undefined
          %115 = ctjs.constant #ctjs.boolean<false>
          %116 = ub.poison : !ctjs.value
          scf.yield %115, %114, %116, %116, %116, %116, %116, %116, %116, %116, %116, %116, %116, %116, %116, %116, %116, %116 : !ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value
        } else {
          %114 = ctjs.constant #ctjs.string<"return">
          %115 = ctjs.get_property %19[%114]
          %116:18 = ctjs.invoke {
            %117 = ctjs.call %115(%19)
            ctjs.invoke_exit %117 state(%76#12, %76#13, %76#14, %76#15, %76#16, %76#17, %76#18, %76#19, %76#20, %76#21, %89, %76#22, %90, %76#16, %91)
          } normal {
          ^bb0(%arg4: !ctjs.value):
            %117 = ctjs.constant #ctjs.boolean<true>
            %118 = ub.poison : !ctjs.value
            ctjs.invoke_yield(%117, %118, %arg4, %76#12, %76#13, %76#14, %76#15, %76#16, %76#17, %76#18, %76#19, %76#20, %76#21, %89, %76#22, %90, %76#16, %91)
          } unwind {
          ^bb0(%arg4: !ctjs.value, %arg5: !ctjs.value, %arg6: !ctjs.value, %arg7: !ctjs.value, %arg8: !ctjs.value, %arg9: !ctjs.value, %arg10: !ctjs.value, %arg11: !ctjs.value, %arg12: !ctjs.value, %arg13: !ctjs.value, %arg14: !ctjs.value, %arg15: !ctjs.value, %arg16: !ctjs.value, %arg17: !ctjs.value, %arg18: !ctjs.value, %arg19: !ctjs.value):
            %117 = ctjs.constant #ctjs.boolean<true>
            %118 = ub.poison : !ctjs.value
            ctjs.invoke_yield(%117, %118, %arg4, %arg5, %arg6, %arg7, %arg8, %arg9, %arg10, %arg11, %arg12, %arg13, %arg14, %arg15, %arg16, %arg17, %arg18, %arg19)
          } : !ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value
          scf.yield %116#0, %116#1, %116#2, %116#3, %116#4, %116#5, %116#6, %116#7, %116#8, %116#9, %116#10, %116#11, %116#12, %116#13, %116#14, %116#15, %116#16, %116#17 : !ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value
        }
        %96 = ctjs.truthy %95#0
        %97 = ub.poison : !ctjs.value
        %98 = ub.poison : !ctjs.value
        %99 = ub.poison : !ctjs.value
        %100 = ub.poison : !ctjs.value
        %101 = ub.poison : !ctjs.value
        %102 = ub.poison : !ctjs.value
        %103 = ub.poison : !ctjs.value
        %104 = ub.poison : !ctjs.value
        %105 = ub.poison : !ctjs.value
        %106 = ub.poison : !ctjs.value
        %107 = ub.poison : !ctjs.value
        %108 = ub.poison : !ctjs.value
        %109 = ub.poison : !ctjs.value
        %110 = ub.poison : !ctjs.value
        %111 = ub.poison : !ctjs.value
        %112 = ub.poison : !ctjs.value
        %113:6 = scf.if %96 -> (!ctjs.value, !ctjs.value, i1, !ctjs.value, i32, !ctjs.value) {
          scf.yield %95#2, %95#3, %34, %33, %c0_i32, %93 : !ctjs.value, !ctjs.value, i1, !ctjs.value, i32, !ctjs.value
        } else {
          %false_3 = arith.constant false
          %114 = ub.poison : !ctjs.value
          scf.yield %33, %33, %false_3, %89, %c1_i32, %93 : !ctjs.value, !ctjs.value, i1, !ctjs.value, i32, !ctjs.value
        }
        scf.yield %113#0, %113#1, %113#2, %113#3, %113#4, %113#5 : !ctjs.value, !ctjs.value, i1, !ctjs.value, i32, !ctjs.value
      } else {
        scf.yield %76#23, %76#24, %47, %48, %76#25, %76#27 : !ctjs.value, !ctjs.value, i1, !ctjs.value, i32, !ctjs.value
      }
      scf.yield %88#0, %88#1, %88#2, %88#3, %88#4, %88#5 : !ctjs.value, !ctjs.value, i1, !ctjs.value, i32, !ctjs.value
    }
    %82 = arith.index_castui %81#4 : i32 to index
    %c0_2 = arith.constant 0 : index
    %83 = arith.cmpi eq, %82, %c0_2 : index
    %84:5 = scf.if %83 -> (i1, !ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value) {
      %true = arith.constant true
      %85 = ub.poison : !ctjs.value
      scf.yield %true, %85, %81#0, %81#1, %81#5 : i1, !ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value
    } else {
      scf.yield %81#2, %81#3, %39, %40, %81#5 : i1, !ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value
    }
    ctjs.try_exit %84#0 normal %84#1 caught(%84#2, %84#3)
  } catch {
  ^bb0(%arg4: !ctjs.value, %arg5: !ctjs.value):
    %32 = ctjs.cell_get %arg5
    %33 = ctjs.compare strict_eq %arg4, %32
    ctjs.try_yield %33
  } : !ctjs.value
  ctjs.frame_exit %2
  ctjs.return %31
}
)MLIR";

} // namespace ctcompile::test::exception_recovery
