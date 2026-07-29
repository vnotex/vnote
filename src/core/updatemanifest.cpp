#include "updatemanifest.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonValue>
#include <QSet>

#include <algorithm>

using namespace vnotex;

namespace {

const QString c_keySchema = QStringLiteral("schema");
const QString c_keyProduct = QStringLiteral("product");
const QString c_keyChannel = QStringLiteral("channel");
const QString c_keyVersion = QStringLiteral("version");
const QString c_keyVariant = QStringLiteral("variant");
const QString c_keyPlatform = QStringLiteral("platform");
const QString c_keyCommit = QStringLiteral("commit");
const QString c_keyGeneratedAt = QStringLiteral("generatedAt");
const QString c_keyFiles = QStringLiteral("files");
const QString c_keyPath = QStringLiteral("path");
const QString c_keySize = QStringLiteral("size");
const QString c_keySha256 = QStringLiteral("sha256");
const QString c_keyFullPackage = QStringLiteral("fullPackage");
const QString c_keyDelta = QStringLiteral("delta");
const QString c_keyAsset = QStringLiteral("asset");
const QString c_keyBaseVersion = QStringLiteral("baseVersion");

// A SHA-256 hex digest, case-insensitive.
bool isSha256Hex(const QString &p_value) {
  if (p_value.size() != 64) {
    return false;
  }
  for (const QChar ch : p_value) {
    if (!((ch >= QLatin1Char('0') && ch <= QLatin1Char('9')) ||
          (ch >= QLatin1Char('a') && ch <= QLatin1Char('f')) ||
          (ch >= QLatin1Char('A') && ch <= QLatin1Char('F')))) {
      return false;
    }
  }
  return true;
}

// Win32 device names. Reserved with OR without an extension, so "NUL.txt" is
// just as unusable as "NUL".
bool isReservedWindowsName(const QString &p_segment) {
  QString stem = p_segment;
  const int dot = stem.indexOf(QLatin1Char('.'));
  if (dot >= 0) {
    stem = stem.left(dot);
  }
  stem = stem.toUpper();

  static const QSet<QString> simple{QStringLiteral("CON"), QStringLiteral("PRN"),
                                    QStringLiteral("AUX"), QStringLiteral("NUL")};
  if (simple.contains(stem)) {
    return true;
  }

  if (stem.size() == 4 &&
      (stem.startsWith(QLatin1String("COM")) || stem.startsWith(QLatin1String("LPT"))) &&
      stem.at(3) >= QLatin1Char('1') && stem.at(3) <= QLatin1Char('9')) {
    return true;
  }

  return false;
}

bool hasForbiddenChar(const QString &p_segment) {
  for (const QChar ch : p_segment) {
    const ushort u = ch.unicode();
    if (u < 0x20) {
      return true;
    }
    switch (u) {
    case '<':
    case '>':
    case ':':
    case '"':
    case '|':
    case '?':
    case '*':
      return true;
    default:
      break;
    }
  }
  return false;
}

QStringList sortedUnique(QStringList p_list) {
  std::sort(p_list.begin(), p_list.end());
  p_list.erase(std::unique(p_list.begin(), p_list.end()), p_list.end());
  return p_list;
}

void setError(QString *p_error, const QString &p_message) {
  if (p_error) {
    *p_error = p_message;
  }
}

} // namespace

constexpr int UpdateManifest::c_supportedSchema;
constexpr int UpdateManifest::c_maxChainHops;
constexpr double UpdateManifest::c_maxChainSizeRatio;

const QString &UpdateManifest::stableChannel() {
  static const QString s{QStringLiteral("stable")};
  return s;
}

const QString &UpdateManifest::continuousChannel() {
  static const QString s{QStringLiteral("continuous")};
  return s;
}

const QString &UpdateManifest::manifestFileName() {
  static const QString s{QStringLiteral("manifest.json")};
  return s;
}

const QString &UpdateManifest::stagingDirName() {
  static const QString s{QStringLiteral(".vnote-update")};
  return s;
}

const QString &UpdateManifest::backupDirName() {
  static const QString s{QStringLiteral(".vnote-old")};
  return s;
}

