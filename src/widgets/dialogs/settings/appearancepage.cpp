#include "appearancepage.h"

#include <QCheckBox>
#include <QDockWidget>
#include <QFormLayout>
#include <QSpinBox>
#include <QVBoxLayout>

#include <core/configmgr.h>
#include <core/coreconfig.h>
#include <core/sessionconfig.h>
#include <core/vnotex.h>
#include <core/widgetconfig.h>
#include <utils/widgetutils.h>
#include <widgets/mainwindow.h>
#include <widgets/propertydefs.h>
#include <widgets/widgetsfactory.h>

using namespace vnotex;

AppearancePage::AppearancePage(QWidget *p_parent) : SettingsPage(p_parent) { setupUI(); }

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
    const auto &docks = VNoteX::getInst().getMainWindow()->getDocks();
    Q_ASSERT(!docks.isEmpty());
    m_keepDocksExpandingContentArea.resize(docks.size());

    auto layout = new QVBoxLayout();

    for (int i = 0; i < docks.size(); ++i) {
      m_keepDocksExpandingContentArea[i].first = WidgetsFactory::createCheckBox(
          docks[i]->property(PropertyDefs::c_dockWidgetTitle).toString(), this);
      m_keepDocksExpandingContentArea[i].second = docks[i]->objectName();
      layout->addWidget(m_keepDocksExpandingContentArea[i].first);
      connect(m_keepDocksExpandingContentArea[i].first, &QCheckBox::stateChanged, this,
              &AppearancePage::pageIsChanged);
    }

    const QString label(tr("Dock widgets kept when expanding content area:"));
    mainLayout->addRow(label, layout);
    addSearchItem(label, label, m_keepDocksExpandingContentArea.first().first);
  }
}

void AppearancePage::loadInternal() {
  const auto &sessionConfig = ConfigMgr::getInst().getSessionConfig();
  const auto &coreConfig = ConfigMgr::getInst().getCoreConfig();
  const auto &widgetConfig = ConfigMgr::getInst().getWidgetConfig();

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
  auto &sessionConfig = ConfigMgr::getInst().getSessionConfig();
  auto &coreConfig = ConfigMgr::getInst().getCoreConfig();
  auto &widgetConfig = ConfigMgr::getInst().getWidgetConfig();

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
