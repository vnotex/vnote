#include "syncpage.h"

#include <QSpinBox>
#include <QVBoxLayout>

#include "settingspagehelper.h"
#include <core/servicelocator.h>
#include <core/services/configcoreservice.h>
#include <widgets/widgetsfactory.h>

using namespace vnotex;

SyncPage::SyncPage(ServiceLocator &p_services, QWidget *p_parent)
    : SettingsPage(p_services, p_parent) {
  setupUI();
}

void SyncPage::setupUI() {
  auto *mainLayout = new QVBoxLayout(this);

  auto *cardLayout = SettingsPageHelper::addSection(mainLayout, tr("Sync"), QString(), this);

  {
    m_autoSyncDebounceSpinBox = WidgetsFactory::createSpinBox(this);
    m_autoSyncDebounceSpinBox->setRange(0, 86400);
    m_autoSyncDebounceSpinBox->setSingleStep(30);
    m_autoSyncDebounceSpinBox->setSuffix(QStringLiteral(" s"));
    m_autoSyncDebounceSpinBox->setSpecialValueText(tr("Immediate"));
    m_autoSyncDebounceSpinBox->setToolTip(
        tr("Auto-sync runs at most once per this interval. 0 = sync immediately on every change."));

    const QString label(tr("Auto-sync interval"));
    cardLayout->addWidget(SettingsPageHelper::createSettingRow(
        label, m_autoSyncDebounceSpinBox->toolTip(), m_autoSyncDebounceSpinBox, this));
    addSearchItem(label, m_autoSyncDebounceSpinBox->toolTip(), m_autoSyncDebounceSpinBox);
    connect(m_autoSyncDebounceSpinBox, QOverload<int>::of(&QSpinBox::valueChanged), this,
            &SyncPage::pageIsChanged);
  }

  mainLayout->addStretch();
}

void SyncPage::loadInternal() {
  m_autoSyncDebounceSpinBox->setValue(
      m_services.get<ConfigCoreService>()->getAutoSyncDebounceSeconds());
}

bool SyncPage::saveInternal() {
  if (!m_services.get<ConfigCoreService>()->setAutoSyncDebounceSeconds(
          m_autoSyncDebounceSpinBox->value())) {
    setError(tr("Failed to save auto-sync interval."));
    return false;
  }

  return true;
}

QString SyncPage::title() const { return tr("Sync"); }

QString SyncPage::slug() const { return QStringLiteral("sync"); }
