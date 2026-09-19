# ctbrowser.dom - the WHATWG tokenizer and tree builder, and the document as a slab.
add_library(ctbrowser-dom)
target_sources(ctbrowser-dom PUBLIC FILE_SET HEADERS BASE_DIRS "${CTBROWSER_INCLUDE_DIR}")
target_sources(ctbrowser-dom PRIVATE
  dataset.cpp
  document.cpp
  element.cpp
  encoding.cpp
  token_list.cpp
  tokenizer.cpp
  treebuilder.cpp
  xml.cpp
)

add_subdirectory(tokenizer)
add_subdirectory(treebuilder)
target_compile_features(ctbrowser-dom PUBLIC cxx_std_23)
ctbrowser_target(ctbrowser-dom)
target_link_libraries(ctbrowser-dom PUBLIC ctbrowser-core)

add_library(ctbrowser::dom ALIAS ctbrowser-dom)
