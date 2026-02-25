#include "markdowneditorconfig.h"

#include <QDebug>

#include "mainconfig.h"
#include "texteditorconfig.h"

using namespace vnotex;

#define READSTR(key) readString(p_jobj, (key))
#define READBOOL(key) readBool(p_jobj, (key))
#define READREAL(key) readReal(p_jobj, (key))
#define READINT(key) readInt(p_jobj, (key))

MarkdownEditorConfig::MarkdownEditorConfig(
    IConfigMgr *p_mgr, IConfig *p_topConfig,
    const QSharedPointer<TextEditorConfig> &p_textEditorConfig)
    : IConfig(p_mgr, p_topConfig), m_textEditorConfig(p_textEditorConfig) {
  m_sectionName = QStringLiteral("markdown_editor");
}

void MarkdownEditorConfig::fromJson(const QJsonObject &p_jobj) {
  loadViewerResource(p_jobj);
  loadExportResource(p_jobj);

  m_webPlantUml = READBOOL(QStringLiteral("webPlantUml"));

  m_plantUmlJar = READSTR(QStringLiteral("plantUmlJar"));

  m_plantUmlCommand = READSTR(QStringLiteral("plantUmlCommand"));

  m_plantUmlWebService = READSTR(QStringLiteral("plantUmlWebService"));

  m_webGraphviz = READBOOL(QStringLiteral("webGraphviz"));

  m_graphvizExe = READSTR(QStringLiteral("graphvizExe"));

  m_mathJaxScript = READSTR(QStringLiteral("mathJaxScript"));

  m_prependDotInRelativeLink = READBOOL(QStringLiteral("prependDotInRelativeLink"));
  m_confirmBeforeClearObsoleteImages =
      READBOOL(QStringLiteral("confirmBeforeClearObsoleteImages"));
  m_insertFileNameAsTitle = READBOOL(QStringLiteral("insertFileNameAsTitle"));

  m_sectionNumberMode = stringToSectionNumberMode(READSTR(QStringLiteral("sectionNumber")));
  m_sectionNumberBaseLevel = READINT(QStringLiteral("sectionNumberBaseLevel"));
  m_sectionNumberStyle =
      stringToSectionNumberStyle(READSTR(QStringLiteral("sectionNumberStyle")));

  m_constrainImageWidthEnabled = READBOOL(QStringLiteral("constrainImageWidth"));
  m_imageAlignCenterEnabled = READBOOL(QStringLiteral("imageAlignCenter"));
  m_constrainInplacePreviewWidthEnabled =
      READBOOL(QStringLiteral("constrainInplacePreviewWidth"));
  m_zoomFactorInReadMode = READREAL(QStringLiteral("zoomFactorInReadMode"));
  m_fetchImagesInParseAndPaste = READBOOL(QStringLiteral("fetchImagesInParseAndPaste"));

  m_protectFromXss = READBOOL(QStringLiteral("protectFromXss"));
  m_htmlTagEnabled = READBOOL(QStringLiteral("htmlTag"));
  m_autoBreakEnabled = READBOOL(QStringLiteral("autoBreak"));
  m_linkifyEnabled = READBOOL(QStringLiteral("linkify"));
  m_indentFirstLineEnabled = READBOOL(QStringLiteral("indentFirstLine"));
  m_codeBlockLineNumberEnabled = READBOOL(QStringLiteral("codeBlockLineNumber"));

  m_smartTableEnabled = READBOOL(QStringLiteral("smartTable"));
  m_smartTableInterval = READINT(QStringLiteral("smartTableInterval"));

  m_spellCheckEnabled = READBOOL(QStringLiteral("spellCheck"));

  m_editorOverriddenFontFamily = READSTR(QStringLiteral("editorOverriddenFontFamily"));

  {
    m_inplacePreviewSources = InplacePreviewSource::NoInplacePreview;
    auto srcs = READSTR(QStringLiteral("inplacePreviewSources")).split(QLatin1Char(';'));
    for (const auto &src : srcs) {
      m_inplacePreviewSources |= stringToInplacePreviewSource(src);
    }
  }

  m_editViewMode = stringToEditViewMode(READSTR(QStringLiteral("editViewMode")));

  m_richPasteByDefaultEnabled = READBOOL(QStringLiteral("richPasteByDefault"));
}

