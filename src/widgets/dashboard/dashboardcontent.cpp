#include "dashboardcontent.h"

#include <QAction>
#include <QMenu>
#include <QToolBar>
#include <QToolButton>
#include <QVBoxLayout>

#include <core/servicelocator.h>
#include <gui/services/themeservice.h>
#include <gui/utils/iconutils.h>

#include "dashboardboard.h"

using namespace vnotex;

DashboardContent::DashboardContent(ServiceLocator &p_services, QWidget *p_parent)
    : QWidget(p_parent), m_services(p_services) {
  auto *layout = new QVBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);

  m_board = new DashboardBoard(m_services, this);
  layout->addWidget(m_board);

  // The board persists itself; surface changes to the host tab.
  connect(m_board, &DashboardBoard::contentChanged, this, &DashboardContent::contentChanged);
}

QString DashboardContent::title() const { return tr("Home"); }

QIcon DashboardContent::icon() const {
  auto *themeService = m_services.get<ThemeService>();
  if (!themeService) {
    return QIcon();
  }
  const QString fg =
      themeService->paletteColor(QStringLiteral("widgets#viewsplit#action_button#fg"));
  return IconUtils::fetchIcon(themeService->getIconFile(QStringLiteral("home_dashboard.svg")),
                              fg);
}

QString DashboardContent::virtualAddress() const { return QStringLiteral("vx://home"); }

void DashboardContent::setupToolBar(QToolBar *p_toolBar) {
  if (!p_toolBar || !m_board) {
    return;
  }

  auto *btn = new QToolButton(p_toolBar);
  btn->setPopupMode(QToolButton::InstantPopup);

  // Hide the dropdown arrow indicator, matching the main window Theme button.
  btn->setStyleSheet(QStringLiteral("QToolButton::menu-indicator { image: none; }"));

  auto *addAct = new QAction(tr("Add Sticker"), btn);
  if (auto *themeService = m_services.get<ThemeService>()) {
    const auto fg = themeService->paletteColor(QStringLiteral("widgets#toolbar#icon#fg"));
    const auto disabledFg =
        themeService->paletteColor(QStringLiteral("widgets#toolbar#icon#disabled#fg"));
    QVector<IconUtils::OverriddenColor> colors;
    colors.push_back(IconUtils::OverriddenColor(fg, QIcon::Normal));
    colors.push_back(IconUtils::OverriddenColor(disabledFg, QIcon::Disabled));
    addAct->setIcon(
        IconUtils::fetchIcon(themeService->getIconFile(QStringLiteral("add.svg")), colors));
  }
  btn->setDefaultAction(addAct);

  auto *menu = new QMenu(p_toolBar);
  btn->setMenu(menu);
  connect(menu, &QMenu::aboutToShow, menu, [this, menu]() {
    menu->clear();
    const QStringList types = m_board->availableStickerTypes();
    for (const QString &type : types) {
      menu->addAction(type, this, [this, type]() { m_board->addStickerOfType(type); });
    }
  });

  p_toolBar->addWidget(btn);

  // Lock/unlock toggle beside the Add button: hides or shows the per-sticker
  // Move/Remove affordances so the layout cannot be changed by accident.
  auto *lockAct = new QAction(p_toolBar);
  lockAct->setCheckable(true);
  auto updateLockAct = [this, lockAct]() {
    const bool locked = m_board->isLocked();
    lockAct->setChecked(!locked);
    lockAct->setText(locked ? tr("Unlock Dashboard") : tr("Lock Dashboard"));
    lockAct->setToolTip(locked ? tr("Locked") : tr("Unlocked"));
    if (auto *themeService = m_services.get<ThemeService>()) {
      const auto fg = themeService->paletteColor(QStringLiteral("widgets#toolbar#icon#fg"));
      const auto disabledFg =
          themeService->paletteColor(QStringLiteral("widgets#toolbar#icon#disabled#fg"));
      QVector<IconUtils::OverriddenColor> colors;
      colors.push_back(IconUtils::OverriddenColor(fg, QIcon::Normal));
      colors.push_back(IconUtils::OverriddenColor(disabledFg, QIcon::Disabled));
      const QString iconName =
          locked ? QStringLiteral("lock.svg") : QStringLiteral("unlock.svg");
      lockAct->setIcon(IconUtils::fetchIcon(themeService->getIconFile(iconName), colors));
    }
  };
  updateLockAct();
  connect(lockAct, &QAction::triggered, this,
          [this]() { m_board->setLocked(!m_board->isLocked()); });
  connect(m_board, &DashboardBoard::lockedChanged, lockAct,
          [updateLockAct]() { updateLockAct(); });
  p_toolBar->addAction(lockAct);
}

bool DashboardContent::isDirty() const { return false; }

bool DashboardContent::save() { return true; }

void DashboardContent::reset() {}

bool DashboardContent::canClose(bool p_force) {
  Q_UNUSED(p_force)
  return true;
}

QWidget *DashboardContent::contentWidget() { return this; }
