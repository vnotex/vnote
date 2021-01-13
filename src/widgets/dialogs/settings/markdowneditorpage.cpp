#include "markdowneditorpage.h"

#include <QFormLayout>
#include <QVBoxLayout>
#include <QCheckBox>
#include <QGroupBox>
#include <QDoubleSpinBox>
#include <QComboBox>
#include <QSpinBox>
#include <QHBoxLayout>

#include <widgets/widgetsfactory.h>
#include <core/editorconfig.h>
#include <core/markdowneditorconfig.h>
#include <core/configmgr.h>
#include <utils/widgetutils.h>

#include "editorpage.h"

using namespace vnotex;

MarkdownEditorPage::MarkdownEditorPage(QWidget *p_parent)
    : SettingsPage(p_parent)
{
    setupUI();
}

void MarkdownEditorPage::setupUI()
{
    auto mainLayout = new QVBoxLayout(this);

    auto generalBox = setupGeneralGroup();
    mainLayout->addWidget(generalBox);

    auto readBox = setupReadGroup();
    mainLayout->addWidget(readBox);

    auto editBox = setupEditGroup();
    mainLayout->addWidget(editBox);
}

void MarkdownEditorPage::loadInternal()
{
    const auto &markdownConfig = ConfigMgr::getInst().getEditorConfig().getMarkdownEditorConfig();

    m_insertFileNameAsTitleCheckBox->setChecked(markdownConfig.getInsertFileNameAsTitle());

    {
        int idx = m_sectionNumberComboBox->findData(static_cast<int>(markdownConfig.getSectionNumberMode()));
        Q_ASSERT(idx != -1);
        m_sectionNumberComboBox->setCurrentIndex(idx);

        m_sectionNumberBaseLevelSpinBox->setValue(markdownConfig.getSectionNumberBaseLevel());

        idx = m_sectionNumberStyleComboBox->findData(static_cast<int>(markdownConfig.getSectionNumberStyle()));
        Q_ASSERT(idx != -1);
        m_sectionNumberStyleComboBox->setCurrentIndex(idx);
    }

    m_constrainImageWidthCheckBox->setChecked(markdownConfig.getConstrainImageWidthEnabled());

    m_zoomFactorSpinBox->setValue(markdownConfig.getZoomFactorInReadMode());

    m_constrainInPlacePreviewWidthCheckBox->setChecked(markdownConfig.getConstrainInPlacePreviewWidthEnabled());

    m_fetchImagesToLocalCheckBox->setChecked(markdownConfig.getFetchImagesInParseAndPaste());

    m_htmlTagCheckBox->setChecked(markdownConfig.getHtmlTagEnabled());

    m_autoBreakCheckBox->setChecked(markdownConfig.getAutoBreakEnabled());

    m_linkifyCheckBox->setChecked(markdownConfig.getLinkifyEnabled());

    m_indentFirstLineCheckBox->setChecked(markdownConfig.getIndentFirstLineEnabled());

    m_smartTableCheckBox->setChecked(markdownConfig.getSmartTableEnabled());
}

void MarkdownEditorPage::saveInternal()
{
    auto &markdownConfig = ConfigMgr::getInst().getEditorConfig().getMarkdownEditorConfig();

    markdownConfig.setInsertFileNameAsTitle(m_insertFileNameAsTitleCheckBox->isChecked());

    {
        auto mode = m_sectionNumberComboBox->currentData().toInt();
        markdownConfig.setSectionNumberMode(static_cast<MarkdownEditorConfig::SectionNumberMode>(mode));

        if (m_sectionNumberBaseLevelSpinBox->isEnabled()) {
            markdownConfig.setSectionNumberBaseLevel(m_sectionNumberBaseLevelSpinBox->value());
        }

        if (m_sectionNumberStyleComboBox->isEnabled()) {
            auto style = m_sectionNumberStyleComboBox->currentData().toInt();
            markdownConfig.setSectionNumberStyle(static_cast<MarkdownEditorConfig::SectionNumberStyle>(style));
        }
    }

    markdownConfig.setConstrainImageWidthEnabled(m_constrainImageWidthCheckBox->isChecked());

    markdownConfig.setZoomFactorInReadMode(m_zoomFactorSpinBox->value());

    markdownConfig.setConstrainInPlacePreviewWidthEnabled(m_constrainInPlacePreviewWidthCheckBox->isChecked());

    markdownConfig.setFetchImagesInParseAndPaste(m_fetchImagesToLocalCheckBox->isChecked());

    markdownConfig.setHtmlTagEnabled(m_htmlTagCheckBox->isChecked());

    markdownConfig.setAutoBreakEnabled(m_autoBreakCheckBox->isChecked());

    markdownConfig.setLinkifyEnabled(m_linkifyCheckBox->isChecked());

    markdownConfig.setIndentFirstLineEnabled(m_indentFirstLineCheckBox->isChecked());

    markdownConfig.setSmartTableEnabled(m_smartTableCheckBox->isChecked());

    EditorPage::notifyEditorConfigChange();
}

