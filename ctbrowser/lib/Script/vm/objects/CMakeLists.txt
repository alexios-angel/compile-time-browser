# The VM's object model: prototype chains, context construction, property
# descriptors, the garbage collector, property lookup and storage, and strings.
target_sources(ctbrowser-script PRIVATE
  chain.cpp
  context.cpp
  descriptors.cpp
  gc.cpp
  lookup.cpp
  store.cpp
  strings.cpp
)
