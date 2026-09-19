target_sources(ctbrowser-shell PUBLIC FILE_SET HEADERS FILES
  app_bundle.hpp
  bindings.hpp
  browser.hpp
  embedded_fonts.hpp
  input.hpp
  metrics.hpp
  shell.hpp
)

add_subdirectory(bindings)
add_subdirectory(browser)
add_subdirectory(image)
add_subdirectory(net)
add_subdirectory(page)
