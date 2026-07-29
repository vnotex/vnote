/*
 * TweetNaCl declares `randombytes` as an extern that the embedding application
 * must supply. VNote only ever VERIFIES signatures (crypto_sign_open), which
 * touches no randomness whatsoever -- the symbol exists solely to satisfy the
 * linker for the key-generation and encryption entry points we never call.
 *
 * Aborting rather than returning zeroed "randomness" is deliberate: silently
 * producing predictable bytes would turn a build/link mistake into a silent
 * cryptographic failure. If this ever fires, someone has started calling a
 * TweetNaCl API that VNote is not supposed to use.
 */

#include <stdio.h>
#include <stdlib.h>

void randombytes(unsigned char *buffer, unsigned long long length);

void randombytes(unsigned char *buffer, unsigned long long length) {
  (void)buffer;
  (void)length;
  fprintf(stderr,
          "VNote: randombytes() was called, but this build only verifies "
          "signatures and has no entropy source wired up. Aborting rather than "
          "returning predictable bytes.\n");
  abort();
}
