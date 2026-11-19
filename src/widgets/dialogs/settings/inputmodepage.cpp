#include "inputmodepage.h"

#include <QCheckBox>
#include <QComboBox>
#include <QVBoxLayout>

#include <vtextedit/viconfig.h>

#include "settingspagehelper.h"
#include <core/configmgr2.h>
#include <core/editorconfig.h>
#include <core/servicelocator.h>
#include <core/services/hookmanager.h>
#include <utils/widgetutils.h>
#include <widgets/widgetsfactory.h>

#include "editorpage.h"

using namespace vnotex;

InputModePage::InputModePage(ServiceLocator &p_services, QWidget *p_parent)
    : SettingsPage(p_services, p_parent) {
  setupUI();
}

void InputModePage::setupUI() {
  auto *mainLayout = new QVBoxLayout(this);

  auto *cardLayout = SettingsPageHelper::addSection(mainLayout, tr("Input Mode"), QString(), this);

  {
    m_inputModeComboBox = WidgetsFactory::createComboBox(this);
    m_inputModeComboBox->setToolTip(tr("Input mode for editors"));

    m_inputModeComboBox->addItem(tr("Normal"), (int)EditorConfig::InputMode::NormalMode);
    m_inputModeComboBox->addItem(tr("Vi"), (int)EditorConfig::InputMode::ViMode);
    m_inputModeComboBox->addItem(tr("VSCode"), (int)EditorConfig::InputMode::VscodeMode);

    const QString label(tr("Input mode"));
    cardLayout->addWidget(SettingsPageHelper::createSettingRow(
        label, m_inputModeComboBox->toolTip(), m_inputModeComboBox, this));
    addSearchItem(label, m_inputModeComboBox->toolTip(), m_inputModeComboBox);
    connect(m_inputModeComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &InputModePage::pageIsChanged);
  }

  cardLayout = SettingsPageHelper::addSection(mainLayout, tr("Vi Input Mode"), QString(), this);

  {
    const QString label(tr("Ctrl+C/X to copy/cut"));
    m_controlCToCopyCheckBox = WidgetsFactory::createCheckBox(label, this);
    m_controlCToCopyCheckBox->setToolTip(tr("Use Ctrl+C/X to copy/cut text"));
    cardLayout->addWidget(SettingsPageHelper::createCheckBoxRow(
        m_controlCToCopyCheckBox, m_controlCToCopyCheckBox->toolTip(), this));
    addSearchItem(label, m_controlCToCopyCheckBox->toolTip(), m_controlCToCopyCheckBox);
    connect(m_controlCToCopyCheckBox, &QCheckBox::stateChanged, this,
            &InputModePage::pageIsChanged);
  }

  mainLayout->addStretch();
}

void InputModePage::loadInternal() {
  const auto &editorConfig = m_services.get<ConfigMgr2>()->getEditorConfig();
  const auto &viConfig = editorConfig.getViConfig();

  {
    int idx = m_inputModeComboBox->findData(static_cast<int>(editorConfig.getInputMode()));
    Q_ASSERT(idx != -1);
    m_inputModeComboBox->setCurrentIndex(idx);
  }

  m_controlCToCopyCheckBox->setChecked(viConfig->m_controlCToCopy);
}

bool InputModePage::saveInternal() {
  auto &editorConfig = m_services.get<ConfigMgr2>()->getEditorConfig();
  auto &viConfig = editorConfig.getViConfig();

  editorConfig.setInputMode(
      static_cast<EditorConfig::InputMode>(m_inputModeComboBox->currentData().toInt()));

  viConfig->m_controlCToCopy = m_controlCToCopyCheckBox->isChecked();

  editorConfig.update();

  EditorPage::notifyEditorConfigChange(m_services.get<HookManager>());

  return true;
}

QString InputModePage::title() const { return tr("Input Mode"); }

QString InputModePage::slug() const { return QStringLiteral("inputmode"); }
