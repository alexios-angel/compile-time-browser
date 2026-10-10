target_sources(ctbrowser-dom PUBLIC FILE_SET HEADERS FILES
  dataset.hpp
  document.hpp
  dom.hpp
  element.hpp
  encoding.hpp
  entities.hpp
  html.hpp
  node.hpp
  token_list.hpp
  tokenizer.hpp
  treebuilder.hpp
  xml.hpp
)

# Private sources: included by the headers above but not installed.
target_sources(ctbrowser-dom PRIVATE
  encoding_tables.inc
)

add_subdirectory(entities)
