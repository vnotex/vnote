#include "zipextractor.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSet>

#include <miniz.h>

#include "updatemanifest.h"

#ifdef Q_OS_WIN
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

using namespace vnotex;

namespace {

// ---------------------------------------------------------------------------
// miniz I/O shim
// ---------------------------------------------------------------------------
// miniz's own mz_zip_reader_init_file() goes through fopen(), which on Windows
// (MSVC) is ANSI/locale-dependent and therefore cannot open an archive under a
// path containing non-ASCII characters. VNote installs live wherever the user
// unpacked them, so we drive miniz through its custom-I/O hook backed by a
// QFile instead.
struct QFileZipSource {
  QFile file;
};

size_t qfileReadFunc(void *p_opaque, mz_uint64 p_offset, void *p_buf, size_t p_n) {
  auto *src = static_cast<QFileZipSource *>(p_opaque);
  if (!src->file.seek(static_cast<qint64>(p_offset))) {
    return 0;
  }
  const qint64 read = src->file.read(static_cast<char *>(p_buf), static_cast<qint64>(p_n));
  return read < 0 ? 0 : static_cast<size_t>(read);
}

// RAII wrapper: opens the archive and initializes the miniz reader.
class ZipReader {
public:
  explicit ZipReader(const QString &p_archivePath) {
    memset(&m_zip, 0, sizeof(m_zip));
    m_source.file.setFileName(p_archivePath);
    if (!m_source.file.open(QIODevice::ReadOnly)) {
      return;
    }
    m_opened = true;

    m_zip.m_pRead = &qfileReadFunc;
    m_zip.m_pIO_opaque = &m_source;

    // NOTE: on failure the file stays OPEN so callers can distinguish "cannot
    // read the archive file" (OpenFailed) from "the bytes are not a valid ZIP"
    // (CorruptArchive). The destructor closes it either way.
    m_initialized = mz_zip_reader_init(&m_zip, static_cast<mz_uint64>(m_source.file.size()), 0) !=
                    MZ_FALSE;
  }

  ~ZipReader() {
    if (m_initialized) {
      mz_zip_reader_end(&m_zip);
    }
    if (m_source.file.isOpen()) {
      m_source.file.close();
    }
  }

  ZipReader(const ZipReader &) = delete;
  ZipReader &operator=(const ZipReader &) = delete;

