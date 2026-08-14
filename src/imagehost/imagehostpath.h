#ifndef IMAGEHOSTPATH_H
#define IMAGEHOSTPATH_H

#include <QSet>
#include <QString>
#include <QStringList>

namespace vnotex {

// Pure policy helpers for image-host remote paths and for deciding which
// remote images may be deleted. Kept free of widgets/services so the rules
// that drive an IRREVERSIBLE remote deletion can be unit tested standalone.
class ImageHostPath {
public:
  ImageHostPath() = delete;

  // Build the remote path used when uploading an image belonging to a note.
  // @p_contentDirPath is the DIRECTORY containing the note (what
  // MarkdownEditor::setContentPath() receives), not the note file itself.
  // Returns "<dirName>/<destFileName>", or just "<destFileName>" when no
  // usable directory name is available (empty path, or a filesystem root).
  static QString remotePath(const QString &p_contentDirPath, const QString &p_destFileName);

  // Whether @p_url points at an image host (http/https) rather than at a
  // notebook asset. QDir::isAbsolutePath() does NOT recognize such a URL on
  // Windows, so callers deleting local assets must filter with this.
  static bool isRemoteUrl(const QString &p_url);

  // Identity of the remote object behind @p_url, for comparing two spellings
  // of the same image: lower-cased scheme/host, default port dropped, query and
  // fragment dropped, path segments normalized, unreserved percent-encoding
  // decoded. Path case and encoded delimiters (%2F) are preserved. Returns an
  // empty string for a non-remote or unparsable URL.
  static QString remoteUrlIdentity(const QString &p_url);

  // Decide which of @p_candidates (URLs that are no longer found in the parsed
  // content) may actually be deleted at the image host.
  //
  // @p_enabled is the "Clear obsolete images" setting; when false the result is
  // always empty. The gate lives here so it is covered by the same tests as the
  // classification.
  //
  // Deleting the wrong object is IRREVERSIBLE, so this is deliberately
  // conservative and drops a candidate whenever there is any sign that the
  // note still refers to it:
  //   - it is not a remote (http/https) URL at all;
  //   - it is present verbatim in @p_currentUrls;
  //   - some URL in @p_currentUrls, or any http(s) URL found in the raw
  //     @p_content, denotes the same remote object (remoteUrlIdentity match).
  //     Scanning the raw content is what covers the valid markdown forms the
  //     image scan misses: angle-bracket destinations "![a](<url>)",
  //     reference-style "![a][ref]", and raw HTML <img src="...">.
  //   - its object path still shows up anywhere in @p_content, compared
  //     case-insensitively and after decoding HTML character references and
  //     ignoring whitespace. This is the safety net for the URLs the scan above
  //     cannot delimit exactly (a path containing ')' or ']', a path ending in
  //     '.', an entity-escaped or line-folded HTML attribute).
  // The result is sorted, for a deterministic order.
  static QStringList remoteUrlsToDelete(bool p_enabled, const QSet<QString> &p_candidates,
                                        const QSet<QString> &p_currentUrls,
                                        const QString &p_content);
};

} // namespace vnotex

#endif // IMAGEHOSTPATH_H
