#include "vipage.h"

#include <QCheckBox>
#include <QVBoxLayout>

#include <vtextedit/viconfig.h>

#include <core/configmgr2.h>
#include <core/editorconfig.h>
#include "settingspagehelper.h"
#include <core/servicelocator.h>
#include <core/services/hookmanager.h>
#include <utils/widgetutils.h>
#include <widgets/widgetsfactory.h>

#include "editorpage.h"

using namespace vnotex;

ViPage::ViPage(ServiceLocator &p_services, QWidget *p_parent)
    : SettingsPage(p_services, p_parent) {
  setupUI();
}

void ViPage::setupUI() {
  auto *mainLayout = new QVBoxLayout(this);

  auto *cardLayout =
      SettingsPageHelper::addSection(mainLayout, tr("Vi Input Mode"), QString(), this);

  {
    const QString label(tr("Ctrl+C/X to copy/cut"));
    m_controlCToCopyCheckBox = WidgetsFactory::createCheckBox(label, this);
    m_controlCToCopyCheckBox->setToolTip(tr("Use Ctrl+C/X to copy/cut text"));
    cardLayout->addWidget(SettingsPageHelper::createCheckBoxRow(
        m_controlCToCopyCheckBox, m_controlCToCopyCheckBox->toolTip(), this));
    addSearchItem(label, m_controlCToCopyCheckBox->toolTip(), m_controlCToCopyCheckBox);
    connect(m_controlCToCopyCheckBox, &QCheckBox::stateChanged, this, &ViPage::pageIsChanged);
  }

  mainLayout->addStretch();
}

void ViPage::loadInternal() {
  const auto &viConfig = m_services.get<ConfigMgr2>()->getEditorConfig().getViConfig();

  m_controlCToCopyCheckBox->setChecked(viConfig->m_controlCToCopy);
}

bool ViPage::saveInternal() {
  auto &editorConfig = m_services.get<ConfigMgr2>()->getEditorConfig();
  auto &viConfig = editorConfig.getViConfig();

  viConfig->m_controlCToCopy = m_controlCToCopyCheckBox->isChecked();

  editorConfig.update();

  EditorPage::notifyEditorConfigChange(m_services.get<HookManager>());

  return true;
}

QString ViPage::title() const { return tr("Vi Input Mode"); }

QString ViPage::slug() const { return QStringLiteral("vi"); }
