#ifndef CONTENTFULLSCREENHOST_H
#define CONTENTFULLSCREENHOST_H

#include <QObject>
#include <QPointer>

class QBoxLayout;
class QPushButton;
class QWidget;

namespace vnotex {

// Lifts a widget out of its layout into a frameless fullscreen top-level, and
// puts it back.
//
// Extracted out of ViewWindow2 for the same reason PdfViewerToolBar was
// extracted out of PdfViewWindow2: no test compiles viewwindow2.cpp (it drags
// in the whole widget world), so mechanics left inline there would be
// ungated -- and reparenting is exactly the kind of code that fails as a stuck
// or orphaned window rather than as a crash.
//
// Deliberately knows NOTHING about what it is moving. A web view asking for
// HTML5 fullscreen, a distraction-free editor, a slideshow: all the same
// operation.
class ContentFullScreenHost : public QObject {
  Q_OBJECT
public:
  explicit ContentFullScreenHost(QObject *p_parent = nullptr);

  ~ContentFullScreenHost() Q_DECL_OVERRIDE;

  // @p_content: the widget to lift. Only read when entering.
  // @p_home: the layout it normally lives in, and returns to.
  // @p_ownerWindow: parent for the container, so closing that window cannot
  //   leave a fullscreen widget stranded on screen. May be null.
  //
  // Returns false when there is nothing to do: already in the requested state,
  // or entering with no content / no home layout. A caller driving this from a
  // page request should treat false as "the page and Qt disagree" rather than
  // ignoring it.
  bool setFullScreen(bool p_on, QWidget *p_content, QBoxLayout *p_home, QWidget *p_ownerWindow);

  // Convenience for leaving; the content and layout are remembered.
  bool exitFullScreen();

  bool isFullScreen() const;

  // The container, or null. For tests and for a caller that needs to title it.
  QWidget *container() const;

  // The always-visible exit button, or null. While the content is fullscreen
  // this is the ONLY thing on screen that can end it -- the window's own chrome
  // stayed behind.
  QPushButton *exitButton() const;

  // Label for that button. Name the MODE the owner entered, not the mechanism:
  // "Exit Full Screen" reads as the application's own full-screen toggle and
  // sends the user looking at the View menu. PdfViewWindow2 passes
  // "Exit Presentation Mode". The Escape hint is appended to the tooltip
  // automatically, so the visible label stays short.
  void setExitButtonText(const QString &p_text);

  // The widget currently lifted, or null.
  QWidget *content() const;

signals:
  // Escape was pressed on the container. Deliberately an INTENT rather than an
  // automatic exit: the owner usually has to tell the content first (a web page
  // must be driven out through Chromium, or it keeps believing it is fullscreen
  // and rejects the next request).
  void exitRequested();

protected:
  bool eventFilter(QObject *p_obj, QEvent *p_event) Q_DECL_OVERRIDE;

private:
  // Whether an event destined for @p_obj belongs to this host's container. The
  // application-level filter MUST be scoped this way, or it would swallow
  // Escape everywhere else in the app.
  bool ownsEventTarget(QObject *p_obj) const;

  void layoutExitButton();

  // QPointer throughout: the content and the home layout belong to the caller
  // and can be destroyed while this object is still alive (a view window torn
  // down from underneath). A raw pointer here would be a dangling reparent.
  QPointer<QWidget> m_container;

  QPointer<QPushButton> m_exitButton;

  QString m_exitButtonText;

  QPointer<QWidget> m_content;

  QPointer<QBoxLayout> m_home;

  // Where the content sat in its home layout, so putting it back does not
  // silently reorder the layout or collapse the content to its size hint.
  int m_homeIndex = -1;

  int m_homeStretch = 1;
};

} // namespace vnotex

#endif // CONTENTFULLSCREENHOST_H
