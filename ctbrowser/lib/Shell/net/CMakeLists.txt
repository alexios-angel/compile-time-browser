# Networking. ctbrowser-net is the part with no transport: the WHATWG URL
# parser with its generated IDNA and NFC tables (the .inc files) and the fetch
# policy, which ctbrowser-page reads assets through as well. HTTP over libcurl
# (net.cpp) stays in ctbrowser-shell.
add_library(ctbrowser-net)
target_sources(ctbrowser-net PUBLIC FILE_SET HEADERS BASE_DIRS "${CTBROWSER_INCLUDE_DIR}")
target_sources(ctbrowser-net PRIVATE
  idna_table.inc
  nfc_table.inc
  policy.cpp
  url.cpp
  url_encoding.cpp
  url_internal.hpp
  url_parser.cpp
  url_unicode.cpp
)
target_compile_features(ctbrowser-net PUBLIC cxx_std_23)
ctbrowser_target(ctbrowser-net)
target_link_libraries(ctbrowser-net PUBLIC ctbrowser-core)
add_library(ctbrowser::net ALIAS ctbrowser-net)

target_sources(ctbrowser-shell PRIVATE
  net.cpp
)

add_subdirectory(idna_table)
add_subdirectory(nfc_table)
