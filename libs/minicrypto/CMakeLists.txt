# Vendored verify-only crypto for update-manifest signatures.
# See PROVENANCE.md for pinned upstream sources and hashes.
#
# Scope is deliberately tiny: VNote only ever VERIFIES a minisign signature over
# a release manifest. It never signs, never generates keys, and never encrypts.

add_library(minicrypto STATIC
  tweetnacl.c
  tweetnacl.h
  blake2b-ref.c
  blake2.h
  blake2-impl.h
  randombytes_stub.c
)

target_include_directories(minicrypto PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

if(MSVC)
  # Third-party C; do not fail the build on its warnings.
  target_compile_options(minicrypto PRIVATE /W0)
else()
  target_compile_options(minicrypto PRIVATE -w)
endif()

set_target_properties(minicrypto PROPERTIES
  C_STANDARD 99
  POSITION_INDEPENDENT_CODE ON
  FOLDER "libs"
)
