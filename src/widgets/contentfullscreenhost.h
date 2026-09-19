#ifndef CONTENTFULLSCREENHOST_H
#define CONTENTFULLSCREENHOST_H

#include <QObject>
#include <QPointer>
#include <QRect>

class QLayout;
class QScreen;
class QWidget;

namespace vnotex {

// Promotes an existing child widget to a frameless fullscreen window in place.
// Its parent, tab identity and child layout remain unchanged. The containing
// layout is suspended until the widget returns to its original window flags.
class ContentFullScreenHost : public QObject {
  Q_OBJECT
public:
  explicit ContentFullScreenHost(QObject *p_parent = nullptr);

  ~ContentFullScreenHost() Q_DECL_OVERRIDE;

  // Entry requires a visible, parented, non-window widget. Returns false for
  // invalid or redundant transitions; p_content is only read on entry.
  bool setFullScreen(bool p_on, QWidget *p_content);

  bool exitFullScreen();

  bool isFullScreen() const;

  QWidget *content() const;

signals:
  // An exit intent, not an automatic transition: the owner reconciles its mode
  // state as well as the native window. An owned popup gets Escape first.
  void exitRequested();

protected:
  bool eventFilter(QObject *p_obj, QEvent *p_event) Q_DECL_OVERRIDE;

private:
  bool ownsEventTarget(QObject *p_obj) const;

  bool hasOwnedPopup() const;

  void restore(bool p_contentDestroyed);

  QPointer<QWidget> m_content;

  QPointer<QWidget> m_parent;

  QPointer<QLayout> m_parentLayout;

  QPointer<QWidget> m_focusWidget;

  QPointer<QScreen> m_screen;

  QMetaObject::Connection m_destroyedConnection;

  Qt::WindowFlags m_windowFlags;

  Qt::WindowStates m_windowState;

  QRect m_geometry;

  bool m_parentLayoutEnabled = false;

  bool m_restoreVisibility = false;

  bool m_quitOnClose = false;

  bool m_transitionInProgress = false;

  // A deferred Hide intent must not escape into a later presentation session.
  quint64 m_generation = 0;
};

} // namespace vnotex

#endif // CONTENTFULLSCREENHOST_H