QString UpdateManifest::normalizePath(const QString &p_path) {
  if (p_path.isEmpty()) {
    return QString();
  }

  QString path = p_path;
  path.replace(QLatin1Char('\\'), QLatin1Char('/'));

  // Absolute, UNC, and Win32 device-namespace paths.
  if (path.startsWith(QLatin1Char('/'))) {
    return QString();
  }

  // Drive-qualified, both "C:/x" and the drive-relative "C:x".
  if (path.size() >= 2 && path.at(1) == QLatin1Char(':')) {
    return QString();
  }

  if (path.endsWith(QLatin1Char('/'))) {
    // Directory entries are handled by the extractor, never by the manifest.
    return QString();
  }

  const QStringList segments = path.split(QLatin1Char('/'));
  for (const QString &seg : segments) {
    if (seg.isEmpty()) {
      // Leading, trailing, or doubled separator.
      return QString();
    }
    if (seg == QLatin1String(".") || seg == QLatin1String("..")) {
      return QString();
    }
    if (seg.endsWith(QLatin1Char('.')) || seg.endsWith(QLatin1Char(' ')) ||
        seg.startsWith(QLatin1Char(' '))) {
      // Win32 strips trailing dots/spaces, so these alias another name.
      return QString();
    }
    if (hasForbiddenChar(seg)) {
      return QString();
    }
    if (isReservedWindowsName(seg)) {
      return QString();
    }
  }

  return path;
}

QString UpdateManifest::pathKey(const QString &p_normalizedPath) {
  return p_normalizedPath.toLower();
}

bool UpdateManifest::isReservedPath(const QString &p_normalizedPath) {
  const QString lower = p_normalizedPath.toLower();
  return lower.startsWith(stagingDirName() + QLatin1Char('/')) ||
         lower.startsWith(backupDirName() + QLatin1Char('/')) || lower == stagingDirName() ||
         lower == backupDirName();
}

QString UpdateManifest::variantForBuild() {
#if QT_VERSION_MAJOR >= 6
  return QStringLiteral("win64");
#else
  return QStringLiteral("win64-windows7");
#endif
}

bool UpdateManifest::parseArchiveRef(const QJsonObject &p_obj, UpdateArchiveRef *p_out) {
  UpdateArchiveRef ref;
  ref.asset = p_obj.value(c_keyAsset).toString();
  ref.sha256 = p_obj.value(c_keySha256).toString().toLower();
  ref.baseVersion = p_obj.value(c_keyBaseVersion).toString();

  const QJsonValue sizeVal = p_obj.value(c_keySize);
  ref.size = sizeVal.isDouble() ? static_cast<qint64>(sizeVal.toDouble()) : -1;

  if (ref.asset.isEmpty() || ref.size <= 0 || !isSha256Hex(ref.sha256)) {
    return false;
  }

  // The asset name is used to build a download URL; it must be a bare file
  // name, never a path or a traversal.
  if (ref.asset != normalizePath(ref.asset) || ref.asset.contains(QLatin1Char('/'))) {
    return false;
  }

  *p_out = ref;
  return true;
}

