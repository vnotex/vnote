#include "unitedentrypage.h"

#include <QCheckBox>
#include <QVBoxLayout>

#include "settingspagehelper.h"
#include <core/configmgr2.h>
#include <core/servicelocator.h>
#include <core/widgetconfig.h>
#include <widgets/widgetsfactory.h>

using namespace vnotex;

UnitedEntryPage::UnitedEntryPage(ServiceLocator &p_services, QWidget *p_parent)
    : SettingsPage(p_services, p_parent) {
  setupUI();
}

void UnitedEntryPage::setupUI() {
  auto *mainLayout = new QVBoxLayout(this);
  auto *cardLayout = SettingsPageHelper::addSection(mainLayout, title(), QString(), this);

  const auto label = tr("Expand All");
  m_expandAllCheckBox = WidgetsFactory::createCheckBox(label, this);
  m_expandAllCheckBox->setToolTip(tr("Automatically expand result groups in United Entry"));
  cardLayout->addWidget(SettingsPageHelper::createCheckBoxRow(
      m_expandAllCheckBox, m_expandAllCheckBox->toolTip(), this));
  addSearchItem(label, m_expandAllCheckBox->toolTip(), m_expandAllCheckBox);
  connect(m_expandAllCheckBox, &QCheckBox::toggled, this, &UnitedEntryPage::pageIsChanged);

  mainLayout->addStretch();
}

void UnitedEntryPage::loadInternal() {
  m_expandAllCheckBox->setChecked(
      m_services.get<ConfigMgr2>()->getWidgetConfig().getUnitedEntryExpandAllEnabled());
}

bool UnitedEntryPage::saveInternal() {
  m_services.get<ConfigMgr2>()->getWidgetConfig().setUnitedEntryExpandAllEnabled(
      m_expandAllCheckBox->isChecked());
  return true;
}

QString UnitedEntryPage::title() const { return tr("United Entry"); }

QString UnitedEntryPage::slug() const { return QStringLiteral("unitedentry"); }