  bool isOpen() const { return m_opened; }
  bool isInitialized() const { return m_initialized; }
  mz_zip_archive *zip() { return &m_zip; }

private:
  QFileZipSource m_source;
  mz_zip_archive m_zip{};
  bool m_opened = false;
  bool m_initialized = false;
};

// ---------------------------------------------------------------------------
// Filesystem helpers
// ---------------------------------------------------------------------------

// Authoritative reparse-point test. QFileInfo::isSymLink() has historically
// varied in how it reports NTFS junctions across Qt versions, so query the
// attribute directly on Windows.
bool isReparsePoint(const QString &p_path) {
#ifdef Q_OS_WIN
  const DWORD attrs = ::GetFileAttributesW(reinterpret_cast<const wchar_t *>(
      QDir::toNativeSeparators(p_path).utf16()));
  if (attrs == INVALID_FILE_ATTRIBUTES) {
    return false; // Does not exist: nothing to traverse.
  }
  return (attrs & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
#else
  const QFileInfo info(p_path);
  return info.exists() && info.isSymLink();
#endif
}

// True when any EXISTING component of p_relativePath under p_root is a reparse
// point. The final component is included: overwriting through a symlinked file
// would write outside the tree just as surely as traversing a junction.
bool anyComponentIsReparsePoint(const QString &p_root, const QString &p_relativePath) {
  const QStringList parts = p_relativePath.split(QLatin1Char('/'), Qt::SkipEmptyParts);
  QString current = p_root;
  for (const QString &part : parts) {
    current += QLatin1Char('/');
    current += part;
    if (isReparsePoint(current)) {
      return true;
    }
  }
  return false;
}

// Re-verified at every write, not just once at planning time.
bool isContained(const QString &p_root, const QString &p_target) {
  const QString root = QDir::cleanPath(p_root);
  const QString target = QDir::cleanPath(p_target);
  if (target == root) {
    return false;
  }
  // Windows path comparison is case-insensitive.
  return target.startsWith(root + QLatin1Char('/'), Qt::CaseInsensitive);
}

QString zipEntryName(mz_zip_archive *p_zip, mz_uint p_index) {
  const mz_uint size = mz_zip_reader_get_filename(p_zip, p_index, nullptr, 0);
  if (size == 0) {
    return QString();
  }
  QByteArray buf(static_cast<int>(size), '\0');
  mz_zip_reader_get_filename(p_zip, p_index, buf.data(), size);
  // Trim the NUL miniz appends.
  const int nul = buf.indexOf('\0');
  if (nul >= 0) {
    buf.truncate(nul);
  }
  // ZIP names are either UTF-8 (general purpose bit 11, which every producer we
  // care about sets) or CP437. Decoding as UTF-8 is correct for our producers
  // and is lossless for pure ASCII, which is all a CP437 archive of ours could
  // legitimately contain.
  return QString::fromUtf8(buf);
}

ZipExtractor::Result makeError(ZipExtractor::Status p_status, const QString &p_message) {
  ZipExtractor::Result r;
  r.status = p_status;
  r.message = p_message;
  return r;
}

struct WriteContext {
  QFile *file = nullptr;
  bool failed = false;
};

size_t writeToQFile(void *p_opaque, mz_uint64 /*p_offset*/, const void *p_buf, size_t p_n) {
  auto *ctx = static_cast<WriteContext *>(p_opaque);
  if (ctx->failed) {
    return 0;
  }
  const qint64 written = ctx->file->write(static_cast<const char *>(p_buf), static_cast<qint64>(p_n));
  if (written != static_cast<qint64>(p_n)) {
    ctx->failed = true;
    return 0;
  }
  return p_n;
}

// Discard sink: used to force full decompression (and miniz's own CRC-32 check)
// without materializing anything on disk.
size_t discardSink(void * /*p_opaque*/, mz_uint64 /*p_offset*/, const void * /*p_buf*/,
                   size_t p_n) {
  return p_n;
}

} // namespace

QString ZipExtractor::statusToString(Status p_status) {
  switch (p_status) {
  case Status::Ok:
    return QStringLiteral("ok");
  case Status::OpenFailed:
    return QStringLiteral("archive could not be opened");
  case Status::CorruptArchive:
    return QStringLiteral("archive is corrupt");
  case Status::UnsafePath:
    return QStringLiteral("archive contains an unsafe path");
  case Status::DuplicatePath:
    return QStringLiteral("archive contains duplicate paths");
  case Status::PathTypeConflict:
    return QStringLiteral("archive declares a path as both file and directory");
  case Status::ReservedPath:
    return QStringLiteral("archive targets a reserved directory");
  case Status::EntrySetMismatch:
    return QStringLiteral("archive entry set does not match the manifest");
  case Status::SizeCapExceeded:
    return QStringLiteral("archive exceeds the expansion cap");
  case Status::TopLevelDirMismatch:
    return QStringLiteral("archive does not have exactly one top-level directory");
  case Status::ContainmentViolation:
    return QStringLiteral("archive entry escapes the destination directory");
  case Status::ReparsePointRefused:
    return QStringLiteral("destination path traverses a reparse point");
  case Status::WriteFailed:
    return QStringLiteral("failed to write extracted file");
  }
  return QStringLiteral("unknown");
}

ZipExtractor::Result ZipExtractor::validate(const QString &p_archivePath,
                                            const Options &p_options,
                                            QVector<Entry> *p_outEntries, bool p_verifyPayload) {
  ZipReader reader(p_archivePath);
  if (!reader.isOpen()) {
    return makeError(Status::OpenFailed,
                     QStringLiteral("cannot open archive '%1'").arg(p_archivePath));
  }
  if (!reader.isInitialized()) {
    return makeError(Status::CorruptArchive,
                     QStringLiteral("miniz rejected archive '%1'").arg(p_archivePath));
  }

  mz_zip_archive *zip = reader.zip();
  const mz_uint count = mz_zip_reader_get_num_files(zip);
  if (static_cast<int>(count) > p_options.maxEntries) {
    return makeError(Status::SizeCapExceeded,
                     QStringLiteral("archive has %1 entries, cap is %2")
                         .arg(count)
                         .arg(p_options.maxEntries));
  }

  // --- Pass 1: raw names, top-level-directory detection -------------------
  QVector<QString> rawNames;
  rawNames.reserve(static_cast<int>(count));
  QString topDir;
  for (mz_uint i = 0; i < count; ++i) {
    QString name = zipEntryName(zip, i);
    if (name.isEmpty()) {
      return makeError(Status::CorruptArchive,
                       QStringLiteral("entry %1 has an empty name").arg(i));
    }
    name.replace(QLatin1Char('\\'), QLatin1Char('/'));
    rawNames.append(name);

    if (p_options.stripTopLevelDir) {
      const int slash = name.indexOf(QLatin1Char('/'));
      if (slash <= 0) {
        // A root-level file (or a name starting with '/'): there is no single
        // wrapping directory to strip.
        return makeError(Status::TopLevelDirMismatch,
                         QStringLiteral("entry '%1' is not under a top-level directory").arg(name));
      }
      const QString candidate = name.left(slash);
      if (topDir.isEmpty()) {
        topDir = candidate;
      } else if (topDir.compare(candidate, Qt::CaseInsensitive) != 0) {
        return makeError(Status::TopLevelDirMismatch,
                         QStringLiteral("archive has multiple top-level directories ('%1', '%2')")
                             .arg(topDir, candidate));
      }
    }
  }

  if (p_options.stripTopLevelDir && topDir.isEmpty()) {
    return makeError(Status::TopLevelDirMismatch,
                     QStringLiteral("archive has no top-level directory to strip"));
  }

  // --- Pass 2: normalize, classify, and check every rule ------------------
  QVector<Entry> entries;
  entries.reserve(static_cast<int>(count));

  QSet<QString> fileKeys;
  QSet<QString> dirKeys;
  qint64 totalUncompressed = 0;

  for (mz_uint i = 0; i < count; ++i) {
    QString name = rawNames.at(static_cast<int>(i));

    if (p_options.stripTopLevelDir) {
      name = name.mid(topDir.size() + 1);
      if (name.isEmpty()) {
        // The top-level directory entry itself. Nothing to extract.
        continue;
      }
    }

    const bool isDir = mz_zip_reader_is_file_a_directory(zip, i) || name.endsWith(QLatin1Char('/'));
    QString bare = name;
    while (bare.endsWith(QLatin1Char('/'))) {
      bare.chop(1);
    }
    if (bare.isEmpty()) {
      continue;
    }

    const QString normalized = UpdateManifest::normalizePath(bare);
    if (normalized.isEmpty()) {
      return makeError(Status::UnsafePath,
                       QStringLiteral("unsafe entry path '%1'").arg(name));
    }
    if (UpdateManifest::isReservedPath(normalized)) {
      return makeError(Status::ReservedPath,
                       QStringLiteral("entry '%1' targets a reserved directory").arg(normalized));
    }

    const QString key = UpdateManifest::pathKey(normalized);

    if (isDir) {
      if (fileKeys.contains(key)) {
        return makeError(Status::PathTypeConflict,
                         QStringLiteral("'%1' is declared as both file and directory")
                             .arg(normalized));
      }
      if (dirKeys.contains(key)) {
        return makeError(Status::DuplicatePath,
                         QStringLiteral("duplicate directory entry '%1'").arg(normalized));
      }
      dirKeys.insert(key);

      Entry e;
      e.path = normalized;
      e.isDirectory = true;
      e.index = i;
      entries.append(e);
      continue;
    }

    if (dirKeys.contains(key)) {
      return makeError(Status::PathTypeConflict,
                       QStringLiteral("'%1' is declared as both file and directory").arg(normalized));
    }
    if (fileKeys.contains(key)) {
      return makeError(Status::DuplicatePath,
                       QStringLiteral("duplicate entry '%1' (case-insensitive)").arg(normalized));
    }
    fileKeys.insert(key);

    mz_zip_archive_file_stat stat;
    if (!mz_zip_reader_file_stat(zip, i, &stat)) {
      return makeError(Status::CorruptArchive,
                       QStringLiteral("cannot stat entry '%1'").arg(normalized));
    }

    const qint64 uncompressed = static_cast<qint64>(stat.m_uncomp_size);
    if (uncompressed < 0) {
      return makeError(Status::CorruptArchive,
                       QStringLiteral("entry '%1' declares a negative size").arg(normalized));
    }

    totalUncompressed += uncompressed;
    if (totalUncompressed > p_options.maxTotalUncompressedSize) {
      return makeError(Status::SizeCapExceeded,
                       QStringLiteral("total uncompressed size exceeds cap of %1 bytes")
                           .arg(p_options.maxTotalUncompressedSize));
    }

    Entry e;
    e.path = normalized;
    e.uncompressedSize = uncompressed;
    e.isDirectory = false;
    e.index = i;
    entries.append(e);
  }

  // A directory entry must not collide with a file entry declared later.
  for (const QString &dirKey : dirKeys) {
    if (fileKeys.contains(dirKey)) {
      return makeError(Status::PathTypeConflict,
                       QStringLiteral("'%1' is declared as both file and directory").arg(dirKey));
    }
  }

  // A file must not be nested under something another entry declares as a file.
  for (const QString &fileKey : fileKeys) {
    int slash = fileKey.lastIndexOf(QLatin1Char('/'));
    while (slash > 0) {
      const QString ancestor = fileKey.left(slash);
      if (fileKeys.contains(ancestor)) {
        return makeError(Status::PathTypeConflict,
                         QStringLiteral("'%1' is a file but also a parent directory of '%2'")
                             .arg(ancestor, fileKey));
      }
      slash = ancestor.lastIndexOf(QLatin1Char('/'));
    }
  }

  // --- Entry-set / declared-size equality against the manifest ------------
  if (!p_options.expectedEntries.isEmpty()) {
    if (fileKeys.size() != p_options.expectedEntries.size()) {
      return makeError(Status::EntrySetMismatch,
                       QStringLiteral("archive has %1 file entries, expected %2")
                           .arg(fileKeys.size())
                           .arg(p_options.expectedEntries.size()));
    }
    for (const Entry &e : entries) {
      if (e.isDirectory) {
        continue;
      }
      const QString key = UpdateManifest::pathKey(e.path);
      const auto it = p_options.expectedEntries.constFind(key);
      if (it == p_options.expectedEntries.constEnd()) {
        return makeError(Status::EntrySetMismatch,
                         QStringLiteral("unexpected archive entry '%1'").arg(e.path));
      }
      if (it.value() >= 0 && it.value() != e.uncompressedSize) {
        return makeError(Status::EntrySetMismatch,
                         QStringLiteral("entry '%1' declares %2 bytes, manifest says %3")
                             .arg(e.path)
                             .arg(e.uncompressedSize)
                             .arg(it.value()));
      }
    }
  }

  if (p_outEntries) {
    *p_outEntries = entries;
  }

  // --- Full decompression pass, to a discard sink -------------------------
  //
  // Everything above validates only the CENTRAL DIRECTORY. A structurally sound
  // archive can still carry a corrupt compressed stream, and extract() writes
  // entries one at a time -- so without this pass a bad byte in the LAST entry
  // would be discovered only after every earlier entry had already been written
  // into the destination, breaking the "nothing is written unless the whole
  // archive is good" contract.
  //
  // mz_zip_reader_extract_to_callback verifies the CRC-32 of each entry as it
  // inflates, so this doubles as an integrity check. Cost is one extra inflate
  // of the archive; correctness of the staging tree is worth it.
  if (p_verifyPayload) {
    for (const Entry &e : entries) {
      if (e.isDirectory) {
        continue;
      }
      if (!mz_zip_reader_extract_to_callback(zip, e.index, &discardSink, nullptr, 0)) {
        return makeError(Status::CorruptArchive,
                         QStringLiteral("entry '%1' failed to decompress or its CRC is wrong")
                             .arg(e.path));
      }
    }
  }

  Result ok;
  ok.status = Status::Ok;
  return ok;
}

ZipExtractor::Result ZipExtractor::extract(const QString &p_archivePath, const QString &p_destDir,
                                           const Options &p_options) {
  QVector<Entry> entries;
  Result validation = validate(p_archivePath, p_options, &entries, /*verifyPayload*/ true);
  if (!validation.isOk()) {
    return validation;
  }

  QDir destDir(p_destDir);
  if (!destDir.exists() && !QDir().mkpath(p_destDir)) {
    return makeError(Status::WriteFailed,
                     QStringLiteral("cannot create destination '%1'").arg(p_destDir));
  }

  // Resolve the destination root ONCE, after creation, so containment checks
  // compare against a real path.
  const QString root = QDir::cleanPath(QFileInfo(p_destDir).absoluteFilePath());

  ZipReader reader(p_archivePath);
  if (!reader.isInitialized()) {
    // Between validate() and here the archive changed or vanished.
    return makeError(Status::CorruptArchive,
                     QStringLiteral("archive '%1' became unreadable").arg(p_archivePath));
  }
  mz_zip_archive *zip = reader.zip();

  Result result;
  result.status = Status::Ok;

  for (const Entry &e : entries) {
    const QString target = QDir::cleanPath(root + QLatin1Char('/') + e.path);

    // Re-verify containment for EVERY entry, not once up front.
    if (!isContained(root, target)) {
      return makeError(Status::ContainmentViolation,
                       QStringLiteral("entry '%1' escapes '%2'").arg(e.path, root));
    }
    if (anyComponentIsReparsePoint(root, e.path)) {
      return makeError(Status::ReparsePointRefused,
                       QStringLiteral("path '%1' traverses a reparse point").arg(e.path));
    }

    if (e.isDirectory) {
      if (!QDir().mkpath(target)) {
        return makeError(Status::WriteFailed,
                         QStringLiteral("cannot create directory '%1'").arg(target));
      }
      continue;
    }

    const QString parent = QFileInfo(target).absolutePath();
    if (!QDir().mkpath(parent)) {
      return makeError(Status::WriteFailed,
                       QStringLiteral("cannot create directory '%1'").arg(parent));
    }
    // mkpath may have followed a component that only just appeared; re-check.
    if (anyComponentIsReparsePoint(root, e.path)) {
      return makeError(Status::ReparsePointRefused,
                       QStringLiteral("path '%1' traverses a reparse point").arg(e.path));
    }

    QFile out(target);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
      return makeError(Status::WriteFailed,
                       QStringLiteral("cannot write '%1': %2").arg(target, out.errorString()));
    }

    WriteContext ctx;
    ctx.file = &out;
    const mz_bool extracted =
        mz_zip_reader_extract_to_callback(zip, e.index, &writeToQFile, &ctx, 0);
    const bool flushed = out.flush();
    out.close();

    if (!extracted || ctx.failed || !flushed) {
      // Do not leave a truncated file behind.
      QFile::remove(target);
      return makeError(ctx.failed || !flushed ? Status::WriteFailed : Status::CorruptArchive,
                       QStringLiteral("failed to extract '%1'").arg(e.path));
    }

    result.extractedPaths.append(e.path);
  }

