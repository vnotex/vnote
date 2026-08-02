#include "notificationtoast.h"

#include <QEvent>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QMouseEvent>
#include <QPointer>
#include <QProgressBar>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>

#include <core/servicelocator.h>
#include <gui/services/themeservice.h>
#include <gui/utils/iconutils.h>
#include <gui/utils/widgetutils.h>

#include "propertydefs.h"

using namespace vnotex;

namespace {

// Same auto-hide budgets the popup used before it stopped auto-showing.
constexpr int c_shortMs = 3000;
constexpr int c_longMs = 7000;

// Persist means "stays in the LIST until dismissed", not "stays on screen
// forever". A toast that never leaves would be a permanent obstruction, so it
// still retires after this cap; the message itself remains in the popup.
constexpr int c_persistToastCapMs = 15000;

constexpr int c_margin = 16;
constexpr int c_maxWidth = 420;

} // namespace

NotificationToast::NotificationToast(ServiceLocator &p_services, QWidget *p_parent)
    : QFrame(p_parent), m_services(p_services) {
  setObjectName(QStringLiteral("NotificationToast"));
  setFrameShape(QFrame::StyledPanel);
  // A child widget cannot take the window's activation, which is the entire
  // point: an arriving toast must not interrupt typing.
  setFocusPolicy(Qt::NoFocus);
  setAttribute(Qt::WA_ShowWithoutActivating);
  setCursor(Qt::PointingHandCursor);
  setMaximumWidth(c_maxWidth);

  setupUI();

  m_autoHideTimer = new QTimer(this);
  m_autoHideTimer->setSingleShot(true);
  connect(m_autoHideTimer, &QTimer::timeout, this, [this]() { hideToast(); });

  setupConnections();

  hide();

  if (p_parent) {
    p_parent->installEventFilter(this);
  }
}

void NotificationToast::setAnchorWidget(QWidget *p_anchor) {
  if (m_anchor) {
    m_anchor->removeEventFilter(this);
  }
  m_anchor = p_anchor;
  if (m_anchor) {
    // Dock/toolbar changes resize the anchor WITHOUT resizing the top-level
    // window, so watching only the parent's resize is not enough.
    m_anchor->installEventFilter(this);
  }
  if (isVisible()) {
    reposition();
  }
}

void NotificationToast::setupUI() {
  m_mainLayout = new QVBoxLayout(this);
  m_mainLayout->setContentsMargins(10, 8, 10, 8);
  m_mainLayout->setSpacing(4);

  auto *headerLayout = new QHBoxLayout();
  headerLayout->setContentsMargins(0, 0, 0, 0);
  headerLayout->setSpacing(6);

  m_iconLabel = new QLabel(this);
  headerLayout->addWidget(m_iconLabel);

  m_titleLabel = new QLabel(this);
  QFont titleFont = m_titleLabel->font();
  titleFont.setBold(true);
  m_titleLabel->setFont(titleFont);
  headerLayout->addWidget(m_titleLabel);
  headerLayout->addStretch();

  auto *closeBtn = new QPushButton(tr("Close"), this);
  closeBtn->setFocusPolicy(Qt::NoFocus);
  connect(closeBtn, &QPushButton::clicked, this, [this]() { hideToast(); });
  headerLayout->addWidget(closeBtn);

  m_mainLayout->addLayout(headerLayout);

  m_textLabel = new QLabel(this);
  m_textLabel->setWordWrap(true);
  m_mainLayout->addWidget(m_textLabel);

  m_progressBar = new QProgressBar(this);
  m_progressBar->setTextVisible(false);
  m_progressBar->hide();
  m_mainLayout->addWidget(m_progressBar);

  m_actionLayout = new QHBoxLayout();
  m_actionLayout->setContentsMargins(0, 0, 0, 0);
  m_actionLayout->setSpacing(4);
  m_actionLayout->addStretch();
  m_mainLayout->addLayout(m_actionLayout);
}

