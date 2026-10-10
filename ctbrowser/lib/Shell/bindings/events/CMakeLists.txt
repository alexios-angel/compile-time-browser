# Event bindings: dispatch, listeners, on* handler attributes, input events and
# the Event interface family.
target_sources(ctbrowser-shell PRIVATE
  dispatch.cpp
  handler_attributes.cpp
  handler_names.hpp
  input.cpp
  interfaces.cpp
  internal.hpp
  listeners.cpp
)
