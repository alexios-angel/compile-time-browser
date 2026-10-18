# page-program-main.cpp is the native page host of check-workload.py's
# script-run probe. No target here builds it: check-workload.py compiles it at
# check time, with -DCTCOMPILE_PAGE_PROGRAM naming the C++ that ctcompile
# generated for the page program, so the file has nothing to build without
# that program. This file records that the directory owns its C++, as
# tools/check/check-cmake-hygiene.py requires of every directory that has any.
