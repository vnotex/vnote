#ifndef ZIPEXTRACTOR_H
#define ZIPEXTRACTOR_H

#include <QByteArray>
#include <QHash>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QtGlobal>

namespace vnotex {

// Hardened ZIP reader over vendored miniz (libs/miniz), used by the incremental
// updater. Implements step 7 of the client algorithm in
// .kilo/plans/1785337074532-incremental-update-plan.md:
//
//   "Archive validation before writing any bytes."
//
// The whole central directory is validated FIRST; extraction only starts once
// every entry has been proven safe. Containment is then re-verified at every
// write, and reparse points are never traversed.
//
// Pure: no widgets, no network, no Qt application object. Lives in
// core_services so tests can link it.
class ZipExtractor {
public:
  enum class Status {
    Ok,
    // Could not open / read the archive file itself.
    OpenFailed,
    // miniz rejected the archive, or an entry failed to decompress.
    CorruptArchive,
    // An entry path is absolute, drive-qualified, UNC, device-namespaced,
    // contains "..", uses a reserved Windows name, or aliases via a trailing
    // dot/space.
    UnsafePath,
    // Two entries normalize to the same case-folded path.
    DuplicatePath,
    // The same path appears as both a file and a directory.
    PathTypeConflict,
    // An entry lives under .vnote-update/ or .vnote-old/.
    ReservedPath,
    // The archive's entry set does not equal the expected set, or a declared
    // uncompressed size disagrees with the manifest.
    EntrySetMismatch,
    // Total declared uncompressed size exceeds the configured cap, or the entry
    // count exceeds the cap.
    SizeCapExceeded,
    // stripTopLevelDir was requested but the archive has zero or multiple
    // top-level directories, or a root-level file.
    TopLevelDirMismatch,
    // A destination path escaped the destination root.
    ContainmentViolation,
    // A path component is a symlink / junction / other reparse point.
    ReparsePointRefused,
    // Could not create a directory or write a file.
    WriteFailed,
  };

  struct Entry {
    // Normalized, forward-slash, destination-root-relative path. The top-level
    // directory has already been stripped when Options::stripTopLevelDir is set.
    QString path;

    // Declared uncompressed size from the central directory.
    qint64 uncompressedSize = 0;

    bool isDirectory = false;

    // miniz index, used by extract().
    quint32 index = 0;
  };

  struct Options {
    // Strip exactly one top-level directory. CPack's full ZIP wraps everything
    // in "VNote-<ver>-<variant>/"; delta ZIPs have no top directory.
    bool stripTopLevelDir = false;

    // Hard cap on the SUM of declared uncompressed sizes (zip-bomb defense).
    qint64 maxTotalUncompressedSize = 4LL * 1024 * 1024 * 1024;

    // Hard cap on the number of central-directory entries.
    int maxEntries = 200000;

    // When non-empty, the archive's FILE entry set must equal this map's key
    // set exactly (keys are UpdateManifest::pathKey values), and each entry's
    // declared uncompressed size must equal the mapped value. A mapped value
    // < 0 means "any size".
    //
    // This is what enforces the per-hop entry-set equality of step 8 and the
    // "declared uncompressed size disagreeing with the manifest" rule of step 7.
    QHash<QString, qint64> expectedEntries;
  };

  struct Result {
    Status status = Status::Ok;

    // Human-readable detail, always populated on failure.
    QString message;

    // Normalized paths of the FILES written by extract(), in archive order.
    // Directories are not listed.
    QStringList extractedPaths;

    bool isOk() const { return status == Status::Ok; }
  };

  // Validates the central directory without writing anything. p_outEntries, when
  // non-null, receives the normalized entry list on success.
  //
  // p_verifyPayload additionally INFLATES every entry to a discard sink, which
  // makes miniz verify each CRC-32. That is what lets extract() promise "either
  // the whole archive lands or nothing does": a corrupt stream in the last entry
  // would otherwise only be discovered after every earlier entry had already
  // been written. Callers that only need metadata can pass false.
  static Result validate(const QString &p_archivePath, const Options &p_options,
                         QVector<Entry> *p_outEntries = nullptr, bool p_verifyPayload = true);

  // validate() (with payload verification) followed by extraction into
  // p_destDir (created if absent).
  //
  // Overwrites existing files. Nothing is written when validation fails, so a
  // rejected or corrupt archive can never leave a partial tree behind.
  static Result extract(const QString &p_archivePath, const QString &p_destDir,
                        const Options &p_options);

  // Reads a single entry into memory without extracting anything to disk. Used
  // to pull manifest.json out of band (step 9). Returns false when the entry is
  // absent or fails to decompress.
  static bool readEntry(const QString &p_archivePath, const QString &p_entryPath,
                        QByteArray *p_out, const Options &p_options = Options());

  static QString statusToString(Status p_status);

  // ---------------------------------------------------------------------
  // Archive creation. Used by tests and by local tooling; production code only
  // ever READS archives. Entries are stored with forward-slash paths and the
  // UTF-8 filename flag, matching what the CI generator produces.
  // ---------------------------------------------------------------------
  static bool createArchive(const QString &p_archivePath,
                            const QVector<QPair<QString, QByteArray>> &p_entries,
                            const QStringList &p_directoryEntries = QStringList());

private:
  ZipExtractor() = delete;
};

} // namespace vnotex

#endif // ZIPEXTRACTOR_H
