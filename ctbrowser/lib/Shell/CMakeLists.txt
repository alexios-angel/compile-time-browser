# ctbrowser.shell - the engine assembled into something that loads a page.
#
# browser/ is the assembly itself, bindings/ the web platform bound to the VM,
# net/ where bytes come from, image/ the codecs (each behind a two-function
# header, none of them SDL) and page/ what a PAGE can do. Every subdirectory's
# internal.hpp is private to it; bindings/names.hpp is the one header two of
# them share. Every source is listed here, subdirectory-prefixed: one
# CMakeLists per subsystem.
add_library(ctbrowser-shell)
target_sources(ctbrowser-shell PUBLIC FILE_SET HEADERS BASE_DIRS "${CTBROWSER_INCLUDE_DIR}")
target_sources(ctbrowser-shell PRIVATE
  app_bundle.cpp
  embedded_fonts.cpp
  bindings/abort.cpp
  bindings/animation_effects.cpp
  bindings/animations.cpp
  bindings/canvas.cpp
  bindings/canvas_path2d.cpp
  bindings/css.cpp
  bindings/custom_element_reactions.cpp
  bindings/custom_elements.cpp
  bindings/domparsing.cpp
  bindings/element_internals.cpp
  bindings/exceptions.cpp
  bindings/frames.cpp
  bindings/image_data.hpp
  bindings/media_queries.cpp
  bindings/mutation.cpp
  bindings/names.hpp
  bindings/performance.cpp
  bindings/promise_rejections.cpp
  bindings/resources.cpp
  bindings/selection.cpp
  bindings/shadow_dom.cpp
  bindings/xhr.cpp
  bindings/animations/css.cpp
  bindings/animations/helpers.hpp
  bindings/computed_style/entries.cpp
  bindings/computed_style/geometry.hpp
  bindings/computed_style/internal.hpp
  bindings/computed_style/object.cpp
  bindings/computed_style/serialize.cpp
  bindings/document/as_node.cpp
  bindings/document/collections.cpp
  bindings/document/entry.cpp
  bindings/document/geometry.cpp
  bindings/document/install.cpp
  bindings/document/internal.hpp
  bindings/document/live_properties.cpp
  bindings/document/named_access.cpp
  bindings/document/range.cpp
  bindings/document/second_document.cpp
  bindings/document/traversal.cpp
  bindings/document/tree_accessors.cpp
  bindings/document/tree_ops.cpp
  bindings/document/write.cpp
  bindings/document/range/helpers.hpp
  bindings/document/range/live.cpp
  bindings/element/attributes.cpp
  bindings/element/autocomplete.cpp
  bindings/element/character_data.cpp
  bindings/element/child_views.cpp
  bindings/element/control_methods.cpp
  bindings/element/dataset.cpp
  bindings/element/declarations.cpp
  bindings/element/geometry_views.cpp
  bindings/element/hyperlink.cpp
  bindings/element/interfaces.cpp
  bindings/element/internal.hpp
  bindings/element/methods.cpp
  bindings/element/namespace_methods.cpp
  bindings/element/node_methods.cpp
  bindings/element/pre_insert.cpp
  bindings/element/reflection.cpp
  bindings/element/reflection_rows.cpp
  bindings/element/rendered_text.cpp
  bindings/element/shadow.cpp
  bindings/element/style_views.cpp
  bindings/element/view_geometry.hpp
  bindings/element/views.cpp
  bindings/element/wrapper.cpp
  bindings/element/control_methods/collections_and_labels.cpp
  bindings/element/control_methods/data_transfer.cpp
  bindings/element/control_methods/focus_and_canvas.cpp
  bindings/element/control_methods/form_writes.cpp
  bindings/element/control_methods/form_data_and_submission.cpp
  bindings/element/control_methods/helpers.hpp
  bindings/element/control_methods/legacy_objects.cpp
  bindings/element/control_methods/numbers_and_validation.cpp
  bindings/element/control_methods/numbers_and_validation_helpers.cpp
  bindings/element/control_methods/pickers.cpp
  bindings/element/control_methods/select_and_options.cpp
  bindings/element/control_methods/select_and_options_helpers.cpp
  bindings/element/control_methods/tables_and_form_owners.cpp
  bindings/element/control_methods/tables_and_form_owners_helpers.cpp
  bindings/element/control_methods/text_encoding.cpp
  bindings/element/control_methods/text_selection.cpp
  bindings/events/dispatch.cpp
  bindings/events/handler_attributes.cpp
  bindings/events/handler_names.hpp
  bindings/events/input.cpp
  bindings/events/interfaces.cpp
  bindings/events/internal.hpp
  bindings/events/listeners.cpp
  bindings/stylesheets/internal.hpp
  bindings/stylesheets/objects.cpp
  bindings/stylesheets/prototypes.cpp
  bindings/stylesheets/rules.cpp
  bindings/stylesheets/serialize.cpp
  bindings/stylesheets/sheets.cpp
  bindings/stylesheets/source.cpp
  bindings/stylesheets/prototypes/collections.cpp
  bindings/stylesheets/prototypes/declarations.cpp
  bindings/stylesheets/prototypes/helpers.cpp
  bindings/stylesheets/prototypes/helpers.hpp
  bindings/stylesheets/prototypes/rules.cpp
  bindings/webgl/constants.cpp
  bindings/webgl/context.cpp
  bindings/webgl/drawing.cpp
  bindings/webgl/internal.hpp
  bindings/webgl/objects.cpp
  bindings/window/activation.cpp
  bindings/window/callbacks.cpp
  bindings/window/encoding.cpp
  bindings/window/history.cpp
  bindings/window/image_bitmap.cpp
  bindings/window/internal.hpp
  bindings/window/messaging.cpp
  bindings/window/scrolling.cpp
  bindings/window/url.cpp
  bindings/window/window.cpp
  browser/actions.cpp
  browser/chrome.cpp
  browser/focus.cpp
  browser/frame.cpp
  browser/input.cpp
  browser/internal.hpp
  browser/lifecycle.cpp
  browser/nested.cpp
  browser/resources.cpp
  browser/scripts.cpp
  browser/selection.cpp
  browser/styles.cpp
  image/jpeg.cpp
  image/png.cpp
  net/idna_table.inc
  net/net.cpp
  net/nfc_table.inc
  net/url.cpp
  net/url_encoding.cpp
  net/url_internal.hpp
  net/url_parser.cpp
  net/url_unicode.cpp
  net/idna_table/bidi_ranges-01.inc
  net/idna_table/combining_mark_ranges-01.inc
  net/idna_table/idna_mappings-01.inc
  net/idna_table/idna_mappings-02.inc
  net/idna_table/idna_ranges-01.inc
  net/idna_table/idna_ranges-02.inc
  net/idna_table/idna_ranges-03.inc
  net/idna_table/idna_ranges-04.inc
  net/idna_table/idna_ranges-05.inc
  net/idna_table/idna_ranges-06.inc
  net/idna_table/idna_ranges-07.inc
  net/idna_table/idna_ranges-08.inc
  net/idna_table/idna_ranges-09.inc
  net/idna_table/idna_ranges-10.inc
  net/idna_table/joining_ranges-01.inc
  net/idna_table/virama_ranges-01.inc
  net/nfc_table/ccc_ranges-01.inc
  net/nfc_table/compose_pairs-01.inc
  net/nfc_table/compose_pairs-02.inc
  net/nfc_table/nfd_data-01.inc
  net/nfc_table/nfd_table-01.inc
  net/nfc_table/nfd_table-02.inc
  net/nfc_table/nfd_table-03.inc
  page/accessibility.cpp
  page/canvas.cpp
  page/canvas_path.cpp
  page/forms.cpp
  page/input_types.cpp
  page/svg_cache.cpp
  page/webgl.cpp
)

