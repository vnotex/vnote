#include "texteditorpage.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QSpinBox>
#include <QVBoxLayout>

#include <core/configmgr2.h>
#include <core/editorconfig.h>
#include <core/servicelocator.h>
#include <core/services/hookmanager.h>
#include <core/texteditorconfig.h>
#include <utils/widgetutils.h>
#include <widgets/widgetsfactory.h>

#include "editorpage.h"
#include "settingspagehelper.h"

using namespace vnotex;

TextEditorPage::TextEditorPage(ServiceLocator &p_services, QWidget *p_parent)
    : SettingsPage(p_services, p_parent) {
  setupUI();
}

void TextEditorPage::setupUI() {
  auto *mainLayout = new QVBoxLayout(this);

  auto *cardLayout = SettingsPageHelper::addSection(mainLayout, tr("Text Editor"), QString(), this);

  {
    m_lineNumberComboBox = WidgetsFactory::createComboBox(this);
    m_lineNumberComboBox->setToolTip(tr("Line number type"));

    m_lineNumberComboBox->addItem(tr("None"), (int)TextEditorConfig::LineNumberType::None);
    m_lineNumberComboBox->addItem(tr("Absolute"), (int)TextEditorConfig::LineNumberType::Absolute);
    m_lineNumberComboBox->addItem(tr("Relative"), (int)TextEditorConfig::LineNumberType::Relative);

    const QString label(tr("Line number:"));
    cardLayout->addWidget(SettingsPageHelper::createSettingRow(
        label, m_lineNumberComboBox->toolTip(), m_lineNumberComboBox, this));
    addSearchItem(label, m_lineNumberComboBox->toolTip(), m_lineNumberComboBox);
    connect(m_lineNumberComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &TextEditorPage::pageIsChanged);
  }

  {
    const QString label(tr("Fold text"));
    m_textFoldingCheckBox = WidgetsFactory::createCheckBox(label, this);
    m_textFoldingCheckBox->setToolTip(tr("Text folding"));
    cardLayout->addWidget(SettingsPageHelper::createSeparator(this));
    cardLayout->addWidget(SettingsPageHelper::createCheckBoxRow(
        m_textFoldingCheckBox, m_textFoldingCheckBox->toolTip(), this));
    addSearchItem(label, m_textFoldingCheckBox->toolTip(), m_textFoldingCheckBox);
    connect(m_textFoldingCheckBox, &QCheckBox::stateChanged, this, &TextEditorPage::pageIsChanged);
  }

  {
    m_inputModeComboBox = WidgetsFactory::createComboBox(this);
    m_inputModeComboBox->setToolTip(tr("Input mode like Vi"));

    m_inputModeComboBox->addItem(tr("Normal"), (int)TextEditorConfig::InputMode::NormalMode);
    m_inputModeComboBox->addItem(tr("Vi"), (int)TextEditorConfig::InputMode::ViMode);
    m_inputModeComboBox->addItem(tr("VSCode"), (int)TextEditorConfig::InputMode::VscodeMode);

    const QString label(tr("Input mode:"));
    cardLayout->addWidget(SettingsPageHelper::createSeparator(this));
    cardLayout->addWidget(SettingsPageHelper::createSettingRow(
        label, m_inputModeComboBox->toolTip(), m_inputModeComboBox, this));
    addSearchItem(label, m_inputModeComboBox->toolTip(), m_inputModeComboBox);
    connect(m_inputModeComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &TextEditorPage::pageIsChanged);
  }

  {
    m_centerCursorComboBox = WidgetsFactory::createComboBox(this);
    m_centerCursorComboBox->setToolTip(tr("Force to center text cursor"));

    m_centerCursorComboBox->addItem(tr("Never"), (int)TextEditorConfig::CenterCursor::NeverCenter);
    m_centerCursorComboBox->addItem(tr("Always Center"),
                                    (int)TextEditorConfig::CenterCursor::AlwaysCenter);
    m_centerCursorComboBox->addItem(tr("Center On Bottom"),
                                    (int)TextEditorConfig::CenterCursor::CenterOnBottom);

    const QString label(tr("Center cursor:"));
    cardLayout->addWidget(SettingsPageHelper::createSeparator(this));
    cardLayout->addWidget(SettingsPageHelper::createSettingRow(
        label, m_centerCursorComboBox->toolTip(), m_centerCursorComboBox, this));
    addSearchItem(label, m_centerCursorComboBox->toolTip(), m_centerCursorComboBox);
    connect(m_centerCursorComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &TextEditorPage::pageIsChanged);
  }

  {
    m_wrapModeComboBox = WidgetsFactory::createComboBox(this);
    m_wrapModeComboBox->setToolTip(tr("Word wrap mode of editor"));

    m_wrapModeComboBox->addItem(tr("No Wrap"), (int)TextEditorConfig::WrapMode::NoWrap);
    m_wrapModeComboBox->addItem(tr("Word Wrap"), (int)TextEditorConfig::WrapMode::WordWrap);
    m_wrapModeComboBox->addItem(tr("Wrap Anywhere"), (int)TextEditorConfig::WrapMode::WrapAnywhere);
    m_wrapModeComboBox->addItem(tr("Word Wrap Or Wrap Anywhere"),
                                (int)TextEditorConfig::WrapMode::WordWrapOrAnywhere);

    const QString label(tr("Wrap mode:"));
    cardLayout->addWidget(SettingsPageHelper::createSeparator(this));
    cardLayout->addWidget(SettingsPageHelper::createSettingRow(label, m_wrapModeComboBox->toolTip(),
                                                               m_wrapModeComboBox, this));
    addSearchItem(label, m_wrapModeComboBox->toolTip(), m_wrapModeComboBox);
    connect(m_wrapModeComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &TextEditorPage::pageIsChanged);
  }

  {
    const QString label(tr("Expand Tab"));
    m_expandTabCheckBox = WidgetsFactory::createCheckBox(label, this);
    m_expandTabCheckBox->setToolTip(tr("Expand Tab into spaces"));
    cardLayout->addWidget(SettingsPageHelper::createSeparator(this));
    cardLayout->addWidget(SettingsPageHelper::createCheckBoxRow(
        m_expandTabCheckBox, m_expandTabCheckBox->toolTip(), this));
    addSearchItem(label, m_expandTabCheckBox->toolTip(), m_expandTabCheckBox);
    connect(m_expandTabCheckBox, &QCheckBox::stateChanged, this, &TextEditorPage::pageIsChanged);
  }

  {
    m_tabStopWidthSpinBox = WidgetsFactory::createSpinBox(this);
    m_tabStopWidthSpinBox->setToolTip(tr("Number of spaces to use where Tab is needed"));

    m_tabStopWidthSpinBox->setRange(1, 32);
    m_tabStopWidthSpinBox->setSingleStep(1);

    const QString label(tr("Tab stop width:"));
    cardLayout->addWidget(SettingsPageHelper::createSeparator(this));
    cardLayout->addWidget(SettingsPageHelper::createSettingRow(
        label, m_tabStopWidthSpinBox->toolTip(), m_tabStopWidthSpinBox, this));
    addSearchItem(label, m_tabStopWidthSpinBox->toolTip(), m_tabStopWidthSpinBox);
    connect(m_tabStopWidthSpinBox, QOverload<int>::of(&QSpinBox::valueChanged), this,
            &TextEditorPage::pageIsChanged);
  }

  {
    const QString label(tr("Highlight whitespace"));
    m_highlightWhitespaceCheckBox = WidgetsFactory::createCheckBox(label, this);
    m_highlightWhitespaceCheckBox->setToolTip(tr("Highlight Tab and trailing space"));
    cardLayout->addWidget(SettingsPageHelper::createSeparator(this));
    cardLayout->addWidget(SettingsPageHelper::createCheckBoxRow(
        m_highlightWhitespaceCheckBox, m_highlightWhitespaceCheckBox->toolTip(), this));
    addSearchItem(label, m_highlightWhitespaceCheckBox->toolTip(), m_highlightWhitespaceCheckBox);
    connect(m_highlightWhitespaceCheckBox, &QCheckBox::stateChanged, this,
            &TextEditorPage::pageIsChanged);
  }

  {
    m_lineSpacingSpinBox = WidgetsFactory::createDoubleSpinBox(this);
    m_lineSpacingSpinBox->setToolTip(tr("Line spacing multiplier (1.0 = default)"));
    m_lineSpacingSpinBox->setRange(1.0, 5.0);
    m_lineSpacingSpinBox->setSingleStep(0.1);
    m_lineSpacingSpinBox->setDecimals(1);

    const QString label(tr("Line spacing:"));
    cardLayout->addWidget(SettingsPageHelper::createSeparator(this));
    cardLayout->addWidget(SettingsPageHelper::createSettingRow(
        label, m_lineSpacingSpinBox->toolTip(), m_lineSpacingSpinBox, this));
    addSearchItem(label, m_lineSpacingSpinBox->toolTip(), m_lineSpacingSpinBox);
    connect(m_lineSpacingSpinBox, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
            &TextEditorPage::pageIsChanged);
  }

  {
    m_zoomDeltaSpinBox = WidgetsFactory::createSpinBox(this);
    m_zoomDeltaSpinBox->setToolTip(tr("Zoom delta of the basic font size"));

    m_zoomDeltaSpinBox->setRange(-20, 20);
    m_zoomDeltaSpinBox->setSingleStep(1);

    const QString label(tr("Zoom delta:"));
    cardLayout->addWidget(SettingsPageHelper::createSeparator(this));
    cardLayout->addWidget(SettingsPageHelper::createSettingRow(label, m_zoomDeltaSpinBox->toolTip(),
                                                               m_zoomDeltaSpinBox, this));
    addSearchItem(label, m_zoomDeltaSpinBox->toolTip(), m_zoomDeltaSpinBox);
    connect(m_zoomDeltaSpinBox, QOverload<int>::of(&QSpinBox::valueChanged), this,
            &TextEditorPage::pageIsChanged);
  }

  {
    const QString label(tr("Spell check"));
    m_spellCheckCheckBox = WidgetsFactory::createCheckBox(label, this);
    m_spellCheckCheckBox->setToolTip(tr("Spell check"));
    cardLayout->addWidget(SettingsPageHelper::createSeparator(this));
    cardLayout->addWidget(SettingsPageHelper::createCheckBoxRow(
        m_spellCheckCheckBox, m_spellCheckCheckBox->toolTip(), this));
    addSearchItem(label, m_spellCheckCheckBox->toolTip(), m_spellCheckCheckBox);
    connect(m_spellCheckCheckBox, &QCheckBox::stateChanged, this, &TextEditorPage::pageIsChanged);
  }

  mainLayout->addStretch();
}

