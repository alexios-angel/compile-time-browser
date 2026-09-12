# ctbrowser.core - the foundation: slab allocation, generation-tagged handles,
# atoms, the thread pool, geometry. Knows nothing about browsers.
add_library(ctbrowser-core)
ctbrowser_subsystem_sources(ctbrowser-core core)
target_compile_features(ctbrowser-core PUBLIC cxx_std_23)
ctbrowser_target(ctbrowser-core)
target_link_libraries(ctbrowser-core PUBLIC Boost::headers)

target_link_libraries(ctbrowser-core PUBLIC Threads::Threads)

# mimalloc backs operator new/delete and simdutf backs base64_decode's fast
# path; both are found in cmake/dependencies.cmake, which says why each is
# looked for the way it is. PUBLIC on purpose - an executable that linked the
# engine but not mimalloc would silently resolve operator new from libstdc++.
target_link_libraries(ctbrowser-core PUBLIC "${CTBROWSER_SIMDUTF}")
target_include_directories(ctbrowser-core SYSTEM PUBLIC "${CTBROWSER_SIMDUTF_INCLUDE}")

if(CTBROWSER_USE_MIMALLOC)
  target_compile_definitions(ctbrowser-core PRIVATE CTBROWSER_WITH_MIMALLOC=1)
  target_link_libraries(ctbrowser-core PUBLIC "${CTBROWSER_MIMALLOC}")
endif()
# SYSTEM, and this is not cosmetic. That path is a whole brew prefix's include
# directory: adding it normally puts every header brew has installed - Boost's
# among them - on the strict-warning path, and the build stops on
# -Wunused-template inside boost/move/algo. Third-party headers are not this
# project's warnings to answer, which is the same rule ctbrowser_target already
# states about its own exported interface.
if(CTBROWSER_USE_MIMALLOC)
  target_include_directories(ctbrowser-core SYSTEM PUBLIC "${CTBROWSER_MIMALLOC_INCLUDE}")
endif()

add_library(ctbrowser::core ALIAS ctbrowser-core)