target_compile_features(ctbrowser-shell PUBLIC cxx_std_23)
ctbrowser_target(ctbrowser-shell)

# --- the fonts, in the binary --------------------------------------------
#
# `#embed` is C23 and C++26. Clang has had it since 19 and the llvm-mingw cross
# toolchain has it; GCC gained it in 15, so GCC 13 - which builds this tree
# perfectly well otherwise - does not. Hence a PROBE rather than a version
# check: compile the directive and see.
#
# --embed-dir rather than -I. `#embed` has its own search path and the quoted
# form looks beside the source file, so the angle-bracket form plus this flag is
# the only combination that finds a file in another directory.
#
# OFF by default and deliberately: the twelve faces are 5.2 MB, and they would
# be linked into every test binary as well as every example - about 160 MB of
# build output for something only a DISTRIBUTABLE build needs. Turn it on for
# one (`-DCTBROWSER_EMBED_FONTS=ON`) and the exe runs anywhere with no fonts/
# beside it. The SOURCE tree spells that directory resources/fonts/; a SHIPPED
# application spells it fonts/, beside the exe. See CTBROWSER_FONT_PATH.
# beside it; leave it off and it reads the directory, as it always has.
option(CTBROWSER_EMBED_FONTS "Bake the vendored OFL faces into the binary" OFF)
if(CTBROWSER_EMBED_FONTS)
  include(CheckCXXSourceCompiles)
  set(CMAKE_REQUIRED_FLAGS "--embed-dir=${PROJECT_SOURCE_DIR}/resources/fonts")
  check_cxx_source_compiles("
    #ifndef __has_embed
    #error no __has_embed
    #endif
    #if __has_embed(<Tinos-Regular.ttf>) != 1
    #error not on the embed path
    #endif
    int main() {}
  " CTBROWSER_COMPILER_HAS_EMBED)
  unset(CMAKE_REQUIRED_FLAGS)
  if(CTBROWSER_COMPILER_HAS_EMBED)
    set_source_files_properties("${CMAKE_CURRENT_SOURCE_DIR}/embedded_fonts.cpp" PROPERTIES COMPILE_OPTIONS
      "--embed-dir=${PROJECT_SOURCE_DIR}/resources/fonts")
    message(STATUS "ctbrowser: fonts embedded in the binary (+5.2 MB)")
  else()
    message(STATUS "ctbrowser: no #embed here; fonts load from fonts/ at runtime")
  endif()
endif()
target_link_libraries(ctbrowser-shell PUBLIC ctbrowser-core ctbrowser-dom ctbrowser-style
                                             ctbrowser-layout ctbrowser-paint ctbrowser-raster
                                             ctbrowser-script Boost::headers)
# PRIVATE: boost/url is 1.2 MB of headers and url.cpp is the only place it
# appears. Nothing that links the engine should have to parse it - the same rule
# net.hpp states for curl.h.
target_link_libraries(ctbrowser-shell PRIVATE Boost::url)
# PRIVATE for the same reason: curl.h is the transport's business and
# net_curl.cpp is the only place it appears.
target_link_libraries(ctbrowser-shell PRIVATE CURL::libcurl)

# --- PNG AND JPEG, WITHOUT SDL -------------------------------------------
# Both found in cmake/dependencies.cmake, which records why the engine decodes
# them itself rather than through SDL3_image: a format that only works when a
# satellite happened to be found is one tests/ cannot assert on and no golden
# can compare, which is exactly how PNG stayed broken until Phaser arrived.
#
# PRIVATE, like curl and boost/url: png.h is png.cpp's business.
target_link_libraries(ctbrowser-shell PRIVATE PNG::PNG)

# TurboJPEG arrives one of two ways and the exported NAME differs, which is
# the only reason this is a branch: the pkg-config route yields an imported
# TARGET a consumer has to be able to recreate, the fallback a resolved PATH
# that needs nothing found again. cmake/dependencies.cmake does the looking.
if(TARGET PkgConfig::CTB_TURBOJPEG)
  target_link_libraries(ctbrowser-shell PRIVATE PkgConfig::CTB_TURBOJPEG)
  message(STATUS "ctbrowser: JPEG through libjpeg-turbo ${CTB_TURBOJPEG_VERSION}")
else()
  target_include_directories(ctbrowser-shell PRIVATE ${CTBROWSER_TURBOJPEG_INCLUDE_DIR})
  target_link_libraries(ctbrowser-shell PRIVATE ${CTBROWSER_TURBOJPEG_LIBRARY})
  message(STATUS "ctbrowser: JPEG through ${CTBROWSER_TURBOJPEG_LIBRARY}")
endif()

# TLS BELONGS TO THE TRANSPORT, which is most of the point of moving to libcurl.
# curl brings its own - SCHANNEL on Windows, the OS stack, so the cross build
# gets https:// with no OpenSSL cross-compiled at all. It reports what it has at
# run time through curl_version_info, which is why tls_available() is not a
# constexpr on a build-system guess.
#
# NOTHING HERE LOOKS FOR OpenSSL ANY MORE. The Asio transport needed it and the
# Asio transport is gone.
message(STATUS "ctbrowser: https:// through libcurl's own TLS (Schannel on Windows)")

add_library(ctbrowser::shell ALIAS ctbrowser-shell)
