#include "dashboardcontent.h"

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

  auto *addAct = p_toolBar->addAction(tr("Add Sticker"));
  auto *menu = new QMenu(p_toolBar);
  connect(menu, &QMenu::aboutToShow, menu, [this, menu]() {
    menu->clear();
    const QStringList types = m_board->availableStickerTypes();
    for (const QString &type : types) {
      menu->addAction(type, this, [this, type]() { m_board->addStickerOfType(type); });
    }
  });
  addAct->setMenu(menu);

  // Ensure the action shows its menu when triggered from the toolbar button.
  for (QWidget *w : addAct->associatedWidgets()) {
    if (auto *btn = qobject_cast<QToolButton *>(w)) {
      btn->setPopupMode(QToolButton::InstantPopup);
    }
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
