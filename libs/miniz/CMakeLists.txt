# Vendored miniz (MIT). See PROVENANCE.md for the pinned upstream tag and hashes.
#
# Only the amalgamated release sources (miniz.c + miniz.h) are vendored; that is
# the complete source set the upstream 3.0.2 *release archive* ships (the tarball
# on the GitHub release page is pre-amalgamated, unlike a git checkout which
# splits the code across miniz_tdef/miniz_tinfl/miniz_zip).

add_library(miniz STATIC
  miniz.c
  miniz.h
)

target_include_directories(miniz PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# VNote uses miniz strictly as a ZIP reader/writer over in-memory buffers and
# files we open ourselves. Keep the surface small and deterministic:
#   - MINIZ_NO_TIME: do not stamp/interpret archive timestamps (we never rely on
#     them, and it removes a <time.h> dependency that varies across toolchains).
target_compile_definitions(miniz PUBLIC MINIZ_NO_TIME)

if(MSVC)
  # miniz is third-party C; do not fail the build on its warnings.
  target_compile_options(miniz PRIVATE /W0)
else()
  target_compile_options(miniz PRIVATE -w)
endif()

set_target_properties(miniz PROPERTIES
  C_STANDARD 99
  POSITION_INDEPENDENT_CODE ON
  FOLDER "libs"
)
