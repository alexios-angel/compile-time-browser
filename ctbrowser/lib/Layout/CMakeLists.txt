# ctbrowser.layout - styled elements -> placed geometry.
#
# Consumes a styled box tree and produces an immutable fragment tree. Depends
# on core (scheduler, geometry), dom (read_txn) and style (computed styles) -
# but NOT the other way round: nothing upstream knows layout exists, which is
# what keeps the pipeline acyclic.
add_library(ctbrowser-layout)
ctbrowser_subsystem_sources(ctbrowser-layout layout)
target_compile_features(ctbrowser-layout PUBLIC cxx_std_23)
ctbrowser_target(ctbrowser-layout)
target_link_libraries(ctbrowser-layout PUBLIC ctbrowser-core ctbrowser-dom ctbrowser-style)

add_library(ctbrowser::layout ALIAS ctbrowser-layout)
