#ifndef MINDMAPVIEWWINDOWCONTROLLER_H
#define MINDMAPVIEWWINDOWCONTROLLER_H

#include <QObject>
#include <QString>

namespace vnotex {

class ServiceLocator;
struct NodeIdentifier;

class MindMapViewWindowController : public QObject {
  Q_OBJECT
public:
  explicit MindMapViewWindowController(ServiceLocator &p_services, QObject *p_parent = nullptr);

  // ============ Config Revision Tracking ============

  // Check if editor config or mindmap editor config has changed.
  // Updates internal revision tracking. Returns true if config changed since last check.
  bool checkAndUpdateConfigRevision();

  // ============ Path Resolution ============

  // Resolve a NodeIdentifier to an absolute file path on disk.
  // Queries NotebookCoreService for the notebook root folder.
  QString buildAbsolutePath(const NodeIdentifier &p_nodeId) const;

private:
  ServiceLocator &m_services;
  int m_editorConfigRevision = 0;
  int m_mindMapEditorConfigRevision = 0;
};

} // namespace vnotex

#endif // MINDMAPVIEWWINDOWCONTROLLER_H
