#ifndef VNOTEX_TODOTASKVIEW_H
#define VNOTEX_TODOTASKVIEW_H

#include <QListView>
#include <QPersistentModelIndex>

#include <array>

class QColor;
class QPainter;

namespace vnotex {
class ServiceLocator;

void paintTodoCompletionIndicator(QPainter *p_painter, const QRect &p_rect, bool p_checked,
                                  const QColor &p_foreground);

class TodoTaskView : public QListView {
  Q_OBJECT
public:
  explicit TodoTaskView(ServiceLocator &p_services, QWidget *p_parent = nullptr);

  void setShowLocation(bool p_show);
  void setModel(QAbstractItemModel *p_model) override;

signals:
  void taskClicked(const QModelIndex &p_index);
  void taskActivated(const QModelIndex &p_index);

protected:
  void mousePressEvent(QMouseEvent *p_event) override;
  void mouseMoveEvent(QMouseEvent *p_event) override;
  void mouseReleaseEvent(QMouseEvent *p_event) override;
  void mouseDoubleClickEvent(QMouseEvent *p_event) override;
  void keyPressEvent(QKeyEvent *p_event) override;
  bool viewportEvent(QEvent *p_event) override;
  void changeEvent(QEvent *p_event) override;

private:
  enum class HitPart { None, Body, Check, Star, Section };

  HitPart hitPart(const QModelIndex &p_index, const QPoint &p_position) const;
  bool isTaskIndex(const QModelIndex &p_index) const;
  void toggleControl(const QModelIndex &p_index, HitPart p_part);
  void cancelControlCommit();
  void updateTaskCursor();
  void setTaskCursor(const QModelIndex &p_index);

  std::array<QMetaObject::Connection, 8> m_modelConnections;
  QPersistentModelIndex m_pressedIndex;
  HitPart m_pressedPart = HitPart::None;
  bool m_controlGesture = false;
  bool m_lastPressWasControl = false;
  bool m_bodyRelease = false;
  bool m_bodyDoubleClick = false;
  bool m_suppressActivation = false;
};
} // namespace vnotex

#endif // VNOTEX_TODOTASKVIEW_H
