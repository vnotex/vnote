#include "appearancepage.h"

#include <QCheckBox>
#include <QDockWidget>
#include <QFormLayout>
#include <QSpinBox>
#include <QVBoxLayout>

#include <core/configmgr2.h>
#include <core/coreconfig.h>
#include <core/servicelocator.h>
#include <core/sessionconfig.h>
#include <core/widgetconfig.h>
#include <utils/widgetutils.h>
#include <widgets/mainwindow2.h>
#include <widgets/propertydefs.h>
#include <widgets/widgetsfactory.h>

using namespace vnotex;

AppearancePage::AppearancePage(ServiceLocator &p_services, MainWindow2 *p_mainWindow,
                               QWidget *p_parent)
    : SettingsPage(p_services, p_parent), m_mainWindow(p_mainWindow) {
  setupUI();
}

void AppearancePage::setupUI() {
  auto mainLayout = WidgetsFactory::createFormLayout(this);

  {
    const QString label(tr("System title bar"));
    m_systemTitleBarCheckBox = WidgetsFactory::createCheckBox(label, this);
    m_systemTitleBarCheckBox->setToolTip(tr("Use system title bar"));
    mainLayout->addRow(m_systemTitleBarCheckBox);
    addSearchItem(label, m_systemTitleBarCheckBox->toolTip(), m_systemTitleBarCheckBox);
    connect(m_systemTitleBarCheckBox, &QCheckBox::stateChanged, this,
            &AppearancePage::pageIsChangedWithRestartNeeded);
  }

  {
    m_toolBarIconSizeSpinBox = WidgetsFactory::createSpinBox(this);
    m_toolBarIconSizeSpinBox->setToolTip(tr("Icon size of the main window tool bar"));

    m_toolBarIconSizeSpinBox->setRange(1, 48);
    m_toolBarIconSizeSpinBox->setSingleStep(1);

    const QString label(tr("Main tool bar icon size:"));
    mainLayout->addRow(label, m_toolBarIconSizeSpinBox);
    addSearchItem(label, m_toolBarIconSizeSpinBox->toolTip(), m_toolBarIconSizeSpinBox);
    connect(m_toolBarIconSizeSpinBox, QOverload<int>::of(&QSpinBox::valueChanged), this,
            &AppearancePage::pageIsChangedWithRestartNeeded);
  }

  {
    Q_ASSERT(m_mainWindow);
    const auto &docks = m_mainWindow->getDocks();

    auto layout = new QVBoxLayout();

    for (int i = 0; i < docks.size(); ++i) {
      if (!docks[i]) {
        continue;
      }
      auto cb = WidgetsFactory::createCheckBox(
          docks[i]->property(PropertyDefs::c_dockWidgetTitle).toString(), this);
      m_keepDocksExpandingContentArea.append(qMakePair(cb, docks[i]->objectName()));
      layout->addWidget(cb);
      connect(cb, &QCheckBox::stateChanged, this, &AppearancePage::pageIsChanged);
    }

    const QString label(tr("Dock widgets kept when expanding content area:"));
    mainLayout->addRow(label, layout);
    if (!m_keepDocksExpandingContentArea.isEmpty()) {
      addSearchItem(label, label, m_keepDocksExpandingContentArea.first().first);
    }
  }
}

void AppearancePage::loadInternal() {
  auto *configMgr = m_services.get<ConfigMgr2>();
  const auto &sessionConfig = configMgr->getSessionConfig();
  const auto &coreConfig = configMgr->getCoreConfig();
  const auto &widgetConfig = configMgr->getWidgetConfig();

  if (m_systemTitleBarCheckBox) {
    m_systemTitleBarCheckBox->setChecked(sessionConfig.getSystemTitleBarEnabled());
  }

  m_toolBarIconSizeSpinBox->setValue(coreConfig.getToolBarIconSize());

  {
    const auto &docks = widgetConfig.getMainWindowKeepDocksExpandingContentArea();
    for (const auto &cb : m_keepDocksExpandingContentArea) {
      if (docks.contains(cb.second)) {
        cb.first->setChecked(true);
      }
    }
  }
}

bool AppearancePage::saveInternal() {
  auto *configMgr = m_services.get<ConfigMgr2>();
  auto &sessionConfig = configMgr->getSessionConfig();
  auto &coreConfig = configMgr->getCoreConfig();
  auto &widgetConfig = configMgr->getWidgetConfig();

  if (m_systemTitleBarCheckBox) {
    sessionConfig.setSystemTitleBarEnabled(m_systemTitleBarCheckBox->isChecked());
  }

  coreConfig.setToolBarIconSize(m_toolBarIconSizeSpinBox->value());

  {
    QStringList docks;
    for (const auto &cb : m_keepDocksExpandingContentArea) {
      if (cb.first->isChecked()) {
        docks << cb.second;
      }
    }
    widgetConfig.setMainWindowKeepDocksExpandingContentArea(docks);
  }

  return true;
}

QString AppearancePage::title() const { return tr("Appearance"); }
