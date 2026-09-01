#include "contentfullscreenhost.h"

#include <QApplication>
#include <QBoxLayout>
#include <QEvent>
#include <QKeyEvent>
#include <QPushButton>
#include <QVBoxLayout>
#include <QWidget>

using namespace vnotex;

ContentFullScreenHost::ContentFullScreenHost(QObject *p_parent)
    : QObject(p_parent), m_exitButtonText(tr("Exit Full Screen")) {}

void ContentFullScreenHost::setExitButtonText(const QString &p_text) {
  if (p_text.isEmpty()) {
    return;
  }
  m_exitButtonText = p_text;
  if (m_exitButton) {
    m_exitButton->setText(p_text);
    m_exitButton->setToolTip(tr("%1 (Esc)").arg(p_text));
    layoutExitButton();
  }
}

ContentFullScreenHost::~ContentFullScreenHost() {
  // The content belongs to the caller and is currently parented to a container
  // that is about to die with this object. Put it back first, or it is
  // destroyed along with the container.
  exitFullScreen();
}

bool ContentFullScreenHost::isFullScreen() const { return !m_container.isNull(); }

QWidget *ContentFullScreenHost::container() const { return m_container.data(); }

QPushButton *ContentFullScreenHost::exitButton() const { return m_exitButton.data(); }

QWidget *ContentFullScreenHost::content() const { return m_content.data(); }

bool ContentFullScreenHost::exitFullScreen() {
  return setFullScreen(false, nullptr, nullptr, nullptr);
}

bool ContentFullScreenHost::setFullScreen(bool p_on, QWidget *p_content, QBoxLayout *p_home,
                                          QWidget *p_ownerWindow) {
  if (p_on == isFullScreen()) {
    return false;
  }

  if (p_on) {
    if (!p_content || !p_home) {
      return false;
    }

    m_content = p_content;
    m_home = p_home;
    // Both remembered BEFORE the widget leaves, because a layout reports
    // nothing about an item it no longer holds. Restoring by APPENDING would
    // silently reorder a layout that has widgets on both sides of the content,
    // and a hardcoded stretch would collapse it to its size hint.
    m_homeIndex = p_home->indexOf(p_content);
    m_homeStretch = m_homeIndex >= 0 ? p_home->stretch(m_homeIndex) : 1;

    // Parented to the owner window rather than left ownerless: closing that
    // window must not leave a fullscreen widget behind on screen.
    m_container = new QWidget(p_ownerWindow, Qt::Window);
    auto *layout = new QVBoxLayout(m_container);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    p_home->removeWidget(p_content);
    layout->addWidget(p_content, 1);

    // A VISIBLE way out, not only a key.
    //
    // Everything else in the window -- toolbar, menus -- stayed behind, so
    // while the content is fullscreen this button is the ONLY thing on screen
    // that can end it. Relying on Escape alone left users with no affordance at
    // all, and (see the application-level filter below) the key does not even
    // reach a QWebEngineView's host reliably.
    //
    // The LABEL names the mode the owner entered, not the mechanism. "Exit
    // Full Screen" reads as the application's own full-screen toggle and sends
    // the user looking at the View menu; the owner supplies the term its users
    // actually saw on the way in (PdfViewWindow2 passes "Exit Presentation
    // Mode"). The Escape hint lives in the tooltip so the visible label stays
    // short.
    //
    // A child of the container rather than a layout item, so it floats OVER the
    // content instead of stealing a strip of it. No stylesheet: the theme's
    // QSS styles a plain QPushButton, and a literal colour here would be wrong
    // in 11 of the 12 themes.
    m_exitButton = new QPushButton(m_exitButtonText, m_container);
    m_exitButton->setToolTip(tr("%1 (Esc)").arg(m_exitButtonText));
    m_exitButton->setFocusPolicy(Qt::NoFocus);
    m_exitButton->setCursor(Qt::ArrowCursor);
    connect(m_exitButton, &QPushButton::clicked, this, [this]() { emit exitRequested(); });
    m_exitButton->adjustSize();
    m_exitButton->raise();

    // Escape must be caught at the APPLICATION level, not on the container.
    //
    // A filter on the container only sees events DELIVERED to the container,
    // and a key press goes to the focus widget -- which for a QWebEngineView is
    // Chromium's render widget, several levels down, and it consumes the event
    // rather than letting it propagate up. That is why an earlier
    // container-only filter left presentation mode with no working exit at all.
    // An application filter runs inside QCoreApplication::notify, before the
    // target sees anything.
    //
    // Scoped in eventFilter() to events destined for this container, so it
    // cannot swallow Escape anywhere else in the app.
    m_container->installEventFilter(this);
    if (auto *app = QCoreApplication::instance()) {
      app->installEventFilter(this);
    }

    m_container->showFullScreen();
    p_content->show();
    p_content->setFocus();
    layoutExitButton();
    return true;
  }

  auto *container = m_container.data();
  auto *content = m_content.data();
  auto *home = m_home.data();

  // Cleared FIRST, so anything the restore below re-enters already sees a
  // non-fullscreen host.
  m_container = nullptr;
  m_content = nullptr;
  m_home = nullptr;

  if (content && home) {
    if (container->layout()) {
      container->layout()->removeWidget(content);
    }
    // insertWidget, not addWidget: the content goes back where it was, not on
    // the end. Clamped, because the layout may have changed while it was away.
    const int index = m_homeIndex >= 0 ? qMin(m_homeIndex, home->count()) : home->count();
    home->insertWidget(index, content, m_homeStretch);
    content->show();
  }

  if (container) {
    container->removeEventFilter(this);
    if (auto *app = QCoreApplication::instance()) {
      app->removeEventFilter(this);
    }
    // hide() before deleteLater(): a fast exit/enter cycle must not leave a
    // still-visible fullscreen window queued for destruction on top of the new
    // one.
    container->hide();
    container->deleteLater();
  }
  m_exitButton = nullptr;
  return true;
}

