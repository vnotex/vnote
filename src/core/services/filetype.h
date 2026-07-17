#ifndef FILETYPE_H
#define FILETYPE_H

#include <QJsonObject>
#include <QString>
#include <QStringList>

namespace vnotex {
class FileType {
public:
  // There may be other types after Others.
  // Deprecated.
  enum Type { Markdown = 0, Text, Pdf, MindMap, Others };

  QString preferredSuffix() const;

  bool isMarkdown() const;

  // Type.
  // Deprecated.
  int m_type = -1;

  QString m_typeName;

  QString m_displayName;

  // Original (non-locale-resolved) display name for serialization round-trip.
  QString m_defaultDisplayName;

  QStringList m_suffixes;

  // Whether we can new this type of file.
  bool m_isNewable = true;

  // Opaque metadata from vxcore (preserved on round-trip).
  QJsonObject m_metadata;
};
} // namespace vnotex

#endif // FILETYPE_H
