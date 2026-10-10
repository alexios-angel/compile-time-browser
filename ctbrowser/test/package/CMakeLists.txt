# A consumer of the installed package.
#
# Not built as part of the project: tools/check/check-package.sh configures it
# against an install prefix. Building it from the source tree would prove
# nothing, because the install exists to make the source tree unnecessary.
cmake_minimum_required(VERSION 3.20)
project(ctbrowser-package-check CXX)

find_package(ctbrowser REQUIRED CONFIG)

add_executable(package-check main.cpp)
target_link_libraries(package-check PRIVATE ctbrowser::ctbrowser)
target_compile_features(package-check PRIVATE cxx_std_23)
