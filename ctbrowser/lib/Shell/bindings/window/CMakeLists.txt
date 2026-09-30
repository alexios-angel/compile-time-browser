target_sources(ctbrowser-shell PRIVATE
  activation.cpp
  callbacks.cpp
  crypto.cpp
  encoding.cpp
  history.cpp
  image_bitmap.cpp
  internal.hpp
  messaging.cpp
  os_random.cpp
  os_random.hpp
  scrolling.cpp
  url.cpp
  window.cpp
)
# BCryptGenRandom, the Windows CSPRNG behind os_random.cpp.
if(WIN32)
  target_link_libraries(ctbrowser-shell PRIVATE bcrypt)
endif()
