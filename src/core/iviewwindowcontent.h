#ifndef IVIEWWINDOWCONTENT_H
#define IVIEWWINDOWCONTENT_H

#include <QIcon>
#include <QString>

class QToolBar;
class QWidget;

namespace vnotex {

// Pure abstract interface for widgets that can be hosted in WidgetViewWindow2.
//
// Implementors are expected to be QWidget subclasses. WidgetViewWindow2 will call
// contentWidget() to obtain the hosted widget and embed it in the view area.
//
// Dirty-state notification contract:
// Implementors must emit a signal named `contentChanged()` from the QWidget
// returned by contentWidget() when dirty state changes. WidgetViewWindow2
// connects to this signal to update the tab title.
class IViewWindowContent {
public:
  virtual ~IViewWindowContent() = default;

  // --- Identity ---

  // Tab display name.
  virtual QString title() const = 0;

  // Tab icon.
  virtual QIcon icon() const = 0;

  // Virtual buffer address (e.g., "vx://settings") used for deduplication.
  // Two content widgets with the same virtualAddress() are considered the same
  // and will not be opened twice.
  virtual QString virtualAddress() const = 0;

  // --- Toolbar ---

  // Populate the given toolbar with actions specific to this content.
  // Called each time this content becomes the active tab.
  virtual void setupToolBar(QToolBar *p_toolBar) = 0;

  // --- Dirty state ---

  // Whether the content has unsaved changes.
  virtual bool isDirty() const = 0;

  // Save/apply changes. Returns true on success.
  virtual bool save() = 0;

  // Discard changes and revert to last saved state.
  virtual void reset() = 0;

  // --- Lifecycle ---

  // Whether this content can be closed. If @p_force is true, close without
  // prompting even if dirty.
  virtual bool canClose(bool p_force) = 0;

  // Return the QWidget to embed in the view area.
  virtual QWidget *contentWidget() = 0;
};

} // namespace vnotex

#endif // IVIEWWINDOWCONTENT_H