void NotificationToast::setupConnections() {
  auto *service = m_services.get<NotificationService>();
  if (!service) {
    return;
  }

  // ROUTING TABLE. The single rule for raising the toast is
  // "messageAdded carrying Interrupt". There is deliberately no escalation
  // signal: a producer that must re-interrupt an ongoing incident calls
  // NotificationService::renotify(), which removes the old generation and posts
  // a new one -- so it arrives here as a plain messageAdded.
  connect(service, &NotificationService::messageAdded, this,
          [this](const NotificationMessage &p_msg) {
            if (p_msg.m_attention != NotificationMessage::Attention::Interrupt) {
              return;
            }
            present(p_msg, true);
          });

  connect(service, &NotificationService::messageUpdated, this,
          [this](const NotificationMessage &p_msg) {
            // An update only ever concerns the message already on screen. It is
            // never a new event, so it can neither raise the toast nor extend it.
            if (m_shownId == 0 || p_msg.m_id != m_shownId) {
              return;
            }
            if (p_msg.m_attention == NotificationMessage::Attention::Passive) {
              // The producer downgraded this message (e.g. the update offer
              // became a passive "downloading" state). Leaving the old
              // interrupting content up would show a stale title and a stale
              // action button.
              hideToast();
              return;
            }
            present(p_msg, false);
          });

  connect(service, &NotificationService::messageDismissed, this, [this](quint64 p_id) {
    if (m_shownId != 0 && p_id == m_shownId) {
      hideToast();
    }
  });

  // The message is GONE (replaced by renotify(), or evicted by the retention
  // cap). Holding it on screen would leave inert buttons behind.
  connect(service, &NotificationService::messageRemoved, this, [this](quint64 p_id) {
    if (m_shownId != 0 && p_id == m_shownId) {
      hideToast();
    }
  });

  connect(service, &NotificationService::messagesCleared, this, [this]() { hideToast(); });

  auto *themeService = m_services.get<ThemeService>();
  if (themeService) {
    connect(themeService, &ThemeService::themeChanged, this, [this]() {
      if (!isVisible() || m_shownId == 0) {
        return;
      }
      auto *service = m_services.get<NotificationService>();
      if (!service) {
        return;
      }
      for (const auto &msg : service->messages()) {
        if (msg.m_id == m_shownId) {
          render(msg);
          break;
        }
      }
    });
  }
}

void NotificationToast::setCanShowInWindow(std::function<bool()> p_predicate) {
  m_canShowInWindow = std::move(p_predicate);
}

void NotificationToast::setFallbackSink(std::function<void(const NotificationMessage &)> p_sink) {
  m_fallbackSink = std::move(p_sink);
}

quint64 NotificationToast::shownId() const { return m_shownId; }

const char *NotificationToast::severityState(NotificationMessage::Severity p_severity) {
  switch (p_severity) {
  case NotificationMessage::Severity::Info:
    return "info";
  case NotificationMessage::Severity::Success:
    return "success";
  case NotificationMessage::Severity::Warning:
    return "warning";
  case NotificationMessage::Severity::Error:
    return "error";
  }
  return "info";
}

QIcon NotificationToast::severityIcon(NotificationMessage::Severity p_severity) const {
  QString iconName;
  // Explicit cases (no default label) so a new Severity value is visible to a
  // reader and to static analysis rather than silently rendering as Info. The
  // vnote target does not enable -Wswitch, so this is not compiler-enforced.
  switch (p_severity) {
  case NotificationMessage::Severity::Success:
    iconName = QStringLiteral("success.svg");
    break;
  case NotificationMessage::Severity::Warning:
    iconName = QStringLiteral("warning.svg");
    break;
  case NotificationMessage::Severity::Error:
    iconName = QStringLiteral("error.svg");
    break;
  case NotificationMessage::Severity::Info:
    iconName = QStringLiteral("info.svg");
    break;
  }
  if (iconName.isEmpty()) {
    iconName = QStringLiteral("info.svg");
  }

  auto *themeService = m_services.get<ThemeService>();
  if (!themeService) {
    return QIcon();
  }

  // Per-severity tint via the shared semantic roles, which every bundled theme
  // already defines.
  const QString token =
      QStringLiteral("base#%1#fg").arg(QLatin1String(severityState(p_severity)));
  QString fg = themeService->paletteColor(token);
  if (fg.isEmpty()) {
    fg = themeService->paletteColor(QStringLiteral("widgets#toolbar#icon#fg"));
  }
  return IconUtils::fetchIcon(themeService->getIconFile(iconName), fg);
}

