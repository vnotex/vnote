#include "outlineviewer.h"

#include <QShowEvent>
#include <QToolButton>
#include <QToolTip>
#include <QVBoxLayout>

#include <controllers/outlinecontroller.h>
#include <core/servicelocator.h>
#include <gui/services/themeservice.h>
#include <models/outlinemodel.h>
#include <utils/widgetutils.h>
#include <views/outlineview.h>

#include <widgets/navigationmodeviewwrapper.h>

#include "titlebar.h"

using namespace vnotex;

OutlineViewer::OutlineViewer(ServiceLocator &p_services, const QString &p_title,
                             QWidget *p_parent)
    : QFrame(p_parent), m_services(p_services) {
  setupUI(p_title);
}

void OutlineViewer::setupUI(const QString &p_title) {
  auto mainLayout = new QVBoxLayout(this);
  WidgetUtils::setContentsMargins(mainLayout);

  // Controller and view must be created before setupTitleBar(),
  // because setupTitleBar() reads m_controller->isSectionNumberEnabled().
  m_outlineView = new OutlineView(this);
  m_controller = new OutlineController(m_services, this);
  m_outlineView->setModel(m_controller->model());
  m_controller->setView(m_outlineView);

  {
    auto titleBar = setupTitleBar(p_title, this);
    if (titleBar) {
      mainLayout->addWidget(titleBar);
    }
  }

  mainLayout->addWidget(m_outlineView);

  setFocusProxy(m_outlineView);

  // Navigation wrapper for keyboard navigation mode.
  auto *themeService = m_services.get<ThemeService>();
  m_outlineNavWrapper.reset(
      new NavigationModeViewWrapper<OutlineView>(m_outlineView, themeService));

  connect(m_controller, &OutlineController::focusViewAreaRequested,
          this, &OutlineViewer::focusViewArea);
}

TitleBar *OutlineViewer::setupTitleBar(const QString &p_title, QWidget *p_parent) {
  auto *themeService = m_services.get<ThemeService>();
  auto titleBar =
      new TitleBar(themeService, p_title, false, TitleBar::Action::Menu, p_parent);
  titleBar->setActionButtonsAlwaysShown(true);

  auto decreaseBtn = titleBar->addActionButton(
      QStringLiteral("decrease_outline_level.svg"), tr("Decrease Expansion Level"));
  connect(decreaseBtn, &QToolButton::clicked, this, [this]() {
    m_controller->decreaseExpandLevel();
    showLevel();
  });

  auto increaseBtn = titleBar->addActionButton(
      QStringLiteral("increase_outline_level.svg"), tr("Increase Expansion Level"));
  connect(increaseBtn, &QToolButton::clicked, this, [this]() {
    m_controller->increaseExpandLevel();
    showLevel();
  });

  {
    auto act = titleBar->addMenuAction(
        tr("Section Number"), titleBar, [this](bool p_checked) {
          Q_UNUSED(p_checked);
          m_controller->toggleSectionNumber();
        });
    act->setCheckable(true);
    act->setChecked(m_controller->isSectionNumberEnabled());
  }

  connect(m_controller, &OutlineController::expandLevelChanged,
          this, &OutlineViewer::showLevel);

  return titleBar;
}

void OutlineViewer::setOutlineProvider(
    const QSharedPointer<OutlineProvider> &p_provider) {
  m_controller->setOutlineProvider(p_provider);
}

void OutlineViewer::showEvent(QShowEvent *p_event) {
  QFrame::showEvent(p_event);
}

void OutlineViewer::showLevel() {
  QToolTip::showText(mapToGlobal(QPoint(0, 0)),
                     tr("Expansion level: %1").arg(m_controller->getExpandLevel()),
                     this);
}

NavigationMode *OutlineViewer::getOutlineNavigationWrapper() const {
  return m_outlineNavWrapper.data();
}
