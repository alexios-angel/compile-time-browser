target_sources(ctbrowser-script PUBLIC FILE_SET HEADERS FILES
  aot_helpers.def
)

# Keep the existing installed header set; these support files stay private.
target_sources(ctbrowser-script PRIVATE
  aot.hpp
  aot_entry.h
)

add_subdirectory(helpers)
