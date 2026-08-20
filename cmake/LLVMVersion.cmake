# THE LLVM/MLIR PIN, and it is a pin rather than a floor.
#
# ODS, PDLL and pass-generation syntax change between LLVM releases: rewrite
# pattern APIs, effect interfaces, property storage and TypeDef constraint
# behaviour have all moved inside recent release windows, and PDLL is the
# youngest and fastest-moving of them. So ctcompile names one version, checks
# it at configure time, and an upgrade is its own change rather than a surprise
# in the middle of a feature.
#
# 22 rather than the 20 the master plan names: 20 is not installable on the
# devbox. apt ships MLIR 18 and brew ships 22.1.8, and the project's package
# policy has been brew-first since 2026-08-01 - so the pin is what the build
# machine can actually have. Every ODS/PDLL construct the plan spells has to be
# verified against THIS revision before it is relied on; see
# ctcompile/docs/LLVMUpgrade.md.
set(CTCOMPILE_REQUIRED_LLVM_MAJOR 22)
set(CTCOMPILE_TESTED_LLVM_VERSION "22.1.8")

# How far ahead of the pin a build may drift before it is refused. A patch
# release is fine; a major is not.
set(CTCOMPILE_MAX_LLVM_MAJOR 22)
