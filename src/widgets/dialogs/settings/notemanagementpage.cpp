#include "notemanagementpage.h"

#include <QCheckBox>
#include <QComboBox>
#include <QVBoxLayout>

#include <core/configmgr2.h>
#include <core/coreconfig.h>
#include "settingspagehelper.h"
#include <core/servicelocator.h>
#include <utils/widgetutils.h>
#include <widgets/widgetsfactory.h>

using namespace vnotex;

NoteManagementPage::NoteManagementPage(ServiceLocator &p_services, QWidget *p_parent)
    : SettingsPage(p_services, p_parent) {
  setupUI();
}

void NoteManagementPage::setupUI() {
  auto *mainLayout = new QVBoxLayout(this);

  auto *cardLayout =
      SettingsPageHelper::addSection(mainLayout, tr("Note Management"), QString(), this);

  {
    const QString label(tr("Per-Notebook access history"));
    m_perNotebookHistoryCheckBox = WidgetsFactory::createCheckBox(label, this);
    m_perNotebookHistoryCheckBox->setToolTip(tr("Store note access history in its notebook"));
    cardLayout->addWidget(SettingsPageHelper::createCheckBoxRow(
        m_perNotebookHistoryCheckBox, m_perNotebookHistoryCheckBox->toolTip(), this));
    addSearchItem(label, m_perNotebookHistoryCheckBox->toolTip(), m_perNotebookHistoryCheckBox);
    connect(m_perNotebookHistoryCheckBox, &QCheckBox::stateChanged, this,
            &NoteManagementPage::pageIsChanged);
  }

  {
    m_lineEndingComboBox = WidgetsFactory::createComboBox(this);
    m_lineEndingComboBox->setToolTip(tr("Line ending used to write configuration files"));

    m_lineEndingComboBox->addItem(tr("Follow Platform"), (int)LineEndingPolicy::Platform);
    m_lineEndingComboBox->addItem(tr("LF (Linux/macOS)"), (int)LineEndingPolicy::LF);
    m_lineEndingComboBox->addItem(tr("CR LF (Windows)"), (int)LineEndingPolicy::CRLF);
    m_lineEndingComboBox->addItem(tr("CR"), (int)LineEndingPolicy::CR);

    const QString label(tr("Line ending:"));
    cardLayout->addWidget(SettingsPageHelper::createSeparator(this));
    cardLayout->addWidget(SettingsPageHelper::createSettingRow(
        label, m_lineEndingComboBox->toolTip(), m_lineEndingComboBox, this));
    addSearchItem(label, m_lineEndingComboBox->toolTip(), m_lineEndingComboBox);
    connect(m_lineEndingComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &NoteManagementPage::pageIsChanged);
  }

  {
    m_defaultOpenModeComboBox = WidgetsFactory::createComboBox(this);
    m_defaultOpenModeComboBox->setToolTip(tr("Default mode when opening notes"));

    m_defaultOpenModeComboBox->addItem(tr("Read"), (int)ViewWindowMode::Read);
    m_defaultOpenModeComboBox->addItem(tr("Edit"), (int)ViewWindowMode::Edit);

    const QString label(tr("Default open mode:"));
    cardLayout->addWidget(SettingsPageHelper::createSeparator(this));
    cardLayout->addWidget(SettingsPageHelper::createSettingRow(
        label, m_defaultOpenModeComboBox->toolTip(), m_defaultOpenModeComboBox, this));
    addSearchItem(label, m_defaultOpenModeComboBox->toolTip(), m_defaultOpenModeComboBox);
    connect(m_defaultOpenModeComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &NoteManagementPage::pageIsChanged);
  }

  mainLayout->addStretch();
}

void NoteManagementPage::loadInternal() {
  const auto &coreConfig = m_services.get<ConfigMgr2>()->getCoreConfig();

  m_perNotebookHistoryCheckBox->setChecked(coreConfig.isPerNotebookHistoryEnabled());

  {
    int idx = m_lineEndingComboBox->findData(static_cast<int>(coreConfig.getLineEndingPolicy()));
    if (idx == -1) {
      idx = 0;
    }
    m_lineEndingComboBox->setCurrentIndex(idx);
  }

  {
    int idx =
        m_defaultOpenModeComboBox->findData(static_cast<int>(coreConfig.getDefaultOpenMode()));
    if (idx == -1) {
      idx = 0;
    }
    m_defaultOpenModeComboBox->setCurrentIndex(idx);
  }
}

bool NoteManagementPage::saveInternal() {
  auto &coreConfig = m_services.get<ConfigMgr2>()->getCoreConfig();

  coreConfig.setPerNotebookHistoryEnabled(m_perNotebookHistoryCheckBox->isChecked());

  {
    auto ending = m_lineEndingComboBox->currentData().toInt();
    coreConfig.setLineEndingPolicy(static_cast<LineEndingPolicy>(ending));
  }

  {
    auto mode = m_defaultOpenModeComboBox->currentData().toInt();
    coreConfig.setDefaultOpenMode(static_cast<ViewWindowMode>(mode));
  }

  return true;
}

QString NoteManagementPage::title() const { return tr("Note Management"); }
