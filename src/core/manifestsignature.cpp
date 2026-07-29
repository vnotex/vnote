#include "manifestsignature.h"

#include <QDebug>
#include <QStringList>

// blake2.h carries its own extern "C" guards; tweetnacl.h does NOT, so it must
// be wrapped here or the C++ compiler mangles the names and the link fails
// against the C-compiled minicrypto library.
extern "C" {
#include <tweetnacl.h>
}

#include <blake2.h>

#include <cstring>

using namespace vnotex;

namespace {

// ---------------------------------------------------------------------------
// Compiled-in trusted public keys
// ---------------------------------------------------------------------------
//
// Base64 bodies of minisign .pub files (the SECOND line of the file, not the
// `untrusted comment:` line).
//
// ROTATION CONTRACT: this is a LIST, and it is meant to hold more than one
// entry. Because the key is compiled into the binary, a build that trusts a
// single key can never be migrated to a new key -- if that key is lost or
// retired, every already-installed client is cut off from updates permanently
// and can only be recovered by a manual reinstall. So:
//
//   * ship the current key AND a cold spare from the very first signed release;
//   * to rotate, start signing with the spare, then add a NEW spare in the
//     next release and drop the retired key only once the population has moved.
//
// EMPTY LIST = FAIL CLOSED. With no key configured, ManifestSignature::verify()
// returns NoTrustedKeys and UpdateService refuses to stage anything. That is
// deliberate: an unsigned update path is strictly worse than no update path.
// See docs/update-signing.md for how to generate and install these.
//
// A QStringList rather than a C array because the list is legitimately empty
// in-tree, and a zero-length C array does not compile.
QStringList trustedPublicKeyStrings() {
  return QStringList{
      // ACTIVE key (key id 334F7ED65256CDE8). Held by CI as the
      // MINISIGN_SECRET_KEY secret; signs every release manifest.
      QStringLiteral("RWTozVZS1n5PM5euO7/ieR6o6daenLdTCK0EIhYnf0ACb47j6usoRtnJ"),

      // COLD SPARE (key id B56AD74F9A82C266). Private half is stored OFFLINE
      // and never enters CI. It exists so the active key can be retired
      // without stranding installed clients -- see the rotation contract
      // above and docs/update-signing.md.
      QStringLiteral("RWRmwoKaT9dqtSFII74FcPaet3Ork43BpcJx/SOnuiX3JR6wG9864WjO"),
  };
}

constexpr int c_keyIdSize = 8;
constexpr int c_publicKeySize = 32;
constexpr int c_signatureSize = 64;

// minisign algorithm tags.
//
// Only the PREHASHED form is accepted. minisign has produced `ED` by default
// since 0.6 (2017), and VNote controls its own signer, so accepting the legacy
// `Ed` form would add a code path that no release ever exercises. Being liberal
// in what you accept is not a virtue in signature verification; an old signer
// fails loudly as UnsupportedAlgorithm instead of silently taking an untested
// branch.
const char c_algLegacy[2] = {'E', 'd'};    // signs the message directly - REJECTED
const char c_algPrehashed[2] = {'E', 'D'}; // signs BLAKE2b-512 of the message

QVector<ManifestSignature::PublicKey> *testKeysOverride() {
  static QVector<ManifestSignature::PublicKey> keys;
  return &keys;
}

bool *testKeysOverrideActive() {
  static bool active = false;
  return &active;
}

// Splits a .minisig into its non-empty lines, tolerating CRLF and a trailing
// newline.
QList<QByteArray> signatureLines(const QByteArray &p_contents) {
  QList<QByteArray> out;
  for (const QByteArray &raw : p_contents.split('\n')) {
    QByteArray line = raw;
    if (line.endsWith('\r')) {
      line.chop(1);
    }
    out.append(line);
  }
  // Drop a single trailing empty line produced by the final newline, but keep
  // interior blanks so line indices stay meaningful.
  while (!out.isEmpty() && out.last().isEmpty()) {
    out.removeLast();
  }
  return out;
}

QByteArray decodeBase64Strict(const QByteArray &p_input, bool *p_ok) {
  const auto decoded =
      QByteArray::fromBase64Encoding(p_input.trimmed(), QByteArray::AbortOnBase64DecodingErrors);
  *p_ok = decoded.decodingStatus == QByteArray::Base64DecodingStatus::Ok;
  return *p_ok ? *decoded : QByteArray();
}

// Constant-time compare for the key id. Not strictly required (the key id is
// public), but it costs nothing and keeps the habit.
bool equalsConstantTime(const QByteArray &p_a, const QByteArray &p_b) {
  if (p_a.size() != p_b.size()) {
    return false;
  }
  unsigned char diff = 0;
  for (int i = 0; i < p_a.size(); ++i) {
    diff |= static_cast<unsigned char>(p_a[i]) ^ static_cast<unsigned char>(p_b[i]);
  }
  return diff == 0;
}

// Ed25519 detached verification via TweetNaCl.
//
// TweetNaCl only exposes the COMBINED form (crypto_sign_open over
// signature||message), so the detached signature and message are concatenated
// into a scratch buffer here.
bool ed25519Verify(const QByteArray &p_message, const QByteArray &p_signature,
                   const QByteArray &p_publicKey) {
  if (p_signature.size() != c_signatureSize || p_publicKey.size() != c_publicKeySize) {
    return false;
  }

  QByteArray signedMessage;
  signedMessage.reserve(p_signature.size() + p_message.size());
  signedMessage.append(p_signature);
  signedMessage.append(p_message);

  QByteArray opened(signedMessage.size(), Qt::Uninitialized);
  unsigned long long openedLen = 0;

  const int rc = crypto_sign_open(
      reinterpret_cast<unsigned char *>(opened.data()), &openedLen,
      reinterpret_cast<const unsigned char *>(signedMessage.constData()),
      static_cast<unsigned long long>(signedMessage.size()),
      reinterpret_cast<const unsigned char *>(p_publicKey.constData()));

  return rc == 0;
}

QByteArray blake2b512(const QByteArray &p_data) {
  QByteArray digest(64, Qt::Uninitialized);
  if (blake2b(digest.data(), 64, p_data.constData(), static_cast<size_t>(p_data.size()), nullptr,
              0) != 0) {
    return QByteArray();
  }
  return digest;
}

} // namespace

