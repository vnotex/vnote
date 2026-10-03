#include "mindmapeditorpage.h"

#include <QCheckBox>
#include <QLabel>
#include <QLineEdit>
#include <QSpinBox>
#include <QVBoxLayout>

#include <limits>

#include <core/configmgr2.h>
#include <core/editorconfig.h>
#include <core/mindmapeditorconfig.h>
#include <core/servicelocator.h>
#include <core/services/hookmanager.h>
#include <widgets/widgetsfactory.h>

#include "editorpage.h"
#include "settingspagehelper.h"

using namespace vnotex;

MindMapEditorPage::MindMapEditorPage(ServiceLocator &p_services, QWidget *p_parent)
    : SettingsPage(p_services, p_parent) {
  setupUI();
}

void MindMapEditorPage::setupUI() {
  auto *mainLayout = new QVBoxLayout(this);
  auto *style = SettingsPageHelper::addSection(mainLayout, tr("Style"), QString(), this);
  {
    m_fontFamily = WidgetsFactory::createLineEdit(this);
    m_fontFamily->setObjectName(QStringLiteral("mindMapFontFamily"));
    m_fontFamily->setPlaceholderText(tr("Default (interface font)"));
    m_fontFamily->setToolTip(tr(
        "Comma-separated font families in fallback order; leave empty to use the interface font"));
    const auto label = tr("Font family");
    style->addWidget(
        SettingsPageHelper::createSettingRow(label, m_fontFamily->toolTip(), m_fontFamily, this));
    addSearchItem(label, m_fontFamily->toolTip(), m_fontFamily);
    connect(m_fontFamily, &QLineEdit::textChanged, this, &MindMapEditorPage::pageIsChanged);
  }
  {
    m_fontSize = WidgetsFactory::createSpinBox(this);
    m_fontSize->setObjectName(QStringLiteral("mindMapFontSize"));
    m_fontSize->setRange(0, 256);
    m_fontSize->setSuffix(tr(" pt"));
    m_fontSize->setSpecialValueText(tr("Default"));
    m_fontSize->setToolTip(
        tr("Default text size in points; node-specific sizes still override it"));
    const auto label = tr("Font size");
    style->addWidget(SettingsPageHelper::createSeparator(this));
    style->addWidget(
        SettingsPageHelper::createSettingRow(label, m_fontSize->toolTip(), m_fontSize, this));
    addSearchItem(label, m_fontSize->toolTip(), m_fontSize);
    connect(m_fontSize, QOverload<int>::of(&QSpinBox::valueChanged), this,
            &MindMapEditorPage::pageIsChanged);
  }

  auto *behavior = SettingsPageHelper::addSection(mainLayout, tr("Behavior"), QString(), this);
  {
    m_undoLimit = WidgetsFactory::createSpinBox(this);
    m_undoLimit->setObjectName(QStringLiteral("mindMapUndoLimit"));
    m_undoLimit->setRange(0, std::numeric_limits<int>::max());
    m_undoLimit->setSpecialValueText(tr("Unlimited"));
    m_undoLimit->setToolTip(tr("Maximum retained document commands; lowering this limit discards "
                               "excess undo and redo history"));
    const auto label = tr("Undo limit");
    behavior->addWidget(
        SettingsPageHelper::createSettingRow(label, m_undoLimit->toolTip(), m_undoLimit, this));
    addSearchItem(label, m_undoLimit->toolTip(), m_undoLimit);
    connect(m_undoLimit, QOverload<int>::of(&QSpinBox::valueChanged), this,
            &MindMapEditorPage::pageIsChanged);
  }
  {
    const auto label = tr("Confirm subtree deletion");
    m_confirmSubtreeDeletion = WidgetsFactory::createCheckBox(label, this);
    m_confirmSubtreeDeletion->setObjectName(QStringLiteral("mindMapConfirmSubtreeDeletion"));
    m_confirmSubtreeDeletion->setToolTip(tr("Ask before deleting nodes and their descendants"));
    behavior->addWidget(SettingsPageHelper::createSeparator(this));
    behavior->addWidget(SettingsPageHelper::createCheckBoxRow(
        m_confirmSubtreeDeletion, m_confirmSubtreeDeletion->toolTip(), this));
    addSearchItem(label, m_confirmSubtreeDeletion->toolTip(), m_confirmSubtreeDeletion);
    connect(m_confirmSubtreeDeletion, &QCheckBox::stateChanged, this,
            &MindMapEditorPage::pageIsChanged);
  }
  {
    const auto label = tr("Automatic branch colors");
    m_autoRandomBranchColor = WidgetsFactory::createCheckBox(label, this);
    m_autoRandomBranchColor->setObjectName(QStringLiteral("mindMapAutoRandomBranchColor"));
    m_autoRandomBranchColor->setToolTip(
        tr("Assign colors to new main branches without recoloring existing nodes"));
    behavior->addWidget(SettingsPageHelper::createSeparator(this));
    behavior->addWidget(SettingsPageHelper::createCheckBoxRow(
        m_autoRandomBranchColor, m_autoRandomBranchColor->toolTip(), this));
    addSearchItem(label, m_autoRandomBranchColor->toolTip(), m_autoRandomBranchColor);
    connect(m_autoRandomBranchColor, &QCheckBox::stateChanged, this,
            &MindMapEditorPage::pageIsChanged);
  }
  const auto shortcuts =
      tr("Keyboard shortcuts can be configured in section [editor.mindMapEditor.shortcuts]");
  auto *description = SettingsPageHelper::createDescription(shortcuts, this);
  behavior->addWidget(SettingsPageHelper::createSeparator(this));
  behavior->addWidget(description);
  addSearchItem(shortcuts, description);
  mainLayout->addStretch();
}

void MindMapEditorPage::loadInternal() {
  const auto &config = m_services.get<ConfigMgr2>()->getEditorConfig().getMindMapEditorConfig();
  m_fontFamily->setText(config.getFontFamily());
  m_fontSize->setValue(config.getFontPointSize());
  m_undoLimit->setValue(config.getUndoLimit());
  m_confirmSubtreeDeletion->setChecked(config.getConfirmSubtreeDeletion());
  m_autoRandomBranchColor->setChecked(config.getAutoRandomBranchColor());
}

bool MindMapEditorPage::saveInternal() {
  auto &config = m_services.get<ConfigMgr2>()->getEditorConfig().getMindMapEditorConfig();
  config.setFontFamily(m_fontFamily->text());
  config.setFontPointSize(m_fontSize->value());
  config.setUndoLimit(m_undoLimit->value());
  config.setConfirmSubtreeDeletion(m_confirmSubtreeDeletion->isChecked());
  config.setAutoRandomBranchColor(m_autoRandomBranchColor->isChecked());
  EditorPage::notifyEditorConfigChange(m_services.get<HookManager>());
  return true;
}

QString MindMapEditorPage::title() const { return tr("MindMap Editor"); }

QString MindMapEditorPage::slug() const { return QStringLiteral("mindmapeditor"); }
