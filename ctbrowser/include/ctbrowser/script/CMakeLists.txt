target_sources(ctbrowser-script PUBLIC FILE_SET HEADERS FILES
  bigint.hpp
  builtins.hpp
  bytecode.hpp
  bytecode_opcodes.def
  compile.hpp
  dispatch.hpp
  program_image.hpp
  regex.hpp
  script.hpp
  source_lines.hpp
  type_record.hpp
  value.hpp
  vm.hpp
)

add_subdirectory(value)
add_subdirectory(vm)
