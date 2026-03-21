#include "markdownviewwindowcontroller.h"

#include <core/markdowneditorconfig.h>
#include <core/servicelocator.h>

using namespace vnotex;

MarkdownViewWindowController::MarkdownViewWindowController(ServiceLocator &p_services,
                                                           QObject *p_parent)
    : QObject(p_parent), m_services(p_services) {}

MarkdownViewWindowController::ModeTransition
MarkdownViewWindowController::computeModeTransition(int p_currentMode,
                                                    int p_targetMode,
                                                    bool p_hasEditor,
                                                    bool p_hasViewer,
                                                    bool p_syncBuffer) {
  ModeTransition result;

  if (p_targetMode == p_currentMode) {
    return result;
  }

  const int readMode = static_cast<int>(ViewWindowMode::Read);
  const int editMode = static_cast<int>(ViewWindowMode::Edit);

  if (p_targetMode == readMode) {
    if (!p_hasViewer) {
      result.needSetupViewer = true;
      if (p_syncBuffer) {
        result.syncViewerFromBuffer = true;
      }
    }
    result.syncPositionFromPrevMode = (p_currentMode == editMode && p_syncBuffer);
    if (p_syncBuffer) {
      result.syncBufferToActiveView = true;
    }
  } else if (p_targetMode == editMode) {
    if (!p_hasEditor) {
      if (!p_hasViewer) {
        result.needSetupViewer = true;
      }
      result.needSetupEditor = true;
      if (p_syncBuffer) {
        result.syncEditorFromBuffer = true;
      }
    } else {
      result.restoreEditViewMode = true;
    }
    result.syncPositionFromPrevMode = (p_currentMode == readMode && p_syncBuffer);
    if (p_syncBuffer) {
      result.syncBufferToActiveView = true;
    }
  }

  return result;
}

int MarkdownViewWindowController::previewSyncIntervalMs() {
  // Match legacy MarkdownViewWindow sync timer interval.
  return 500;
}

MarkdownEditorConfig::EditViewMode
MarkdownViewWindowController::getEditViewMode(const MarkdownEditorConfig &p_mdConfig) {
  return p_mdConfig.getEditViewMode();
}
