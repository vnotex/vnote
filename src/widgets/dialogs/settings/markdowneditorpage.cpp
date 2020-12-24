#include "markdowneditorpage.h"

#include <QFormLayout>
#include <QVBoxLayout>
#include <QCheckBox>
#include <QGroupBox>
#include <QDoubleSpinBox>

#include <widgets/widgetsfactory.h>
#include <core/editorconfig.h>
#include <core/markdowneditorconfig.h>
#include <core/configmgr.h>

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

    auto readBox = setupReadGroup();
    mainLayout->addWidget(readBox);

    auto editBox = setupEditGroup();
    mainLayout->addWidget(editBox);
}

void MarkdownEditorPage::loadInternal()
{
    const auto &markdownConfig = ConfigMgr::getInst().getEditorConfig().getMarkdownEditorConfig();

    m_insertFileNameAsTitleCheckBox->setChecked(markdownConfig.getInsertFileNameAsTitle());

    m_sectionNumberCheckBox->setChecked(markdownConfig.getSectionNumberEnabled());

    m_constrainImageWidthCheckBox->setChecked(markdownConfig.getConstrainImageWidthEnabled());

    m_zoomFactorSpinBox->setValue(markdownConfig.getZoomFactorInReadMode());

    m_constrainInPlacePreviewWidthCheckBox->setChecked(markdownConfig.getConstrainInPlacePreviewWidthEnabled());

    m_fetchImagesToLocalCheckBox->setChecked(markdownConfig.getFetchImagesInParseAndPaste());

    m_htmlTagCheckBox->setChecked(markdownConfig.getHtmlTagEnabled());

    m_autoBreakCheckBox->setChecked(markdownConfig.getAutoBreakEnabled());

    m_linkifyCheckBox->setChecked(markdownConfig.getLinkifyEnabled());
}

void MarkdownEditorPage::saveInternal()
{
    auto &markdownConfig = ConfigMgr::getInst().getEditorConfig().getMarkdownEditorConfig();

    markdownConfig.setInsertFileNameAsTitle(m_insertFileNameAsTitleCheckBox->isChecked());

    markdownConfig.setSectionNumberEnabled(m_sectionNumberCheckBox->isChecked());

    markdownConfig.setConstrainImageWidthEnabled(m_constrainImageWidthCheckBox->isChecked());

    markdownConfig.setZoomFactorInReadMode(m_zoomFactorSpinBox->value());

    markdownConfig.setConstrainInPlacePreviewWidthEnabled(m_constrainInPlacePreviewWidthCheckBox->isChecked());

    markdownConfig.setFetchImagesInParseAndPaste(m_fetchImagesToLocalCheckBox->isChecked());

    markdownConfig.setHtmlTagEnabled(m_htmlTagCheckBox->isChecked());

    markdownConfig.setAutoBreakEnabled(m_autoBreakCheckBox->isChecked());

    markdownConfig.setLinkifyEnabled(m_linkifyCheckBox->isChecked());

    EditorPage::notifyEditorConfigChange();
}

QString MarkdownEditorPage::title() const
{
    return tr("Markdown Editor");
}

QGroupBox *MarkdownEditorPage::setupReadGroup()
{
    auto box = new QGroupBox(tr("Read"), this);
    auto layout = new QFormLayout(box);

    {
        const QString label(tr("Section number"));
        m_sectionNumberCheckBox = WidgetsFactory::createCheckBox(label, box);
        m_sectionNumberCheckBox->setToolTip(tr("Display section number of headings in read mode"));
        layout->addRow(m_sectionNumberCheckBox);
        addSearchItem(label, m_sectionNumberCheckBox->toolTip(), m_sectionNumberCheckBox);
        connect(m_sectionNumberCheckBox, &QCheckBox::stateChanged,
                this, &MarkdownEditorPage::pageIsChanged);
    }

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

    return box;
}

QGroupBox *MarkdownEditorPage::setupEditGroup()
{
    auto box = new QGroupBox(tr("Edit"), this);
    auto layout = new QFormLayout(box);

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

    return box;
}