QJsonObject MarkdownEditorConfig::toJson() const {
  QJsonObject obj;
  obj[QStringLiteral("viewerResource")] = saveViewerResource();
  obj[QStringLiteral("exportResource")] = saveExportResource();
  obj[QStringLiteral("webPlantUml")] = m_webPlantUml;
  obj[QStringLiteral("plantUmlJar")] = m_plantUmlJar;
  obj[QStringLiteral("plantUmlCommand")] = m_plantUmlCommand;
  obj[QStringLiteral("plantUmlWebService")] = m_plantUmlWebService;
  obj[QStringLiteral("webGraphviz")] = m_webGraphviz;
  obj[QStringLiteral("graphvizExe")] = m_graphvizExe;
  obj[QStringLiteral("mathJaxScript")] = m_mathJaxScript;
  obj[QStringLiteral("prependDotInRelativeLink")] = m_prependDotInRelativeLink;
  obj[QStringLiteral("confirmBeforeClearObsoleteImages")] = m_confirmBeforeClearObsoleteImages;
  obj[QStringLiteral("insertFileNameAsTitle")] = m_insertFileNameAsTitle;

  obj[QStringLiteral("sectionNumber")] = sectionNumberModeToString(m_sectionNumberMode);
  obj[QStringLiteral("sectionNumberBaseLevel")] = m_sectionNumberBaseLevel;
  obj[QStringLiteral("sectionNumberStyle")] = sectionNumberStyleToString(m_sectionNumberStyle);

  obj[QStringLiteral("constrainImageWidth")] = m_constrainImageWidthEnabled;
  obj[QStringLiteral("imageAlignCenter")] = m_imageAlignCenterEnabled;
  obj[QStringLiteral("constrainInplacePreviewWidth")] = m_constrainInplacePreviewWidthEnabled;
  obj[QStringLiteral("zoomFactorInReadMode")] = m_zoomFactorInReadMode;
  obj[QStringLiteral("fetchImagesInParseAndPaste")] = m_fetchImagesInParseAndPaste;
  obj[QStringLiteral("protectFromXss")] = m_protectFromXss;
  obj[QStringLiteral("htmlTag")] = m_htmlTagEnabled;
  obj[QStringLiteral("autoBreak")] = m_autoBreakEnabled;
  obj[QStringLiteral("linkify")] = m_linkifyEnabled;
  obj[QStringLiteral("indentFirstLine")] = m_indentFirstLineEnabled;
  obj[QStringLiteral("codeBlockLineNumber")] = m_codeBlockLineNumberEnabled;
  obj[QStringLiteral("smartTable")] = m_smartTableEnabled;
  obj[QStringLiteral("smartTableInterval")] = m_smartTableInterval;
  obj[QStringLiteral("spellCheck")] = m_spellCheckEnabled;
  obj[QStringLiteral("editorOverriddenFontFamily")] = m_editorOverriddenFontFamily;

  {
    QStringList srcs;
    if (m_inplacePreviewSources & InplacePreviewSource::ImageLink) {
      srcs << inplacePreviewSourceToString(InplacePreviewSource::ImageLink);
    }
    if (m_inplacePreviewSources & InplacePreviewSource::CodeBlock) {
      srcs << inplacePreviewSourceToString(InplacePreviewSource::CodeBlock);
    }
    if (m_inplacePreviewSources & InplacePreviewSource::Math) {
      srcs << inplacePreviewSourceToString(InplacePreviewSource::Math);
    }
    obj[QStringLiteral("inplacePreviewSources")] = srcs.join(QLatin1Char(';'));
  }

  obj[QStringLiteral("editViewMode")] = editViewModeToString(m_editViewMode);

  obj[QStringLiteral("richPasteByDefault")] = m_richPasteByDefaultEnabled;

  return obj;
}

TextEditorConfig &MarkdownEditorConfig::getTextEditorConfig() { return *m_textEditorConfig; }

