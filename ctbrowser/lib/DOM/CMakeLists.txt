# ctbrowser.dom - the WHATWG tokenizer and tree builder, and the document as a slab.
add_library(ctbrowser-dom)
ctbrowser_subsystem_sources(ctbrowser-dom dom)
target_compile_features(ctbrowser-dom PUBLIC cxx_std_23)
ctbrowser_target(ctbrowser-dom)
target_link_libraries(ctbrowser-dom PUBLIC ctbrowser-core)

add_library(ctbrowser::dom ALIAS ctbrowser-dom)
