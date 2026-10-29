#ifndef LINKPREVIEWOVERLAY_H
#define LINKPREVIEWOVERLAY_H

#include <QFrame>
#include <QPoint>
#include <QRect>

class QMouseEvent;

namespace vnotex {

class ServiceLocator;
class ViewSplit2;
class ViewWindow2;

// Editable child surface outside the source's layout. ViewArea2 supplies the
// content rectangle and owns the cancellable workspace/tab lifecycle.
// Its visible-only application filter captures source-backdrop gestures and
// sends closeRequested without forwarding the dismissal click to the source.
class LinkPreviewOverlay : public QFrame {
  Q_OBJECT

public:
  LinkPreviewOverlay(ServiceLocator &p_services, ViewWindow2 *p_source,
                     const QString &p_workspaceId);

  ViewSplit2 *getViewSplit() const;

  // Source-local content geometry, before the 16-pixel inset and centered 90% sizing.
  void setAnchorRect(const QRect &p_rect);

signals:
  void closeRequested();
  void openInNewTabsRequested();
  void visibilityChanged(bool p_visible);

protected:
  bool event(QEvent *p_event) override;
  bool eventFilter(QObject *p_object, QEvent *p_event) override;
  void mousePressEvent(QMouseEvent *p_event) override;

private:
  void raiseSurface();
  void scheduleRaise();

  ViewSplit2 *m_viewSplit = nullptr;
  bool m_raisePending = false;
  QPoint m_dismissalPressPosition;
  bool m_dismissalPressActive = false;
  bool m_dismissalClickCancelled = false;
};

} // namespace vnotex

#endif // LINKPREVIEWOVERLAY_H
