#ifndef VIEWWINDOW2_H
#define VIEWWINDOW2_H

#include <QFrame>
#include <QIcon>
#include <QPixmap>
#include <QSharedPointer>
#include <QString>
#include <QStringList>

#include <functional>

#include <core/global.h>
#include <core/services/buffer2.h>

#include "viewwindowtoolbarhelper2.h"
#include "wordcountpanel.h"

class QHBoxLayout;
class QVBoxLayout;
class QAction;
class QToolBar;
class QResizeEvent;
class QWheelEvent;

namespace vnotex {

class AttachmentDragDropAreaIndicator2;
class AttachmentPopup2;
class EditReadDiscardAction;
class OutlineProvider;
class ServiceLocator;
class StatusWidget;
class FindAndReplaceWidget2;

// Abstract base class for all view windows in the new architecture.
// A view window displays a single file (Buffer2) in a specific mode (Read/Edit).
// Subclasses implement concrete views for different file types (Markdown, Plain Text, PDF, etc.).
//
// Provides layout slots for subclasses:
//   [toolbar]          ← addToolBar()
//   [top widgets]      ← addTopWidget()
//   [central widget]   ← setCentralWidget() (stretch=1)
//   [bottom widgets]   ← addBottomWidget()
//   [status widget]    ← setStatusWidget()
//
// Usage:
//   class MarkdownViewWindow2 : public ViewWindow2 {
//     // Implement pure virtual methods: setMode(), getLatestContent(),
//     // syncEditorFromBuffer(), setModified(), scrollUp(), scrollDown(), zoom()
//   };
class ViewWindow2 : public QFrame {
  Q_OBJECT
public:
  // Constructor receives ServiceLocator for dependency injection and Buffer2 as the file handle.
  // @p_services: ServiceLocator to access application services (DI).
  // @p_buffer: Buffer2 handle for the file to display. Must be valid (isValid() == true).
  // @p_parent: Parent widget (optional).
  explicit ViewWindow2(ServiceLocator &p_services, const Buffer2 &p_buffer,
                       QWidget *p_parent = nullptr);

  ~ViewWindow2() override;

  // ============ Buffer Access ============

  // Get the buffer handle associated with this window.
  const Buffer2 &getBuffer() const;

  // Get the mutable buffer handle (for content writes, save, etc.).
  Buffer2 &getBuffer();

  // Get the node identifier (notebook + relative path) for this window's file.
  const NodeIdentifier &getNodeId() const;

  // ============ Rename Support ============

  // Called when the underlying file has been renamed on disk.
  // Updates the buffer's NodeIdentifier and emits nameChanged() to refresh tab title.
  void onNodeRenamed(const NodeIdentifier &p_newNodeId);

  // ============ Window Identity ============

  // Get the display icon for the tab (e.g., file type icon).
  virtual QIcon getIcon() const;

  // Get the display name for the tab (extracted from file path).
  virtual QString getName() const;

  // Get the window title (display name + optional modification indicator).
  QString getTitle() const;

  // Get the outline provider for this window (nullptr if not supported).
  // Subclasses that support outlines (e.g., MarkdownViewWindow2) override this.
  virtual QSharedPointer<OutlineProvider> getOutlineProvider() const;

  // ============ Mode ============

  // Get the current view mode (Read or Edit).
  ViewWindowMode getMode() const;

  // Get cursor position for session persistence.
  // Returns the cursor's block (line) number, or -1 if unavailable.
  // Subclasses override to provide actual position from their editor widget.
  virtual int getCursorPosition() const;

  // Get scroll position for session persistence.
  // Returns the vertical scrollbar value, or -1 if unavailable.
  // Subclasses override to provide actual position from their editor widget.
  virtual int getScrollPosition() const;

  // Set view mode (Read/Edit). Subclasses implement mode switching and UI updates.
  // Pure virtual: must be implemented by subclasses.
  virtual void setMode(ViewWindowMode p_mode) = 0;

  // ============ Layout Mode ============

  // Get the effective layout mode (local override > global default).
  ViewWindowLayoutMode getLayoutMode() const;

  // Set a local (per-tab) layout mode override. Does not persist to config.
  void setLayoutMode(ViewWindowLayoutMode p_mode);

  // Toggle between FullWidth and ReadableWidth.
  void toggleLayoutMode();

  // ============ Content ============

