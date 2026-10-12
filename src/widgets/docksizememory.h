#ifndef DOCKSIZEMEMORY_H
#define DOCKSIZEMEMORY_H

#include <QObject>
#include <QSize>
#include <QTimer>

class QDockWidget;
class QMainWindow;

namespace vnotex {
// Remembers a capability-dependent dock's split size while its content is unavailable.
// QMainWindow remains responsible for placement and floating/tabbed geometry.
class DockSizeMemory : public QObject {
public:
  DockSizeMemory(QMainWindow *p_window, QDockWidget *p_dock);

  QSize getSize() const;

  // An empty size clears the preference and cancels pending restoration.
  void setSize(const QSize &p_size);

protected:
  bool eventFilter(QObject *p_object, QEvent *p_event) override;

private:
  bool isSeparateDock() const;

  void restoreSize(Qt::Orientation p_orientation);

  QMainWindow *m_window;
  QDockWidget *m_dock;
  QSize m_size;
  QTimer m_restoreTimer;
  bool m_restoreOnShow = false;
};
} // namespace vnotex

#endif // DOCKSIZEMEMORY_H
