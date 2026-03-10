#ifndef NOTEBOOKTAGMGR_H
#define NOTEBOOKTAGMGR_H

#include <QObject>

#include "tagi.h"

#include <functional>

#include <QSharedPointer>
#include <QVector>

#include "notebookdatabaseaccess.h"

namespace vnotex {
class BundleNotebook;
class Tag;

class NotebookTagMgr : public QObject, public TagI {
  Q_OBJECT
public:
  struct TagGraphPair {
    QString m_parent;

    QString m_child;
  };

  explicit NotebookTagMgr(BundleNotebook *p_notebook);

  void update();

  static QVector<TagGraphPair> stringToTagGraph(const QString &p_text);

  static QString tagGraphToString(const QVector<TagGraphPair> &p_tagGraph);

  // TagI.
public:
  const QVector<QSharedPointer<Tag>> &getTopLevelTags() const Q_DECL_OVERRIDE;

  QStringList findNodesOfTag(const QString &p_name) Q_DECL_OVERRIDE;

  QSharedPointer<Tag> findTag(const QString &p_name) Q_DECL_OVERRIDE;

  bool newTag(const QString &p_name, const QString &p_parentName) Q_DECL_OVERRIDE;

  bool renameTag(const QString &p_name, const QString &p_newName) Q_DECL_OVERRIDE;

  bool updateNodeTags(Node *p_node) Q_DECL_OVERRIDE;

  bool updateNodeTags(Node *p_node, const QStringList &p_newTags) Q_DECL_OVERRIDE;

  bool removeTag(const QString &p_name) Q_DECL_OVERRIDE;

  bool moveTag(const QString &p_name, const QString &p_newParentName) Q_DECL_OVERRIDE;

private:
  typedef std::function<bool(const QSharedPointer<Tag> &p_tag)> TagFinder;

  // @p_func: return false to abort the search.
  void forEachTag(const TagFinder &p_func) const;

  // Return false if abort.
  bool forEachTag(const QSharedPointer<Tag> &p_tag, const TagFinder &p_func) const;

  void update(const QList<NotebookDatabaseAccess::TagRecord> &p_allTags);

  void updateNotebookTagGraph(const QList<NotebookDatabaseAccess::TagRecord> &p_allTags);

  BundleNotebook *m_notebook = nullptr;

  QVector<QSharedPointer<Tag>> m_topLevelTags;
};
} // namespace vnotex

#endif // NOTEBOOKTAGMGR_H
