#include "notemanagementpage.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QSpinBox>
#include <QVBoxLayout>

#include "settingspagehelper.h"
#include <core/configmgr2.h>
#include <core/coreconfig.h>
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
    m_lineEndingComboBox = WidgetsFactory::createComboBox(this);
    m_lineEndingComboBox->setToolTip(tr("Line ending used to write configuration files"));

    m_lineEndingComboBox->addItem(tr("Follow platform"), (int)LineEndingPolicy::Platform);
    m_lineEndingComboBox->addItem(tr("LF (Linux/macOS)"), (int)LineEndingPolicy::LF);
    m_lineEndingComboBox->addItem(tr("CR LF (Windows)"), (int)LineEndingPolicy::CRLF);
    m_lineEndingComboBox->addItem(tr("CR"), (int)LineEndingPolicy::CR);

    const QString label(tr("Line ending"));
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

    const QString label(tr("Default open mode"));
    cardLayout->addWidget(SettingsPageHelper::createSeparator(this));
    cardLayout->addWidget(SettingsPageHelper::createSettingRow(
        label, m_defaultOpenModeComboBox->toolTip(), m_defaultOpenModeComboBox, this));
    addSearchItem(label, m_defaultOpenModeComboBox->toolTip(), m_defaultOpenModeComboBox);
    connect(m_defaultOpenModeComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &NoteManagementPage::pageIsChanged);
  }

  {
    m_searchMaxResultsSpinBox = WidgetsFactory::createSpinBox(this);
    m_searchMaxResultsSpinBox->setObjectName(QStringLiteral("SearchMaxResultsSpinBox"));
    m_searchMaxResultsSpinBox->setRange(1, 100000);
    m_searchMaxResultsSpinBox->setSingleStep(100);
    m_searchMaxResultsSpinBox->setToolTip(tr("Maximum number of results returned by a search"));

    const QString label(tr("Search maximum results"));
    cardLayout->addWidget(SettingsPageHelper::createSeparator(this));
    cardLayout->addWidget(SettingsPageHelper::createSettingRow(
        label, m_searchMaxResultsSpinBox->toolTip(), m_searchMaxResultsSpinBox, this));
    addSearchItem(label, m_searchMaxResultsSpinBox->toolTip(), m_searchMaxResultsSpinBox);
    connect(m_searchMaxResultsSpinBox, QOverload<int>::of(&QSpinBox::valueChanged), this,
            &NoteManagementPage::pageIsChanged);
  }

  {
    const QString label(tr("Automatically clean recycle bins"));
    m_recycleBinAutoCleanupCheckBox = WidgetsFactory::createCheckBox(label, this);
    m_recycleBinAutoCleanupCheckBox->setObjectName(QStringLiteral("RecycleBinAutoCleanupCheckBox"));
    m_recycleBinAutoCleanupCheckBox->setToolTip(
        tr("Permanently delete recycle bin entries older than the retention period"));
    cardLayout->addWidget(SettingsPageHelper::createSeparator(this));
    cardLayout->addWidget(SettingsPageHelper::createCheckBoxRow(
        m_recycleBinAutoCleanupCheckBox, m_recycleBinAutoCleanupCheckBox->toolTip(), this));
    addSearchItem(label, m_recycleBinAutoCleanupCheckBox->toolTip(),
                  m_recycleBinAutoCleanupCheckBox);
    connect(m_recycleBinAutoCleanupCheckBox, &QCheckBox::toggled, this, [this](bool p_enabled) {
      m_recycleBinRetentionDaysSpinBox->setEnabled(p_enabled);
      pageIsChanged();
    });
  }

  {
    m_recycleBinRetentionDaysSpinBox = WidgetsFactory::createSpinBox(this);
    m_recycleBinRetentionDaysSpinBox->setObjectName(
        QStringLiteral("RecycleBinRetentionDaysSpinBox"));
    m_recycleBinRetentionDaysSpinBox->setRange(1, 3650);
    m_recycleBinRetentionDaysSpinBox->setSuffix(tr(" days"));
    m_recycleBinRetentionDaysSpinBox->setToolTip(
        tr("Number of days to keep items in notebook recycle bins"));
    m_recycleBinRetentionDaysSpinBox->setEnabled(false);

    const QString label(tr("Recycle bin retention"));
    cardLayout->addWidget(SettingsPageHelper::createSeparator(this));
    cardLayout->addWidget(
        SettingsPageHelper::createSettingRow(label, m_recycleBinRetentionDaysSpinBox->toolTip(),
                                             m_recycleBinRetentionDaysSpinBox, this));
    addSearchItem(label, m_recycleBinRetentionDaysSpinBox->toolTip(),
                  m_recycleBinRetentionDaysSpinBox);
    connect(m_recycleBinRetentionDaysSpinBox, QOverload<int>::of(&QSpinBox::valueChanged), this,
            &NoteManagementPage::pageIsChanged);
  }

  mainLayout->addStretch();
}

void NoteManagementPage::loadInternal() {
  const auto &coreConfig = m_services.get<ConfigMgr2>()->getCoreConfig();

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

  m_searchMaxResultsSpinBox->setValue(coreConfig.getSearchMaxResults());

  m_recycleBinAutoCleanupCheckBox->setChecked(coreConfig.isRecycleBinAutoCleanupEnabled());
  m_recycleBinRetentionDaysSpinBox->setValue(coreConfig.getRecycleBinRetentionDays());
  m_recycleBinRetentionDaysSpinBox->setEnabled(m_recycleBinAutoCleanupCheckBox->isChecked());
}

bool NoteManagementPage::saveInternal() {
  auto &coreConfig = m_services.get<ConfigMgr2>()->getCoreConfig();

  {
    auto ending = m_lineEndingComboBox->currentData().toInt();
    coreConfig.setLineEndingPolicy(static_cast<LineEndingPolicy>(ending));
  }

  {
    auto mode = m_defaultOpenModeComboBox->currentData().toInt();
    coreConfig.setDefaultOpenMode(static_cast<ViewWindowMode>(mode));
  }

  coreConfig.setSearchMaxResults(m_searchMaxResultsSpinBox->value());

  coreConfig.setRecycleBinRetentionDays(m_recycleBinRetentionDaysSpinBox->value());
  coreConfig.setRecycleBinAutoCleanupEnabled(m_recycleBinAutoCleanupCheckBox->isChecked(),
                                             QDateTime::currentMSecsSinceEpoch());

  return true;
}

QString NoteManagementPage::title() const { return tr("Note Management"); }

QString NoteManagementPage::slug() const { return QStringLiteral("notemanagement"); }
