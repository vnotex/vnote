#include "generalpage.h"

#include <QComboBox>
#include <QFormLayout>
#include <QCheckBox>

#include <widgets/widgetsfactory.h>
#include <core/coreconfig.h>
#include <core/sessionconfig.h>
#include <core/configmgr.h>
#include <utils/widgetutils.h>

using namespace vnotex;

GeneralPage::GeneralPage(QWidget *p_parent)
    : SettingsPage(p_parent)
{
    setupUI();
}

void GeneralPage::setupUI()
{
    auto mainLayout = WidgetsFactory::createFormLayout(this);

    {
        m_localeComboBox = WidgetsFactory::createComboBox(this);
        m_localeComboBox->setToolTip(tr("Interface language"));

        m_localeComboBox->addItem(tr("Default"), QString());
        for (const auto &loc : ConfigMgr::getInst().getCoreConfig().getAvailableLocales()) {
            QLocale locale(loc);
            m_localeComboBox->addItem(QStringLiteral("%1 (%2)").arg(locale.nativeLanguageName(), locale.nativeCountryName()),
                                      loc);
        }

        const QString label(tr("Language:"));
        mainLayout->addRow(label, m_localeComboBox);
        addSearchItem(label, m_localeComboBox->toolTip(), m_localeComboBox);
        connect(m_localeComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged),
                this, &GeneralPage::pageIsChangedWithRestartNeeded);
    }

#if defined(Q_OS_WIN)
    {
        m_openGLComboBox = WidgetsFactory::createComboBox(this);
        m_openGLComboBox->setToolTip(tr("OpenGL implementation used to render application"));

        m_openGLComboBox->addItem(tr("None"), SessionConfig::OpenGL::None);
        m_openGLComboBox->addItem(tr("Desktop"), SessionConfig::OpenGL::Desktop);
        m_openGLComboBox->addItem(tr("OpenGL ES"), SessionConfig::OpenGL::Angle);
        m_openGLComboBox->addItem(tr("Software"), SessionConfig::OpenGL::Software);

        const QString label(tr("OpenGL:"));
        mainLayout->addRow(label, m_openGLComboBox);
        addSearchItem(label, m_openGLComboBox->toolTip(), m_openGLComboBox);
        connect(m_openGLComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged),
                this, &GeneralPage::pageIsChangedWithRestartNeeded);
    }
#endif

#if !defined(Q_OS_MACOS)
    {
        const QString label(tr("Minimize to system tray"));
        m_systemTrayCheckBox = WidgetsFactory::createCheckBox(label, this);
        m_systemTrayCheckBox->setToolTip(tr("Minimize to system tray when closed"));
        mainLayout->addRow(m_systemTrayCheckBox);
        addSearchItem(label, m_systemTrayCheckBox->toolTip(), m_systemTrayCheckBox);
        connect(m_systemTrayCheckBox, &QCheckBox::stateChanged,
                this, &GeneralPage::pageIsChanged);
    }
#endif

    {
        const QString label(tr("Recover last session on start"));
        m_recoverLastSessionCheckBox = WidgetsFactory::createCheckBox(label, this);
        m_recoverLastSessionCheckBox->setToolTip(tr("Recover last session (like buffers) on start of VNote"));
        mainLayout->addRow(m_recoverLastSessionCheckBox);
        addSearchItem(label, m_recoverLastSessionCheckBox->toolTip(), m_recoverLastSessionCheckBox);
        connect(m_recoverLastSessionCheckBox, &QCheckBox::stateChanged,
                this, &GeneralPage::pageIsChanged);
    }

    {
        const QString label(tr("Check for updates on start"));
        m_checkForUpdatesCheckBox = WidgetsFactory::createCheckBox(label, this);
        m_checkForUpdatesCheckBox->setToolTip(tr("Check for updates on start of VNote"));
        mainLayout->addRow(m_checkForUpdatesCheckBox);
        addSearchItem(label, m_checkForUpdatesCheckBox->toolTip(), m_checkForUpdatesCheckBox);
        connect(m_checkForUpdatesCheckBox, &QCheckBox::stateChanged,
                this, &GeneralPage::pageIsChanged);
    }
}

void GeneralPage::loadInternal()
{
    const auto &coreConfig = ConfigMgr::getInst().getCoreConfig();
    const auto &sessionConfig = ConfigMgr::getInst().getSessionConfig();

    {
        int idx = m_localeComboBox->findData(coreConfig.getLocale());
        Q_ASSERT(idx != -1);
        m_localeComboBox->setCurrentIndex(idx);
    }

    if (m_openGLComboBox) {
        int idx = m_openGLComboBox->findData(sessionConfig.getOpenGL());
        Q_ASSERT(idx != -1);
        m_openGLComboBox->setCurrentIndex(idx);
    }

    if (m_systemTrayCheckBox) {
        int toTray = sessionConfig.getMinimizeToSystemTray();
        m_systemTrayCheckBox->setChecked(toTray > 0);
    }

    m_recoverLastSessionCheckBox->setChecked(coreConfig.isRecoverLastSessionOnStartEnabled());

    m_checkForUpdatesCheckBox->setChecked(coreConfig.isCheckForUpdatesOnStartEnabled());
}

bool GeneralPage::saveInternal()
{
    auto &coreConfig = ConfigMgr::getInst().getCoreConfig();
    auto &sessionConfig = ConfigMgr::getInst().getSessionConfig();

    {
        auto locale = m_localeComboBox->currentData().toString();
        coreConfig.setLocale(locale);
    }

    if (m_openGLComboBox) {
        int opt = m_openGLComboBox->currentData().toInt();
        sessionConfig.setOpenGL(static_cast<SessionConfig::OpenGL>(opt));
    }

    if (m_systemTrayCheckBox) {
        // This will override the -1 state. That is fine.
        sessionConfig.setMinimizeToSystemTray(m_systemTrayCheckBox->isChecked());
    }

    coreConfig.setRecoverLastSessionOnStartEnabled(m_recoverLastSessionCheckBox->isChecked());

    coreConfig.setCheckForUpdatesOnStartEnabled(m_checkForUpdatesCheckBox->isChecked());

    return true;
}

QString GeneralPage::title() const
{
    return tr("General");
}
