# The Object, Function, Error, Proxy and ShadowRealm built-ins, and the natives
# the bytecode compiler calls for destructuring, classes and iteration.
target_sources(ctbrowser-script PRIVATE
  destructuring.cpp
  errors.cpp
  function.cpp
  internal.hpp
  object.cpp
  operations.cpp
  proxy.cpp
  shadow_realm.cpp
)
