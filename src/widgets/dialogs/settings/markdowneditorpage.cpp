#include "markdowneditorpage.h"

#include <QFormLayout>
#include <QVBoxLayout>
#include <QCheckBox>
#include <QGroupBox>
#include <QDoubleSpinBox>
#include <QComboBox>
#include <QSpinBox>
#include <QHBoxLayout>
#include <QPushButton>
#include <QFileDialog>
#include <QFontComboBox>
#include <QFont>

#include <widgets/widgetsfactory.h>
#include <core/editorconfig.h>
#include <core/markdowneditorconfig.h>
#include <core/configmgr.h>
#include <utils/widgetutils.h>

#include "editorpage.h"
#include <widgets/locationinputwithbrowsebutton.h>
#include <widgets/messageboxhelper.h>
#include <widgets/editors/plantumlhelper.h>
#include <widgets/editors/graphvizhelper.h>
#include <widgets/lineedit.h>

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

    mainLayout->addStretch();
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

    m_imageAlignCenterCheckBox->setChecked(markdownConfig.getImageAlignCenterEnabled());

    m_zoomFactorSpinBox->setValue(markdownConfig.getZoomFactorInReadMode());

    m_constrainInplacePreviewWidthCheckBox->setChecked(markdownConfig.getConstrainInplacePreviewWidthEnabled());

    {
        auto srcs = markdownConfig.getInplacePreviewSources();
        m_inplacePreviewSourceImageLinkCheckBox->setChecked(srcs & MarkdownEditorConfig::InplacePreviewSource::ImageLink);
        m_inplacePreviewSourceCodeBlockCheckBox->setChecked(srcs & MarkdownEditorConfig::InplacePreviewSource::CodeBlock);
        m_inplacePreviewSourceMathCheckBox->setChecked(srcs & MarkdownEditorConfig::InplacePreviewSource::Math);
    }

    m_fetchImagesToLocalCheckBox->setChecked(markdownConfig.getFetchImagesInParseAndPaste());

    m_htmlTagCheckBox->setChecked(markdownConfig.getHtmlTagEnabled());

    m_autoBreakCheckBox->setChecked(markdownConfig.getAutoBreakEnabled());

    m_linkifyCheckBox->setChecked(markdownConfig.getLinkifyEnabled());

    m_indentFirstLineCheckBox->setChecked(markdownConfig.getIndentFirstLineEnabled());

    m_codeBlockLineNumberCheckBox->setChecked(markdownConfig.getCodeBlockLineNumberEnabled());

    m_smartTableCheckBox->setChecked(markdownConfig.getSmartTableEnabled());

    m_spellCheckCheckBox->setChecked(markdownConfig.isSpellCheckEnabled());

    {
        int idx = m_plantUmlModeComboBox->findData(markdownConfig.getWebPlantUml() ? 0 : 1);
        m_plantUmlModeComboBox->setCurrentIndex(idx);
    }

    m_plantUmlJarFileInput->setText(markdownConfig.getPlantUmlJar());

    m_plantUmlWebServiceLineEdit->setText(markdownConfig.getPlantUmlWebService());

    {
        int idx = m_graphvizModeComboBox->findData(markdownConfig.getWebGraphviz() ? 0 : 1);
        m_graphvizModeComboBox->setCurrentIndex(idx);
    }

    m_graphvizFileInput->setText(markdownConfig.getGraphvizExe());

    m_mathJaxScriptLineEdit->setText(markdownConfig.getMathJaxScript());

    {
        const auto &fontFamily = markdownConfig.getEditorOverriddenFontFamily();
        m_editorOverriddenFontFamilyCheckBox->setChecked(!fontFamily.isEmpty());
        if (!fontFamily.isEmpty()) {
            QFont font;
            font.setFamily(fontFamily);
            m_editorOverriddenFontFamilyComboBox->setCurrentFont(font);
        }
    }

    m_richPasteByDefaultCheckBox->setChecked(markdownConfig.getRichPasteByDefaultEnabled());
}

