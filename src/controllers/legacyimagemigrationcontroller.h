#ifndef LEGACYIMAGEMIGRATIONCONTROLLER_H
#define LEGACYIMAGEMIGRATIONCONTROLLER_H

#include <QObject>
#include <QSet>
#include <QString>
#include <QVector>

#include <functional>

namespace vnotex {

class ServiceLocator;
class Buffer2;

// One markdown image OCCURRENCE that points into a pre-v4 legacy image folder
// (vx_images / _v_images). Two occurrences of the same file share a
// canonicalSrcKey and must be staged only once.
struct LegacyImageRef {
  QString urlInLink;       // Exact URL spelling as it appears in the source text.
  QString srcAbsolutePath; // Resolved absolute path of the legacy image.
  QString canonicalSrcKey; // Dedup key (canonical path, lower-cased on Windows).
  // Half-open span of the RAW destination in the source text. detect() only
  // accepts links whose raw spelling IS urlInLink, but the end is carried
  // explicitly so that no rewrite measures itself with urlInLink.size().
  int urlStart = -1;
  int urlEnd = -1;
  QString legacyFolderAbsolutePath; // The matched vx_images/_v_images directory itself.
};

// A staged migration: the copy already exists in the assets folder, the link
// text has not been rewritten yet.
struct LegacyImageRewrite {
  QString oldUrlInLink;
  QString newUrlInLink;
  // Half-open span of the destination being replaced. The replacement length is
  // always (urlEnd - urlStart), never oldUrlInLink.size().
  int urlStart = -1;
  int urlEnd = -1;
  QString srcAbsolutePath; // For containment-checked deletion at finalize time.
  QString destAbsolutePath;
  QString legacyFolderAbsolutePath;
};

// Detects markdown images living in a pre-v4 image folder, stages copies into
// the note's v4 per-file assets folder, and (once, at window close) removes the
// originals.
//
// Deleting the original is deliberately DEFERRED to close: ViewWindow2::save()
// is not a durability barrier under AutoSavePolicy::AutoSave, and an undo
// performed after a successful delete would leave the note referencing a file
// that no longer exists. See finalize() for the three-part gate.
//
// MVC: a QObject controller, never a QWidget. stageAssets() takes injected
// callables rather than a Buffer2& so it stays pure and unit-testable without a
// vxcore context.
class LegacyImageMigrationController : public QObject {
  Q_OBJECT

public:
  // Raw Buffer2::insertAsset() result for a source file (may be relative).
  using AssetInserter = std::function<QString(const QString &p_srcAbsolutePath)>;
  // Absolute destination path -> markdown URL (MarkdownEditor::getRelativeLink).
  using Linkifier = std::function<QString(const QString &p_destAbsolutePath)>;

  enum class FinalizeResult {
    Done,   // Originals removed (or there was nothing to remove).
    NotYet, // Gate not satisfied; originals kept. Never retried.
    Failed  // Gate satisfied but every deletion failed.
  };

  explicit LegacyImageMigrationController(ServiceLocator &p_services, QObject *p_parent = nullptr);

  // ============ Pure helpers ============

  // Case-insensitive match against the deprecated folder names ONLY.
  // "vx_attachments" / "_v_attachments" are NOT legacy image folders.
  static bool isLegacyFolderName(const QString &p_name);

  // True when the URL carries at least one %XX escape. Do NOT use
  // vte::TextUtils::decodeUrl() for this: it is QUrl(url).toString(), a
  // normalizer that lets encoded reserved delimiters such as %2F through.
  static bool containsPercentEscape(const QString &p_url);

