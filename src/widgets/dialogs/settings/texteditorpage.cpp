#include "texteditorpage.h"

#include <QComboBox>
#include <QFormLayout>
#include <QCheckBox>
#include <QSpinBox>

#include <widgets/widgetsfactory.h>
#include <core/editorconfig.h>
#include <core/texteditorconfig.h>
#include <core/configmgr.h>
#include <utils/widgetutils.h>

#include "editorpage.h"

using namespace vnotex;

TextEditorPage::TextEditorPage(QWidget *p_parent)
    : SettingsPage(p_parent)
{
    setupUI();
}

void TextEditorPage::setupUI()
{
    auto mainLayout = WidgetUtils::createFormLayout(this);

    {
        m_lineNumberComboBox = WidgetsFactory::createComboBox(this);
        m_lineNumberComboBox->setToolTip(tr("Line number type"));

        m_lineNumberComboBox->addItem(tr("None"), (int)TextEditorConfig::LineNumberType::None);
        m_lineNumberComboBox->addItem(tr("Absolute"), (int)TextEditorConfig::LineNumberType::Absolute);
        m_lineNumberComboBox->addItem(tr("Relative"), (int)TextEditorConfig::LineNumberType::Relative);

        const QString label(tr("Line number:"));
        mainLayout->addRow(label, m_lineNumberComboBox);
        addSearchItem(label, m_lineNumberComboBox->toolTip(), m_lineNumberComboBox);
        connect(m_lineNumberComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged),
                this, &TextEditorPage::pageIsChanged);
    }

    {
        const QString label(tr("Fold text"));
        m_textFoldingCheckBox = WidgetsFactory::createCheckBox(label, this);
        m_textFoldingCheckBox->setToolTip(tr("Text folding"));
        mainLayout->addRow(m_textFoldingCheckBox);
        addSearchItem(label, m_textFoldingCheckBox->toolTip(), m_textFoldingCheckBox);
        connect(m_textFoldingCheckBox, &QCheckBox::stateChanged,
                this, &TextEditorPage::pageIsChanged);
    }

    {
        m_inputModeComboBox = WidgetsFactory::createComboBox(this);
        m_inputModeComboBox->setToolTip(tr("Input mode like Vi"));

        m_inputModeComboBox->addItem(tr("Normal"), (int)TextEditorConfig::InputMode::NormalMode);
        m_inputModeComboBox->addItem(tr("Vi"), (int)TextEditorConfig::InputMode::ViMode);

        const QString label(tr("Input mode:"));
        mainLayout->addRow(label, m_inputModeComboBox);
        addSearchItem(label, m_inputModeComboBox->toolTip(), m_inputModeComboBox);
        connect(m_inputModeComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged),
                this, &TextEditorPage::pageIsChanged);
    }

    {
        m_centerCursorComboBox = WidgetsFactory::createComboBox(this);
        m_centerCursorComboBox->setToolTip(tr("Force to center text cursor"));

        m_centerCursorComboBox->addItem(tr("Never"), (int)TextEditorConfig::CenterCursor::NeverCenter);
        m_centerCursorComboBox->addItem(tr("Always Center"), (int)TextEditorConfig::CenterCursor::AlwaysCenter);
        m_centerCursorComboBox->addItem(tr("Center On Bottom"), (int)TextEditorConfig::CenterCursor::CenterOnBottom);

        const QString label(tr("Center cursor:"));
        mainLayout->addRow(label, m_centerCursorComboBox);
        addSearchItem(label, m_centerCursorComboBox->toolTip(), m_centerCursorComboBox);
        connect(m_centerCursorComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged),
                this, &TextEditorPage::pageIsChanged);
    }

    {
        m_wrapModeComboBox = WidgetsFactory::createComboBox(this);
        m_wrapModeComboBox->setToolTip(tr("Word wrap mode of editor"));

        m_wrapModeComboBox->addItem(tr("No Wrap"), (int)TextEditorConfig::WrapMode::NoWrap);
        m_wrapModeComboBox->addItem(tr("Word Wrap"), (int)TextEditorConfig::WrapMode::WordWrap);
        m_wrapModeComboBox->addItem(tr("Wrap Anywhere"), (int)TextEditorConfig::WrapMode::WrapAnywhere);
        m_wrapModeComboBox->addItem(tr("Word Wrap Or Wrap Anywhere"), (int)TextEditorConfig::WrapMode::WordWrapOrAnywhere);

        const QString label(tr("Wrap mode:"));
        mainLayout->addRow(label, m_wrapModeComboBox);
        addSearchItem(label, m_wrapModeComboBox->toolTip(), m_wrapModeComboBox);
        connect(m_wrapModeComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged),
                this, &TextEditorPage::pageIsChanged);
    }

    {
        const QString label(tr("Expand Tab"));
        m_expandTabCheckBox = WidgetsFactory::createCheckBox(label, this);
        m_expandTabCheckBox->setToolTip(tr("Expand Tab into spaces"));
        mainLayout->addRow(m_expandTabCheckBox);
        addSearchItem(label, m_expandTabCheckBox->toolTip(), m_expandTabCheckBox);
        connect(m_expandTabCheckBox, &QCheckBox::stateChanged,
                this, &TextEditorPage::pageIsChanged);
    }

    {
        m_tabStopWidthSpinBox = WidgetsFactory::createSpinBox(this);
        m_tabStopWidthSpinBox->setToolTip(tr("Number of spaces to use where Tab is needed"));

        m_tabStopWidthSpinBox->setRange(1, 32);
        m_tabStopWidthSpinBox->setSingleStep(1);

        const QString label(tr("Tab stop width:"));
        mainLayout->addRow(label, m_tabStopWidthSpinBox);
        addSearchItem(label, m_tabStopWidthSpinBox->toolTip(), m_tabStopWidthSpinBox);
        connect(m_tabStopWidthSpinBox, QOverload<int>::of(&QSpinBox::valueChanged),
                this, &TextEditorPage::pageIsChanged);
    }

    {
        m_zoomDeltaSpinBox = WidgetsFactory::createSpinBox(this);
        m_zoomDeltaSpinBox->setToolTip(tr("Zoom delta of the basic font size"));

        m_zoomDeltaSpinBox->setRange(-20, 20);
        m_zoomDeltaSpinBox->setSingleStep(1);

        const QString label(tr("Zoom delta:"));
        mainLayout->addRow(label, m_zoomDeltaSpinBox);
        addSearchItem(label, m_zoomDeltaSpinBox->toolTip(), m_zoomDeltaSpinBox);
        connect(m_zoomDeltaSpinBox, QOverload<int>::of(&QSpinBox::valueChanged),
                this, &TextEditorPage::pageIsChanged);
    }
}

void TextEditorPage::loadInternal()
{
    const auto &textConfig = ConfigMgr::getInst().getEditorConfig().getTextEditorConfig();

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

    m_zoomDeltaSpinBox->setValue(textConfig.getZoomDelta());
}

void TextEditorPage::saveInternal()
{
    auto &textConfig = ConfigMgr::getInst().getEditorConfig().getTextEditorConfig();

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

    textConfig.setZoomDelta(m_zoomDeltaSpinBox->value());

    EditorPage::notifyEditorConfigChange();
}

QString TextEditorPage::title() const
{
    return tr("Text Editor");
}