ManifestSignature::PublicKey
ManifestSignature::parsePublicKey(const QString &p_minisignPublicKey) {
  PublicKey key;

  // Accept either the bare base64 body or a whole .pub file.
  QByteArray body;
  const QStringList lines = p_minisignPublicKey.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
  for (const QString &line : lines) {
    const QString trimmed = line.trimmed();
    if (trimmed.isEmpty() || trimmed.startsWith(QLatin1String("untrusted comment:"))) {
      continue;
    }
    body = trimmed.toLatin1();
    break;
  }
  if (body.isEmpty()) {
    return key;
  }

  bool ok = false;
  const QByteArray decoded = decodeBase64Strict(body, &ok);
  // 2-byte algorithm tag + 8-byte key id + 32-byte public key.
  if (!ok || decoded.size() != 2 + c_keyIdSize + c_publicKeySize) {
    return key;
  }
  if (decoded[0] != 'E' || (decoded[1] != 'd' && decoded[1] != 'D')) {
    return key;
  }

  key.keyId = decoded.mid(2, c_keyIdSize);
  key.publicKey = decoded.mid(2 + c_keyIdSize, c_publicKeySize);
  return key;
}

const QVector<ManifestSignature::PublicKey> &ManifestSignature::trustedKeys() {
  if (*testKeysOverrideActive()) {
    return *testKeysOverride();
  }

  static const QVector<PublicKey> keys = []() {
    QVector<PublicKey> parsed;
    for (const QString &encoded : trustedPublicKeyStrings()) {
      const PublicKey key = parsePublicKey(encoded);
      if (key.isValid()) {
        parsed.append(key);
      } else {
        qCritical() << "update: a compiled-in trusted public key is malformed and was ignored";
      }
    }
    return parsed;
  }();
  return keys;
}

bool ManifestSignature::hasTrustedKeys() { return !trustedKeys().isEmpty(); }

void ManifestSignature::testSetTrustedKeys(const QVector<PublicKey> &p_keys) {
  *testKeysOverride() = p_keys;
  *testKeysOverrideActive() = !p_keys.isEmpty();
}

ManifestSignature::Result ManifestSignature::verify(const QByteArray &p_message,
                                                    const QByteArray &p_signatureFile,
                                                    QString *p_trustedComment) {
  return verify(p_message, p_signatureFile, trustedKeys(), p_trustedComment);
}

