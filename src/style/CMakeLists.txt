# ctbrowser.style - selector matching, the cascade, computed values, the UA sheet.
#
# Sources are relative to this directory; headers are named in full under
# ${CTBROWSER_INCLUDE_DIR}. Adding a file to this subsystem now touches this
# file and nothing else - which is why the 897-line src/CMakeLists.txt was
# split on 2026-08-09. Dependencies are found in cmake/dependencies.cmake.

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
  PUBLIC
    FILE_SET HEADERS
      BASE_DIRS "${CTBROWSER_INCLUDE_DIR}"
      FILES
        "${CTBROWSER_INCLUDE_DIR}/ctbrowser/style/style.hpp"
        "${CTBROWSER_INCLUDE_DIR}/ctbrowser/style/css/token.hpp"
        "${CTBROWSER_INCLUDE_DIR}/ctbrowser/style/css/value.hpp"
        "${CTBROWSER_INCLUDE_DIR}/ctbrowser/style/css/selector.hpp"
        "${CTBROWSER_INCLUDE_DIR}/ctbrowser/style/css/parser.hpp"
        "${CTBROWSER_INCLUDE_DIR}/ctbrowser/style/computed.hpp"
        "${CTBROWSER_INCLUDE_DIR}/ctbrowser/style/selector.hpp"
        "${CTBROWSER_INCLUDE_DIR}/ctbrowser/style/engine.hpp"
        "${CTBROWSER_INCLUDE_DIR}/ctbrowser/style/ua.hpp")
target_compile_features(ctbrowser-style PUBLIC cxx_std_23)
ctbrowser_target(ctbrowser-style)
# NO ctcss INTERFACE, AND NOTHING OF ITS TO INSTALL, since the CSS front end
# landed. `style/engine.hpp` used to include <ctcss.hpp> for one private member's
# parameter type, and because that is a PUBLIC header the path and the headers
# themselves both had to travel - so an installed prefix carried a COPY of
# ctcss's include tree, and a consumer that had its own ended up with two.
#
# It took an install to notice the second interface was missing in the first
# place: an exported target may not name a source-tree path, so the build tree got
# BUILD_INTERFACE and until 2026-08-09 that was the only one. `find_package`
# succeeded and the first `#include <ctbrowser.hpp>` died on "ctcss.hpp: No such
# file or directory". Nothing said so, because everything in this tree consumes
# the engine through add_subdirectory.
#
# Deleting the whole arrangement is the real fix, and the parser being ours is
# what allows it. tools/check/check-package.sh is what proves an install still
# compiles; it is the only thing that can.
target_link_libraries(ctbrowser-style PUBLIC ctbrowser-core ctbrowser-dom Boost::headers)

add_library(ctbrowser::style ALIAS ctbrowser-style)
