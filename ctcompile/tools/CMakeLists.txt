# The built executables. One so far, and it is the product.
add_subdirectory(ctcompile)

# THE MEASUREMENT TOOLS ARE BUILT BY `all`, AND THIS TREE HAS PAID THREE TIMES
# TO LEARN IT. `EXCLUDE_FROM_ALL` keeps a target out of the default build
# entirely, so `cmake --build` rebuilds the engine, relinks every test, reports
# a green suite - and leaves the measurement binary beside it at whatever
# revision it was last built by hand. Its number is then about code that is no
# longer there, and nothing says so.
#
#   * docs/history/computed-goto.md - a computed-goto result withdrawn: the
#     benchmark "compared two things neither of which was computed goto".
#   * docs/performance.md - the same run, recorded as invalid.
#   * ctcompile/docs/baseline/page-load.json - a page-load figure that was the
#     PREVIOUS SESSION'S binary. The tell was that it matched, to the
#     hundredth of a millisecond, the arm the change was replacing.
#
# Both earlier write-ups end by telling the next person to check the binary.
# That instruction has now failed twice, so this is a build edge instead: the
# idiom is `ctbrowser-tests`, which is how the test executables are
# EXCLUDE_FROM_ALL and still always current. It costs 3.1 s of a 4.1 s
# incremental build, and that is the whole price of never reading a stale
# number again.
add_custom_target(ctcompile-tools ALL)
add_subdirectory(ctbaseline)
add_subdirectory(ctpageload)
