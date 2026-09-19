if(TARGET ctcompile-test-html_comparator)
  target_sources(ctcompile-test-html_comparator PRIVATE
    Html.cpp
  )
endif()

if(TARGET ctcompile-test-css_comparator)
  target_sources(ctcompile-test-css_comparator PRIVATE
    Css.cpp
  )
endif()
