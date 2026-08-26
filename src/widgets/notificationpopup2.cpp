#include "notificationpopup2.h"

#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPointer>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QToolButton>
#include <QVBoxLayout>

#include <core/servicelocator.h>
#include <gui/services/themeservice.h>
#include <gui/utils/iconutils.h>
#include <gui/utils/widgetutils.h>

#include "propertydefs.h"
#include "titlebar.h"
#include "widgetsfactory.h"

using namespace vnotex;

namespace {
const QString c_fgPalette = QStringLiteral("widgets#toolbar#icon#fg");

// Cap the list so a long backlog cannot grow the menu past the screen. Without
// this the QMenu simply keeps growing; there is no implicit bound.
constexpr double c_maxHeightRatio = 0.6;

// The rows are built from word-wrapping labels, whose horizontal size hint
// collapses to nearly nothing. Without an explicit floor the QMenu adopts that
// tiny hint and truncates titles and action buttons. Expressed in average
// character widths so it follows the UI font / DPI instead of being a fixed
// pixel count.
constexpr int c_minWidthChars = 44;
} // namespace

NotificationPopup2::NotificationPopup2(ServiceLocator &p_services, QToolButton *p_btn,
                                       QWidget *p_parent)
    : ButtonPopup(p_btn, p_parent), m_services(p_services) {
  setupUI();

  connect(this, &QMenu::aboutToShow, this, [this]() { rebuild(); });

  // Keep the visible popup consistent with the service: if messages are added,
  // cleared, dismissed or removed elsewhere, rebuild so no stale rows (or their
  // callbacks) remain.
  auto *service = m_services.get<NotificationService>();
  if (service) {
    // messageAdded matters now that nothing auto-pops: an already-open popup
    // would otherwise miss a message that arrived while it was on screen.
    connect(service, &NotificationService::messageAdded, this, [this](const NotificationMessage &) {
      if (isVisible()) {
        rebuild();
      }
    });
    connect(service, &NotificationService::messagesCleared, this, [this]() {
      if (isVisible()) {
        rebuild();
      }
    });
    connect(service, &NotificationService::messageDismissed, this, [this](quint64) {
      if (isVisible()) {
        rebuild();
      }
    });
    // The message is gone from the store (renotify() replacement, or retention
    // eviction) -- its row and its buttons must go with it.
    connect(service, &NotificationService::messageRemoved, this, [this](quint64) {
      if (isVisible()) {
        rebuild();
      }
    });
    // An in-place content change (e.g. download progress) refreshes the rows
    // only when the popup is ALREADY open: an update must never re-pop the
    // popup and steal focus the way a brand-new message does.
    connect(service, &NotificationService::messageUpdated, this,
            [this](const NotificationMessage &) {
              if (isVisible()) {
                rebuild();
              }
            });
  }

  // Recolor severity icons if the theme changes while the popup is open. The
  // TitleBar refreshes its own icons independently.
  auto *themeService = m_services.get<ThemeService>();
  if (themeService) {
    connect(themeService, &ThemeService::themeChanged, this, [this]() {
      if (isVisible()) {
        rebuild();
      }
    });
  }
}