void TextEditorPage::loadInternal() {
  const auto &textConfig = m_services.get<ConfigMgr2>()->getEditorConfig().getTextEditorConfig();

  {
    int idx = m_lineNumberComboBox->findData(static_cast<int>(textConfig.getLineNumberType()));
    Q_ASSERT(idx != -1);
    m_lineNumberComboBox->setCurrentIndex(idx);
  }

  m_textFoldingCheckBox->setChecked(textConfig.getTextFoldingEnabled());

  {
    int idx = m_inputModeComboBox->findData(static_cast<int>(textConfig.getInputMode()));
    Q_ASSERT(idx != -1);
    m_inputModeComboBox->setCurrentIndex(idx);
  }

  {
    int idx = m_centerCursorComboBox->findData(static_cast<int>(textConfig.getCenterCursor()));
    Q_ASSERT(idx != -1);
    m_centerCursorComboBox->setCurrentIndex(idx);
  }

  {
    int idx = m_wrapModeComboBox->findData(static_cast<int>(textConfig.getWrapMode()));
    Q_ASSERT(idx != -1);
    m_wrapModeComboBox->setCurrentIndex(idx);
  }

  m_expandTabCheckBox->setChecked(textConfig.getExpandTabEnabled());

  m_tabStopWidthSpinBox->setValue(textConfig.getTabStopWidth());

  m_highlightWhitespaceCheckBox->setChecked(textConfig.getHighlightWhitespaceEnabled());

  m_lineSpacingSpinBox->setValue(textConfig.getLineSpacing());

  m_zoomDeltaSpinBox->setValue(textConfig.getZoomDelta());

  m_spellCheckCheckBox->setChecked(textConfig.isSpellCheckEnabled());
}

