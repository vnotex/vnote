#include "markdownviewwindowcontroller.h"

#include <QAction>
#include <QMenu>

#include <functional>

#include <core/markdowneditorconfig.h>
#include <core/servicelocator.h>
#include <gui/utils/widgetutils.h>

using namespace vnotex;

MarkdownViewWindowController::MarkdownViewWindowController(ServiceLocator &p_services,
                                                           QObject *p_parent)
    : QObject(p_parent), m_services(p_services) {}

QMenu *MarkdownViewWindowController::createContextMenu(
    const MarkdownViewerContextInfo &p_info, QMenu *p_standardMenu,
    const std::function<void()> &p_copyImageHandler, const std::function<void()> &p_editHandler,
    const std::function<void(const QString &)> &p_crossCopyHandler, QWidget *p_parent) {
  Q_UNUSED(p_parent)

  const QList<QAction *> actions = p_standardMenu->actions();
  auto firstAct = actions.isEmpty() ? nullptr : actions[0];

  if (!p_info.hasSelection && p_info.inReadMode) {
    auto editAct = new QAction(QObject::tr("&Edit"), p_standardMenu);
    WidgetUtils::addActionShortcutText(editAct, p_info.editShortcutText);
    connect(editAct, &QAction::triggered, p_standardMenu, p_editHandler);
    p_standardMenu->insertAction(firstAct, editAct);
    if (firstAct) {
      p_standardMenu->insertSeparator(firstAct);
    }
  }

  if (p_info.defaultCopyImageAction && actions.contains(p_info.defaultCopyImageAction)) {
    auto *copyImageAct = new QAction(QObject::tr("Copy"), p_standardMenu);
    copyImageAct->setToolTip(p_info.defaultCopyImageAction->toolTip());
    connect(copyImageAct, &QAction::triggered, p_standardMenu, p_copyImageHandler);
    p_standardMenu->insertAction(p_info.defaultCopyImageAction, copyImageAct);
    p_info.defaultCopyImageAction->setVisible(false);
  }

  if (p_info.copyAction && actions.contains(p_info.copyAction)) {
    setupCrossCopyMenu(p_standardMenu, p_info.copyAction, p_info, p_crossCopyHandler);
  }

  return p_standardMenu;
}

MarkdownViewWindowController::ModeTransition MarkdownViewWindowController::computeModeTransition(
    int p_currentMode, int p_targetMode, bool p_hasEditor, bool p_hasViewer, bool p_syncBuffer) {
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
        // Also sync viewer from buffer so its HTML template is loaded.
        // Without this, switching to Read mode later would show a blank
        // viewer because the WebEngine was never given the template.
        if (p_syncBuffer) {
          result.syncViewerFromBuffer = true;
        }
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

void MarkdownViewWindowController::setupCrossCopyMenu(
    QMenu *p_menu, QAction *p_copyAct, const MarkdownViewerContextInfo &p_info,
    const std::function<void(const QString &)> &p_crossCopyHandler) {
  if (p_info.crossCopyTargets.isEmpty()) {
    return;
  }

  auto subMenu = new QMenu(QObject::tr("Cross Copy"), p_menu);

  const int count = qMin(p_info.crossCopyTargets.size(), p_info.crossCopyDisplayNames.size());
  for (int i = 0; i < count; ++i) {
    auto act = subMenu->addAction(p_info.crossCopyDisplayNames[i]);
    act->setData(p_info.crossCopyTargets[i]);
  }

  connect(subMenu, &QMenu::triggered, p_menu,
          [p_crossCopyHandler](QAction *p_act) { p_crossCopyHandler(p_act->data().toString()); });

  auto menuAct = p_menu->insertMenu(p_copyAct, subMenu);
  p_menu->removeAction(p_copyAct);
  p_menu->insertAction(menuAct, p_copyAct);
}
