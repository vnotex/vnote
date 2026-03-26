#ifndef TAGCORESERVICE_H
#define TAGCORESERVICE_H

#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QStringList>

#include <core/noncopyable.h>

#include <vxcore/vxcore.h>
#include <vxcore/vxcore_types.h>

namespace vnotex {

// Service layer for tag operations. Wraps VxCore C API.
class TagCoreService : public QObject, private Noncopyable {
  Q_OBJECT

public:
  // Constructor receives VxCore context handle via dependency injection.
  explicit TagCoreService(VxCoreContextHandle p_context, QObject *p_parent = nullptr);
  ~TagCoreService();

  bool createTag(const QString &p_notebookId, const QString &p_tagName);
  bool createTagPath(const QString &p_notebookId, const QString &p_tagPath);
  bool deleteTag(const QString &p_notebookId, const QString &p_tagName);
  QJsonArray listTags(const QString &p_notebookId) const;
  bool moveTag(const QString &p_notebookId, const QString &p_tagName, const QString &p_parentTag);
  bool updateFileTags(const QString &p_notebookId, const QString &p_filePath,
                      const QString &p_tagsJson);
  bool tagFile(const QString &p_notebookId, const QString &p_filePath, const QString &p_tagName);
  bool untagFile(const QString &p_notebookId, const QString &p_filePath, const QString &p_tagName);
  QJsonArray searchByTags(const QString &p_notebookId, const QStringList &p_tags);

  // Find files by tags using efficient database lookup (via vxcore_tag_find_files).
  // p_op: "AND" to match files with ALL tags, "OR" to match files with ANY tag.
  // Returns a QJsonArray of match objects: [{"filePath":"...","fileName":"...","tags":[...]}, ...]
  QJsonArray findFilesByTags(const QString &p_notebookId, const QStringList &p_tags,
                             const QString &p_op = QString(QLatin1String("AND")));

private:
  // Check context validity before operations.
  bool checkContext() const;

  // Convert C string from vxcore and free it.
  static QString cstrToQString(char *p_str);

  // Convert QString to C string for vxcore (immediate use only).
  static const char *qstringToCStr(const QString &p_str);

  // Parse JSON string to QJsonObject from C string (takes ownership, frees p_str).
  static QJsonObject parseJsonObjectFromCStr(char *p_str);

  // Parse JSON string to QJsonArray from C string (takes ownership, frees p_str).
  static QJsonArray parseJsonArrayFromCStr(char *p_str);

  VxCoreContextHandle m_context = nullptr;
};

} // namespace vnotex

#endif // TAGCORESERVICE_H