UpdateManifest UpdateManifest::fromJson(const QJsonObject &p_obj, QString *p_error) {
  UpdateManifest m;

  const QJsonValue schemaVal = p_obj.value(c_keySchema);
  if (!schemaVal.isDouble()) {
    setError(p_error, QStringLiteral("missing or non-numeric 'schema'"));
    return m;
  }
  m.m_schema = schemaVal.toInt();
  if (m.m_schema != c_supportedSchema) {
    setError(p_error, QStringLiteral("unsupported schema %1 (expected %2)")
                          .arg(m.m_schema)
                          .arg(c_supportedSchema));
    return m;
  }

  m.m_product = p_obj.value(c_keyProduct).toString();
  m.m_channel = p_obj.value(c_keyChannel).toString();
  m.m_version = p_obj.value(c_keyVersion).toString();
  m.m_variant = p_obj.value(c_keyVariant).toString();
  m.m_platform = p_obj.value(c_keyPlatform).toString();
  m.m_commit = p_obj.value(c_keyCommit).toString();
  m.m_generatedAt = p_obj.value(c_keyGeneratedAt).toString();

  if (m.m_version.isEmpty()) {
    setError(p_error, QStringLiteral("missing 'version'"));
    return m;
  }
  if (m.m_variant.isEmpty()) {
    setError(p_error, QStringLiteral("missing 'variant'"));
    return m;
  }
  if (m.m_platform.isEmpty()) {
    setError(p_error, QStringLiteral("missing 'platform'"));
    return m;
  }
  if (m.m_channel.isEmpty()) {
    setError(p_error, QStringLiteral("missing 'channel'"));
    return m;
  }

  const QJsonValue filesVal = p_obj.value(c_keyFiles);
  if (!filesVal.isArray()) {
    setError(p_error, QStringLiteral("missing or non-array 'files'"));
    return m;
  }

  const QJsonArray filesArr = filesVal.toArray();
  m.m_files.reserve(filesArr.size());
  for (const QJsonValue &entryVal : filesArr) {
    if (!entryVal.isObject()) {
      setError(p_error, QStringLiteral("'files' entry is not an object"));
      return UpdateManifest();
    }
    const QJsonObject entry = entryVal.toObject();

    UpdateManifestFile f;
    const QString rawPath = entry.value(c_keyPath).toString();
    f.path = normalizePath(rawPath);
    if (f.path.isEmpty()) {
      setError(p_error, QStringLiteral("unsafe or malformed path '%1'").arg(rawPath));
      return UpdateManifest();
    }
    if (isReservedPath(f.path)) {
      setError(p_error, QStringLiteral("path '%1' is inside a reserved directory").arg(f.path));
      return UpdateManifest();
    }
    if (pathKey(f.path) == pathKey(manifestFileName())) {
      setError(p_error, QStringLiteral("'files' must not contain manifest.json itself"));
      return UpdateManifest();
    }

    const QJsonValue sizeVal = entry.value(c_keySize);
    if (!sizeVal.isDouble()) {
      setError(p_error, QStringLiteral("missing or non-numeric 'size' for '%1'").arg(f.path));
      return UpdateManifest();
    }
    f.size = static_cast<qint64>(sizeVal.toDouble());
    if (f.size < 0) {
      setError(p_error, QStringLiteral("negative 'size' for '%1'").arg(f.path));
      return UpdateManifest();
    }

    f.sha256 = entry.value(c_keySha256).toString().toLower();
    if (!isSha256Hex(f.sha256)) {
      setError(p_error, QStringLiteral("invalid 'sha256' for '%1'").arg(f.path));
      return UpdateManifest();
    }

    const QString key = pathKey(f.path);
    if (m.m_fileMap.contains(key)) {
      setError(p_error, QStringLiteral("duplicate path '%1' (case-insensitive)").arg(f.path));
      return UpdateManifest();
    }

    m.m_fileMap.insert(key, f);
    m.m_files.append(f);
  }

  // Optional release-asset-only blocks. A malformed block is a hard error: it
  // would otherwise silently degrade to "no delta available" and mask a broken
  // generator.
  if (p_obj.contains(c_keyFullPackage)) {
    if (!p_obj.value(c_keyFullPackage).isObject() ||
        !parseArchiveRef(p_obj.value(c_keyFullPackage).toObject(), &m.m_fullPackage)) {
      setError(p_error, QStringLiteral("malformed 'fullPackage' block"));
      return UpdateManifest();
    }
  }

  if (p_obj.contains(c_keyDelta)) {
    if (!p_obj.value(c_keyDelta).isObject() ||
        !parseArchiveRef(p_obj.value(c_keyDelta).toObject(), &m.m_delta) ||
        m.m_delta.baseVersion.isEmpty()) {
      setError(p_error, QStringLiteral("malformed 'delta' block"));
      return UpdateManifest();
    }
    if (m.m_delta.baseVersion == m.m_version) {
      setError(p_error, QStringLiteral("'delta.baseVersion' equals 'version'"));
      return UpdateManifest();
    }
  }

  m.m_valid = true;
  return m;
}

UpdateManifest UpdateManifest::fromJsonBytes(const QByteArray &p_bytes, QString *p_error) {
  QJsonParseError parseError{};
  const QJsonDocument doc = QJsonDocument::fromJson(p_bytes, &parseError);
  if (parseError.error != QJsonParseError::NoError) {
    setError(p_error, QStringLiteral("JSON parse error at offset %1: %2")
                          .arg(parseError.offset)
                          .arg(parseError.errorString()));
    return UpdateManifest();
  }
  if (!doc.isObject()) {
    setError(p_error, QStringLiteral("manifest root is not a JSON object"));
    return UpdateManifest();
  }
  return fromJson(doc.object(), p_error);
}

