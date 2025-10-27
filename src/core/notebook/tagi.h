#ifndef TAGI_H
#define TAGI_H

#include <QVector>

#include "tag.h"

namespace vnotex {
class Node;

// Tag interface for notebook.
class TagI {
public:
  virtual ~TagI() = default;

  virtual const QVector<QSharedPointer<Tag>> &getTopLevelTags() const = 0;

  virtual QStringList findNodesOfTag(const QString &p_name) = 0;

  virtual QSharedPointer<Tag> findTag(const QString &p_name) = 0;

  virtual bool newTag(const QString &p_name, const QString &p_parentName) = 0;

  virtual bool renameTag(const QString &p_name, const QString &p_newName) = 0;

  virtual bool updateNodeTags(Node *p_node) = 0;

  virtual bool updateNodeTags(Node *p_node, const QStringList &p_newTags) = 0;

  virtual bool removeTag(const QString &p_name) = 0;

  virtual bool moveTag(const QString &p_name, const QString &p_newParentName) = 0;
};
} // namespace vnotex
#endif // TAGI_H
