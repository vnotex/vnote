#ifndef IMAGEUPLOADPLACEHOLDER_H
#define IMAGEUPLOADPLACEHOLDER_H

#include <QString>

namespace vnotex {

// The Markdown placeholder an async image-host upload leaves in the buffer, and
// the two ways it is retired.
//
// Extracted from MarkdownEditor so it can be gated directly: MarkdownEditor
// itself is not compiled by any test target, and a mirrored copy in a test
// gates nothing (it stays green while production drifts).
//
// The PLACEHOLDER is always Markdown, whatever the final reference turns out to
// be. That is what keeps the crude `lastIndexOf("![")` / `indexOf(')')` scan
// below valid even when the replacement it writes is an HTML `<img …/>`.
class ImageUploadPlaceholder {
public:
  ImageUploadPlaceholder() = delete;

  // `![Uploading <name>...](vnote-upload-<token>)`.
  static QString generate(int p_token, const QString &p_fileName);

  // Replace the placeholder for @p_token with the real reference. A nonzero
  // size makes that an HTML `<img …/>`; Markdown has no portable way to spell
  // one. Returns @p_content unchanged when the placeholder is not found.
  static QString replace(const QString &p_content, int p_token, const QString &p_realUrl,
                         const QString &p_title, int p_width = 0, int p_height = 0);

  // Remove the placeholder for @p_token, and the newline after it if there is
  // one. Returns @p_content unchanged when the placeholder is not found.
  static QString remove(const QString &p_content, int p_token);
};

} // namespace vnotex

#endif // IMAGEUPLOADPLACEHOLDER_H
