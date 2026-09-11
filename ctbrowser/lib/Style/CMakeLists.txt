# ctbrowser.style - selector matching, the cascade, computed values, the UA sheet.
#
# Sources are relative to this directory; headers are named in full under
# ${CTBROWSER_INCLUDE_DIR}. Dependencies are found in cmake/dependencies.cmake.

# --- ctbrowser.style -----------------------------------------------------
# Owns the CSS front end (css/) as well as the cascade. It used to borrow a
# submodule's parser, which had no tokenizer: a `;` inside a string ended a
# declaration and a `}` inside one desynchronised the rest of the file.
# Matching buckets rules by their rightmost simple selector and rejects
# descendant selectors with an ancestor filter.
# Depends on dom for read_txn - matching consumes a stable read view, never
# the live tree, which is what makes it parallelisable.
add_library(ctbrowser-style)
target_sources(ctbrowser-style
  PRIVATE
    engine.cpp
    # css/ - the CSS front end. A SUBDIRECTORY, not an eleventh subsystem:
    # one target, one aggregate header, and the parser lives beside the
    # cascade that is its only consumer. Same shape as shell/net/ and
    # raster/backend/ - a subdirectory gets no target and no CMakeLists.
    css/token.cpp
    css/parser.cpp
    css/selector.cpp
    css/substitute.cpp
    css/media.cpp
    # calc/ - the math functions; the pieces share calc/internal.hpp.
    css/calc/evaluator.cpp
    css/calc/units.cpp
    css/calc/serialize.cpp
    css/calc/fold.cpp
    css/calc/simplify.cpp
    # properties/ - the property table, the value grammar and the canonical
    # serialisation: what el.style, getComputedStyle and CSS.supports all three
    # ask; the pieces share properties/internal.hpp.
    css/properties/table.cpp
    css/properties/grammar.cpp
    css/properties/declaration.cpp
  PUBLIC
    FILE_SET HEADERS
      BASE_DIRS "${CTBROWSER_INCLUDE_DIR}"
      FILES
        "${CTBROWSER_INCLUDE_DIR}/ctbrowser/style/style.hpp"
        "${CTBROWSER_INCLUDE_DIR}/ctbrowser/style/css/token.hpp"
        "${CTBROWSER_INCLUDE_DIR}/ctbrowser/style/css/calc.hpp"
        "${CTBROWSER_INCLUDE_DIR}/ctbrowser/style/css/value.hpp"
        "${CTBROWSER_INCLUDE_DIR}/ctbrowser/style/css/properties.hpp"
        "${CTBROWSER_INCLUDE_DIR}/ctbrowser/style/css/selector.hpp"
        "${CTBROWSER_INCLUDE_DIR}/ctbrowser/style/css/parser.hpp"
        "${CTBROWSER_INCLUDE_DIR}/ctbrowser/style/css/substitute.hpp"
        "${CTBROWSER_INCLUDE_DIR}/ctbrowser/style/css/media_fwd.hpp"
        "${CTBROWSER_INCLUDE_DIR}/ctbrowser/style/css/media.hpp"
        "${CTBROWSER_INCLUDE_DIR}/ctbrowser/style/computed.hpp"
        "${CTBROWSER_INCLUDE_DIR}/ctbrowser/style/selector.hpp"
        "${CTBROWSER_INCLUDE_DIR}/ctbrowser/style/engine.hpp"
        "${CTBROWSER_INCLUDE_DIR}/ctbrowser/style/ua.hpp")
target_compile_features(ctbrowser-style PUBLIC cxx_std_23)
ctbrowser_target(ctbrowser-style)
target_link_libraries(ctbrowser-style PUBLIC ctbrowser-core ctbrowser-dom Boost::headers)

add_library(ctbrowser::style ALIAS ctbrowser-style)
