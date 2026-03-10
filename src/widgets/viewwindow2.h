#ifndef VIEWWINDOW2_H
#define VIEWWINDOW2_H

#include <QFrame>
#include <QIcon>
#include <QString>

#include <core/global.h>
#include <core/services/buffer2.h>

class QVBoxLayout;
class QToolBar;

namespace vnotex {

class ServiceLocator;

// Abstract base class for all view windows in the new architecture.
// A view window displays a single file (Buffer2) in a specific mode (Read/Edit).
// Subclasses implement concrete views for different file types (Markdown, Plain Text, PDF, etc.).
//
// Responsibilities:
// - Manage a file buffer (obtained via Buffer2 handle)
// - Provide UI for viewing/editing based on file type
// - Handle mode switching (Read <-> Edit)
// - Sync content between editor widget and buffer
// - Report modification state
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
  // Default implementation returns true (no unsaved changes handling).
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
  // Set the central editor widget. Takes ownership via layout.
  // Call this in subclass constructors to provide the main editing widget.
  // @p_widget: The editor widget to display. nullptr is allowed (clears existing widget).
  void setCentralWidget(QWidget *p_widget);

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

  // Current view mode (Read or Edit).
  ViewWindowMode m_mode = ViewWindowMode::Read;

private:
  void setupUI();

  ServiceLocator &m_services;

  Buffer2 m_buffer;

  // Managed by QObject (deleted automatically on this->deleteLater()).
  QVBoxLayout *m_mainLayout = nullptr;

  // Managed by QObject (deleted automatically on this->deleteLater()).
  QWidget *m_centralWidget = nullptr;
};

} // namespace vnotex

#endif // VIEWWINDOW2_H