QJsonObject UpdateManifest::toJson() const {
  QJsonObject obj;
  obj[c_keySchema] = m_schema;
  obj[c_keyProduct] = m_product;
  obj[c_keyChannel] = m_channel;
  obj[c_keyVersion] = m_version;
  obj[c_keyVariant] = m_variant;
  obj[c_keyPlatform] = m_platform;
  obj[c_keyCommit] = m_commit;
  obj[c_keyGeneratedAt] = m_generatedAt;

  QJsonArray filesArr;
  for (const auto &f : m_files) {
    QJsonObject entry;
    entry[c_keyPath] = f.path;
    entry[c_keySize] = static_cast<double>(f.size);
    entry[c_keySha256] = f.sha256;
    filesArr.append(entry);
  }
  obj[c_keyFiles] = filesArr;

  auto writeRef = [](const UpdateArchiveRef &p_ref, bool p_withBase) {
    QJsonObject o;
    o[c_keyAsset] = p_ref.asset;
    o[c_keySize] = static_cast<double>(p_ref.size);
    o[c_keySha256] = p_ref.sha256;
    if (p_withBase) {
      o[c_keyBaseVersion] = p_ref.baseVersion;
    }
    return o;
  };

  if (m_fullPackage.isValid()) {
    obj[c_keyFullPackage] = writeRef(m_fullPackage, false);
  }
  if (hasDelta()) {
    obj[c_keyDelta] = writeRef(m_delta, true);
  }

  return obj;
}

qint64 UpdateManifest::totalExpandedSize() const {
  qint64 total = 0;
  for (const auto &f : m_files) {
    total += f.size;
  }
  return total;
}

bool UpdateManifest::lookup(const QString &p_path, UpdateManifestFile *p_out) const {
  const auto it = m_fileMap.constFind(pathKey(p_path));
  if (it == m_fileMap.constEnd()) {
    return false;
  }
  if (p_out) {
    *p_out = it.value();
  }
  return true;
}

UpdateManifest::Diff UpdateManifest::diff(const UpdateManifest &p_base,
                                          const UpdateManifest &p_target) {
  Diff d;

  for (const auto &tf : p_target.files()) {
    UpdateManifestFile bf;
    if (!p_base.lookup(tf.path, &bf)) {
      d.added.append(tf.path);
    } else if (bf.sha256.compare(tf.sha256, Qt::CaseInsensitive) != 0) {
      d.changed.append(tf.path);
    }
  }

  for (const auto &bf : p_base.files()) {
    if (!p_target.lookup(bf.path)) {
      d.removed.append(bf.path);
    }
  }

  d.added = sortedUnique(d.added);
  d.changed = sortedUnique(d.changed);
  d.removed = sortedUnique(d.removed);
  return d;
}

QStringList UpdateManifest::expectedChanged(const UpdateManifest &p_base,
                                            const UpdateManifest &p_target) {
  QStringList out;
  for (const auto &tf : p_target.files()) {
    UpdateManifestFile bf;
    if (!p_base.lookup(tf.path, &bf) || bf.sha256.compare(tf.sha256, Qt::CaseInsensitive) != 0) {
      out.append(tf.path);
    }
  }
  return sortedUnique(out);
}

QStringList UpdateManifest::hopArchiveSet(const UpdateManifest &p_hopBase,
                                          const UpdateManifest &p_hopTarget) {
  // Identical formula; deletions are derived and carry no archive entry.
  return expectedChanged(p_hopBase, p_hopTarget);
}

QStringList UpdateManifest::deletions(const UpdateManifest &p_base,
                                      const UpdateManifest &p_target) {
  QStringList out;
  for (const auto &bf : p_base.files()) {
    if (!p_target.lookup(bf.path)) {
      out.append(bf.path);
    }
  }
  return sortedUnique(out);
}

