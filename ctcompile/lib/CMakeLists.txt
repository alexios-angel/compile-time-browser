# The compiler's libraries. Organised by the phases they serve - Support first,
# because it is the only one that exists yet.
#
# The full set the plan lays out is Application, CodeGen, CSS, CTJS, HTML,
# JavaScript, Lowering, Packaging and Support. They arrive as the phases that
# need them do; a directory with nothing in it is not architecture.
add_subdirectory(Support)
add_subdirectory(HTML)
add_subdirectory(CSS)
