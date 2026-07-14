#include "dashboardcontent.h"

#include <QAction>
#include <QMenu>
#include <QMessageBox>
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
  m_addAct = addAct;
  refreshAddStickerIcon();
  btn->setDefaultAction(addAct);

  auto *menu = new QMenu(p_toolBar);
  btn->setMenu(menu);
  connect(menu, &QMenu::aboutToShow, menu, [this, menu]() {
    menu->clear();
    const QStringList types = m_board->availableStickerTypes();
    for (const QString &type : types) {
      // Show a human-friendly, capitalized label (e.g. "calendar" -> "Calendar")
      // while still dispatching on the raw lower-cased type id.
      QString label = type;
      if (!label.isEmpty()) {
        label[0] = label[0].toUpper();
      }
      menu->addAction(label, this, [this, type]() { m_board->addStickerOfType(type); });
    }
  });

  p_toolBar->addWidget(btn);

  // Lock/unlock toggle beside the Add button: hides or shows the per-sticker
  // Move/Remove affordances so the layout cannot be changed by accident.
  auto *lockAct = new QAction(p_toolBar);
  lockAct->setCheckable(true);
  m_lockAct = lockAct;
  updateLockAction();
  connect(lockAct, &QAction::triggered, this,
          [this]() { m_board->setLocked(!m_board->isLocked()); });
  connect(m_board, &DashboardBoard::lockedChanged, lockAct, [this]() { updateLockAction(); });
  p_toolBar->addAction(lockAct);

  // Reset: restore the built-in default stickers/layout. Destructive, so gated
  // behind a confirmation prompt.
  auto *resetAct = new QAction(tr("Reset"), p_toolBar);
  m_resetAct = resetAct;
  resetAct->setToolTip(tr("Reset stickers and layout to defaults"));
  refreshResetIcon();
  connect(resetAct, &QAction::triggered, this, [this]() {
    const auto ret = QMessageBox::question(
        this, tr("Reset Dashboard"),
        tr("Reset all stickers and layout to the defaults? This cannot be undone."),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (ret == QMessageBox::Yes) {
      m_board->resetLayout();
    }
  });
  p_toolBar->addAction(resetAct);
}

void DashboardContent::refreshAddStickerIcon() {
  if (!m_addAct) {
    return;
  }
  if (auto *themeService = m_services.get<ThemeService>()) {
    const auto fg = themeService->paletteColor(QStringLiteral("widgets#toolbar#icon#fg"));
    const auto disabledFg =
        themeService->paletteColor(QStringLiteral("widgets#toolbar#icon#disabled#fg"));
    QVector<IconUtils::OverriddenColor> colors;
    colors.push_back(IconUtils::OverriddenColor(fg, QIcon::Normal));
    colors.push_back(IconUtils::OverriddenColor(disabledFg, QIcon::Disabled));
    m_addAct->setIcon(
        IconUtils::fetchIcon(themeService->getIconFile(QStringLiteral("add.svg")), colors));
  }
}

void DashboardContent::refreshResetIcon() {
  if (!m_resetAct) {
    return;
  }
  if (auto *themeService = m_services.get<ThemeService>()) {
    const auto fg = themeService->paletteColor(QStringLiteral("widgets#toolbar#icon#fg"));
    const auto disabledFg =
        themeService->paletteColor(QStringLiteral("widgets#toolbar#icon#disabled#fg"));
    QVector<IconUtils::OverriddenColor> colors;
    colors.push_back(IconUtils::OverriddenColor(fg, QIcon::Normal));
    colors.push_back(IconUtils::OverriddenColor(disabledFg, QIcon::Disabled));
    m_resetAct->setIcon(IconUtils::fetchIcon(
        themeService->getIconFile(QStringLiteral("reset_editor.svg")), colors));
  }
}

void DashboardContent::updateLockAction() {
  if (!m_lockAct || !m_board) {
    return;
  }
  const bool locked = m_board->isLocked();
  m_lockAct->setChecked(!locked);
  m_lockAct->setText(locked ? tr("Unlock Dashboard") : tr("Lock Dashboard"));
  m_lockAct->setToolTip(locked ? tr("Locked") : tr("Unlocked"));
  if (auto *themeService = m_services.get<ThemeService>()) {
    const auto fg = themeService->paletteColor(QStringLiteral("widgets#toolbar#icon#fg"));
    const auto disabledFg =
        themeService->paletteColor(QStringLiteral("widgets#toolbar#icon#disabled#fg"));
    QVector<IconUtils::OverriddenColor> colors;
    colors.push_back(IconUtils::OverriddenColor(fg, QIcon::Normal));
    colors.push_back(IconUtils::OverriddenColor(disabledFg, QIcon::Disabled));
    const QString iconName = locked ? QStringLiteral("lock.svg") : QStringLiteral("unlock.svg");
    m_lockAct->setIcon(IconUtils::fetchIcon(themeService->getIconFile(iconName), colors));
  }
}

bool DashboardContent::isDirty() const { return false; }

bool DashboardContent::save() { return true; }

void DashboardContent::reset() {}

bool DashboardContent::canClose(bool p_force) {
  Q_UNUSED(p_force)
  return true;
}

QWidget *DashboardContent::contentWidget() { return this; }

void DashboardContent::handleThemeChanged() {
  refreshAddStickerIcon();
  updateLockAction();
  refreshResetIcon();
}
