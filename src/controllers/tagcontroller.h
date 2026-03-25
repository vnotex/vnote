#ifndef TAGCONTROLLER_H
#define TAGCONTROLLER_H

#include <QJsonArray>
#include <QJsonValue>
#include <QObject>
#include <QString>
#include <QStringList>

#include <core/nodeidentifier.h>

namespace vnotex {

class ServiceLocator;

// Controller for tag explorer interactions.
// Mediates between tag views and tag services.
class TagController : public QObject {
  Q_OBJECT

public:
  explicit TagController(ServiceLocator &p_services, QObject *p_parent = nullptr);

  void setNotebookId(const QString &p_notebookId);

public slots:
  void onTagsSelected(const QStringList &p_selectedTags);
  void onNodeActivated(const NodeIdentifier &p_nodeId);
  void onNewTagAction(const QString &p_notebookId);
  void onDeleteTagAction(const QString &p_notebookId, const QString &p_tagName);
  void onMoveTagAction(const QString &p_notebookId, const QString &p_tagName,
                       const QString &p_newParent);

  void handleNewTagResult(const QString &p_notebookId, const QString &p_tagName);
  void handleDeleteTagConfirmed(const QString &p_notebookId, const QString &p_tagName);
  void handleMoveTagResult(const QString &p_notebookId, const QString &p_tagName,
                           const QString &p_newParent);

signals:
  void newTagRequested(const QString &p_notebookId);
  void deleteTagRequested(const QString &p_notebookId, const QString &p_tagName);
  void errorOccurred(const QString &p_title, const QString &p_message);
  void matchingNodesChanged(const QJsonArray &p_nodes);
  void tagCompatibilityChanged(const QStringList &p_incompatibleTags);
  void openNodeRequested(const NodeIdentifier &p_nodeId);
  void tagOperationCompleted();

private:
  QString resolveNotebookId(const QString &p_notebookId) const;
  QString extractTagName(const QJsonValue &p_tagValue) const;
  NodeIdentifier extractNodeIdentifier(const QJsonValue &p_nodeValue) const;

  ServiceLocator &m_services;
  QString m_notebookId;
  QJsonArray m_matchingNodes;
};

} // namespace vnotex

#endif // TAGCONTROLLER_H