void NotificationToast::render(const NotificationMessage &p_msg) {
  WidgetUtils::setPropertyDynamically(this, PropertyDefs::c_state,
                                      QLatin1String(severityState(p_msg.m_severity)));

  m_iconLabel->setPixmap(severityIcon(p_msg.m_severity).pixmap(16, 16));

  m_titleLabel->setText(p_msg.m_title);
  m_titleLabel->setVisible(!p_msg.m_title.isEmpty());

  m_textLabel->setText(p_msg.m_text);
  m_textLabel->setVisible(!p_msg.m_text.isEmpty());

  // m_details is deliberately NOT rendered: the toast must stay small. It lives
  // in the popup list's collapsible disclosure instead.

  if (p_msg.m_progressIndeterminate) {
    m_progressBar->setRange(0, 0);
    m_progressBar->show();
  } else if (p_msg.m_progressPermille >= 0) {
    m_progressBar->setRange(0, 1000);
    m_progressBar->setValue(qBound(0, p_msg.m_progressPermille, 1000));
    m_progressBar->show();
  } else {
    m_progressBar->hide();
  }

  // Rebuild the action buttons. Nothing captured before this point may be used
  // afterwards.
  while (m_actionLayout->count() > 0) {
    QLayoutItem *item = m_actionLayout->takeAt(0);
    if (auto *w = item->widget()) {
      w->deleteLater();
    }
    delete item;
  }
  m_actionLayout->addStretch();

  const quint64 id = p_msg.m_id;
  for (int ai = 0; ai < p_msg.m_actions.size(); ++ai) {
    auto *btn = new QPushButton(p_msg.m_actions.at(ai).m_label, this);
    btn->setFocusPolicy(Qt::NoFocus);
    const int actionIndex = ai;
    connect(btn, &QPushButton::clicked, this, [this, id, actionIndex]() {
      auto *service = m_services.get<NotificationService>();
      if (!service) {
        return;
      }

      // Resolve from CURRENT service state, so a message dismissed or removed
      // since render becomes an inert no-op. The callback AND its dismiss policy
      // come from the SAME lookup: an Update/Retry callback synchronously
      // replaces the action vector, so a second lookup would read a different
      // action's flag.
      std::function<void()> callback;
      bool dismissOnTrigger = true;
      for (const auto &m : service->messages()) {
        if (m.m_id == id && !m.m_dismissed && actionIndex < m.m_actions.size()) {
          callback = m.m_actions.at(actionIndex).m_callback;
          dismissOnTrigger = m.m_actions.at(actionIndex).m_dismissOnTrigger;
          break;
        }
      }

      // The callback may synchronously destroy this widget (e.g. restart the
      // app), so guard every post-callback access.
      QPointer<NotificationToast> guard(this);
      if (callback) {
        callback();
      }
      if (!guard) {
        return;
      }
      if (!dismissOnTrigger) {
        return;
      }
      if (auto *svc = m_services.get<NotificationService>()) {
        svc->dismiss(id);
      }
    });
    m_actionLayout->addWidget(btn);
  }

  adjustSize();
  reposition();
}

