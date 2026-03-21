#ifndef MARKDOWNVIEWWINDOWCONTROLLER_H
#define MARKDOWNVIEWWINDOWCONTROLLER_H

#include <QObject>

#include <core/global.h>
#include <core/markdowneditorconfig.h>

namespace vnotex {

class ServiceLocator;

class MarkdownViewWindowController : public QObject {
  Q_OBJECT
public:
  explicit MarkdownViewWindowController(ServiceLocator &p_services, QObject *p_parent = nullptr);

  // Result of computing what needs to happen for a mode transition.
  struct ModeTransition {
    bool needSetupEditor = false;     // Lazy init: editor not yet created
    bool needSetupViewer = false;     // Lazy init: viewer not yet created
    bool syncEditorFromBuffer = false;
    bool syncViewerFromBuffer = false;
    bool syncPositionFromPrevMode = false;
    bool restoreEditViewMode = false; // Re-entering Edit with existing editor
    bool syncBufferToActiveView = false; // Post-switch: sync buffer to whichever view is active
  };

  // ============ Mode Switching ============

  // Compute what operations are needed for a mode transition.
  // @p_currentMode: current ViewWindowMode as int (Invalid=-1/Read=0/Edit=1).
  // @p_targetMode: target ViewWindowMode as int.
  // @p_hasEditor: whether the editor widget has been lazily created.
  // @p_hasViewer: whether the viewer widget has been lazily created.
  // @p_syncBuffer: whether to sync content from buffer (false during initial setup).
  //
  // Pure logic -- no side effects. Caller uses the returned struct to drive
  // widget creation and sync operations.
  static ModeTransition computeModeTransition(int p_currentMode,
                                              int p_targetMode,
                                              bool p_hasEditor,
                                              bool p_hasViewer,
                                              bool p_syncBuffer);

  // ============ Preview Config ============

  // Get the preview sync timer interval in milliseconds.
  // Sourced from a constant (currently 500ms, matching legacy behavior).
  static int previewSyncIntervalMs();

  // ============ Edit View Mode ============

  // Get the EditViewMode from MarkdownEditorConfig.
  // Used to determine whether Edit mode shows editor-only or editor+preview.
  static MarkdownEditorConfig::EditViewMode getEditViewMode(
      const MarkdownEditorConfig &p_mdConfig);

private:
  ServiceLocator &m_services;
};

} // namespace vnotex

#endif // MARKDOWNVIEWWINDOWCONTROLLER_H
