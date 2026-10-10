# Networking: HTTP over libcurl, the fetch policy, and the WHATWG URL parser
# with its generated IDNA and NFC tables (the .inc files).
target_sources(ctbrowser-shell PRIVATE
  idna_table.inc
  net.cpp
  nfc_table.inc
  policy.cpp
  url.cpp
  url_encoding.cpp
  url_internal.hpp
  url_parser.cpp
  url_unicode.cpp
)

add_subdirectory(idna_table)
add_subdirectory(nfc_table)
