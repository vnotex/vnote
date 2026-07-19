#include "notificationbutton2.h"

#include <QAction>
#include <QBrush>
#include <QColor>
#include <QFont>
#include <QPaintEvent>
#include <QPainter>
#include <QRectF>

#include <core/servicelocator.h>
#include <core/services/notificationservice.h>
#include <gui/services/themeservice.h>
#include <gui/utils/iconutils.h>

#include "notificationpopup2.h"

using namespace vnotex;

namespace {
const QString c_fgPalette = QStringLiteral("widgets#toolbar#icon#fg");
const QString c_disabledPalette = QStringLiteral("widgets#toolbar#icon#disabled#fg");
} // namespace

NotificationButton2::NotificationButton2(ServiceLocator &p_services, const QSize &p_iconSize,
                                         QWidget *p_parent)
    : QToolButton(p_parent), m_services(p_services) {
  setIconSize(p_iconSize);
  setPopupMode(QToolButton::InstantPopup);
  setToolTip(tr("Notifications"));

  // Hide the dropdown arrow indicator.
  setStyleSheet(QStringLiteral("QToolButton::menu-indicator { image: none; }"));

  auto *act = new QAction(tr("Notifications"), this);
  setDefaultAction(act);
  refreshIcon();

  m_popup = new NotificationPopup2(m_services, this, this);
  setMenu(m_popup);

  auto *service = m_services.get<NotificationService>();
  if (service) {
    m_activeCount = service->activeCount();
    connect(service, &NotificationService::messageAdded, this,
            [this](const NotificationMessage &p_msg) {
              updateBadge();
              if (m_popup) {
                m_popup->showTransient(p_msg);
              }
            });
    connect(service, &NotificationService::messageDismissed, this,
            [this](quint64) { updateBadge(); });
    connect(service, &NotificationService::messagesCleared, this, [this]() { updateBadge(); });
  }

  auto *themeService = m_services.get<ThemeService>();
  if (themeService) {
    connect(themeService, &ThemeService::themeChanged, this, [this]() { refreshIcon(); });
  }
}

void NotificationButton2::refreshIcon() {
  auto *themeService = m_services.get<ThemeService>();
  if (!themeService) {
    return;
  }
  const auto fg = themeService->paletteColor(c_fgPalette);
  const auto disabledFg = themeService->paletteColor(c_disabledPalette);

  QVector<IconUtils::OverriddenColor> colors;
  colors.push_back(IconUtils::OverriddenColor(fg, QIcon::Normal));
  colors.push_back(IconUtils::OverriddenColor(disabledFg, QIcon::Disabled));

  const auto iconFile = themeService->getIconFile(QStringLiteral("notification.svg"));
  auto *act = defaultAction();
  if (act) {
    act->setIcon(IconUtils::fetchIcon(iconFile, colors));
  } else {
    setIcon(IconUtils::fetchIcon(iconFile, colors));
  }
}

void NotificationButton2::updateBadge() {
  auto *service = m_services.get<NotificationService>();
  m_activeCount = service ? service->activeCount() : 0;
  update();
}

void NotificationButton2::paintEvent(QPaintEvent *p_event) {
  QToolButton::paintEvent(p_event);

  if (m_activeCount <= 0) {
    return;
  }

  QPainter painter(this);
  painter.setRenderHint(QPainter::Antialiasing, true);

  const QString text = m_activeCount > 99 ? QStringLiteral("99+")
                                          : QString::number(m_activeCount);

  QFont font = painter.font();
  font.setPointSizeF(qMax(6.0, font.pointSizeF() - 2.0));
  font.setBold(true);
  painter.setFont(font);

  const QFontMetrics fm(font);
  const int textWidth = fm.horizontalAdvance(text);
  const int diameter = qMax(fm.height(), textWidth + 6);
  const qreal badgeSize = qMin(qreal(diameter), height() * 0.6);

  const QRectF badgeRect(width() - badgeSize - 1, 1, badgeSize, badgeSize);

  painter.setPen(Qt::NoPen);
  painter.setBrush(QBrush(QColor(0xE5, 0x39, 0x35))); // Red badge.
  painter.drawEllipse(badgeRect);

  painter.setPen(QColor(Qt::white));
  painter.drawText(badgeRect, Qt::AlignCenter, text);
}