QString MarkdownEditorPage::title() const
{
    return tr("Markdown Editor");
}

QGroupBox *MarkdownEditorPage::setupReadGroup()
{
    auto box = new QGroupBox(tr("Read"), this);
    auto layout = WidgetsFactory::createFormLayout(box);

    {
        const QString label(tr("Constrain image width"));
        m_constrainImageWidthCheckBox = WidgetsFactory::createCheckBox(label, box);
        m_constrainImageWidthCheckBox->setToolTip(tr("Constrain image width to the window"));
        layout->addRow(m_constrainImageWidthCheckBox);
        addSearchItem(label, m_constrainImageWidthCheckBox->toolTip(), m_constrainImageWidthCheckBox);
        connect(m_constrainImageWidthCheckBox, &QCheckBox::stateChanged,
                this, &MarkdownEditorPage::pageIsChanged);
    }

    {
        m_zoomFactorSpinBox = WidgetsFactory::createDoubleSpinBox(box);
        m_zoomFactorSpinBox->setToolTip(tr("Zoom factor in read mode"));

        m_zoomFactorSpinBox->setRange(0.25, 10);
        m_zoomFactorSpinBox->setSingleStep(0.25);

        const QString label(tr("Zoom factor:"));
        layout->addRow(label, m_zoomFactorSpinBox);
        addSearchItem(label, m_zoomFactorSpinBox->toolTip(), m_zoomFactorSpinBox);
        connect(m_zoomFactorSpinBox, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
                this, &MarkdownEditorPage::pageIsChanged);
    }

    {
        const QString label(tr("HTML tag"));
        m_htmlTagCheckBox = WidgetsFactory::createCheckBox(label, box);
        m_htmlTagCheckBox->setToolTip(tr("Allow HTML tags in source"));
        layout->addRow(m_htmlTagCheckBox);
        addSearchItem(label, m_htmlTagCheckBox->toolTip(), m_htmlTagCheckBox);
        connect(m_htmlTagCheckBox, &QCheckBox::stateChanged,
                this, &MarkdownEditorPage::pageIsChanged);
    }

    {
        const QString label(tr("Auto break"));
        m_autoBreakCheckBox = WidgetsFactory::createCheckBox(label, box);
        m_autoBreakCheckBox->setToolTip(tr("Automatically break a line with '\\n'"));
        layout->addRow(m_autoBreakCheckBox);
        addSearchItem(label, m_autoBreakCheckBox->toolTip(), m_autoBreakCheckBox);
        connect(m_autoBreakCheckBox, &QCheckBox::stateChanged,
                this, &MarkdownEditorPage::pageIsChanged);
    }

    {
        const QString label(tr("Linkify"));
        m_linkifyCheckBox = WidgetsFactory::createCheckBox(label, box);
        m_linkifyCheckBox->setToolTip(tr("Convert URL-like text to links"));
        layout->addRow(m_linkifyCheckBox);
        addSearchItem(label, m_linkifyCheckBox->toolTip(), m_linkifyCheckBox);
        connect(m_linkifyCheckBox, &QCheckBox::stateChanged,
                this, &MarkdownEditorPage::pageIsChanged);
    }

    {
        const QString label(tr("Indent first line"));
        m_indentFirstLineCheckBox = WidgetsFactory::createCheckBox(label, box);
        m_indentFirstLineCheckBox->setToolTip(tr("Indent the first line of each paragraph"));
        layout->addRow(m_indentFirstLineCheckBox);
        addSearchItem(label, m_indentFirstLineCheckBox->toolTip(), m_indentFirstLineCheckBox);
        connect(m_indentFirstLineCheckBox, &QCheckBox::stateChanged,
                this, &MarkdownEditorPage::pageIsChanged);
    }

    return box;
}