void ContentFullScreenHost::layoutExitButton() {
  if (!m_container || !m_exitButton) {
    return;
  }
  // Top-right, inset by a margin. Positioned by hand rather than through the
  // layout so it floats OVER the content instead of taking a strip of it.
  const int margin = 12;
  const QSize size = m_exitButton->sizeHint();
  m_exitButton->resize(size);
  m_exitButton->move(qMax(0, m_container->width() - size.width() - margin), margin);
  m_exitButton->raise();
}

// Whether an event destined for @p_obj belongs to this host's fullscreen
// container. The application-level filter MUST be scoped this way, or it would
// swallow Escape everywhere else in the app.
bool ContentFullScreenHost::ownsEventTarget(QObject *p_obj) const {
  if (!m_container) {
    return false;
  }
  if (p_obj == m_container) {
    return true;
  }
  if (auto *widget = qobject_cast<QWidget *>(p_obj)) {
    // Covers Chromium's render widget, which is several levels below the
    // QWebEngineView and is where the key press is actually delivered.
    return m_container->isAncestorOf(widget) || widget->window() == m_container;
  }
  // A native window event (the render widget's QWindow) carries no widget
  // parent chain; fall back to window activation.
  return QApplication::activeWindow() == m_container;
}

bool ContentFullScreenHost::eventFilter(QObject *p_obj, QEvent *p_event) {
  switch (p_event->type()) {
  case QEvent::KeyPress:
    if (static_cast<QKeyEvent *>(p_event)->key() == Qt::Key_Escape && ownsEventTarget(p_obj)) {
      emit exitRequested();
      return true;
    }
    break;

  case QEvent::ShortcutOverride:
    // Claim Escape before any QShortcut or the focus widget can act on it, so
    // the KeyPress above is guaranteed to arrive.
    if (static_cast<QKeyEvent *>(p_event)->key() == Qt::Key_Escape && ownsEventTarget(p_obj)) {
      p_event->accept();
      return true;
    }
    break;

  case QEvent::Resize:
    if (p_obj == m_container) {
      layoutExitButton();
    }
    break;

  default:
    break;
  }

  return QObject::eventFilter(p_obj, p_event);
}
