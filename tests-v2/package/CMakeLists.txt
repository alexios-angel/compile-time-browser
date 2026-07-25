# A consumer of the INSTALLED v2 package.
#
# Not built as part of the project - it is configured against an install prefix
# by tools/check-package.sh. That indirection is the point: building it from the
# source tree would prove nothing, because the source tree is exactly what the
# install is supposed to make unnecessary.
cmake_minimum_required(VERSION 3.28)
project(ctbrowser-package-check CXX)

find_package(ctbrowser-v2 REQUIRED CONFIG)

add_executable(package-check main.cpp)
target_link_libraries(package-check PRIVATE ctbrowser::ctbrowser-v2)
target_compile_features(package-check PRIVATE cxx_std_23)
