# The interpreter's larger opcode handlers (await, call, construct), kept out of
# vm/run_loop.cpp so that file stays readable.
target_sources(ctbrowser-script PRIVATE
  await.cpp
  call.cpp
  construct.cpp
)
