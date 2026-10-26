#ifndef LINKPREVIEWOVERLAY_H
#define LINKPREVIEWOVERLAY_H

#include <QFrame>
#include <QRect>

class QAction;
class QMouseEvent;

namespace vnotex {

class ServiceLocator;
class ViewSplit2;
class ViewWindow2;

// Editable child surface outside the source's layout. ViewArea2 supplies the
// content rectangle and owns the cancellable workspace/tab lifecycle.
class LinkPreviewOverlay : public QFrame {
  Q_OBJECT

public:
  LinkPreviewOverlay(ServiceLocator &p_services, ViewWindow2 *p_source,
                     const QString &p_workspaceId);

  ViewSplit2 *getViewSplit() const;

  // Source-local content geometry, before the overlay's 16-pixel edge inset.
  void setAnchorRect(const QRect &p_rect);

signals:
  void closeRequested();
  void visibilityChanged(bool p_visible);

protected:
  bool event(QEvent *p_event) override;
  void mousePressEvent(QMouseEvent *p_event) override;

private:
  void raiseSurface();
  void scheduleRaise();

  ViewSplit2 *m_viewSplit = nullptr;
  QAction *m_closeAction = nullptr;
  bool m_raisePending = false;
};

} // namespace vnotex

#endif // LINKPREVIEWOVERLAY_H