void NotificationPopup2::setupUI() {
  m_container = new QWidget(this);

  // QMenu sizes a QWidgetAction from its widget's size hint expanded to the
  // widget's minimum size, so this is what actually widens the popup.
  const int minWidth = m_container->fontMetrics().averageCharWidth() * c_minWidthChars;
  m_container->setMinimumWidth(qMin(minWidth, WidgetUtils::availableScreenSize(this).width() / 3));

  auto *mainLayout = new QVBoxLayout(m_container);
  mainLayout->setContentsMargins(0, 0, 0, 0);
  mainLayout->setSpacing(4);

  // Reuse the shared dock-panel TitleBar for a consistent look (theme QSS +
  // self-managed icon refresh). Holds the "Notifications" title and Clear All.
  m_titleBar = new TitleBar(m_services.get<ThemeService>(), tr("Notifications"), false,
                            TitleBar::Action::None, m_container);
  m_titleBar->setActionButtonsAlwaysShown(true);

  auto *clearBtn = m_titleBar->addActionButton(QStringLiteral("clear.svg"), tr("Clear All"));
  connect(clearBtn, &QToolButton::clicked, this, [this]() {
    auto *service = m_services.get<NotificationService>();
    if (service) {
      service->clearAll();
    }
    hide();
  });

  mainLayout->addWidget(m_titleBar);

  auto *bodyWidget = new QWidget(m_container);
  auto *bodyLayout = new QVBoxLayout(bodyWidget);
  bodyLayout->setContentsMargins(4, 4, 4, 4);
  bodyLayout->setSpacing(4);

  m_emptyLabel = new QLabel(tr("No notifications"), bodyWidget);
  bodyLayout->addWidget(m_emptyLabel);

  m_listLayout = new QVBoxLayout();
  m_listLayout->setContentsMargins(0, 0, 0, 0);
  m_listLayout->setSpacing(4);
  bodyLayout->addLayout(m_listLayout);
  bodyLayout->addStretch();

  auto *scroll = new QScrollArea(m_container);
  scroll->setWidgetResizable(true);
  scroll->setFrameShape(QFrame::NoFrame);
  scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  scroll->setWidget(bodyWidget);
  scroll->setMaximumHeight(qMax(
      120, static_cast<int>(WidgetUtils::availableScreenSize(this).height() * c_maxHeightRatio)));

  mainLayout->addWidget(scroll);

  addWidget(m_container);
}