  // Get the latest content from the editor widget (not from buffer).
  // Used for commit/save operations before calling buffer.save().
  // Pure virtual: must be implemented by subclasses.
  virtual QString getLatestContent() const = 0;

  // Whether this window has unsaved modifications.
  bool isModified() const;

  // ============ Lifecycle ============

  // Called before the window is closed.
  // Return true if it is OK to proceed (e.g., unsaved changes have been handled).
  // @p_force: if true, close without prompting user.
  // Default implementation syncs dirty content and unregisters from BufferService.
  virtual bool aboutToClose(bool p_force);

public slots:
  void findNext(const QString &p_text, FindOptions p_options);

  void replace(const QString &p_text, FindOptions p_options, const QString &p_replaceText);

  void replaceAll(const QString &p_text, FindOptions p_options, const QString &p_replaceText);

  // Apply a snippet by name. Called from snippet panel.
  // Base implementation logs a warning; subclasses override for actual snippet support.
  virtual void applySnippet(const QString &p_name);

  // Apply a snippet with auto-detect or prompt. Called from shortcut.
  // Base implementation logs a warning; subclasses override for actual snippet support.
  virtual void applySnippet();

  // Clear search/find highlights in the editor.
  // Base implementation does nothing; subclasses override to clear their highlights.
  virtual void clearHighlights();

  // Called when editor configuration changes at runtime.
  // Subclasses should override to reload config and update their editor widget.
  // Default implementation re-applies content margins for readable-width mode.
  virtual void handleEditorConfigChange();

signals:
  // Emitted when this window gains keyboard focus.
  void focused(ViewWindow2 *p_win);

  // Emitted when window status changes (modification state, etc.).
  void statusChanged();

  // Emitted when the view mode changes (Read <-> Edit).
  void modeChanged();

  // Emitted when the display name changes.
  void nameChanged();

protected slots:
  virtual void handleFindTextChanged(const QString &p_text, FindOptions p_options);

  virtual void handleFindNext(const QStringList &p_texts, FindOptions p_options);

  virtual void handleReplace(const QString &p_text, FindOptions p_options,
                             const QString &p_replaceText);

  virtual void handleReplaceAll(const QString &p_text, FindOptions p_options,
                                const QString &p_replaceText);

  virtual void handleFindAndReplaceWidgetClosed();

  virtual void handleFindAndReplaceWidgetOpened();

protected:
  // ============ Type Action IDs ============

  // Type action IDs for formatting toolbar buttons.
  // Used by handleTypeAction(). Shared across all ViewWindow2 subclasses.
  enum TypeAction {
    TypeHeadingNone = 0,
    TypeHeading1 = 1,
    TypeHeading2 = 2,
    TypeHeading3 = 3,
    TypeHeading4 = 4,
    TypeHeading5 = 5,
    TypeHeading6 = 6,
    TypeBold = 10,
    TypeItalic,
    TypeStrikethrough,
    TypeMark,
    TypeUnorderedList,
    TypeOrderedList,
    TypeTodoList,
    TypeCheckedTodoList,
    TypeCode,
    TypeCodeBlock,
    TypeMath,
    TypeMathBlock,
    TypeQuote,
    TypeLink,
    TypeImage,
    TypeTable
  };

  // ============ Toolbar Action Factory ============

  // Create a toolbar action and wire its standard signals.
  // Subclasses call this in setupToolBar() instead of wiring manually.
  // Returns the created action (caller may connect additional signals).
  QAction *addAction(QToolBar *p_toolBar, ViewWindowToolBarHelper2::Action p_action);

  // Handle a formatting type action by type action ID.
  // @p_action: a TypeAction value (e.g., TypeBold, TypeHeading1).
  // Default: no-op. Override in subclasses that support formatting.
  virtual void handleTypeAction(int p_action);

  // Fetch word count info asynchronously.
  // Subclasses override to provide mode-specific word counting (e.g., async JS
  // extraction in read mode). The callback receives a WordCountInfo struct.
  // Default: calculates from getLatestContent() synchronously.
  virtual void fetchWordCountInfo(
      const std::function<void(const WordCountInfo &)> &p_callback) const;

  // ============ Layout Slots (for subclasses) ============

  // Add a toolbar at the top. Call once in subclass setupUI().
  // Takes ownership via QObject parent. Must be called before addTopWidget().
  void addToolBar(QToolBar *p_bar);