ManifestSignature::Result ManifestSignature::verify(const QByteArray &p_message,
                                                    const QByteArray &p_signatureFile,
                                                    const QVector<PublicKey> &p_trustedKeys,
                                                    QString *p_trustedComment) {
  if (p_trustedKeys.isEmpty()) {
    // FAIL CLOSED. "No key configured" must never mean "signature optional".
    return Result::NoTrustedKeys;
  }

  const QList<QByteArray> lines = signatureLines(p_signatureFile);
  if (lines.size() < 4) {
    return Result::MalformedSignature;
  }

  // Line 0 is the untrusted comment (NOT authenticated); line 1 the signature;
  // line 2 the trusted comment; line 3 the global signature.
  //
  // The prefix INCLUDES the trailing space: minisign writes
  // "trusted comment: <text>" and signs exactly <text>. Dropping only
  // "trusted comment:" would leave a leading space in the payload and every
  // global-signature check would fail.
  const QByteArray trustedCommentPrefix = QByteArrayLiteral("trusted comment: ");
  if (!lines.at(2).startsWith(trustedCommentPrefix)) {
    return Result::MalformedSignature;
  }
  const QByteArray trustedComment = lines.at(2).mid(trustedCommentPrefix.size());

  bool ok = false;
  const QByteArray sigBlob = decodeBase64Strict(lines.at(1), &ok);
  if (!ok || sigBlob.size() != 2 + c_keyIdSize + c_signatureSize) {
    return Result::MalformedSignature;
  }

  const QByteArray algorithm = sigBlob.left(2);
  const QByteArray keyId = sigBlob.mid(2, c_keyIdSize);
  const QByteArray signature = sigBlob.mid(2 + c_keyIdSize, c_signatureSize);

  const bool prehashed = algorithm == QByteArray(c_algPrehashed, 2);
  if (!prehashed) {
    // Includes the legacy `Ed` form; see the comment on c_algLegacy.
    return Result::UnsupportedAlgorithm;
  }

  const QByteArray globalSig = decodeBase64Strict(lines.at(3), &ok);
  if (!ok || globalSig.size() != c_signatureSize) {
    return Result::MalformedSignature;
  }

  // Select the key by id. An unknown id is reported distinctly from a bad
  // signature so a rotated-away key produces an actionable message.
  const PublicKey *match = nullptr;
  for (const PublicKey &candidate : p_trustedKeys) {
    if (candidate.isValid() && equalsConstantTime(candidate.keyId, keyId)) {
      match = &candidate;
      break;
    }
  }
  if (!match) {
    return Result::UnknownKey;
  }

  // The signed payload is BLAKE2b-512(message) for the `ED` algorithm.
  const QByteArray signedPayload = blake2b512(p_message);
  if (signedPayload.isEmpty()) {
    return Result::BadSignature;
  }

  if (!ed25519Verify(signedPayload, signature, match->publicKey)) {
    return Result::BadSignature;
  }

  // The global signature binds the trusted comment to the file signature.
  // Without this check the comment minisign displays as "trusted" could be
  // rewritten at will.
  QByteArray globalPayload;
  globalPayload.reserve(signature.size() + trustedComment.size());
  globalPayload.append(signature);
  globalPayload.append(trustedComment);
  if (!ed25519Verify(globalPayload, globalSig, match->publicKey)) {
    return Result::BadGlobalSignature;
  }

  if (p_trustedComment) {
    *p_trustedComment = QString::fromUtf8(trustedComment);
  }
  return Result::Valid;
}

QString ManifestSignature::resultToString(Result p_result) {
  switch (p_result) {
  case Result::Valid:
    return QStringLiteral("signature is valid");
  case Result::MalformedSignature:
    return QStringLiteral("the signature file is malformed");
  case Result::UnknownKey:
    return QStringLiteral("the signature was made with a key this build does not trust");
  case Result::BadSignature:
    return QStringLiteral("the signature does not match the manifest");
  case Result::BadGlobalSignature:
    return QStringLiteral("the signature's trusted comment has been tampered with");
  case Result::NoTrustedKeys:
    return QStringLiteral("this build has no update signing key configured");
  case Result::UnsupportedAlgorithm:
    return QStringLiteral("the signature uses an unsupported algorithm");
  }
  return QStringLiteral("unknown signature verification result");
}
