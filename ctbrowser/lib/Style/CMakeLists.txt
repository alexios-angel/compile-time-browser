# ctbrowser.style - selector matching, the cascade, computed values, the UA sheet.
#
# Owns the CSS front end (css/) as well as the cascade. It used to borrow a
# submodule's parser, which had no tokenizer: a `;` inside a string ended a
# declaration and a `}` inside one desynchronised the rest of the file.
# Matching buckets rules by their rightmost simple selector and rejects
# descendant selectors with an ancestor filter.
# Depends on dom for read_txn - matching consumes a stable read view, never
# the live tree.
#
# css/ is a SUBDIRECTORY, not an eleventh subsystem: one target, one aggregate
# header, and the parser lives beside the cascade that is its only consumer.
add_library(ctbrowser-style)
target_sources(ctbrowser-style PUBLIC FILE_SET HEADERS BASE_DIRS "${CTBROWSER_INCLUDE_DIR}")
target_sources(ctbrowser-style PRIVATE
  easing.cpp
  engine.cpp
)

add_subdirectory(css)
add_subdirectory(easing)
add_subdirectory(engine)
target_compile_features(ctbrowser-style PUBLIC cxx_std_23)
ctbrowser_target(ctbrowser-style)
target_link_libraries(ctbrowser-style PUBLIC ctbrowser-core ctbrowser-dom Boost::headers)

add_library(ctbrowser::style ALIAS ctbrowser-style)
