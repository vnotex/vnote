#include "notificationpopup2.h"

#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPointer>
#include <QPushButton>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include <core/servicelocator.h>
#include <gui/services/themeservice.h>
#include <gui/utils/iconutils.h>

#include "titlebar.h"
#include "widgetsfactory.h"

using namespace vnotex;

namespace {
const QString c_fgPalette = QStringLiteral("widgets#toolbar#icon#fg");
}

NotificationPopup2::NotificationPopup2(ServiceLocator &p_services, QToolButton *p_btn,
                                       QWidget *p_parent)
    : ButtonPopup(p_btn, p_parent), m_services(p_services) {
  setupUI();

  m_autoHideTimer = new QTimer(this);
  m_autoHideTimer->setSingleShot(true);
  connect(m_autoHideTimer, &QTimer::timeout, this, [this]() { hide(); });

  connect(this, &QMenu::aboutToShow, this, [this]() {
    // Manual open: no auto-hide.
    m_autoHideTimer->stop();
    rebuild();
  });

  // Keep the visible popup consistent with the service: if messages are cleared
  // or dismissed elsewhere, rebuild so no stale rows (or their callbacks) remain.
  auto *service = m_services.get<NotificationService>();
  if (service) {
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

  auto *bodyLayout = new QVBoxLayout();
  bodyLayout->setContentsMargins(4, 4, 4, 4);
  bodyLayout->setSpacing(4);

  m_emptyLabel = new QLabel(tr("No notifications"), m_container);
  bodyLayout->addWidget(m_emptyLabel);

  m_listLayout = new QVBoxLayout();
  m_listLayout->setContentsMargins(0, 0, 0, 0);
  m_listLayout->setSpacing(4);
  bodyLayout->addLayout(m_listLayout);

  mainLayout->addLayout(bodyLayout);

  addWidget(m_container);
}

QIcon NotificationPopup2::severityIcon(NotificationMessage::Severity p_severity) const {
  QString iconName;
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
  default:
    iconName = QStringLiteral("info.svg");
    break;
  }

  auto *themeService = m_services.get<ThemeService>();
  if (!themeService) {
    return QIcon();
  }
  const auto fg = themeService->paletteColor(c_fgPalette);
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
        std::function<void()> callback;
        for (const auto &m : service->messages()) {
          if (m.m_id == id && !m.m_dismissed && actionIndex < m.m_actions.size()) {
            callback = m.m_actions.at(actionIndex).m_callback;
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

void NotificationPopup2::showTransient(const NotificationMessage &p_msg) {
  rebuild();

  if (m_button) {
    // Pop up just below the button.
    const QPoint pos = m_button->mapToGlobal(QPoint(0, m_button->height()));
    popup(pos);
  } else {
    show();
  }

  m_autoHideTimer->stop();
  switch (p_msg.m_duration) {
  case NotificationMessage::Duration::Short:
    m_autoHideTimer->start(3000);
    break;
  case NotificationMessage::Duration::Long:
    m_autoHideTimer->start(7000);
    break;
  case NotificationMessage::Duration::Persist:
    // No auto-hide.
    break;
  }
}
