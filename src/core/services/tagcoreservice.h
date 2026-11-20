#ifndef TAGCORESERVICE_H
#define TAGCORESERVICE_H

#include <QObject>
#include <QString>
#include <QStringList>

#include <core/noncopyable.h>
#include <core/notebookresulttypes.h>
#include <core/searchresulttypes.h>

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
  QVector<TagRecord> listTags(const QString &p_notebookId) const;
  // Direct note associations for every indexed tag; absent records remain unknown.
  QVector<TagCountRecord> countFilesByTag(const QString &p_notebookId) const;
  bool moveTag(const QString &p_notebookId, const QString &p_tagName, const QString &p_parentTag);
  bool updateFileTags(const QString &p_notebookId, const QString &p_filePath,
                      const QString &p_tagsJson);
  bool tagFile(const QString &p_notebookId, const QString &p_filePath, const QString &p_tagName);
  bool untagFile(const QString &p_notebookId, const QString &p_filePath, const QString &p_tagName);
  SearchResult searchByTags(const QString &p_notebookId, const QStringList &p_tags);

  // Find files by tags using efficient database lookup.
  // p_op: "AND" to match files with ALL tags, "OR" to match files with ANY tag.
  // Each match contains the file's complete tag list.
  QVector<TagFileMatch> findFilesByTags(const QString &p_notebookId, const QStringList &p_tags,
                                        const QString &p_op = QString(QLatin1String("AND")));

  // Pure conversions: returned values do not borrow their C view's storage.
  static QVector<TagRecord> tagsFromView(const VxCoreTagListView &p_view);
  static QVector<TagCountRecord> tagCountsFromView(const VxCoreTagCountsView &p_view);
  static QVector<TagFileMatch> tagFilesFromView(const VxCoreTagFileResultView &p_view);

private:
  // Check context validity before operations.
  bool checkContext() const;

  VxCoreContextHandle m_context = nullptr;
};

} // namespace vnotex

#endif // TAGCORESERVICE_H
