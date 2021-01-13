#include "appearancepage.h"

#include <QCheckBox>
#include <QFormLayout>
#include <QSpinBox>

#include <widgets/widgetsfactory.h>
#include <core/sessionconfig.h>
#include <core/coreconfig.h>
#include <core/configmgr.h>
#include <utils/widgetutils.h>

using namespace vnotex;

AppearancePage::AppearancePage(QWidget *p_parent)
    : SettingsPage(p_parent)
{
    setupUI();
}

void AppearancePage::setupUI()
{
    auto mainLayout = WidgetsFactory::createFormLayout(this);

    {
        const QString label(tr("System title bar"));
        m_systemTitleBarCheckBox = WidgetsFactory::createCheckBox(label, this);
        m_systemTitleBarCheckBox->setToolTip(tr("Use system title bar"));
        mainLayout->addRow(m_systemTitleBarCheckBox);
        addSearchItem(label, m_systemTitleBarCheckBox->toolTip(), m_systemTitleBarCheckBox);
        connect(m_systemTitleBarCheckBox, &QCheckBox::stateChanged,
                this, &AppearancePage::pageIsChanged);
    }

    {
        m_toolBarIconSizeSpinBox = WidgetsFactory::createSpinBox(this);
        m_toolBarIconSizeSpinBox->setToolTip(tr("Icon size of the main window tool bar"));

        m_toolBarIconSizeSpinBox->setRange(1, 48);
        m_toolBarIconSizeSpinBox->setSingleStep(1);

        const QString label(tr("Main tool bar icon size:"));
        mainLayout->addRow(label, m_toolBarIconSizeSpinBox);
        addSearchItem(label, m_toolBarIconSizeSpinBox->toolTip(), m_toolBarIconSizeSpinBox);
        connect(m_toolBarIconSizeSpinBox, QOverload<int>::of(&QSpinBox::valueChanged),
                this, &AppearancePage::pageIsChanged);
    }
}

void AppearancePage::loadInternal()
{
    const auto &sessionConfig = ConfigMgr::getInst().getSessionConfig();
    const auto &coreConfig = ConfigMgr::getInst().getCoreConfig();

    if (m_systemTitleBarCheckBox) {
        m_systemTitleBarCheckBox->setChecked(sessionConfig.getSystemTitleBarEnabled());
    }

    m_toolBarIconSizeSpinBox->setValue(coreConfig.getToolBarIconSize());
}

void AppearancePage::saveInternal()
{
    auto &sessionConfig = ConfigMgr::getInst().getSessionConfig();
    auto &coreConfig = ConfigMgr::getInst().getCoreConfig();

    if (m_systemTitleBarCheckBox) {
        sessionConfig.setSystemTitleBarEnabled(m_systemTitleBarCheckBox->isChecked());
    }

    coreConfig.setToolBarIconSize(m_toolBarIconSizeSpinBox->value());
}

QString AppearancePage::title() const
{
    return tr("Appearance");
}
