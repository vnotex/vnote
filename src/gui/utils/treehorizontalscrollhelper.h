#ifndef TREEHORIZONTALSCROLLHELPER_H
#define TREEHORIZONTALSCROLLHELPER_H

#include <QObject>
#include <QPointer>

class QTreeView;
class QAbstractItemModel;
class QTimer;

namespace vnotex {
// Makes a QTreeView show a horizontal scrollbar when the content of column 0 is
// wider than the viewport, while never letting the column become narrower than
// the viewport (so hit testing, drop targeting and full-row painting still cover
// the whole viewport width).
//
// The helper installs itself on @p_view as a child object; there is no need to
// keep the returned pointer.
class TreeHorizontalScrollHelper : public QObject {
  Q_OBJECT

public:
  explicit TreeHorizontalScrollHelper(QTreeView *p_view);

protected:
  bool eventFilter(QObject *p_obj, QEvent *p_event) override;

private:
  void scheduleRecompute();

  void recompute();

  // Re-wire the model signals if the view's model has been swapped.
  void attachModel();

  QTreeView *m_view = nullptr;

  QTimer *m_timer = nullptr;

  QPointer<QAbstractItemModel> m_model;

  bool m_updating = false;
};
} // namespace vnotex

#endif // TREEHORIZONTALSCROLLHELPER_H