  // Parse @p_markdownText and return every legacy-image occurrence that this
  // feature can safely MOVE. Results keep fetchImageLinks()'s descending
  // urlStart order, which the QTextCursor rewrite loop depends on.
  //
  // Deliberately excluded (the bar simply does not appear):
  //   - parent-relative URLs ("../vx_images/x.png"): deleteAsset() is
  //     notebook-root relative and clearObsoleteImages() skips them;
  //   - percent-encoded URLs;
  //   - anything resolving outside @p_basePath;
  //   - files already living inside @p_assetsFolderToExclude (a notebook whose
  //     assetsFolder is itself named vx_images would otherwise be flagged
  //     forever).
  static QVector<LegacyImageRef> detect(const QString &p_markdownText, const QString &p_basePath,
                                        const QString &p_assetsFolderToExclude);

  // Copy every DISTINCT source file into the assets folder and compute the new
  // markdown URL for each occurrence. All-or-nothing: on any failure *p_error
  // is set, every destination created by this call is removed, and an empty
  // vector is returned (nothing has been rewritten yet at that point).
  static QVector<LegacyImageRewrite> stageAssets(const QVector<LegacyImageRef> &p_refs,
                                                 const AssetInserter &p_insert,
                                                 const QString &p_assetsFolder,
                                                 const Linkifier &p_linkify, QString *p_error);

  // True when @p_path is @p_dir itself or lives underneath it, compared
  // CANONICALLY (symlinks/junctions resolved) and case-insensitively on
  // Windows. A lexical compare would let a directory junction inside the
  // notebook reach a file outside it, and deleteAsset() would then delete the
  // outside target.
  static bool isPathContained(const QString &p_dir, const QString &p_path);

  // Third part of the finalize gate, split out so it is testable on its own:
  // the on-disk text must contain EVERY new URL and NONE of the old ones.
  static bool diskStateSatisfies(const QString &p_decodedText,
                                 const QVector<LegacyImageRewrite> &p_rewrites);

  // The complete finalize gate as a pure predicate over its four inputs.
  static bool finalizeGateSatisfied(bool p_bufferDirty, bool p_saveQueueBusy,
                                    const QString &p_decodedDiskText,
                                    const QVector<LegacyImageRewrite> &p_rewrites);

  // Canonical identities of every LOCAL image the given markdown text still
  // resolves to — relative, parent-relative, absolute and file: URLs alike.
  // finalize() refuses to delete anything in this set: the URL comparison in
  // diskStateSatisfies() cannot see a link added AFTER the migration that
  // reaches the same original through a different spelling (e.g.
  // "vx_images/./a.png" or "C:/notebook/vx_images/a.png").
  static QSet<QString> referencedSourceKeys(const QString &p_decodedText,
                                            const QString &p_basePath);

  // The skip decision finalize() makes for one original, exposed so the
  // normalization on both sides is pinned by a test.
  static bool isStillReferenced(const QString &p_srcAbsolutePath,
                                const QSet<QString> &p_referencedKeys);

  // ============ Per-notebook opt-out ============

  // metadata.legacyImageMigrationOptOut. Fails open (returns false) when the
  // notebook config cannot be read.
  bool isOptedOut(const QString &p_notebookId) const;

  // Returns the updateNotebookConfig() result.
  bool setOptedOut(const QString &p_notebookId);

  // ============ Close-time finalize ============

  // Delete the originals, but ONLY when all three hold:
  //   1. !p_bufferDirty;
  //   2. !p_saveQueueBusy (a claimed worker holding an OLD snapshot can still
  //      land after isDirty() has been cleared by syncNow());
  //   3. the note file ON DISK carries every new URL and no old URL.
  //
  // Even then, an original that the final on-disk text still resolves to
  // (through any spelling) is KEPT — see referencedSourceKeys().
  //
  // Residual risk (accepted): if the gate never passes, the originals remain
  // forever. That is a copy rather than a move, with correct links — the safe
  // direction. deleteAsset() failures are logged, never retried.
  FinalizeResult finalize(Buffer2 &p_buffer, const QVector<LegacyImageRewrite> &p_rewrites,
                          bool p_bufferDirty, bool p_saveQueueBusy);

private:
  ServiceLocator &m_services;
};

} // namespace vnotex

#endif // LEGACYIMAGEMIGRATIONCONTROLLER_H
