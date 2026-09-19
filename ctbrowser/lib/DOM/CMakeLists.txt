# ctbrowser.dom - the WHATWG tokenizer and tree builder, and the document as a slab.
add_library(ctbrowser-dom)
ctbrowser_subsystem_sources(ctbrowser-dom dom)
target_compile_features(ctbrowser-dom PUBLIC cxx_std_23)
ctbrowser_target(ctbrowser-dom)
target_link_libraries(ctbrowser-dom PUBLIC ctbrowser-core)

add_library(ctbrowser::dom ALIAS ctbrowser-dom)

# Generated entity rows are part of the public lookup header.
target_sources(ctbrowser-dom PUBLIC FILE_SET HEADERS FILES
  "${CTBROWSER_INCLUDE_DIR}/ctbrowser/dom/entities/entity_table-01.inc"
  "${CTBROWSER_INCLUDE_DIR}/ctbrowser/dom/entities/entity_table-02.inc"
  "${CTBROWSER_INCLUDE_DIR}/ctbrowser/dom/entities/entity_table-03.inc"
)