  // Add a widget above the central widget (below toolbar).
  void addTopWidget(QWidget *p_widget);

  // Set the central editor widget (stretch=1). Takes ownership via layout.
  // Call this in subclass constructors to provide the main editing widget.
  // @p_widget: The editor widget to display. nullptr is allowed (clears existing widget).
  void setCentralWidget(QWidget *p_widget);

  // Add a widget below the central widget (above status widget).
  void addBottomWidget(QWidget *p_widget);

  // Set the status widget at the bottom. Takes shared ownership.
  void setStatusWidget(const QSharedPointer<StatusWidget> &p_widget);

  // Show a transient message in the status widget (or fallback).
  void showMessage(const QString &p_msg);

  // Create a standard toolbar with the configured icon size.
  static QToolBar *createToolBar(QWidget *p_parent = nullptr);

  // Add common left-side toolbar actions: save + word count + tag + attachment.
  // Call near the beginning of subclass setupToolBar() after creating the toolbar.
  // @p_toolBar: The toolbar to add actions to.
  void addLeftCommonToolBarActions(QToolBar *p_toolBar);

  // Add common right-side toolbar actions: spacer + layout mode toggle + find-and-replace
  // + print. Call at the end of subclass setupToolBar() after adding subclass-specific
  // actions.
  // @p_toolBar: The toolbar to add actions to.
  void addRightCommonToolBarActions(QToolBar *p_toolBar);

  // Add subclass-specific right-side toolbar actions between the spacer and the standard
  // right-side actions.
  virtual void addAdditionalRightToolBarActions(QToolBar *p_toolBar);

  // Handle print action.
  virtual void handlePrint();

  // ============ Find and Replace ============

  virtual void showFindAndReplaceWidget();

  void hideFindAndReplaceWidget();

  bool findAndReplaceWidgetVisible() const;

  // Set the replace-enabled state on the find-and-replace widget.
  // Useful for subclasses to disable replace in read-only modes.
  void setFindAndReplaceReplaceEnabled(bool p_enabled);

  // @p_currentMatchIndex: 0-based.
  void showFindResult(const QStringList &p_texts, int p_totalMatches, int p_currentMatchIndex);

  void showReplaceResult(const QString &p_text, int p_totalReplaces);

  virtual QString selectedText() const;

  void findNextOnLastFind(bool p_forward = true);

  bool eventFilter(QObject *p_obj, QEvent *p_event) Q_DECL_OVERRIDE;

  void keyPressEvent(QKeyEvent *p_event) Q_DECL_OVERRIDE;

  void wheelEvent(QWheelEvent *p_event) Q_DECL_OVERRIDE;

  void resizeEvent(QResizeEvent *p_event) Q_DECL_OVERRIDE;

  // ============ Editor Integration ============

  // Access the ServiceLocator to retrieve application services.
  ServiceLocator &getServices() const;

  // Sync editor content from buffer. Called when buffer is reloaded or changes externally.
  // Subclasses must implement to update editor display from buffer content.
  // Pure virtual: must be implemented by subclasses.
  virtual void syncEditorFromBuffer() = 0;

  // Set the editor's modification indicator (visual marker in UI).
  // @p_modified: true to mark as modified, false to clear modification flag.
  // Subclasses must implement to update their editor UI state.
  // Pure virtual: must be implemented by subclasses.
  virtual void setModified(bool p_modified) = 0;

  // Scroll up in the document.
  // Pure virtual: must be implemented by subclasses.
  virtual void scrollUp() = 0;

  // Scroll down in the document.
  // Pure virtual: must be implemented by subclasses.
  virtual void scrollDown() = 0;

  // Zoom in or out.
  // @p_zoomIn: true to zoom in, false to zoom out.
  // Pure virtual: must be implemented by subclasses.
  virtual void zoom(bool p_zoomIn) = 0;

  // Display a zoom factor message (e.g., "Zoomed: 125%").
  // Convenience for subclasses that track zoom as a factor (web views).
  void showZoomFactor(qreal p_factor);

  // Display a zoom delta message (e.g., "Zoomed: +2").
  // Convenience for subclasses that track zoom as a delta (text editors).
  void showZoomDelta(int p_delta);