const TextEditorConfig &MarkdownEditorConfig::getTextEditorConfig() const {
  return *m_textEditorConfig;
}

void MarkdownEditorConfig::loadViewerResource(const QJsonObject &p_jobj) {
  const QString name(QStringLiteral("viewerResource"));
  m_viewerResource.init(p_jobj.value(name).toObject());
}

QJsonObject MarkdownEditorConfig::saveViewerResource() const { return m_viewerResource.toJson(); }

void MarkdownEditorConfig::loadExportResource(const QJsonObject &p_jobj) {
  const QString name(QStringLiteral("exportResource"));
  m_exportResource.init(p_jobj.value(name).toObject());
}

QJsonObject MarkdownEditorConfig::saveExportResource() const { return m_exportResource.toJson(); }

const WebResource &MarkdownEditorConfig::getViewerResource() const { return m_viewerResource; }

const WebResource &MarkdownEditorConfig::getExportResource() const { return m_exportResource; }

bool MarkdownEditorConfig::getWebPlantUml() const { return m_webPlantUml; }

void MarkdownEditorConfig::setWebPlantUml(bool p_enabled) {
  updateConfig(m_webPlantUml, p_enabled, this);
}

const QString &MarkdownEditorConfig::getPlantUmlJar() const { return m_plantUmlJar; }

void MarkdownEditorConfig::setPlantUmlJar(const QString &p_jar) {
  updateConfig(m_plantUmlJar, p_jar, this);
}

const QString &MarkdownEditorConfig::getPlantUmlCommand() const { return m_plantUmlCommand; }

const QString &MarkdownEditorConfig::getPlantUmlWebService() const { return m_plantUmlWebService; }

void MarkdownEditorConfig::setPlantUmlWebService(const QString &p_service) {
  updateConfig(m_plantUmlWebService, p_service, this);
}

bool MarkdownEditorConfig::getWebGraphviz() const { return m_webGraphviz; }

void MarkdownEditorConfig::setWebGraphviz(bool p_enabled) {
  updateConfig(m_webGraphviz, p_enabled, this);
}

const QString &MarkdownEditorConfig::getGraphvizExe() const { return m_graphvizExe; }

void MarkdownEditorConfig::setGraphvizExe(const QString &p_exe) {
  updateConfig(m_graphvizExe, p_exe, this);
}

const QString &MarkdownEditorConfig::getMathJaxScript() const { return m_mathJaxScript; }

void MarkdownEditorConfig::setMathJaxScript(const QString &p_script) {
  updateConfig(m_mathJaxScript, p_script, this);
}

bool MarkdownEditorConfig::getPrependDotInRelativeLink() const {
  return m_prependDotInRelativeLink;
}

bool MarkdownEditorConfig::getConfirmBeforeClearObsoleteImages() const {
  return m_confirmBeforeClearObsoleteImages;
}

void MarkdownEditorConfig::setConfirmBeforeClearObsoleteImages(bool p_confirm) {
  updateConfig(m_confirmBeforeClearObsoleteImages, p_confirm, this);
}

bool MarkdownEditorConfig::getInsertFileNameAsTitle() const { return m_insertFileNameAsTitle; }

void MarkdownEditorConfig::setInsertFileNameAsTitle(bool p_enabled) {
  updateConfig(m_insertFileNameAsTitle, p_enabled, this);
}

bool MarkdownEditorConfig::getConstrainImageWidthEnabled() const {
  return m_constrainImageWidthEnabled;
}

void MarkdownEditorConfig::setConstrainImageWidthEnabled(bool p_enabled) {
  updateConfig(m_constrainImageWidthEnabled, p_enabled, this);
}

bool MarkdownEditorConfig::getImageAlignCenterEnabled() const { return m_imageAlignCenterEnabled; }

void MarkdownEditorConfig::setImageAlignCenterEnabled(bool p_enabled) {
  updateConfig(m_imageAlignCenterEnabled, p_enabled, this);
}

bool MarkdownEditorConfig::getConstrainInplacePreviewWidthEnabled() const {
  return m_constrainInplacePreviewWidthEnabled;
}