bool TextEditorPage::saveInternal() {
  auto &textConfig = m_services.get<ConfigMgr2>()->getEditorConfig().getTextEditorConfig();

  {
    auto lineNumber = m_lineNumberComboBox->currentData().toInt();
    textConfig.setLineNumberType(static_cast<TextEditorConfig::LineNumberType>(lineNumber));
  }

  textConfig.setTextFoldingEnabled(m_textFoldingCheckBox->isChecked());

  {
    auto inputMode = m_inputModeComboBox->currentData().toInt();
    textConfig.setInputMode(static_cast<TextEditorConfig::InputMode>(inputMode));
  }

  {
    auto centerCursor = m_centerCursorComboBox->currentData().toInt();
    textConfig.setCenterCursor(static_cast<TextEditorConfig::CenterCursor>(centerCursor));
  }

  {
    auto wrapMode = m_wrapModeComboBox->currentData().toInt();
    textConfig.setWrapMode(static_cast<TextEditorConfig::WrapMode>(wrapMode));
  }

  textConfig.setExpandTabEnabled(m_expandTabCheckBox->isChecked());

  textConfig.setTabStopWidth(m_tabStopWidthSpinBox->value());

  textConfig.setHighlightWhitespaceEnabled(m_highlightWhitespaceCheckBox->isChecked());

  textConfig.setLineSpacing(m_lineSpacingSpinBox->value());

  textConfig.setZoomDelta(m_zoomDeltaSpinBox->value());

  textConfig.setSpellCheckEnabled(m_spellCheckCheckBox->isChecked());

  EditorPage::notifyEditorConfigChange(m_services.get<HookManager>());

  return true;
}

QString TextEditorPage::title() const { return tr("Text Editor"); }
