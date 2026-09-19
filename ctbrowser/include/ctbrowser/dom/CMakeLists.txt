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

# Keep the existing installed header set; these support files stay private.
target_sources(ctbrowser-dom PRIVATE
  encoding_tables.inc
)

add_subdirectory(entities)