void MarkdownEditorConfig::setConstrainInplacePreviewWidthEnabled(bool p_enabled) {
  updateConfig(m_constrainInplacePreviewWidthEnabled, p_enabled, this);
}

qreal MarkdownEditorConfig::getZoomFactorInReadMode() const { return m_zoomFactorInReadMode; }

void MarkdownEditorConfig::setZoomFactorInReadMode(qreal p_factor) {
  updateConfig(m_zoomFactorInReadMode, p_factor, this);
}

bool MarkdownEditorConfig::getFetchImagesInParseAndPaste() const {
  return m_fetchImagesInParseAndPaste;
}

void MarkdownEditorConfig::setFetchImagesInParseAndPaste(bool p_enabled) {
  updateConfig(m_fetchImagesInParseAndPaste, p_enabled, this);
}

bool MarkdownEditorConfig::getProtectFromXss() const { return m_protectFromXss; }

bool MarkdownEditorConfig::getHtmlTagEnabled() const { return m_htmlTagEnabled; }

void MarkdownEditorConfig::setHtmlTagEnabled(bool p_enabled) {
  updateConfig(m_htmlTagEnabled, p_enabled, this);
}

bool MarkdownEditorConfig::getAutoBreakEnabled() const { return m_autoBreakEnabled; }

void MarkdownEditorConfig::setAutoBreakEnabled(bool p_enabled) {
  updateConfig(m_autoBreakEnabled, p_enabled, this);
}

bool MarkdownEditorConfig::getLinkifyEnabled() const { return m_linkifyEnabled; }

void MarkdownEditorConfig::setLinkifyEnabled(bool p_enabled) {
  updateConfig(m_linkifyEnabled, p_enabled, this);
}

bool MarkdownEditorConfig::getIndentFirstLineEnabled() const { return m_indentFirstLineEnabled; }

void MarkdownEditorConfig::setIndentFirstLineEnabled(bool p_enabled) {
  updateConfig(m_indentFirstLineEnabled, p_enabled, this);
}

bool MarkdownEditorConfig::getCodeBlockLineNumberEnabled() const {
  return m_codeBlockLineNumberEnabled;
}

void MarkdownEditorConfig::setCodeBlockLineNumberEnabled(bool p_enabled) {
  updateConfig(m_codeBlockLineNumberEnabled, p_enabled, this);
}

QString MarkdownEditorConfig::sectionNumberModeToString(SectionNumberMode p_mode) const {
  switch (p_mode) {
  case SectionNumberMode::None:
    return QStringLiteral("none");

  case SectionNumberMode::Edit:
    return QStringLiteral("edit");

  default:
    return QStringLiteral("read");
  }
}

MarkdownEditorConfig::SectionNumberMode
MarkdownEditorConfig::stringToSectionNumberMode(const QString &p_str) const {
  auto mode = p_str.toLower();
  if (mode == QStringLiteral("none")) {
    return SectionNumberMode::None;
  } else if (mode == QStringLiteral("edit")) {
    return SectionNumberMode::Edit;
  } else {
    return SectionNumberMode::Read;
  }
}

QString MarkdownEditorConfig::sectionNumberStyleToString(SectionNumberStyle p_style) const {
  switch (p_style) {
  case SectionNumberStyle::DigDotDig:
    return QStringLiteral("digdotdig");

  default:
    return QStringLiteral("digdotdigdot");
  }
}

MarkdownEditorConfig::SectionNumberStyle
MarkdownEditorConfig::stringToSectionNumberStyle(const QString &p_str) const {
  auto style = p_str.toLower();
  if (style == QStringLiteral("digdotdig")) {
    return SectionNumberStyle::DigDotDig;
  } else {
    return SectionNumberStyle::DigDotDigDot;
  }
}

QString MarkdownEditorConfig::inplacePreviewSourceToString(InplacePreviewSource p_src) const {
  switch (p_src) {
  case InplacePreviewSource::ImageLink:
    return QStringLiteral("imagelink");

  case InplacePreviewSource::CodeBlock:
    return QStringLiteral("codeblock");

  case InplacePreviewSource::Math:
    return QStringLiteral("math");

  default:
    return "";
  }
}