  return result;
}

bool ZipExtractor::readEntry(const QString &p_archivePath, const QString &p_entryPath,
                             QByteArray *p_out, const Options &p_options) {
  if (!p_out) {
    return false;
  }

  QVector<Entry> entries;
  if (!validate(p_archivePath, p_options, &entries, /*verifyPayload*/ false).isOk()) {
    return false;
  }

  const QString wantKey = UpdateManifest::pathKey(UpdateManifest::normalizePath(p_entryPath));
  if (wantKey.isEmpty()) {
    return false;
  }

  const Entry *match = nullptr;
  for (const Entry &e : entries) {
    if (!e.isDirectory && UpdateManifest::pathKey(e.path) == wantKey) {
      match = &e;
      break;
    }
  }
  if (!match) {
    return false;
  }

  ZipReader reader(p_archivePath);
  if (!reader.isInitialized()) {
    return false;
  }

  size_t size = 0;
  void *data = mz_zip_reader_extract_to_heap(reader.zip(), match->index, &size, 0);
  if (!data) {
    return false;
  }
  *p_out = QByteArray(static_cast<const char *>(data), static_cast<int>(size));
  mz_free(data);
  return true;
}

bool ZipExtractor::createArchive(const QString &p_archivePath,
                                 const QVector<QPair<QString, QByteArray>> &p_entries,
                                 const QStringList &p_directoryEntries) {
  QFile::remove(p_archivePath);

  mz_zip_archive zip;
  memset(&zip, 0, sizeof(zip));

  // miniz's writer needs a real file path. Build into a temporary heap archive
  // and write the bytes out through QFile so non-ASCII paths still work.
  if (!mz_zip_writer_init_heap(&zip, 0, 64 * 1024)) {
    return false;
  }

  bool ok = true;

  for (const QString &dir : p_directoryEntries) {
    QString name = dir;
    if (!name.endsWith(QLatin1Char('/'))) {
      name += QLatin1Char('/');
    }
    const QByteArray utf8 = name.toUtf8();
    if (!mz_zip_writer_add_mem(&zip, utf8.constData(), nullptr, 0, MZ_NO_COMPRESSION)) {
      ok = false;
      break;
    }
  }

  if (ok) {
    for (const auto &entry : p_entries) {
      const QByteArray utf8 = entry.first.toUtf8();
      if (!mz_zip_writer_add_mem(&zip, utf8.constData(), entry.second.constData(),
                                 static_cast<size_t>(entry.second.size()),
                                 MZ_DEFAULT_COMPRESSION)) {
        ok = false;
        break;
      }
    }
  }

  void *buf = nullptr;
  size_t bufSize = 0;
  if (ok) {
    ok = mz_zip_writer_finalize_heap_archive(&zip, &buf, &bufSize);
  }
  mz_zip_writer_end(&zip);

  if (!ok || !buf) {
    if (buf) {
      mz_free(buf);
    }
    return false;
  }

  QFile out(p_archivePath);
  if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
    mz_free(buf);
    return false;
  }
  const qint64 written = out.write(static_cast<const char *>(buf), static_cast<qint64>(bufSize));
  const bool flushed = out.flush();
  out.close();
  mz_free(buf);

  return written == static_cast<qint64>(bufSize) && flushed;
}

