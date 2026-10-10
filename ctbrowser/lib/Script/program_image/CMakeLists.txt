# The program image (serialised bytecode) reader and writer; internal.hpp
# holds the format rules both share.
target_sources(ctbrowser-script PRIVATE
  internal.hpp
  read.cpp
  write.cpp
)