MarkdownEditorConfig::InplacePreviewSource
MarkdownEditorConfig::stringToInplacePreviewSource(const QString &p_str) const {
  auto src = p_str.toLower();
  if (src == QStringLiteral("imagelink")) {
    return InplacePreviewSource::ImageLink;
  } else if (src == QStringLiteral("codeblock")) {
    return InplacePreviewSource::CodeBlock;
  } else if (src == QStringLiteral("math")) {
    return InplacePreviewSource::Math;
  } else {
    return InplacePreviewSource::NoInplacePreview;
  }
}

MarkdownEditorConfig::SectionNumberMode MarkdownEditorConfig::getSectionNumberMode() const {
  return m_sectionNumberMode;
}

void MarkdownEditorConfig::setSectionNumberMode(SectionNumberMode p_mode) {
  updateConfig(m_sectionNumberMode, p_mode, this);
}

int MarkdownEditorConfig::getSectionNumberBaseLevel() const { return m_sectionNumberBaseLevel; }

void MarkdownEditorConfig::setSectionNumberBaseLevel(int p_level) {
  updateConfig(m_sectionNumberBaseLevel, p_level, this);
}

MarkdownEditorConfig::SectionNumberStyle MarkdownEditorConfig::getSectionNumberStyle() const {
  return m_sectionNumberStyle;
}

void MarkdownEditorConfig::setSectionNumberStyle(SectionNumberStyle p_style) {
  updateConfig(m_sectionNumberStyle, p_style, this);
}

bool MarkdownEditorConfig::getSmartTableEnabled() const { return m_smartTableEnabled; }

void MarkdownEditorConfig::setSmartTableEnabled(bool p_enabled) {
  updateConfig(m_smartTableEnabled, p_enabled, this);
}

int MarkdownEditorConfig::getSmartTableInterval() const { return m_smartTableInterval; }

bool MarkdownEditorConfig::isSpellCheckEnabled() const { return m_spellCheckEnabled; }

void MarkdownEditorConfig::setSpellCheckEnabled(bool p_enabled) {
  updateConfig(m_spellCheckEnabled, p_enabled, this);
}

const QString &MarkdownEditorConfig::getEditorOverriddenFontFamily() const {
  return m_editorOverriddenFontFamily;
}

void MarkdownEditorConfig::setEditorOverriddenFontFamily(const QString &p_family) {
  updateConfig(m_editorOverriddenFontFamily, p_family, this);
}

MarkdownEditorConfig::InplacePreviewSources MarkdownEditorConfig::getInplacePreviewSources() const {
  return m_inplacePreviewSources;
}

void MarkdownEditorConfig::setInplacePreviewSources(InplacePreviewSources p_src) {
  updateConfig(m_inplacePreviewSources, p_src, this);
}

QString MarkdownEditorConfig::editViewModeToString(EditViewMode p_mode) const {
  switch (p_mode) {
  case EditViewMode::EditPreview:
    return QStringLiteral("editpreview");

  default:
    return QStringLiteral("editonly");
  }
}

MarkdownEditorConfig::EditViewMode
MarkdownEditorConfig::stringToEditViewMode(const QString &p_str) const {
  auto mode = p_str.toLower();
  if (mode == QStringLiteral("editpreview")) {
    return EditViewMode::EditPreview;
  } else {
    return EditViewMode::EditOnly;
  }
}

MarkdownEditorConfig::EditViewMode MarkdownEditorConfig::getEditViewMode() const {
  return m_editViewMode;
}

void MarkdownEditorConfig::setEditViewMode(EditViewMode p_mode) {
  updateConfig(m_editViewMode, p_mode, this);
}

bool MarkdownEditorConfig::getRichPasteByDefaultEnabled() const {
  return m_richPasteByDefaultEnabled;
}

void MarkdownEditorConfig::setRichPasteByDefaultEnabled(bool p_enabled) {
  updateConfig(m_richPasteByDefaultEnabled, p_enabled, this);
}
