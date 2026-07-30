#ifndef MANIFESTSIGNATURE_H
#define MANIFESTSIGNATURE_H

#include <QByteArray>
#include <QString>
#include <QStringList>
#include <QVector>

namespace vnotex {

// Verifies a minisign signature over an update manifest.
//
// VNote downloads and then EXECUTES the bytes an update manifest describes, so
// the manifest is a code-execution path. TLS plus a SHA-256 taken from the same
// origin proves only that the bytes arrived intact from whoever served them; it
// says nothing about whether that party is us. The signature is what makes the
// release artifacts self-authenticating, independent of GitHub, the CDN, and
// the TLS chain.
//
// Format: minisign (https://jedisct1.github.io/minisign/), so the maintainer can
// produce and audit signatures with the stock `minisign` CLI, and the key format
// is a standard one rather than something invented here.
//
//   untrusted comment: <free text, NOT covered by the signature>
//   base64( sig_alg[2] || key_id[8] || signature[64] )
//   trusted comment: <free text, covered by the global signature>
//   base64( global_signature[64] )
//
// Two algorithms exist. `ED` (the modern default) signs BLAKE2b-512 of the
// message; `Ed` (legacy) signs the message directly. Both are accepted.
//
// The global signature covers `signature || trusted_comment`. Verifying it is
// what stops an attacker from rewriting the trusted comment (which is the part
// minisign presents to the user as authenticated) while keeping a valid file
// signature.
//
// Pure: no network, no filesystem, no Qt GUI. Verification is total -- it never
// throws and never aborts on malformed input.
class ManifestSignature {
public:
  enum class Result {
    Valid,
    // The .minisig could not be parsed at all.
    MalformedSignature,
    // Signature is well-formed but its key id matches none of the accepted
    // public keys. This is what a rotated-away or foreign key looks like.
    UnknownKey,
    // Ed25519 verification of the file signature failed.
    BadSignature,
    // The file signature verified but the trusted comment's global signature
    // did not, i.e. the comment was tampered with.
    BadGlobalSignature,
    // No public keys were compiled in, so nothing can be trusted. FAIL CLOSED:
    // never treat "no key configured" as "signature not required".
    NoTrustedKeys,
    // Unsupported signature algorithm.
    UnsupportedAlgorithm,
  };

  // A minisign public key: 8-byte key id + 32-byte Ed25519 public key.
  struct PublicKey {
    QByteArray keyId;     // exactly 8 bytes
    QByteArray publicKey; // exactly 32 bytes

    bool isValid() const { return keyId.size() == 8 && publicKey.size() == 32; }
  };

  // Parses the base64 body of a minisign .pub file (the second line), or the
  // whole file contents (the `untrusted comment:` line is ignored). Returns an
  // invalid key on any problem.
  static PublicKey parsePublicKey(const QString &p_minisignPublicKey);

  // The keys this build trusts, in the order they were configured.
  //
  // Deliberately a LIST: shipping more than one accepted key is what makes key
  // rotation possible at all. The public key is compiled in, so a build that
  // trusts exactly one key can never migrate to a new key without an
  // out-of-band reinstall by every user.
  static const QVector<PublicKey> &trustedKeys();

  // True when this build has at least one usable trusted key. When false, the
  // updater must refuse to apply anything.
  static bool hasTrustedKeys();

  // Verifies p_signatureFile (the full .minisig contents) over p_message (the
  // exact manifest bytes as received -- never a re-serialized form).
  //
  // p_trustedComment, when non-null, receives the authenticated trusted comment
  // on success.
  static Result verify(const QByteArray &p_message, const QByteArray &p_signatureFile,
                       QString *p_trustedComment = nullptr);

  // Same, against an explicit key set. Used by tests and by the key-rotation
  // tooling; production callers use the overload above.
  static Result verify(const QByteArray &p_message, const QByteArray &p_signatureFile,
                       const QVector<PublicKey> &p_trustedKeys,
                       QString *p_trustedComment = nullptr);

  static QString resultToString(Result p_result);

  // ---------------------------------------------------------------------
  // Test seam (unconditional, per ADR-6)
  // ---------------------------------------------------------------------

  // Overrides the compiled-in key list. Passing an empty vector restores the
  // production keys. Tests use this to exercise rotation and rejection without
  // needing the real private key.
  static void testSetTrustedKeys(const QVector<PublicKey> &p_keys);

  // Forces the trusted-key list to be genuinely EMPTY, which is what an
  // unconfigured build looks like and is the only way to reach the fail-closed
  // path from the outside.
  //
  // This needs its own seam because testSetTrustedKeys({}) deliberately means
  // "restore the production keys" -- that is what makes it a safe cleanup call
  // in a test's cleanup() -- and so cannot express "trusts nothing".
  static void testClearTrustedKeys();

private:
  ManifestSignature() = delete;
};

} // namespace vnotex

#endif // MANIFESTSIGNATURE_H
