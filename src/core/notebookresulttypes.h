#ifndef NOTEBOOKRESULTTYPES_H
#define NOTEBOOKRESULTTYPES_H

#include <QJsonObject>
#include <QJsonValue>
#include <QMetaType>
#include <QString>
#include <QStringList>
#include <QVector>

namespace vnotex {

// Owning Qt records: no borrowed vxcore pointers cross model or signal boundaries.
struct FolderEntry {
  QString m_id;
  QString m_name;
  qint64 m_createdUtc = 0;
  qint64 m_modifiedUtc = 0;
  QJsonValue m_metadata = QJsonObject();
  QStringList m_tags;
  QStringList m_attachments;
  bool m_existsKnown = false;
  bool m_exists = true;
};

struct FolderListing {
  QVector<FolderEntry> m_files;
  QVector<FolderEntry> m_folders;

  // Empty is not an error: consult the service's explicit error output.
  bool isEmpty() const { return m_files.isEmpty() && m_folders.isEmpty(); }
};

struct TagRecord {
  QString m_name;
  QString m_parent;
  QJsonValue m_metadata = QJsonObject();
};

struct TagCountRecord {
  QString m_tag;
  qint64 m_count = 0;
};

struct TagFileMatch {
  QString m_filePath;
  QString m_fileName;
  QStringList m_tags;
  // Null means absent/use the caller's notebook fallback. Explicit empty stays empty.
  QString m_notebookId;

  bool operator==(const TagFileMatch &p_other) const {
    return m_filePath == p_other.m_filePath && m_fileName == p_other.m_fileName &&
           m_tags == p_other.m_tags && m_notebookId.isNull() == p_other.m_notebookId.isNull() &&
           m_notebookId == p_other.m_notebookId;
  }
  bool operator!=(const TagFileMatch &p_other) const { return !(*this == p_other); }
};

} // namespace vnotex

Q_DECLARE_METATYPE(vnotex::TagFileMatch)
Q_DECLARE_METATYPE(QVector<vnotex::TagFileMatch>)

#endif // NOTEBOOKRESULTTYPES_H
