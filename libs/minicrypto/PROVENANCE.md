# minicrypto provenance

Verify-only cryptography used by `src/core/manifestsignature.{h,cpp}` to check
the Ed25519 signature over an update manifest (minisign format).

VNote **only verifies**. It never signs, never generates keys, and never
encrypts. `randombytes_stub.c` therefore aborts rather than returning
predictable bytes; see the comment in that file.

## Vendored file set

| File | Upstream | SHA-256 |
|---|---|---|
| `tweetnacl.c` | https://tweetnacl.cr.yp.to/20140427/tweetnacl.c | `02e65bc3013ff2168983365e55906bc783c4c7e0a60d8100f17bb303a17175c4` |
| `tweetnacl.h` | https://tweetnacl.cr.yp.to/20140427/tweetnacl.h | `43f29ad721d9927b747b0100ab4160c119e7bb180c7c98a66e4bf79d31244287` |
| `blake2.h` | https://github.com/BLAKE2/BLAKE2 `ref/blake2.h` | `389bc87a83cdd9e25569a294d01a3347970d117237a66eee9df8edd6058736a4` |
| `blake2b-ref.c` | https://github.com/BLAKE2/BLAKE2 `ref/blake2b-ref.c` | `e2bf9872a8f0a51711b765936d420a7f8c34797db4d938948c24f2ddbc1dc588` |
| `blake2-impl.h` | https://github.com/BLAKE2/BLAKE2 `ref/blake2-impl.h` | `bc0ead7f3259a415325fa40ddebb1876f903d5062d888fc5994e8b2d9e616ec4` |

`randombytes_stub.c` is VNote-authored (see above) and is the only file here
that is not vendored verbatim.

Vendored on 2026-07-30.

## Licenses

- **TweetNaCl** — public domain. Authors: Daniel J. Bernstein, Bernard van
  Gastel, Wesley Janssen, Tanja Lange, Peter Schwabe, Sjaak Smetsers.
- **BLAKE2 reference implementation** — dual-licensed CC0 1.0 / OpenSSL /
  Apache 2.0, at the user's choice. VNote uses it under CC0 1.0. The upstream
  header comment in each file carries the full notice; it has not been modified.

## Why these two, and why not OpenSSL

minisign signatures are Ed25519 over a BLAKE2b-512 prehash (the `ED` algorithm;
the legacy `Ed` variant signs the message directly). Ed25519 needs SHA-512,
which TweetNaCl already contains, so the pair covers the whole format in ~27 KB
of auditable public-domain C with no build-system entanglement.

Qt exposes no Ed25519 API. VNote does bundle OpenSSL for TLS, but it is linked
by Qt rather than by VNote, its presence and version vary per platform and
package, and adding a direct link would make signature verification depend on
the deployment layout of an unrelated component. A vendored constant-size
implementation removes that coupling — the same reasoning as `libs/miniz`.

## Local modifications

**None** to the vendored files. All five are byte-identical to upstream.

## Re-pinning

Re-download from the URLs above, re-record the hashes in the table, and re-run
`ctest -R "^test_manifestsignature$"`. That suite includes known-answer vectors,
so a substituted or corrupted implementation fails immediately.