QGroupBox *MarkdownEditorPage::setupEditGroup()
{
    auto box = new QGroupBox(tr("Edit"), this);
    auto layout = WidgetsFactory::createFormLayout(box);

    {
        const QString label(tr("Insert file name as title"));
        m_insertFileNameAsTitleCheckBox = WidgetsFactory::createCheckBox(label, box);
        m_insertFileNameAsTitleCheckBox->setToolTip(tr("Insert file name as title when creating note"));
        layout->addRow(m_insertFileNameAsTitleCheckBox);
        addSearchItem(label, m_insertFileNameAsTitleCheckBox->toolTip(), m_insertFileNameAsTitleCheckBox);
        connect(m_insertFileNameAsTitleCheckBox, &QCheckBox::stateChanged,
                this, &MarkdownEditorPage::pageIsChanged);
    }

    {
        const QString label(tr("Constrain in-place preview width"));
        m_constrainInPlacePreviewWidthCheckBox = WidgetsFactory::createCheckBox(label, box);
        m_constrainInPlacePreviewWidthCheckBox->setToolTip(tr("Constrain in-place preview width to the window"));
        layout->addRow(m_constrainInPlacePreviewWidthCheckBox);
        addSearchItem(label, m_constrainInPlacePreviewWidthCheckBox->toolTip(), m_constrainInPlacePreviewWidthCheckBox);
        connect(m_constrainInPlacePreviewWidthCheckBox, &QCheckBox::stateChanged,
                this, &MarkdownEditorPage::pageIsChanged);
    }

    {
        const QString label(tr("Fetch images to local in Parse And Paste"));
        m_fetchImagesToLocalCheckBox = WidgetsFactory::createCheckBox(label, box);
        m_fetchImagesToLocalCheckBox->setToolTip(tr("Fetch images to local in Parse To Markdown And Paste"));
        layout->addRow(m_fetchImagesToLocalCheckBox);
        addSearchItem(label, m_fetchImagesToLocalCheckBox->toolTip(), m_fetchImagesToLocalCheckBox);
        connect(m_fetchImagesToLocalCheckBox, &QCheckBox::stateChanged,
                this, &MarkdownEditorPage::pageIsChanged);
    }

    {
        const QString label(tr("Smart table"));
        m_smartTableCheckBox = WidgetsFactory::createCheckBox(label, box);
        m_smartTableCheckBox->setToolTip(tr("Smart table formation"));
        layout->addRow(m_smartTableCheckBox);
        addSearchItem(label, m_smartTableCheckBox->toolTip(), m_smartTableCheckBox);
        connect(m_smartTableCheckBox, &QCheckBox::stateChanged,
                this, &MarkdownEditorPage::pageIsChanged);
    }

    return box;
}

QGroupBox *MarkdownEditorPage::setupGeneralGroup()
{
    auto box = new QGroupBox(tr("General"), this);
    auto layout = WidgetsFactory::createFormLayout(box);

    {
        auto sectionLayout = new QHBoxLayout();

        m_sectionNumberComboBox = WidgetsFactory::createComboBox(this);
        m_sectionNumberComboBox->setToolTip(tr("Section number mode"));
        m_sectionNumberComboBox->addItem(tr("None"), (int)MarkdownEditorConfig::SectionNumberMode::None);
        m_sectionNumberComboBox->addItem(tr("Read"), (int)MarkdownEditorConfig::SectionNumberMode::Read);
        m_sectionNumberComboBox->addItem(tr("Edit"), (int)MarkdownEditorConfig::SectionNumberMode::Edit);
        sectionLayout->addWidget(m_sectionNumberComboBox);
        connect(m_sectionNumberComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged),
                this, &MarkdownEditorPage::pageIsChanged);

        m_sectionNumberBaseLevelSpinBox = WidgetsFactory::createSpinBox(this);
        m_sectionNumberBaseLevelSpinBox->setToolTip(tr("Base level to start section numbering in edit mode"));
        m_sectionNumberBaseLevelSpinBox->setRange(1, 6);
        m_sectionNumberBaseLevelSpinBox->setSingleStep(1);
        sectionLayout->addWidget(m_sectionNumberBaseLevelSpinBox);
        connect(m_sectionNumberBaseLevelSpinBox, QOverload<int>::of(&QSpinBox::valueChanged),
                this, &MarkdownEditorPage::pageIsChanged);

        m_sectionNumberStyleComboBox = WidgetsFactory::createComboBox(this);
        m_sectionNumberStyleComboBox->setToolTip(tr("Section number style"));
        m_sectionNumberStyleComboBox->addItem(tr("1.1."), (int)MarkdownEditorConfig::SectionNumberStyle::DigDotDigDot);
        m_sectionNumberStyleComboBox->addItem(tr("1.1"), (int)MarkdownEditorConfig::SectionNumberStyle::DigDotDig);
        sectionLayout->addWidget(m_sectionNumberStyleComboBox);
        connect(m_sectionNumberStyleComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged),
                this, &MarkdownEditorPage::pageIsChanged);

        connect(m_sectionNumberComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged),
                this, [this](int p_index) {
                    m_sectionNumberBaseLevelSpinBox->setEnabled(p_index != MarkdownEditorConfig::SectionNumberMode::None);
                    m_sectionNumberStyleComboBox->setEnabled(p_index == MarkdownEditorConfig::SectionNumberMode::Edit);
                });

        const QString label(tr("Section number:"));
        layout->addRow(label, sectionLayout);
        addSearchItem(label, m_sectionNumberComboBox->toolTip(), m_sectionNumberComboBox);
    }

    return box;
}