UpdateManifest::ChainResult
UpdateManifest::resolveChain(const UpdateManifest &p_newest, const QString &p_localVersion,
                             const QHash<QString, UpdateManifest> &p_available) {
  ChainResult result;

  if (!p_newest.isValid() || p_localVersion.isEmpty()) {
    result.status = ChainStatus::InvalidInput;
    return result;
  }

  if (p_newest.version() == p_localVersion) {
    result.status = ChainStatus::AlreadyCurrent;
    return result;
  }

  if (!p_newest.isStableChannel()) {
    result.status = ChainStatus::NonStableBase;
    return result;
  }

  // Walk newest -> oldest, collecting hops. Every visited manifest contributes
  // its own delta archive.
  QStringList reverseHops;
  qint64 totalDelta = 0;

  UpdateManifest cursor = p_newest;
  QSet<QString> visited;
  visited.insert(cursor.version());

  for (int hop = 0; hop < c_maxChainHops; ++hop) {
    if (!cursor.hasDelta()) {
      result.status = ChainStatus::MissingDelta;
      return result;
    }

    reverseHops.append(cursor.version());
    totalDelta += cursor.delta().size;

    const QString baseVersion = cursor.delta().baseVersion;
    if (baseVersion == p_localVersion) {
      // Reached the installed version.
      std::reverse(reverseHops.begin(), reverseHops.end());
      result.hopVersions = reverseHops;
      result.totalDeltaSize = totalDelta;

      const qint64 cap =
          static_cast<qint64>(c_maxChainSizeRatio * static_cast<double>(p_newest.totalExpandedSize()));
      if (totalDelta > cap) {
        result.status = ChainStatus::TooLarge;
        result.hopVersions.clear();
        return result;
      }

      result.status = ChainStatus::Ok;
      return result;
    }

    if (visited.contains(baseVersion)) {
      // A cycle in baseVersion pointers.
      result.status = ChainStatus::BrokenChain;
      return result;
    }
    visited.insert(baseVersion);

    const auto it = p_available.constFind(baseVersion);
    if (it == p_available.constEnd() || !it.value().isValid()) {
      result.status = ChainStatus::BrokenChain;
      return result;
    }

    cursor = it.value();
    if (!cursor.isStableChannel()) {
      result.status = ChainStatus::NonStableBase;
      return result;
    }
    if (cursor.variant() != p_newest.variant() || cursor.platform() != p_newest.platform()) {
      result.status = ChainStatus::BrokenChain;
      return result;
    }
  }

  result.status = ChainStatus::TooManyHops;
  return result;
}

bool UpdateManifest::validateBaseIdentity(const UpdateManifest &p_local,
                                          const UpdateManifest &p_published, QString *p_error) {
  if (!p_local.isValid()) {
    setError(p_error, QStringLiteral("local manifest is invalid"));
    return false;
  }
  if (!p_published.isValid()) {
    setError(p_error, QStringLiteral("published manifest is invalid"));
    return false;
  }

  if (p_local.version() != p_published.version()) {
    setError(p_error, QStringLiteral("version mismatch: local '%1' vs published '%2'")
                          .arg(p_local.version(), p_published.version()));
    return false;
  }
  if (p_local.variant() != p_published.variant()) {
    setError(p_error, QStringLiteral("variant mismatch: local '%1' vs published '%2'")
                          .arg(p_local.variant(), p_published.variant()));
    return false;
  }
  if (p_local.platform() != p_published.platform()) {
    setError(p_error, QStringLiteral("platform mismatch: local '%1' vs published '%2'")
                          .arg(p_local.platform(), p_published.platform()));
    return false;
  }
  if (p_local.commit() != p_published.commit()) {
    setError(p_error, QStringLiteral("commit mismatch: local '%1' vs published '%2'")
                          .arg(p_local.commit(), p_published.commit()));
    return false;
  }
  if (!p_local.isStableChannel() || !p_published.isStableChannel()) {
    setError(p_error, QStringLiteral("channel is not '%1' (local '%2', published '%3')")
                          .arg(stableChannel(), p_local.channel(), p_published.channel()));
    return false;
  }

  if (p_local.fileMap().size() != p_published.fileMap().size()) {
    setError(p_error, QStringLiteral("files[] size mismatch: local %1 vs published %2")
                          .arg(p_local.fileMap().size())
                          .arg(p_published.fileMap().size()));
    return false;
  }

  for (auto it = p_published.fileMap().constBegin(); it != p_published.fileMap().constEnd();
       ++it) {
    const auto localIt = p_local.fileMap().constFind(it.key());
    if (localIt == p_local.fileMap().constEnd()) {
      setError(p_error, QStringLiteral("local manifest is missing '%1'").arg(it.value().path));
      return false;
    }
    if (localIt.value() != it.value()) {
      setError(p_error, QStringLiteral("entry mismatch for '%1'").arg(it.value().path));
      return false;
    }
  }

  return true;
}