void NotificationToast::present(const NotificationMessage &p_msg, bool p_restartTimer) {
  const bool canShow = m_canShowInWindow ? m_canShowInWindow() : true;
  if (!canShow) {
    // Out-of-window fallback (tray balloon). Retire whatever is currently on
    // screen first: a toast shown before the window was minimized keeps its own
    // shown state (a child widget is not "hidden" just because its top-level
    // ancestor is), so restoring the window would otherwise pop the OLD message
    // back up even though a NEWER interrupt has since gone to the tray.
    if (m_shownId != 0) {
      hideToast();
    }
    if (m_fallbackSink) {
      m_fallbackSink(p_msg);
    }
    return;
  }

  m_shownId = p_msg.m_id;
  m_shownDuration = p_msg.m_duration;

  render(p_msg);

  // NOTE: renotify() always changes the id, so it necessarily arrives as
  // messageRemoved(old) -> hide, then messageAdded(new) -> show. Both are direct
  // synchronous deliveries under the GUI-thread-only producer contract, so the
  // pair completes before the event loop repaints and no blank frame is
  // rendered. Keep these plain visibility changes: adding a fade or a deferred
  // teardown here would turn that into a visible flicker.
  show();
  raise();

  if (p_restartTimer) {
    startAutoHideTimer(p_msg.m_duration);
  }
}

void NotificationToast::startAutoHideTimer(NotificationMessage::Duration p_duration) {
  m_autoHideTimer->stop();
  switch (p_duration) {
  case NotificationMessage::Duration::Short:
    m_autoHideTimer->start(c_shortMs);
    break;
  case NotificationMessage::Duration::Long:
    m_autoHideTimer->start(c_longMs);
    break;
  case NotificationMessage::Duration::Persist:
    m_autoHideTimer->start(c_persistToastCapMs);
    break;
  }
}

void NotificationToast::hideToast() {
  m_autoHideTimer->stop();
  m_shownId = 0;
  hide();
}

void NotificationToast::reposition() {
  auto *parent = parentWidget();
  if (!parent) {
    return;
  }

  // Anchor to the bottom-right of the content area when one was supplied, so
  // the toast sits over the editor rather than over a dock.
  QRect area(QPoint(0, 0), parent->size());
  if (m_anchor && m_anchor->isVisible()) {
    area = QRect(m_anchor->mapTo(parent, QPoint(0, 0)), m_anchor->size());
  }

  const QSize hint = sizeHint().boundedTo(QSize(c_maxWidth, area.height()));
  resize(hint);
  move(area.right() - hint.width() - c_margin, area.bottom() - hint.height() - c_margin);
}

bool NotificationToast::eventFilter(QObject *p_watched, QEvent *p_event) {
  if (p_event->type() == QEvent::Resize || p_event->type() == QEvent::Move) {
    if (isVisible()) {
      reposition();
    }
  }
  return QFrame::eventFilter(p_watched, p_event);
}

void NotificationToast::enterEvent(
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    QEnterEvent *p_event
#else
    QEvent *p_event
#endif
) {
  // Pause auto-hide while hovered: the toast carries clickable buttons, and
  // yanking them away mid-reach would be worse than showing nothing.
  m_autoHideTimer->stop();
  QFrame::enterEvent(p_event);
}

void NotificationToast::leaveEvent(QEvent *p_event) {
  // Deliberately restarts the FULL budget rather than resuming the remainder:
  // the user has just finished reading (or reaching for a button), so giving
  // them the whole window again is the friendlier behavior and needs no
  // remaining-time bookkeeping. Documented here because "pause" would normally
  // imply resume.
  if (m_shownId != 0) {
    startAutoHideTimer(m_shownDuration);
  }
  QFrame::leaveEvent(p_event);
}

void NotificationToast::mouseReleaseEvent(QMouseEvent *p_event) {
  if (p_event->button() == Qt::LeftButton) {
    emit popupRequested();
    hideToast();
    p_event->accept();
    return;
  }
  QFrame::mouseReleaseEvent(p_event);
}
