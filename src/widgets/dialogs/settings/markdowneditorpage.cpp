#include "markdowneditorpage.h"

#include <QFormLayout>
#include <QCheckBox>

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
    auto mainLayout = new QFormLayout(this);

    {
        const QString label(tr("Insert file name as title"));
        m_insertFileNameAsTitleCheckBox = WidgetsFactory::createCheckBox(label, this);
        m_insertFileNameAsTitleCheckBox->setToolTip(tr("Insert file name as title when creating note"));
        mainLayout->addRow(m_insertFileNameAsTitleCheckBox);
        addSearchItem(label, m_insertFileNameAsTitleCheckBox->toolTip(), m_insertFileNameAsTitleCheckBox);
        connect(m_insertFileNameAsTitleCheckBox, &QCheckBox::stateChanged,
                this, &MarkdownEditorPage::pageIsChanged);
    }

    {
        const QString label(tr("Section number"));
        m_sectionNumberCheckBox = WidgetsFactory::createCheckBox(label, this);
        m_sectionNumberCheckBox->setToolTip(tr("Display section number of headings in read mode"));
        mainLayout->addRow(m_sectionNumberCheckBox);
        addSearchItem(label, m_sectionNumberCheckBox->toolTip(), m_sectionNumberCheckBox);
        connect(m_sectionNumberCheckBox, &QCheckBox::stateChanged,
                this, &MarkdownEditorPage::pageIsChanged);
    }

    {
        const QString label(tr("Constrain image width"));
        m_constrainImageWidthCheckBox = WidgetsFactory::createCheckBox(label, this);
        m_constrainImageWidthCheckBox->setToolTip(tr("Constrain image width to the window"));
        mainLayout->addRow(m_constrainImageWidthCheckBox);
        addSearchItem(label, m_constrainImageWidthCheckBox->toolTip(), m_constrainImageWidthCheckBox);
        connect(m_constrainImageWidthCheckBox, &QCheckBox::stateChanged,
                this, &MarkdownEditorPage::pageIsChanged);
    }

    {
        const QString label(tr("Constrain in-place preview width"));
        m_constrainInPlacePreviewWidthCheckBox = WidgetsFactory::createCheckBox(label, this);
        m_constrainInPlacePreviewWidthCheckBox->setToolTip(tr("Constrain in-place preview width to the window"));
        mainLayout->addRow(m_constrainInPlacePreviewWidthCheckBox);
        addSearchItem(label, m_constrainInPlacePreviewWidthCheckBox->toolTip(), m_constrainInPlacePreviewWidthCheckBox);
        connect(m_constrainInPlacePreviewWidthCheckBox, &QCheckBox::stateChanged,
                this, &MarkdownEditorPage::pageIsChanged);
    }

    {
        const QString label(tr("Fetch images to local in Parse And Paste"));
        m_fetchImagesToLocalCheckBox = WidgetsFactory::createCheckBox(label, this);
        m_fetchImagesToLocalCheckBox->setToolTip(tr("Fetch images to local in Parse To Markdown And Paste"));
        mainLayout->addRow(m_fetchImagesToLocalCheckBox);
        addSearchItem(label, m_fetchImagesToLocalCheckBox->toolTip(), m_fetchImagesToLocalCheckBox);
        connect(m_fetchImagesToLocalCheckBox, &QCheckBox::stateChanged,
                this, &MarkdownEditorPage::pageIsChanged);
    }
}

void MarkdownEditorPage::loadInternal()
{
    const auto &markdownConfig = ConfigMgr::getInst().getEditorConfig().getMarkdownEditorConfig();

    m_insertFileNameAsTitleCheckBox->setChecked(markdownConfig.getInsertFileNameAsTitle());

    m_sectionNumberCheckBox->setChecked(markdownConfig.getSectionNumberEnabled());

    m_constrainImageWidthCheckBox->setChecked(markdownConfig.getConstrainImageWidthEnabled());

    m_constrainInPlacePreviewWidthCheckBox->setChecked(markdownConfig.getConstrainInPlacePreviewWidthEnabled());

    m_fetchImagesToLocalCheckBox->setChecked(markdownConfig.getFetchImagesInParseAndPaste());
}

void MarkdownEditorPage::saveInternal()
{
    auto &markdownConfig = ConfigMgr::getInst().getEditorConfig().getMarkdownEditorConfig();

    markdownConfig.setInsertFileNameAsTitle(m_insertFileNameAsTitleCheckBox->isChecked());

    markdownConfig.setSectionNumberEnabled(m_sectionNumberCheckBox->isChecked());

    markdownConfig.setConstrainImageWidthEnabled(m_constrainImageWidthCheckBox->isChecked());

    markdownConfig.setConstrainInPlacePreviewWidthEnabled(m_constrainInPlacePreviewWidthCheckBox->isChecked());

    markdownConfig.setFetchImagesInParseAndPaste(m_fetchImagesToLocalCheckBox->isChecked());

    EditorPage::notifyEditorConfigChange();
}

QString MarkdownEditorPage::title() const
{
    return tr("Markdown Editor");
}