bool MarkdownEditorPage::saveInternal()
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

    markdownConfig.setImageAlignCenterEnabled(m_imageAlignCenterCheckBox->isChecked());

    markdownConfig.setZoomFactorInReadMode(m_zoomFactorSpinBox->value());

    markdownConfig.setConstrainInplacePreviewWidthEnabled(m_constrainInplacePreviewWidthCheckBox->isChecked());

    {
        MarkdownEditorConfig::InplacePreviewSources srcs = MarkdownEditorConfig::InplacePreviewSource::NoInplacePreview;
        if (m_inplacePreviewSourceImageLinkCheckBox->isChecked()) {
            srcs |= MarkdownEditorConfig::InplacePreviewSource::ImageLink;
        }
        if (m_inplacePreviewSourceCodeBlockCheckBox->isChecked()) {
            srcs |= MarkdownEditorConfig::InplacePreviewSource::CodeBlock;
        }
        if (m_inplacePreviewSourceMathCheckBox->isChecked()) {
            srcs |= MarkdownEditorConfig::InplacePreviewSource::Math;
        }

        markdownConfig.setInplacePreviewSources(srcs);
    }

    markdownConfig.setFetchImagesInParseAndPaste(m_fetchImagesToLocalCheckBox->isChecked());

    markdownConfig.setHtmlTagEnabled(m_htmlTagCheckBox->isChecked());

    markdownConfig.setAutoBreakEnabled(m_autoBreakCheckBox->isChecked());

    markdownConfig.setLinkifyEnabled(m_linkifyCheckBox->isChecked());

    markdownConfig.setIndentFirstLineEnabled(m_indentFirstLineCheckBox->isChecked());

    markdownConfig.setCodeBlockLineNumberEnabled(m_codeBlockLineNumberCheckBox->isChecked());

    markdownConfig.setSmartTableEnabled(m_smartTableCheckBox->isChecked());

    markdownConfig.setSpellCheckEnabled(m_spellCheckCheckBox->isChecked());

    markdownConfig.setWebPlantUml(m_plantUmlModeComboBox->currentData().toInt() == 0);

    markdownConfig.setPlantUmlJar(m_plantUmlJarFileInput->text());

    markdownConfig.setPlantUmlWebService(m_plantUmlWebServiceLineEdit->text());

    markdownConfig.setWebGraphviz(m_graphvizModeComboBox->currentData().toInt() == 0);

    markdownConfig.setGraphvizExe(m_graphvizFileInput->text());

    markdownConfig.setMathJaxScript(m_mathJaxScriptLineEdit->text());

    {
        bool checked = m_editorOverriddenFontFamilyCheckBox->isChecked();
        markdownConfig.setEditorOverriddenFontFamily(checked ? m_editorOverriddenFontFamilyComboBox->currentFont().family() : QString());
    }

    markdownConfig.setRichPasteByDefaultEnabled(m_richPasteByDefaultCheckBox->isChecked());

    EditorPage::notifyEditorConfigChange();

    return true;
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
        const QString label(tr("Center image"));
        m_imageAlignCenterCheckBox = WidgetsFactory::createCheckBox(label, box);
        m_imageAlignCenterCheckBox->setToolTip(tr("Center images"));
        layout->addRow(m_imageAlignCenterCheckBox);
        addSearchItem(label, m_imageAlignCenterCheckBox->toolTip(), m_imageAlignCenterCheckBox);
        connect(m_imageAlignCenterCheckBox, &QCheckBox::stateChanged,
                this, &MarkdownEditorPage::pageIsChanged);
    }

    {
        m_zoomFactorSpinBox = WidgetsFactory::createDoubleSpinBox(box);
        m_zoomFactorSpinBox->setToolTip(tr("Zoom factor in read mode"));

        m_zoomFactorSpinBox->setRange(0.1, 10);
        m_zoomFactorSpinBox->setSingleStep(0.1);

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

    {
        const QString label(tr("Code block line number"));
        m_codeBlockLineNumberCheckBox = WidgetsFactory::createCheckBox(label, box);
        m_codeBlockLineNumberCheckBox->setToolTip(tr("Add line number to code block"));
        layout->addRow(m_codeBlockLineNumberCheckBox);
        addSearchItem(label, m_codeBlockLineNumberCheckBox->toolTip(), m_codeBlockLineNumberCheckBox);
        connect(m_codeBlockLineNumberCheckBox, &QCheckBox::stateChanged,
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
        m_constrainInplacePreviewWidthCheckBox = WidgetsFactory::createCheckBox(label, box);
        m_constrainInplacePreviewWidthCheckBox->setToolTip(tr("Constrain in-place preview width to the window"));
        layout->addRow(m_constrainInplacePreviewWidthCheckBox);
        addSearchItem(label, m_constrainInplacePreviewWidthCheckBox->toolTip(), m_constrainInplacePreviewWidthCheckBox);
        connect(m_constrainInplacePreviewWidthCheckBox, &QCheckBox::stateChanged,
                this, &MarkdownEditorPage::pageIsChanged);
    }

    {
        auto srcLayout = new QVBoxLayout();
        layout->addRow(tr("In-place preview sources:"), srcLayout);

        m_inplacePreviewSourceImageLinkCheckBox = WidgetsFactory::createCheckBox(tr("Image link"), box);
        srcLayout->addWidget(m_inplacePreviewSourceImageLinkCheckBox);
        connect(m_inplacePreviewSourceImageLinkCheckBox, &QCheckBox::stateChanged,
                this, &MarkdownEditorPage::pageIsChanged);

        m_inplacePreviewSourceCodeBlockCheckBox = WidgetsFactory::createCheckBox(tr("Code block"), box);
        srcLayout->addWidget(m_inplacePreviewSourceCodeBlockCheckBox);
        connect(m_inplacePreviewSourceCodeBlockCheckBox, &QCheckBox::stateChanged,
                this, &MarkdownEditorPage::pageIsChanged);

        m_inplacePreviewSourceMathCheckBox = WidgetsFactory::createCheckBox(tr("Math"), box);
        srcLayout->addWidget(m_inplacePreviewSourceMathCheckBox);
        connect(m_inplacePreviewSourceMathCheckBox, &QCheckBox::stateChanged,
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

    {
        const QString label(tr("Spell check"));
        m_spellCheckCheckBox = WidgetsFactory::createCheckBox(label, box);
        m_spellCheckCheckBox->setToolTip(tr("Spell check"));
        layout->addRow(m_spellCheckCheckBox);
        addSearchItem(label, m_spellCheckCheckBox->toolTip(), m_spellCheckCheckBox);
        connect(m_spellCheckCheckBox, &QCheckBox::stateChanged,
                this, &MarkdownEditorPage::pageIsChanged);
    }

    {
        auto fontLayout = new QHBoxLayout();
        fontLayout->setContentsMargins(0, 0, 0, 0);

        const QString label(tr("Override font"));
        m_editorOverriddenFontFamilyCheckBox = WidgetsFactory::createCheckBox(label, box);
        m_editorOverriddenFontFamilyCheckBox->setToolTip(tr("Override editor font family of theme"));
        fontLayout->addWidget(m_editorOverriddenFontFamilyCheckBox);
        addSearchItem(label, m_editorOverriddenFontFamilyCheckBox->toolTip(), m_editorOverriddenFontFamilyCheckBox);

        m_editorOverriddenFontFamilyComboBox = new QFontComboBox(box);
        m_editorOverriddenFontFamilyComboBox->setEnabled(false);
        fontLayout->addWidget(m_editorOverriddenFontFamilyComboBox);
        connect(m_editorOverriddenFontFamilyComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged),
                this, &MarkdownEditorPage::pageIsChanged);

        fontLayout->addStretch();

        connect(m_editorOverriddenFontFamilyCheckBox, &QCheckBox::stateChanged,
                this, [this](int state) {
                    m_editorOverriddenFontFamilyComboBox->setEnabled(state == Qt::Checked);
                    emit pageIsChanged();
                });

        layout->addRow(fontLayout);
    }

    {
        const QString label(tr("Use Rich Paste by default"));
        m_richPasteByDefaultCheckBox = WidgetsFactory::createCheckBox(label, box);
        m_richPasteByDefaultCheckBox->setToolTip(tr("Use Rich Paste by default when pasting text"));
        layout->addRow(m_richPasteByDefaultCheckBox);
        addSearchItem(label, m_richPasteByDefaultCheckBox->toolTip(), m_richPasteByDefaultCheckBox);
        connect(m_richPasteByDefaultCheckBox, &QCheckBox::stateChanged,
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
        sectionLayout->setContentsMargins(0, 0, 0, 0);

        m_sectionNumberComboBox = WidgetsFactory::createComboBox(box);
        m_sectionNumberComboBox->setToolTip(tr("Section number mode"));
        m_sectionNumberComboBox->addItem(tr("None"), (int)MarkdownEditorConfig::SectionNumberMode::None);
        m_sectionNumberComboBox->addItem(tr("Read"), (int)MarkdownEditorConfig::SectionNumberMode::Read);
        m_sectionNumberComboBox->addItem(tr("Edit"), (int)MarkdownEditorConfig::SectionNumberMode::Edit);
        sectionLayout->addWidget(m_sectionNumberComboBox);
        connect(m_sectionNumberComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged),
                this, &MarkdownEditorPage::pageIsChanged);

        m_sectionNumberBaseLevelSpinBox = WidgetsFactory::createSpinBox(box);
        m_sectionNumberBaseLevelSpinBox->setToolTip(tr("Base level to start section numbering in edit mode"));
        m_sectionNumberBaseLevelSpinBox->setRange(1, 6);
        m_sectionNumberBaseLevelSpinBox->setSingleStep(1);
        m_sectionNumberBaseLevelSpinBox->setEnabled(false);
        sectionLayout->addWidget(m_sectionNumberBaseLevelSpinBox);
        connect(m_sectionNumberBaseLevelSpinBox, QOverload<int>::of(&QSpinBox::valueChanged),
                this, &MarkdownEditorPage::pageIsChanged);

        m_sectionNumberStyleComboBox = WidgetsFactory::createComboBox(box);
        m_sectionNumberStyleComboBox->setToolTip(tr("Section number style"));
        m_sectionNumberStyleComboBox->addItem(tr("1.1."), (int)MarkdownEditorConfig::SectionNumberStyle::DigDotDigDot);
        m_sectionNumberStyleComboBox->addItem(tr("1.1"), (int)MarkdownEditorConfig::SectionNumberStyle::DigDotDig);
        m_sectionNumberStyleComboBox->setEnabled(false);
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

    {
        m_plantUmlModeComboBox = WidgetsFactory::createComboBox(box);
        m_plantUmlModeComboBox->setToolTip(tr("Use Web service or local JAR file to render PlantUml graphs"));

        m_plantUmlModeComboBox->addItem(tr("Web Service"), 0);
        m_plantUmlModeComboBox->addItem(tr("Local JAR"), 1);

        const QString label(tr("PlantUml:"));
        layout->addRow(label, m_plantUmlModeComboBox);
        addSearchItem(label, m_plantUmlModeComboBox->toolTip(), m_plantUmlModeComboBox);
        connect(m_plantUmlModeComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged),
                this, &MarkdownEditorPage::pageIsChanged);
    }

    {
        auto jarLayout = new QHBoxLayout();
        jarLayout->setContentsMargins(0, 0, 0, 0);

        m_plantUmlJarFileInput = new LocationInputWithBrowseButton(box);
        m_plantUmlJarFileInput->setToolTip(tr("Local JAR file to render PlantUml graphs"));
        connect(m_plantUmlJarFileInput, &LocationInputWithBrowseButton::clicked,
                this, [this]() {
                    auto filePath = QFileDialog::getOpenFileName(this,
                                                                 tr("Select PlantUml JAR File"),
                                                                 QDir::homePath(),
                                                                 "PlantUml JAR (*.jar)");
                    if (!filePath.isEmpty()) {
                        m_plantUmlJarFileInput->setText(filePath);
                    }
                });
        jarLayout->addWidget(m_plantUmlJarFileInput, 1);

        auto testBtn = new QPushButton(tr("Test"), box);
        testBtn->setToolTip(tr("Test PlantUml JAR and Java Runtime Environment"));
        connect(testBtn, &QPushButton::clicked,
                this, [this]() {
                    const auto jar = m_plantUmlJarFileInput->text();
                    if (jar.isEmpty() || !QFileInfo::exists(jar)) {
                        MessageBoxHelper::notify(MessageBoxHelper::Warning,
                                                 tr("The JAR file (%1) specified does not exist.").arg(jar),
                                                 this);
                        return;
                    }

                    auto testRet = PlantUmlHelper::testPlantUml(jar);
                    MessageBoxHelper::notify(MessageBoxHelper::Information,
                                             tr("Test %1.").arg(testRet.first ? tr("succeeded") : tr("failed")),
                                             QString(),
                                             testRet.second,
                                             this);
                });
        jarLayout->addWidget(testBtn);

        const QString label(tr("PlantUml JAR file:"));
        layout->addRow(label, jarLayout);
        addSearchItem(label, m_plantUmlJarFileInput->toolTip(), m_plantUmlJarFileInput);
        connect(m_plantUmlJarFileInput, &LocationInputWithBrowseButton::textChanged,
                this, &MarkdownEditorPage::pageIsChanged);
    }

    {
        m_plantUmlWebServiceLineEdit = WidgetsFactory::createLineEdit(box);
        m_plantUmlWebServiceLineEdit->setToolTip(tr("Override the Web service used to render PlantUml graphs"));
        m_plantUmlWebServiceLineEdit->setPlaceholderText(tr("Empty to use default one"));

        const QString label(tr("Override PlantUml Web service:"));
        layout->addRow(label, m_plantUmlWebServiceLineEdit);
        addSearchItem(label, m_plantUmlWebServiceLineEdit->toolTip(), m_plantUmlWebServiceLineEdit);
        connect(m_plantUmlWebServiceLineEdit, &QLineEdit::textChanged,
                this, &MarkdownEditorPage::pageIsChanged);
    }

    {
        m_graphvizModeComboBox = WidgetsFactory::createComboBox(box);
        m_graphvizModeComboBox->setToolTip(tr("Use Web service or local executable file to render Graphviz graphs"));

        m_graphvizModeComboBox->addItem(tr("Web Service"), 0);
        m_graphvizModeComboBox->addItem(tr("Local Executable"), 1);

        const QString label(tr("Graphviz:"));
        layout->addRow(label, m_graphvizModeComboBox);
        addSearchItem(label, m_graphvizModeComboBox->toolTip(), m_graphvizModeComboBox);
        connect(m_graphvizModeComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged),
                this, &MarkdownEditorPage::pageIsChanged);
    }

    {
        auto fileLayout = new QHBoxLayout();
        fileLayout->setContentsMargins(0, 0, 0, 0);

        m_graphvizFileInput = new LocationInputWithBrowseButton(box);
        m_graphvizFileInput->setToolTip(tr("Local executable file to render Graphviz graphs"));
        connect(m_graphvizFileInput, &LocationInputWithBrowseButton::clicked,
                this, [this]() {
                    auto filePath = QFileDialog::getOpenFileName(this,
                                                                 tr("Select Graphviz Executable File"),
                                                                 QDir::homePath());
                    if (!filePath.isEmpty()) {
                        m_graphvizFileInput->setText(filePath);
                    }
                });
        fileLayout->addWidget(m_graphvizFileInput, 1);

        auto testBtn = new QPushButton(tr("Test"), box);
        testBtn->setToolTip(tr("Test Graphviz executable file"));
        connect(testBtn, &QPushButton::clicked,
                this, [this]() {
                    const auto exe = m_graphvizFileInput->text();
                    if (exe.isEmpty() || !QFileInfo::exists(exe)) {
                        MessageBoxHelper::notify(MessageBoxHelper::Warning,
                                                 tr("The executable file (%1) specified does not exist.").arg(exe),
                                                 this);
                        return;
                    }

                    auto testRet = GraphvizHelper::testGraphviz(exe);
                    MessageBoxHelper::notify(MessageBoxHelper::Information,
                                             tr("Test %1.").arg(testRet.first ? tr("succeeded") : tr("failed")),
                                             QString(),
                                             testRet.second,
                                             this);
                });
        fileLayout->addWidget(testBtn);

        const QString label(tr("Graphviz executable file:"));
        layout->addRow(label, fileLayout);
        addSearchItem(label, m_graphvizFileInput->toolTip(), m_graphvizFileInput);
        connect(m_graphvizFileInput, &LocationInputWithBrowseButton::textChanged,
                this, &MarkdownEditorPage::pageIsChanged);
    }

    {
        m_mathJaxScriptLineEdit = WidgetsFactory::createLineEdit(box);
        m_mathJaxScriptLineEdit->setToolTip(tr("Override the MathJax script used to render math formulas"));
        m_mathJaxScriptLineEdit->setPlaceholderText(tr("Empty to use default one"));

        const QString label(tr("Override MathJax script:"));
        layout->addRow(label, m_mathJaxScriptLineEdit);
        addSearchItem(label, m_mathJaxScriptLineEdit->toolTip(), m_mathJaxScriptLineEdit);
        connect(m_mathJaxScriptLineEdit, &QLineEdit::textChanged,
                this, &MarkdownEditorPage::pageIsChanged);
    }

    return box;
}
