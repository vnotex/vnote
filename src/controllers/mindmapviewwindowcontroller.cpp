#include "mindmapviewwindowcontroller.h"

#include <core/configmgr2.h>
#include <core/editorconfig.h>
#include <core/mindmapeditorconfig.h>
#include <core/nodeidentifier.h>
#include <core/servicelocator.h>
#include <core/services/notebookcoreservice.h>

using namespace vnotex;

MindMapViewWindowController::MindMapViewWindowController(ServiceLocator &p_services,
                                                         QObject *p_parent)
    : QObject(p_parent), m_services(p_services) {}

bool MindMapViewWindowController::checkAndUpdateConfigRevision() {
  bool changed = false;

  auto *configMgr = m_services.get<ConfigMgr2>();
  const auto &editorConfig = configMgr->getEditorConfig();

  if (m_editorConfigRevision != editorConfig.revision()) {
    changed = true;
    m_editorConfigRevision = editorConfig.revision();
  }

  if (m_mindMapEditorConfigRevision != editorConfig.getMindMapEditorConfig().revision()) {
    changed = true;
    m_mindMapEditorConfigRevision = editorConfig.getMindMapEditorConfig().revision();
  }

  return changed;
}

QString MindMapViewWindowController::buildAbsolutePath(const NodeIdentifier &p_nodeId) const {
  if (!p_nodeId.isValid()) {
    return QString();
  }

  auto *notebookService = m_services.get<NotebookCoreService>();
  return notebookService->buildAbsolutePath(p_nodeId.notebookId, p_nodeId.relativePath);
}
