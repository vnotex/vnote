#ifndef IVIEWWINDOWNAVIGATOR_H
#define IVIEWWINDOWNAVIGATOR_H

#include <QIcon>
#include <QString>
#include <QVector>

#include <core/global.h>

namespace vnotex {

// One row in the "windows" united-entry tree.
// Rows are grouped by workspace: entries with the same workspaceId form a
// workspace parent with its windows as children.
struct OpenWindowEntry {
  QString workspaceId;
  QString workspaceName;
  bool workspaceVisible = false;
  ID windowId = 0;
  QString bufferId;   // for focus + filtering fallback
  QString windowName; // ViewWindow2::getName()
  QString windowTitle; // tooltip (getTitle())
  QIcon windowIcon;
  bool windowCurrent = false;
};

// Injected seam used by WindowsUnitedEntry to enumerate and focus open view
// windows without depending on widget types. Implemented by ViewAreaController.
//
// NOTE: This interface intentionally has NO Q_OBJECT / Qt meta object. A
// QObject-derived class (ViewAreaController) multiply-inherits it, so it must
// stay a plain pure-virtual interface to avoid moc conflicts.
class IViewWindowNavigator {
public:
  virtual ~IViewWindowNavigator() = default;

  // Grouped by workspace, in workspace order; only workspaces with >=1 window.
  virtual QVector<OpenWindowEntry> listOpenWindows() const = 0;

  // Focus a specific window; surfaces a hidden workspace if needed.
  virtual void focusWindow(const QString &p_workspaceId, const QString &p_bufferId) = 0;
};

} // namespace vnotex

#endif // IVIEWWINDOWNAVIGATOR_H
