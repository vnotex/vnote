#include "generalpage.h"

#include <QCheckBox>
#include <QComboBox>
#include <QVBoxLayout>

#include <core/configmgr2.h>
#include <core/coreconfig.h>
#include "settingspagehelper.h"
#include <core/servicelocator.h>
#include <core/services/configcoreservice.h>
#include <core/sessionconfig.h>
#include <utils/widgetutils.h>
#include <widgets/widgetsfactory.h>

using namespace vnotex;

GeneralPage::GeneralPage(ServiceLocator &p_services, QWidget *p_parent)
    : SettingsPage(p_services, p_parent) {
  setupUI();
}

void GeneralPage::setupUI() {
  auto *mainLayout = new QVBoxLayout(this);

  auto *cardLayout = SettingsPageHelper::addSection(mainLayout, tr("General"), QString(), this);

  {
    m_localeComboBox = WidgetsFactory::createComboBox(this);
    m_localeComboBox->setToolTip(tr("Interface language"));

    m_localeComboBox->addItem(tr("Default"), QString());
    for (const auto &loc : m_services.get<ConfigMgr2>()->getCoreConfig().getAvailableLocales()) {
      QLocale locale(loc);
      m_localeComboBox->addItem(
          QStringLiteral("%1 (%2)").arg(locale.nativeLanguageName(), locale.nativeCountryName()),
          loc);
    }

    const QString label(tr("Language:"));
    cardLayout->addWidget(SettingsPageHelper::createSettingRow(
        label, m_localeComboBox->toolTip(), m_localeComboBox, this));
    addSearchItem(label, m_localeComboBox->toolTip(), m_localeComboBox);
    connect(m_localeComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &GeneralPage::pageIsChangedWithRestartNeeded);
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
    cardLayout->addWidget(SettingsPageHelper::createSeparator(this));
    cardLayout->addWidget(SettingsPageHelper::createSettingRow(
        label, m_openGLComboBox->toolTip(), m_openGLComboBox, this));
    addSearchItem(label, m_openGLComboBox->toolTip(), m_openGLComboBox);
    connect(m_openGLComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &GeneralPage::pageIsChangedWithRestartNeeded);
  }
#endif

#if !defined(Q_OS_MACOS)
  {
    const QString label(tr("Minimize to system tray"));
    m_systemTrayCheckBox = WidgetsFactory::createCheckBox(label, this);
    m_systemTrayCheckBox->setToolTip(tr("Minimize to system tray when closed"));
    cardLayout->addWidget(SettingsPageHelper::createSeparator(this));
    cardLayout->addWidget(SettingsPageHelper::createCheckBoxRow(
        m_systemTrayCheckBox, m_systemTrayCheckBox->toolTip(), this));
    addSearchItem(label, m_systemTrayCheckBox->toolTip(), m_systemTrayCheckBox);
    connect(m_systemTrayCheckBox, &QCheckBox::stateChanged, this, &GeneralPage::pageIsChanged);
  }
#endif

  {
    const QString label(tr("Recover last session on start"));
    m_recoverLastSessionCheckBox = WidgetsFactory::createCheckBox(label, this);
    m_recoverLastSessionCheckBox->setToolTip(
        tr("Recover last session (like buffers) on start of VNote"));
    cardLayout->addWidget(SettingsPageHelper::createSeparator(this));
    cardLayout->addWidget(SettingsPageHelper::createCheckBoxRow(
        m_recoverLastSessionCheckBox, m_recoverLastSessionCheckBox->toolTip(), this));
    addSearchItem(label, m_recoverLastSessionCheckBox->toolTip(), m_recoverLastSessionCheckBox);
    connect(m_recoverLastSessionCheckBox, &QCheckBox::stateChanged, this,
            &GeneralPage::pageIsChanged);
  }

  {
    const QString label(tr("Check for updates on start"));
    m_checkForUpdatesCheckBox = WidgetsFactory::createCheckBox(label, this);
    m_checkForUpdatesCheckBox->setToolTip(tr("Check for updates on start of VNote"));
    cardLayout->addWidget(SettingsPageHelper::createSeparator(this));
    cardLayout->addWidget(SettingsPageHelper::createCheckBoxRow(
        m_checkForUpdatesCheckBox, m_checkForUpdatesCheckBox->toolTip(), this));
    addSearchItem(label, m_checkForUpdatesCheckBox->toolTip(), m_checkForUpdatesCheckBox);
    connect(m_checkForUpdatesCheckBox, &QCheckBox::stateChanged, this, &GeneralPage::pageIsChanged);
  }

  mainLayout->addStretch();
}

void GeneralPage::loadInternal() {
  const auto &coreConfig = m_services.get<ConfigMgr2>()->getCoreConfig();
  const auto &sessionConfig = m_services.get<ConfigMgr2>()->getSessionConfig();

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

  m_recoverLastSessionCheckBox->setChecked(
      m_services.get<ConfigCoreService>()->isRecoverLastSessionEnabled());

  m_checkForUpdatesCheckBox->setChecked(coreConfig.isCheckForUpdatesOnStartEnabled());
}

bool GeneralPage::saveInternal() {
  auto &coreConfig = m_services.get<ConfigMgr2>()->getCoreConfig();
  auto &sessionConfig = m_services.get<ConfigMgr2>()->getSessionConfig();

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

  if (!m_services.get<ConfigCoreService>()->setRecoverLastSessionEnabled(
          m_recoverLastSessionCheckBox->isChecked())) {
    setError(tr("Failed to save recover session setting."));
    return false;
  }

  coreConfig.setCheckForUpdatesOnStartEnabled(m_checkForUpdatesCheckBox->isChecked());

  return true;
}

QString GeneralPage::title() const { return tr("General"); }

QString GeneralPage::slug() const { return QStringLiteral("general"); }
