#ifndef WORKSPACEWRAPPER_H
#define WORKSPACEWRAPPER_H

#include <QObject>
#include <QString>
#include <QVector>

namespace vnotex {

// Controller-owned container for workspace lifecycle and ViewWindow storage.
// Mirrors the legacy ViewWorkspace struct but as a QObject with transfer semantics.
//
// When a workspace is visible, its ViewWindows live in a ViewSplit2 (tab widget).
// When hidden, ViewWindows are detached from the split and stored here (unparented).
// Uses QObject* to avoid controller-layer dependency on widget types.
class WorkspaceWrapper : public QObject {
  Q_OBJECT
public:
  explicit WorkspaceWrapper(const QString &p_workspaceId, QObject *p_parent = nullptr);
  ~WorkspaceWrapper() override;

  const QString &workspaceId() const;

  // Transfer ViewWindows IN from a ViewSplit (workspace being hidden).
  // Takes ownership conceptually (no Qt parent set — windows are unparented).
  // Asserts that internal list was empty before receiving.
  void receiveViewWindows(const QVector<QObject *> &p_windows, int p_currentIndex);

  // Transfer ViewWindows OUT to a ViewSplit (workspace being shown).
  // Clears internal list. Caller takes ownership.
  QVector<QObject *> takeAllViewWindows();

  // Query
  const QVector<QObject *> &viewWindows() const;
  int viewWindowCount() const;
  bool isVisible() const;
  void setVisible(bool p_visible);

  // Current window tracking
  int currentIndex() const;
  void setCurrentIndex(int p_index);

private:
  QString m_workspaceId;
  QVector<QObject *> m_viewWindows;
  int m_currentIndex = 0;
  bool m_visible = false;
};

} // namespace vnotex

#endif // WORKSPACEWRAPPER_H
