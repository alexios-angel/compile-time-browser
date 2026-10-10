# One folder per first word the tests' names share, and the rest here. Each
# folder's CMakeLists.txt calls ctbrowser_test_directory(), which names a test
# for its path below this directory - vm/objects.cpp is `vm_objects`
# (cmake/modules/CTTest.cmake) - and adds its own subfolders.
set(CTBROWSER_TEST_ROOT "${CMAKE_CURRENT_SOURCE_DIR}")
ctbrowser_test_directory()
add_subdirectory(array)
add_subdirectory(number)
add_subdirectory(promise)
add_subdirectory(regexp)
add_subdirectory(vm)
