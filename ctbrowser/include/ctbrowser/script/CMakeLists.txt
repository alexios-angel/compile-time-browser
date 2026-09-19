target_sources(ctbrowser-script PUBLIC FILE_SET HEADERS FILES
  bigint.hpp
  builtins.hpp
  bytecode.hpp
  compile.hpp
  program_image.hpp
  regex.hpp
  script.hpp
  source_lines.hpp
  type_record.hpp
  value.hpp
  vm.hpp
)

# Keep the existing installed header set; these support files stay private.
target_sources(ctbrowser-script PRIVATE
  bytecode_opcodes.def
  dispatch.hpp
)

set_source_files_properties(
  "${CMAKE_CURRENT_SOURCE_DIR}/bytecode_opcodes.def"
  TARGET_DIRECTORY ctbrowser-script
  PROPERTIES HEADER_FILE_ONLY TRUE
)

add_subdirectory(value)
add_subdirectory(vm)