  // Called by subclasses when their editor widget's content changes.
  // Sets dirty flag and notifies BufferService.
  // Subclasses should connect their editor's contentsChanged/textChanged signal to this.
  void onEditorContentsChanged();

  // Trigger a manual save of the buffer content to disk.
  // Syncs editor content to vxcore buffer, then saves to disk.
  // Returns true on success.
  bool save();

  // Current view mode (Read or Edit).
  ViewWindowMode m_mode = ViewWindowMode::Read;

  // Whether editor content has changed since last sync to buffer.
  bool m_editorDirty = false;

  // Last known buffer revision (for detecting external changes on focus gain).
  int m_lastKnownRevision = 0;

private slots:
  // Called when BufferService emits bufferAutoSaved for any buffer.
  // Clears dirty/modified state if the buffer ID matches ours.
  void onBufferAutoSaved(const QString &p_bufferId);

  // Called when BufferService emits bufferModifiedChanged for any buffer.
  // Emits statusChanged() if the buffer ID matches ours.
  void onBufferModifiedChanged(const QString &p_bufferId);

  // Called when BufferService emits attachmentChanged for any buffer.
  // Updates the attachment icon if the buffer ID matches ours.
  void onAttachmentChanged(const QString &p_bufferId);

private:
  struct FindInfo {
    QStringList m_texts;
    FindOptions m_options;
  };

  void setupUI();

  // Focus event handlers for auto-save integration.
  void onFocusGained();
  void onFocusLost();

  // Recalculate content margins for readable-width mode.
  void updateContentMargins();

  // Update EditReadDiscardAction state based on current view mode.
  void updateEditReadDiscardActionState();

  // Discard unsaved changes and switch to Read mode.
  // Shows Save/Discard/Cancel dialog if buffer is modified.
  void discardChangesAndRead();

  ServiceLocator &m_services;

  Buffer2 m_buffer;

  // Main vertical layout: [topLayout] [centralWidget (stretch)] [bottomLayout]
  // Managed by QObject.
  QVBoxLayout *m_mainLayout = nullptr;

  // Top layout: toolbar + top widgets.
  // Managed by QObject.
  QVBoxLayout *m_topLayout = nullptr;

  // Bottom layout: bottom widgets + status widget.
  // Managed by QObject.
  QVBoxLayout *m_bottomLayout = nullptr;

  // Content layout wrapping central widget for readable-width margin support.
  // Managed by QObject.
  QHBoxLayout *m_contentLayout = nullptr;

  // Per-tab layout mode override. -1 = use global default from WidgetConfig.
  int m_layoutModeOverride = -1;

  // Managed by QObject.
  QWidget *m_centralWidget = nullptr;

  // Managed by QObject.
  QToolBar *m_toolBar = nullptr;

  // Shared ownership — StatusWidget may be reparented to global status bar.
  QSharedPointer<StatusWidget> m_statusWidget;

  // Find and replace widget. Managed by QObject.
  FindAndReplaceWidget2 *m_findAndReplace = nullptr;

  // Layout mode toggle action. Managed by QObject.
  QAction *m_layoutModeAction = nullptr;

  // Save action. Managed by QObject.
  QAction *m_saveAction = nullptr;

  // Edit/Read/Discard action. Managed by QObject.
  EditReadDiscardAction *m_editReadAction = nullptr;

  // Attachment popup widget. Managed by QObject.
  AttachmentPopup2 *m_attachmentPopup = nullptr;

  // Attachment toolbar action. Managed by QObject.
  QAction *m_attachmentAction = nullptr;

  // Print action. Managed by QObject.
  QAction *m_printAction = nullptr;

  // Attachment drag-drop area indicator. Managed by QObject. Lazily created.
  AttachmentDragDropAreaIndicator2 *m_attachmentDragDropIndicator = nullptr;

  // Last find info for findNextOnLastFind().
  FindInfo m_findInfo;

  // Update the attachment action icon based on whether the buffer has attachments.
  void updateAttachmentIcon();

  static QIcon generateAttachmentFullIcon(const QString &p_iconFile, const QString &p_foreground,
                                          const QString &p_disabledForeground);

  static QString s_attachmentFullIconFile;

  static QString s_attachmentFullIconForeground;

  static QString s_attachmentFullIconDisabledForeground;

  static QIcon s_attachmentFullIcon;
};

} // namespace vnotex

#endif // VIEWWINDOW2_H
