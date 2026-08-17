#ifndef CONTENTMEDIAUTILS_H
#define CONTENTMEDIAUTILS_H

#include <QString>

namespace vnotex {
class INotebookBackend;

// Utils to operate on the media files from a file's content.
class ContentMediaUtils {
public:
  ContentMediaUtils() = delete;

  // @p_filePath: the file path to read the content for parse.
  static void copyMediaFiles(const QString &p_filePath, INotebookBackend *p_backend,
                             const QString &p_destFilePath);

  // Copy every local image referenced by @p_content next to @p_destFilePath,
  // rewriting the note when a name collision forces a rename.
  //
  // Public so the export behaviour can be tested directly: reaching it through
  // copyMediaFiles() would drag in FileTypeHelper's singleton and the whole
  // file-type registry to prove something about image links. Same seam
  // rationale as LinkInsertUtils and ImageLinkLookup.
  static void copyMarkdownMediaFiles(const QString &p_content, const QString &p_basePath,
                                     INotebookBackend *p_backend, const QString &p_destFilePath);
};
} // namespace vnotex

#endif // CONTENTMEDIAUTILS_H
