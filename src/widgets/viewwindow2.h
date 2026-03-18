#ifndef VIEWWINDOW2_H
#define VIEWWINDOW2_H

#include <QFrame>
#include <QIcon>
#include <QSharedPointer>
#include <QString>

#include <core/global.h>
#include <core/services/buffer2.h>

class QVBoxLayout;
class QToolBar;

namespace vnotex {

class ServiceLocator;
class StatusWidget;

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

  // ============ Mode ============

  // Get the current view mode (Read or Edit).
  ViewWindowMode getMode() const;

  // Set view mode (Read/Edit). Subclasses implement mode switching and UI updates.
  // Pure virtual: must be implemented by subclasses.
  virtual void setMode(ViewWindowMode p_mode) = 0;

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

signals:
  // Emitted when this window gains keyboard focus.
  void focused(ViewWindow2 *p_win);

  // Emitted when window status changes (modification state, etc.).
  void statusChanged();

  // Emitted when the view mode changes (Read <-> Edit).
  void modeChanged();

  // Emitted when the display name changes.
  void nameChanged();

protected:
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

private:
  void setupUI();

  // Focus event handlers for auto-save integration.
  void onFocusGained();
  void onFocusLost();

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

  // Managed by QObject.
  QWidget *m_centralWidget = nullptr;

  // Managed by QObject.
  QToolBar *m_toolBar = nullptr;

  // Shared ownership — StatusWidget may be reparented to global status bar.
  QSharedPointer<StatusWidget> m_statusWidget;
};

} // namespace vnotex

#endif // VIEWWINDOW2_H
