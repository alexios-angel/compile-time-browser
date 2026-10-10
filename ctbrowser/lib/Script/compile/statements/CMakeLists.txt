# The statement compiler, one file per statement family; dispatch.cpp holds
# compile_stmt, which routes each statement to its file.
target_sources(ctbrowser-script PRIVATE
  dispatch.cpp
  functions.cpp
  loops.cpp
  try_finally.cpp
  using.cpp
)