const char *NotificationPopup2::severityState(NotificationMessage::Severity p_severity) {
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

QIcon NotificationPopup2::severityIcon(NotificationMessage::Severity p_severity) const {
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

  // Per-severity tint via the shared semantic roles, falling back to the
  // uniform toolbar icon color for a theme that lacks one.
  const QString token = QStringLiteral("base#%1#fg").arg(QLatin1String(severityState(p_severity)));
  QString fg = themeService->paletteColor(token);
  if (fg.isEmpty()) {
    fg = themeService->paletteColor(c_fgPalette);
  }
  return IconUtils::fetchIcon(themeService->getIconFile(iconName), fg);
}

void NotificationPopup2::rebuild() {
  // Remove all existing rows to avoid dangling callback references.
  QLayoutItem *item = nullptr;
  while ((item = m_listLayout->takeAt(0)) != nullptr) {
    if (auto *w = item->widget()) {
      w->deleteLater();
    }
    delete item;
  }

  auto *service = m_services.get<NotificationService>();
  if (!service) {
    m_emptyLabel->setVisible(true);
    return;
  }

  const auto &messages = service->messages();

  int shown = 0;
  // Newest first.
  for (int i = messages.size() - 1; i >= 0; --i) {
    const auto &msg = messages[i];
    if (msg.m_dismissed) {
      continue;
    }
    ++shown;

    auto *row = new QFrame(m_container);
    row->setFrameShape(QFrame::StyledPanel);
    // Per-severity accent, driven by the shared *[State=...] QSS rules.
    WidgetUtils::setPropertyDynamically(row, PropertyDefs::c_state,
                                        QLatin1String(severityState(msg.m_severity)));
    auto *rowLayout = new QVBoxLayout(row);
    rowLayout->setContentsMargins(6, 6, 6, 6);
    rowLayout->setSpacing(2);

    // Header: severity icon + title.
    auto *headerLayout = new QHBoxLayout();
    headerLayout->setContentsMargins(0, 0, 0, 0);
    headerLayout->setSpacing(4);

    auto *iconLabel = new QLabel(row);
    iconLabel->setPixmap(severityIcon(msg.m_severity).pixmap(16, 16));
    headerLayout->addWidget(iconLabel);

    if (!msg.m_title.isEmpty()) {
      auto *titleLabel = new QLabel(msg.m_title, row);
      QFont font = titleLabel->font();
      font.setBold(true);
      titleLabel->setFont(font);
      headerLayout->addWidget(titleLabel);
    }
    headerLayout->addStretch();
    rowLayout->addLayout(headerLayout);

    if (!msg.m_text.isEmpty()) {
      auto *textLabel = new QLabel(msg.m_text, row);
      textLabel->setWordWrap(true);
      rowLayout->addWidget(textLabel);
    }

    // Long-form detail, collapsed by default. This is the home for the error
    // blobs that used to be a QMessageBox detailedText (or were lost to
    // qWarning). Deliberately plain text: producers must not have to escape.
    if (!msg.m_details.isEmpty()) {
      auto *detailsLabel = new QLabel(msg.m_details, row);
      detailsLabel->setWordWrap(true);
      detailsLabel->setTextFormat(Qt::PlainText);
      detailsLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
      detailsLabel->hide();

      auto *toggle = new QPushButton(tr("Details"), row);
      toggle->setCheckable(true);
      toggle->setFlat(true);
      connect(toggle, &QPushButton::toggled, detailsLabel, &QLabel::setVisible);

      auto *toggleLayout = new QHBoxLayout();
      toggleLayout->setContentsMargins(0, 0, 0, 0);
      toggleLayout->addWidget(toggle);
      toggleLayout->addStretch();
      rowLayout->addLayout(toggleLayout);
      rowLayout->addWidget(detailsLabel);
    }

    if (msg.m_progressIndeterminate || msg.m_progressPermille >= 0) {
      auto *progress = new QProgressBar(row);
      if (msg.m_progressIndeterminate) {
        // Busy indicator: wins over any permille value.
        progress->setRange(0, 0);
      } else {
        // Permille rather than percent so a multi-hundred-megabyte download
        // still moves the bar smoothly, matching UpdateDialog.
        progress->setRange(0, 1000);
        progress->setValue(qBound(0, msg.m_progressPermille, 1000));
      }
      progress->setTextVisible(false);
      rowLayout->addWidget(progress);
    }

    // Actions + Dismiss.
    auto *actionLayout = new QHBoxLayout();
    actionLayout->setContentsMargins(0, 0, 0, 0);
    actionLayout->setSpacing(4);
    actionLayout->addStretch();

    const quint64 id = msg.m_id;
    for (int ai = 0; ai < msg.m_actions.size(); ++ai) {
      auto *actBtn = new QPushButton(msg.m_actions.at(ai).m_label, row);
      const int actionIndex = ai;
      connect(actBtn, &QPushButton::clicked, this, [this, id, actionIndex]() {
        auto *service = m_services.get<NotificationService>();
        if (!service) {
          return;
        }
        // Resolve the callback from the CURRENT service state at click time, so
        // a message cleared/dismissed since render becomes an inert no-op.
        //
        // BOTH the callback and its dismiss policy are snapshotted here, in the
        // SAME lookup, and the action index is never resolved again afterwards:
        // an Update/Retry callback synchronously replaces the action vector
        // (with Cancel / Restart), so a post-callback re-lookup would read a
        // different action's flag.
        std::function<void()> callback;
        bool dismissOnTrigger = true;
        for (const auto &m : service->messages()) {
          if (m.m_id == id && !m.m_dismissed && actionIndex < m.m_actions.size()) {
            callback = m.m_actions.at(actionIndex).m_callback;
            dismissOnTrigger = m.m_actions.at(actionIndex).m_dismissOnTrigger;
            break;
          }
        }

        // The callback may synchronously destroy this popup (e.g. restart the
        // main window), so guard every post-callback access to this / m_services.
        QPointer<NotificationPopup2> guard(this);
        if (callback) {
          callback();
        }
        if (!guard) {
          return;
        }
        auto *svc = m_services.get<NotificationService>();
        if (!dismissOnTrigger) {
          // The message lives on so the producer can keep updating it in place.
          // messageUpdated may already have rebuilt the rows during the
          // callback, which is exactly why nothing below may touch the widgets
          // that were alive when this lambda started.
          rebuild();
          return;
        }
        if (svc) {
          svc->dismiss(id);
        }
        if (!guard) {
          return;
        }
        hide();
      });
      actionLayout->addWidget(actBtn);
    }

    auto *dismissBtn = new QPushButton(tr("Dismiss"), row);
    connect(dismissBtn, &QPushButton::clicked, this, [this, id]() {
      auto *service = m_services.get<NotificationService>();
      if (service) {
        service->dismiss(id);
      }
      rebuild();
    });
    actionLayout->addWidget(dismissBtn);

    rowLayout->addLayout(actionLayout);

    m_listLayout->addWidget(row);
  }

  m_emptyLabel->setVisible(shown == 0);
}
