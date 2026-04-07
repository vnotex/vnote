#include "markdowneditorpage.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFileInfo>
#include <QFont>
#include <QFontComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

#include <core/configmgr2.h>
#include <core/editorconfig.h>
#include <core/markdowneditorconfig.h>
#include <core/servicelocator.h>
#include <core/services/hookmanager.h>
#include <utils/widgetutils.h>
#include <widgets/widgetsfactory.h>

#include "editorpage.h"
#include "settingspagehelper.h"
#include <widgets/editors/graphvizhelper.h>
#include <widgets/editors/plantumlhelper.h>
#include <widgets/lineedit.h>
#include <widgets/locationinputwithbrowsebutton.h>
#include <widgets/messageboxhelper.h>
#include <widgets/propertydefs.h>

using namespace vnotex;

MarkdownEditorPage::MarkdownEditorPage(ServiceLocator &p_services, QWidget *p_parent)
    : SettingsPage(p_services, p_parent) {
  setupUI();
}

void MarkdownEditorPage::setupUI() {
  auto *mainLayout = new QVBoxLayout(this);

  setupGeneralGroup();
  setupReadGroup();
  setupEditGroup();

  mainLayout->addStretch();
}

void MarkdownEditorPage::loadInternal() {
  const auto &markdownConfig = m_services.get<ConfigMgr2>()->getEditorConfig().getMarkdownEditorConfig();

  m_insertFileNameAsTitleCheckBox->setChecked(markdownConfig.getInsertFileNameAsTitle());

  {
    int idx =
        m_sectionNumberComboBox->findData(static_cast<int>(markdownConfig.getSectionNumberMode()));
    Q_ASSERT(idx != -1);
    m_sectionNumberComboBox->setCurrentIndex(idx);

    m_sectionNumberBaseLevelSpinBox->setValue(markdownConfig.getSectionNumberBaseLevel());

    idx = m_sectionNumberStyleComboBox->findData(
        static_cast<int>(markdownConfig.getSectionNumberStyle()));
    Q_ASSERT(idx != -1);
    m_sectionNumberStyleComboBox->setCurrentIndex(idx);
  }

  m_constrainImageWidthCheckBox->setChecked(markdownConfig.getConstrainImageWidthEnabled());

  m_imageAlignCenterCheckBox->setChecked(markdownConfig.getImageAlignCenterEnabled());

  m_zoomFactorSpinBox->setValue(markdownConfig.getZoomFactorInReadMode());

  m_constrainInplacePreviewWidthCheckBox->setChecked(
      markdownConfig.getConstrainInplacePreviewWidthEnabled());

  {
    auto srcs = markdownConfig.getInplacePreviewSources();
    m_inplacePreviewSourceImageLinkCheckBox->setChecked(
        srcs & MarkdownEditorConfig::InplacePreviewSource::ImageLink);
    m_inplacePreviewSourceCodeBlockCheckBox->setChecked(
        srcs & MarkdownEditorConfig::InplacePreviewSource::CodeBlock);
    m_inplacePreviewSourceMathCheckBox->setChecked(
        srcs & MarkdownEditorConfig::InplacePreviewSource::Math);
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

bool MarkdownEditorPage::saveInternal() {
  auto &markdownConfig = m_services.get<ConfigMgr2>()->getEditorConfig().getMarkdownEditorConfig();

  markdownConfig.setInsertFileNameAsTitle(m_insertFileNameAsTitleCheckBox->isChecked());

  {
    auto mode = m_sectionNumberComboBox->currentData().toInt();
    markdownConfig.setSectionNumberMode(static_cast<MarkdownEditorConfig::SectionNumberMode>(mode));

    if (m_sectionNumberBaseLevelSpinBox->isEnabled()) {
      markdownConfig.setSectionNumberBaseLevel(m_sectionNumberBaseLevelSpinBox->value());
    }

    if (m_sectionNumberStyleComboBox->isEnabled()) {
      auto style = m_sectionNumberStyleComboBox->currentData().toInt();
      markdownConfig.setSectionNumberStyle(
          static_cast<MarkdownEditorConfig::SectionNumberStyle>(style));
    }
  }

  markdownConfig.setConstrainImageWidthEnabled(m_constrainImageWidthCheckBox->isChecked());

  markdownConfig.setImageAlignCenterEnabled(m_imageAlignCenterCheckBox->isChecked());

  markdownConfig.setZoomFactorInReadMode(m_zoomFactorSpinBox->value());

  markdownConfig.setConstrainInplacePreviewWidthEnabled(
      m_constrainInplacePreviewWidthCheckBox->isChecked());

  {
    MarkdownEditorConfig::InplacePreviewSources srcs =
        MarkdownEditorConfig::InplacePreviewSource::NoInplacePreview;
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
    markdownConfig.setEditorOverriddenFontFamily(
        checked ? m_editorOverriddenFontFamilyComboBox->currentFont().family() : QString());
  }

  markdownConfig.setRichPasteByDefaultEnabled(m_richPasteByDefaultCheckBox->isChecked());

  EditorPage::notifyEditorConfigChange(m_services.get<HookManager>());

  return true;
}

QString MarkdownEditorPage::title() const { return tr("Markdown Editor"); }

void MarkdownEditorPage::setupReadGroup() {
  auto *mainLayout = qobject_cast<QVBoxLayout *>(layout());
  auto *cardLayout = SettingsPageHelper::addSection(mainLayout, tr("Read"), QString(), this);

  {
    const QString label(tr("Constrain image width"));
    m_constrainImageWidthCheckBox = WidgetsFactory::createCheckBox(label, this);
    m_constrainImageWidthCheckBox->setToolTip(tr("Constrain image width to the window"));
    cardLayout->addWidget(SettingsPageHelper::createCheckBoxRow(
        m_constrainImageWidthCheckBox, m_constrainImageWidthCheckBox->toolTip(), this));
    addSearchItem(label, m_constrainImageWidthCheckBox->toolTip(), m_constrainImageWidthCheckBox);
    connect(m_constrainImageWidthCheckBox, &QCheckBox::stateChanged, this,
            &MarkdownEditorPage::pageIsChanged);
  }

  {
    const QString label(tr("Center image"));
    m_imageAlignCenterCheckBox = WidgetsFactory::createCheckBox(label, this);
    m_imageAlignCenterCheckBox->setToolTip(tr("Center images"));
    cardLayout->addWidget(SettingsPageHelper::createSeparator(this));
    cardLayout->addWidget(SettingsPageHelper::createCheckBoxRow(
        m_imageAlignCenterCheckBox, m_imageAlignCenterCheckBox->toolTip(), this));
    addSearchItem(label, m_imageAlignCenterCheckBox->toolTip(), m_imageAlignCenterCheckBox);
    connect(m_imageAlignCenterCheckBox, &QCheckBox::stateChanged, this,
            &MarkdownEditorPage::pageIsChanged);
  }

  {
    m_zoomFactorSpinBox = WidgetsFactory::createDoubleSpinBox(this);
    m_zoomFactorSpinBox->setToolTip(tr("Zoom factor in read mode"));

    m_zoomFactorSpinBox->setRange(0.1, 10);
    m_zoomFactorSpinBox->setSingleStep(0.1);

    const QString label(tr("Zoom factor:"));
    cardLayout->addWidget(SettingsPageHelper::createSeparator(this));
    cardLayout->addWidget(SettingsPageHelper::createSettingRow(
        label, m_zoomFactorSpinBox->toolTip(), m_zoomFactorSpinBox, this));
    addSearchItem(label, m_zoomFactorSpinBox->toolTip(), m_zoomFactorSpinBox);
    connect(m_zoomFactorSpinBox, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
            &MarkdownEditorPage::pageIsChanged);
  }

  {
    const QString label(tr("HTML tag"));
    m_htmlTagCheckBox = WidgetsFactory::createCheckBox(label, this);
    m_htmlTagCheckBox->setToolTip(tr("Allow HTML tags in source"));
    cardLayout->addWidget(SettingsPageHelper::createSeparator(this));
    cardLayout->addWidget(SettingsPageHelper::createCheckBoxRow(
        m_htmlTagCheckBox, m_htmlTagCheckBox->toolTip(), this));
    addSearchItem(label, m_htmlTagCheckBox->toolTip(), m_htmlTagCheckBox);
    connect(m_htmlTagCheckBox, &QCheckBox::stateChanged, this, &MarkdownEditorPage::pageIsChanged);
  }

  {
    const QString label(tr("Auto break"));
    m_autoBreakCheckBox = WidgetsFactory::createCheckBox(label, this);
    m_autoBreakCheckBox->setToolTip(tr("Automatically break a line with '\\n'"));
    cardLayout->addWidget(SettingsPageHelper::createSeparator(this));
    cardLayout->addWidget(SettingsPageHelper::createCheckBoxRow(
        m_autoBreakCheckBox, m_autoBreakCheckBox->toolTip(), this));
    addSearchItem(label, m_autoBreakCheckBox->toolTip(), m_autoBreakCheckBox);
    connect(m_autoBreakCheckBox, &QCheckBox::stateChanged, this,
            &MarkdownEditorPage::pageIsChanged);
  }

  {
    const QString label(tr("Linkify"));
    m_linkifyCheckBox = WidgetsFactory::createCheckBox(label, this);
    m_linkifyCheckBox->setToolTip(tr("Convert URL-like text to links"));
    cardLayout->addWidget(SettingsPageHelper::createSeparator(this));
    cardLayout->addWidget(SettingsPageHelper::createCheckBoxRow(
        m_linkifyCheckBox, m_linkifyCheckBox->toolTip(), this));
    addSearchItem(label, m_linkifyCheckBox->toolTip(), m_linkifyCheckBox);
    connect(m_linkifyCheckBox, &QCheckBox::stateChanged, this, &MarkdownEditorPage::pageIsChanged);
  }

  {
    const QString label(tr("Indent first line"));
    m_indentFirstLineCheckBox = WidgetsFactory::createCheckBox(label, this);
    m_indentFirstLineCheckBox->setToolTip(tr("Indent the first line of each paragraph"));
    cardLayout->addWidget(SettingsPageHelper::createSeparator(this));
    cardLayout->addWidget(SettingsPageHelper::createCheckBoxRow(
        m_indentFirstLineCheckBox, m_indentFirstLineCheckBox->toolTip(), this));
    addSearchItem(label, m_indentFirstLineCheckBox->toolTip(), m_indentFirstLineCheckBox);
    connect(m_indentFirstLineCheckBox, &QCheckBox::stateChanged, this,
            &MarkdownEditorPage::pageIsChanged);
  }

  {
    const QString label(tr("Code block line number"));
    m_codeBlockLineNumberCheckBox = WidgetsFactory::createCheckBox(label, this);
    m_codeBlockLineNumberCheckBox->setToolTip(tr("Add line number to code block"));
    cardLayout->addWidget(SettingsPageHelper::createSeparator(this));
    cardLayout->addWidget(SettingsPageHelper::createCheckBoxRow(
        m_codeBlockLineNumberCheckBox, m_codeBlockLineNumberCheckBox->toolTip(), this));
    addSearchItem(label, m_codeBlockLineNumberCheckBox->toolTip(), m_codeBlockLineNumberCheckBox);
    connect(m_codeBlockLineNumberCheckBox, &QCheckBox::stateChanged, this,
            &MarkdownEditorPage::pageIsChanged);
  }
}

void MarkdownEditorPage::setupEditGroup() {
  auto *mainLayout = qobject_cast<QVBoxLayout *>(layout());
  auto *cardLayout = SettingsPageHelper::addSection(mainLayout, tr("Edit"), QString(), this);

  {
    const QString label(tr("Insert file name as title"));
    m_insertFileNameAsTitleCheckBox = WidgetsFactory::createCheckBox(label, this);
    m_insertFileNameAsTitleCheckBox->setToolTip(tr("Insert file name as title when creating note"));
    cardLayout->addWidget(SettingsPageHelper::createCheckBoxRow(
        m_insertFileNameAsTitleCheckBox, m_insertFileNameAsTitleCheckBox->toolTip(), this));
    addSearchItem(label, m_insertFileNameAsTitleCheckBox->toolTip(),
                  m_insertFileNameAsTitleCheckBox);
    connect(m_insertFileNameAsTitleCheckBox, &QCheckBox::stateChanged, this,
            &MarkdownEditorPage::pageIsChanged);
  }

  {
    const QString label(tr("Constrain in-place preview width"));
    m_constrainInplacePreviewWidthCheckBox = WidgetsFactory::createCheckBox(label, this);
    m_constrainInplacePreviewWidthCheckBox->setToolTip(
        tr("Constrain in-place preview width to the window"));
    cardLayout->addWidget(SettingsPageHelper::createSeparator(this));
    cardLayout->addWidget(SettingsPageHelper::createCheckBoxRow(
        m_constrainInplacePreviewWidthCheckBox,
        m_constrainInplacePreviewWidthCheckBox->toolTip(), this));
    addSearchItem(label, m_constrainInplacePreviewWidthCheckBox->toolTip(),
                  m_constrainInplacePreviewWidthCheckBox);
    connect(m_constrainInplacePreviewWidthCheckBox, &QCheckBox::stateChanged, this,
            &MarkdownEditorPage::pageIsChanged);
  }

  {
    auto *srcRow = new QWidget(this);
    srcRow->setProperty(PropertyDefs::c_settingsRow, true);
    auto *srcRowLayout = new QVBoxLayout(srcRow);
    srcRowLayout->setContentsMargins(16, 6, 16, 6);
    srcRowLayout->setSpacing(4);
    auto *srcLabel = new QLabel(tr("In-place preview sources:"), srcRow);
    srcRowLayout->addWidget(srcLabel);

    m_inplacePreviewSourceImageLinkCheckBox = WidgetsFactory::createCheckBox(tr("Image link"), this);
    srcRowLayout->addWidget(m_inplacePreviewSourceImageLinkCheckBox);
    connect(m_inplacePreviewSourceImageLinkCheckBox, &QCheckBox::stateChanged, this,
            &MarkdownEditorPage::pageIsChanged);

    m_inplacePreviewSourceCodeBlockCheckBox = WidgetsFactory::createCheckBox(tr("Code block"), this);
    srcRowLayout->addWidget(m_inplacePreviewSourceCodeBlockCheckBox);
    connect(m_inplacePreviewSourceCodeBlockCheckBox, &QCheckBox::stateChanged, this,
            &MarkdownEditorPage::pageIsChanged);

    m_inplacePreviewSourceMathCheckBox = WidgetsFactory::createCheckBox(tr("Math"), this);
    srcRowLayout->addWidget(m_inplacePreviewSourceMathCheckBox);
    connect(m_inplacePreviewSourceMathCheckBox, &QCheckBox::stateChanged, this,
            &MarkdownEditorPage::pageIsChanged);

    cardLayout->addWidget(SettingsPageHelper::createSeparator(this));
    cardLayout->addWidget(srcRow);
  }

  {
    const QString label(tr("Fetch images to local in Parse And Paste"));
    m_fetchImagesToLocalCheckBox = WidgetsFactory::createCheckBox(label, this);
    m_fetchImagesToLocalCheckBox->setToolTip(
        tr("Fetch images to local in Parse To Markdown And Paste"));
    cardLayout->addWidget(SettingsPageHelper::createSeparator(this));
    cardLayout->addWidget(SettingsPageHelper::createCheckBoxRow(
        m_fetchImagesToLocalCheckBox, m_fetchImagesToLocalCheckBox->toolTip(), this));
    addSearchItem(label, m_fetchImagesToLocalCheckBox->toolTip(), m_fetchImagesToLocalCheckBox);
    connect(m_fetchImagesToLocalCheckBox, &QCheckBox::stateChanged, this,
            &MarkdownEditorPage::pageIsChanged);
  }

  {
    const QString label(tr("Smart table"));
    m_smartTableCheckBox = WidgetsFactory::createCheckBox(label, this);
    m_smartTableCheckBox->setToolTip(tr("Smart table formation"));
    cardLayout->addWidget(SettingsPageHelper::createSeparator(this));
    cardLayout->addWidget(SettingsPageHelper::createCheckBoxRow(
        m_smartTableCheckBox, m_smartTableCheckBox->toolTip(), this));
    addSearchItem(label, m_smartTableCheckBox->toolTip(), m_smartTableCheckBox);
    connect(m_smartTableCheckBox, &QCheckBox::stateChanged, this,
            &MarkdownEditorPage::pageIsChanged);
  }

  {
    const QString label(tr("Spell check"));
    m_spellCheckCheckBox = WidgetsFactory::createCheckBox(label, this);
    m_spellCheckCheckBox->setToolTip(tr("Spell check"));
    cardLayout->addWidget(SettingsPageHelper::createSeparator(this));
    cardLayout->addWidget(SettingsPageHelper::createCheckBoxRow(
        m_spellCheckCheckBox, m_spellCheckCheckBox->toolTip(), this));
    addSearchItem(label, m_spellCheckCheckBox->toolTip(), m_spellCheckCheckBox);
    connect(m_spellCheckCheckBox, &QCheckBox::stateChanged, this,
            &MarkdownEditorPage::pageIsChanged);
  }

  {
    auto *fontWidget = new QWidget(this);
    fontWidget->setStyleSheet("background-color: transparent;");
    auto *fontLayout = new QHBoxLayout(fontWidget);
    fontLayout->setContentsMargins(0, 0, 0, 0);

    const QString label(tr("Override font"));
    m_editorOverriddenFontFamilyCheckBox = WidgetsFactory::createCheckBox(label, this);
    m_editorOverriddenFontFamilyCheckBox->setToolTip(tr("Override editor font family of theme"));
    fontLayout->addWidget(m_editorOverriddenFontFamilyCheckBox);
    addSearchItem(label, m_editorOverriddenFontFamilyCheckBox->toolTip(),
                  m_editorOverriddenFontFamilyCheckBox);

    m_editorOverriddenFontFamilyComboBox = new QFontComboBox(this);
    m_editorOverriddenFontFamilyComboBox->setEnabled(false);
    fontLayout->addWidget(m_editorOverriddenFontFamilyComboBox, 1);
    connect(m_editorOverriddenFontFamilyComboBox,
            QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &MarkdownEditorPage::pageIsChanged);

    connect(m_editorOverriddenFontFamilyCheckBox, &QCheckBox::stateChanged, this,
            [this](int state) {
              m_editorOverriddenFontFamilyComboBox->setEnabled(state == Qt::Checked);
              emit pageIsChanged();
            });

    auto *fontRow = new QWidget(this);
    fontRow->setProperty(PropertyDefs::c_settingsRow, true);
    auto *fontRowLayout = new QVBoxLayout(fontRow);
    fontRowLayout->setContentsMargins(16, 6, 16, 6);
    fontRowLayout->addWidget(fontWidget);

    cardLayout->addWidget(SettingsPageHelper::createSeparator(this));
    cardLayout->addWidget(fontRow);
  }

  {
    const QString label(tr("Use Rich Paste by default"));
    m_richPasteByDefaultCheckBox = WidgetsFactory::createCheckBox(label, this);
    m_richPasteByDefaultCheckBox->setToolTip(tr("Use Rich Paste by default when pasting text"));
    cardLayout->addWidget(SettingsPageHelper::createSeparator(this));
    cardLayout->addWidget(SettingsPageHelper::createCheckBoxRow(
        m_richPasteByDefaultCheckBox, m_richPasteByDefaultCheckBox->toolTip(), this));
    addSearchItem(label, m_richPasteByDefaultCheckBox->toolTip(), m_richPasteByDefaultCheckBox);
    connect(m_richPasteByDefaultCheckBox, &QCheckBox::stateChanged, this,
            &MarkdownEditorPage::pageIsChanged);
  }
}

void MarkdownEditorPage::setupGeneralGroup() {
  auto *mainLayout = qobject_cast<QVBoxLayout *>(layout());
  auto *cardLayout = SettingsPageHelper::addSection(mainLayout, tr("General"), QString(), this);

  {
    auto *sectionWidget = new QWidget(this);
    auto *sectionLayout = new QHBoxLayout(sectionWidget);
    sectionLayout->setContentsMargins(0, 0, 0, 0);

    m_sectionNumberComboBox = WidgetsFactory::createComboBox(this);
    m_sectionNumberComboBox->setToolTip(tr("Section number mode"));
    m_sectionNumberComboBox->addItem(tr("None"),
                                     (int)MarkdownEditorConfig::SectionNumberMode::None);
    m_sectionNumberComboBox->addItem(tr("Read"),
                                     (int)MarkdownEditorConfig::SectionNumberMode::Read);
    m_sectionNumberComboBox->addItem(tr("Edit"),
                                     (int)MarkdownEditorConfig::SectionNumberMode::Edit);
    sectionLayout->addWidget(m_sectionNumberComboBox);
    connect(m_sectionNumberComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &MarkdownEditorPage::pageIsChanged);

    m_sectionNumberBaseLevelSpinBox = WidgetsFactory::createSpinBox(this);
    m_sectionNumberBaseLevelSpinBox->setToolTip(
        tr("Base level to start section numbering in edit mode"));
    m_sectionNumberBaseLevelSpinBox->setRange(1, 6);
    m_sectionNumberBaseLevelSpinBox->setSingleStep(1);
    m_sectionNumberBaseLevelSpinBox->setEnabled(false);
    sectionLayout->addWidget(m_sectionNumberBaseLevelSpinBox);
    connect(m_sectionNumberBaseLevelSpinBox, QOverload<int>::of(&QSpinBox::valueChanged), this,
            &MarkdownEditorPage::pageIsChanged);

    m_sectionNumberStyleComboBox = WidgetsFactory::createComboBox(this);
    m_sectionNumberStyleComboBox->setToolTip(tr("Section number style"));
    m_sectionNumberStyleComboBox->addItem(
        tr("1.1."), (int)MarkdownEditorConfig::SectionNumberStyle::DigDotDigDot);
    m_sectionNumberStyleComboBox->addItem(tr("1.1"),
                                          (int)MarkdownEditorConfig::SectionNumberStyle::DigDotDig);
    m_sectionNumberStyleComboBox->setEnabled(false);
    sectionLayout->addWidget(m_sectionNumberStyleComboBox);
    connect(m_sectionNumberStyleComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &MarkdownEditorPage::pageIsChanged);

    connect(m_sectionNumberComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this](int p_index) {
              m_sectionNumberBaseLevelSpinBox->setEnabled(
                  p_index != MarkdownEditorConfig::SectionNumberMode::None);
              m_sectionNumberStyleComboBox->setEnabled(
                  p_index == MarkdownEditorConfig::SectionNumberMode::Edit);
            });

    const QString label(tr("Section number:"));
    cardLayout->addWidget(SettingsPageHelper::createSettingRow(
        label, m_sectionNumberComboBox->toolTip(), sectionWidget, this));
    addSearchItem(label, m_sectionNumberComboBox->toolTip(), m_sectionNumberComboBox);
  }

  {
    m_plantUmlModeComboBox = WidgetsFactory::createComboBox(this);
    m_plantUmlModeComboBox->setToolTip(
        tr("Use Web service or local JAR file to render PlantUml graphs"));

    m_plantUmlModeComboBox->addItem(tr("Web Service"), 0);
    m_plantUmlModeComboBox->addItem(tr("Local JAR"), 1);

    const QString label(tr("PlantUml:"));
    cardLayout->addWidget(SettingsPageHelper::createSeparator(this));
    cardLayout->addWidget(SettingsPageHelper::createSettingRow(
        label, m_plantUmlModeComboBox->toolTip(), m_plantUmlModeComboBox, this));
    addSearchItem(label, m_plantUmlModeComboBox->toolTip(), m_plantUmlModeComboBox);
    connect(m_plantUmlModeComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &MarkdownEditorPage::pageIsChanged);
  }

  {
    auto *jarWidget = new QWidget(this);
    auto *jarLayout = new QHBoxLayout(jarWidget);
    jarLayout->setContentsMargins(0, 0, 0, 0);

    m_plantUmlJarFileInput = new LocationInputWithBrowseButton(this);
    m_plantUmlJarFileInput->setToolTip(tr("Local JAR file to render PlantUml graphs"));
    m_plantUmlJarFileInput->setBrowseType(
        LocationInputWithBrowseButton::File,
        tr("Select PlantUml JAR File"),
        QStringLiteral("PlantUml JAR (*.jar)"));
    jarLayout->addWidget(m_plantUmlJarFileInput, 1);

    auto *testBtn = new QPushButton(tr("Test"), this);
    testBtn->setToolTip(tr("Test PlantUml JAR and Java Runtime Environment"));
    connect(testBtn, &QPushButton::clicked, this, [this]() {
      const auto jar = m_plantUmlJarFileInput->text();
      if (jar.isEmpty() || !QFileInfo::exists(jar)) {
        MessageBoxHelper::notify(MessageBoxHelper::Warning,
                                 tr("The JAR file (%1) specified does not exist.").arg(jar), this);
        return;
      }

      auto testRet = PlantUmlHelper::testPlantUml(jar);
      MessageBoxHelper::notify(MessageBoxHelper::Information,
                               tr("Test %1.").arg(testRet.first ? tr("succeeded") : tr("failed")),
                               QString(), testRet.second, this);
    });
    jarLayout->addWidget(testBtn);

    const QString label(tr("PlantUml JAR file:"));
    cardLayout->addWidget(SettingsPageHelper::createSeparator(this));
    cardLayout->addWidget(SettingsPageHelper::createSettingRow(
        label, m_plantUmlJarFileInput->toolTip(), jarWidget, this));
    addSearchItem(label, m_plantUmlJarFileInput->toolTip(), m_plantUmlJarFileInput);
    connect(m_plantUmlJarFileInput, &LocationInputWithBrowseButton::textChanged, this,
            &MarkdownEditorPage::pageIsChanged);
  }

  {
    m_plantUmlWebServiceLineEdit = WidgetsFactory::createLineEdit(this);
    m_plantUmlWebServiceLineEdit->setToolTip(
        tr("Override the Web service used to render PlantUml graphs"));
    m_plantUmlWebServiceLineEdit->setPlaceholderText(tr("Empty to use default one"));

    const QString label(tr("Override PlantUml Web service:"));
    cardLayout->addWidget(SettingsPageHelper::createSeparator(this));
    cardLayout->addWidget(SettingsPageHelper::createSettingRow(
        label, m_plantUmlWebServiceLineEdit->toolTip(), m_plantUmlWebServiceLineEdit, this));
    addSearchItem(label, m_plantUmlWebServiceLineEdit->toolTip(), m_plantUmlWebServiceLineEdit);
    connect(m_plantUmlWebServiceLineEdit, &QLineEdit::textChanged, this,
            &MarkdownEditorPage::pageIsChanged);
  }

  {
    m_graphvizModeComboBox = WidgetsFactory::createComboBox(this);
    m_graphvizModeComboBox->setToolTip(
        tr("Use Web service or local executable file to render Graphviz graphs"));

    m_graphvizModeComboBox->addItem(tr("Web Service"), 0);
    m_graphvizModeComboBox->addItem(tr("Local Executable"), 1);

    const QString label(tr("Graphviz:"));
    cardLayout->addWidget(SettingsPageHelper::createSeparator(this));
    cardLayout->addWidget(SettingsPageHelper::createSettingRow(
        label, m_graphvizModeComboBox->toolTip(), m_graphvizModeComboBox, this));
    addSearchItem(label, m_graphvizModeComboBox->toolTip(), m_graphvizModeComboBox);
    connect(m_graphvizModeComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &MarkdownEditorPage::pageIsChanged);
  }

  {
    auto *fileWidget = new QWidget(this);
    auto *fileLayout = new QHBoxLayout(fileWidget);
    fileLayout->setContentsMargins(0, 0, 0, 0);

    m_graphvizFileInput = new LocationInputWithBrowseButton(this);
    m_graphvizFileInput->setToolTip(tr("Local executable file to render Graphviz graphs"));
    m_graphvizFileInput->setBrowseType(
        LocationInputWithBrowseButton::File,
        tr("Select Graphviz Executable File"));
    fileLayout->addWidget(m_graphvizFileInput, 1);

    auto *testBtn = new QPushButton(tr("Test"), this);
    testBtn->setToolTip(tr("Test Graphviz executable file"));
    connect(testBtn, &QPushButton::clicked, this, [this]() {
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
                               QString(), testRet.second, this);
    });
    fileLayout->addWidget(testBtn);

    const QString label(tr("Graphviz executable file:"));
    cardLayout->addWidget(SettingsPageHelper::createSeparator(this));
    cardLayout->addWidget(SettingsPageHelper::createSettingRow(
        label, m_graphvizFileInput->toolTip(), fileWidget, this));
    addSearchItem(label, m_graphvizFileInput->toolTip(), m_graphvizFileInput);
    connect(m_graphvizFileInput, &LocationInputWithBrowseButton::textChanged, this,
            &MarkdownEditorPage::pageIsChanged);
  }

  {
    m_mathJaxScriptLineEdit = WidgetsFactory::createLineEdit(this);
    m_mathJaxScriptLineEdit->setToolTip(
        tr("Override the MathJax script used to render math formulas"));
    m_mathJaxScriptLineEdit->setPlaceholderText(tr("Empty to use default one"));

    const QString label(tr("Override MathJax script:"));
    cardLayout->addWidget(SettingsPageHelper::createSeparator(this));
    cardLayout->addWidget(SettingsPageHelper::createSettingRow(
        label, m_mathJaxScriptLineEdit->toolTip(), m_mathJaxScriptLineEdit, this));
    addSearchItem(label, m_mathJaxScriptLineEdit->toolTip(), m_mathJaxScriptLineEdit);
    connect(m_mathJaxScriptLineEdit, &QLineEdit::textChanged, this,
            &MarkdownEditorPage::pageIsChanged);
  }
}
