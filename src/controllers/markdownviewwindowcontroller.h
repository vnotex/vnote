#ifndef MARKDOWNVIEWWINDOWCONTROLLER_H
#define MARKDOWNVIEWWINDOWCONTROLLER_H

#include <QObject>
#include <QStringList>
#include <QUrl>

#include <functional>

#include <core/global.h>
#include <core/markdowneditorconfig.h>

class QAction;
class QMenu;
class QWidget;

namespace vnotex {

class ServiceLocator;

// Context data passed from the viewer to the controller for context-menu building.
struct MarkdownViewerContextInfo {
  bool hasSelection = false;
  bool inReadMode = false;
  QStringList crossCopyTargets;      // from adapter->getCrossCopyTargets()
  QStringList crossCopyDisplayNames; // parallel array: display names for each target

  // Pre-resolved shortcut text for Edit action.
  QString editShortcutText;

  // Pre-resolved shortcut text for Export action (hint only, no shortcut registered).
  QString exportShortcutText;

  // Pointers to standard QWebEnginePage actions found in the standard menu.
  // The viewer resolves these via pageAction() and passes them here so the
  // controller can identify and manipulate them without calling pageAction() itself.
  // Any of these may be nullptr if the action is not present in the menu.
  QAction *copyAction = nullptr;             // QWebEnginePage::Copy
  QAction *defaultCopyImageAction = nullptr; // QWebEnginePage::CopyImageToClipboard

  // Media URL of the image under the cursor, if any (read mode only).
  QUrl imageUrl;
};

class MarkdownViewWindowController : public QObject {
  Q_OBJECT
public:
  explicit MarkdownViewWindowController(ServiceLocator &p_services, QObject *p_parent = nullptr);

  // Context menu creation for markdown viewer.
  // Returns the same standard menu after augmenting it in place.
  QMenu *createContextMenu(const MarkdownViewerContextInfo &p_info, QMenu *p_standardMenu,
                           const std::function<void()> &p_copyImageHandler,
                           const std::function<void()> &p_editHandler,
                           const std::function<void(const QString &)> &p_crossCopyHandler,
                           const std::function<void()> &p_viewImageHandler,
                           const std::function<void()> &p_exportHandler,
                           QWidget *p_parent = nullptr);

  // Result of computing what needs to happen for a mode transition.
  struct ModeTransition {
    bool needSetupEditor = false; // Lazy init: editor not yet created
    bool needSetupViewer = false; // Lazy init: viewer not yet created
    bool syncEditorFromBuffer = false;
    bool syncViewerFromBuffer = false;
    bool syncPositionFromPrevMode = false;
    bool restoreEditViewMode = false;    // Re-entering Edit with existing editor
    bool syncBufferToActiveView = false; // Post-switch: sync buffer to whichever view is active

    // The Invalid -> Read/Edit transition made in the constructor. The first buffer
    // read lazily loads content and bumps the vxcore revision, so the revision the
    // base class captured before construction is already stale by the time this
    // transition ends.
    bool adoptInitialRevision = false;
  };

  // ============ Mode Switching ============

  // Compute what operations are needed for a mode transition.
  // @p_currentMode: current ViewWindowMode as int (Read=0/Edit=1/Invalid=2).
  // @p_targetMode: target ViewWindowMode as int.
  // @p_hasEditor: whether the editor widget has been lazily created.
  // @p_hasViewer: whether the viewer widget has been lazily created.
  // @p_syncBuffer: whether to sync content from buffer (false during initial setup).
  //
  // Pure logic -- no side effects. Caller uses the returned struct to drive
  // widget creation and sync operations.
  static ModeTransition computeModeTransition(int p_currentMode, int p_targetMode, bool p_hasEditor,
                                              bool p_hasViewer, bool p_syncBuffer);

  // ============ Preview Config ============

  // Get the preview sync timer interval in milliseconds.
  // Sourced from a constant (currently 500ms, matching legacy behavior).
  static int previewSyncIntervalMs();

  // ============ Edit View Mode ============

  // Get the EditViewMode from MarkdownEditorConfig.
  // Used to determine whether Edit mode shows editor-only or editor+preview.
  static MarkdownEditorConfig::EditViewMode getEditViewMode(const MarkdownEditorConfig &p_mdConfig);

  // ============ Task List ============

  // Rewrite a GFM task list line to the given checked state.
  // Returns the rewritten line, or a null QString when @p_line is not a task
  // list item whose current state is the opposite of @p_checked (stale DOM).
  //
  // Pure logic -- no side effects.
  static QString rewriteTaskListLine(const QString &p_line, bool p_checked);

private:
  void setupCrossCopyMenu(QMenu *p_menu, QAction *p_copyAct,
                          const MarkdownViewerContextInfo &p_info,
                          const std::function<void(const QString &)> &p_crossCopyHandler);

  ServiceLocator &m_services;
};

} // namespace vnotex

#endif // MARKDOWNVIEWWINDOWCONTROLLER_H
