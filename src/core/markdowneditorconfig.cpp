#include "markdowneditorconfig.h"

#include <QDebug>

#include "texteditorconfig.h"
#include "mainconfig.h"

using namespace vnotex;

#define READSTR(key) readString(appObj, userObj, (key))
#define READBOOL(key) readBool(appObj, userObj, (key))
#define READREAL(key) readReal(appObj, userObj, (key))
#define READINT(key) readInt(appObj, userObj, (key))

MarkdownEditorConfig::MarkdownEditorConfig(ConfigMgr *p_mgr,
                                           IConfig *p_topConfig,
                                           const QSharedPointer<TextEditorConfig> &p_textEditorConfig)
    : IConfig(p_mgr, p_topConfig),
      m_textEditorConfig(p_textEditorConfig)
{
    m_sessionName = QStringLiteral("markdown_editor");
}

void MarkdownEditorConfig::init(const QJsonObject &p_app, const QJsonObject &p_user)
{
    const auto appObj = p_app.value(m_sessionName).toObject();
    const auto userObj = p_user.value(m_sessionName).toObject();

    loadViewerResource(appObj, userObj);
    loadExportResource(appObj, userObj);

    m_webPlantUml = READBOOL(QStringLiteral("web_plantuml"));

    m_plantUmlJar = READSTR(QStringLiteral("plantuml_jar"));

    m_plantUmlCommand = READSTR(QStringLiteral("plantuml_command"));

    m_plantUmlWebService = READSTR(QStringLiteral("plantuml_web_service"));

    m_webGraphviz = READBOOL(QStringLiteral("web_graphviz"));

    m_graphvizExe = READSTR(QStringLiteral("graphviz_exe"));

    m_mathJaxScript = READSTR(QStringLiteral("mathjax_script"));

    m_prependDotInRelativeLink = READBOOL(QStringLiteral("prepend_dot_in_relative_link"));
    m_confirmBeforeClearObsoleteImages = READBOOL(QStringLiteral("confirm_before_clear_obsolete_images"));
    m_insertFileNameAsTitle = READBOOL(QStringLiteral("insert_file_name_as_title"));

    m_sectionNumberMode = stringToSectionNumberMode(READSTR(QStringLiteral("section_number")));
    m_sectionNumberBaseLevel = READINT(QStringLiteral("section_number_base_level"));
    m_sectionNumberStyle = stringToSectionNumberStyle(READSTR(QStringLiteral("section_number_style")));

    m_constrainImageWidthEnabled = READBOOL(QStringLiteral("constrain_image_width"));
    m_imageAlignCenterEnabled = READBOOL(QStringLiteral("image_align_center"));
    m_constrainInplacePreviewWidthEnabled = READBOOL(QStringLiteral("constrain_inplace_preview_width"));
    m_zoomFactorInReadMode = READREAL(QStringLiteral("zoom_factor_in_read_mode"));
    m_fetchImagesInParseAndPaste = READBOOL(QStringLiteral("fetch_images_in_parse_and_paste"));

    m_protectFromXss = READBOOL(QStringLiteral("protect_from_xss"));
    m_htmlTagEnabled = READBOOL(QStringLiteral("html_tag"));
    m_autoBreakEnabled = READBOOL(QStringLiteral("auto_break"));
    m_linkifyEnabled = READBOOL(QStringLiteral("linkify"));
    m_indentFirstLineEnabled = READBOOL(QStringLiteral("indent_first_line"));
    m_codeBlockLineNumberEnabled = READBOOL(QStringLiteral("code_block_line_number"));

    m_smartTableEnabled = READBOOL(QStringLiteral("smart_table"));
    m_smartTableInterval = READINT(QStringLiteral("smart_table_interval"));

    m_spellCheckEnabled = READBOOL(QStringLiteral("spell_check"));

    m_editorOverriddenFontFamily = READSTR(QStringLiteral("editor_overridden_font_family"));

    {
        m_inplacePreviewSources = InplacePreviewSource::NoInplacePreview;
        auto srcs = READSTR(QStringLiteral("inplace_preview_sources")).split(QLatin1Char(';'));
        for (const auto &src : srcs) {
            m_inplacePreviewSources |= stringToInplacePreviewSource(src);
        }
    }

    m_editViewMode = stringToEditViewMode(READSTR(QStringLiteral("edit_view_mode")));

    m_richPasteByDefaultEnabled = READBOOL(QStringLiteral("rich_paste_by_default"));
}

QJsonObject MarkdownEditorConfig::toJson() const
{
    QJsonObject obj;
    obj[QStringLiteral("viewer_resource")] = saveViewerResource();
    obj[QStringLiteral("export_resource")] = saveExportResource();
    obj[QStringLiteral("web_plantuml")] = m_webPlantUml;
    obj[QStringLiteral("plantuml_jar")] = m_plantUmlJar;
    obj[QStringLiteral("plantuml_command")] = m_plantUmlCommand;
    obj[QStringLiteral("plantuml_web_service")] = m_plantUmlWebService;
    obj[QStringLiteral("web_graphviz")] = m_webGraphviz;
    obj[QStringLiteral("graphviz_exe")] = m_graphvizExe;
    obj[QStringLiteral("mathjax_script")] = m_mathJaxScript;
    obj[QStringLiteral("prepend_dot_in_relative_link")] = m_prependDotInRelativeLink;
    obj[QStringLiteral("confirm_before_clear_obsolete_images")] = m_confirmBeforeClearObsoleteImages;
    obj[QStringLiteral("insert_file_name_as_title")] = m_insertFileNameAsTitle;

    obj[QStringLiteral("section_number")] = sectionNumberModeToString(m_sectionNumberMode);
    obj[QStringLiteral("section_number_base_level")] = m_sectionNumberBaseLevel;
    obj[QStringLiteral("section_number_style")] = sectionNumberStyleToString(m_sectionNumberStyle);

    obj[QStringLiteral("constrain_image_width")] = m_constrainImageWidthEnabled;
    obj[QStringLiteral("image_align_center")] = m_imageAlignCenterEnabled;
    obj[QStringLiteral("constrain_inplace_preview_width")] = m_constrainInplacePreviewWidthEnabled;
    obj[QStringLiteral("zoom_factor_in_read_mode")] = m_zoomFactorInReadMode;
    obj[QStringLiteral("fetch_images_in_parse_and_paste")] = m_fetchImagesInParseAndPaste;
    obj[QStringLiteral("protect_from_xss")] = m_protectFromXss;
    obj[QStringLiteral("html_tag")] = m_htmlTagEnabled;
    obj[QStringLiteral("auto_break")] = m_autoBreakEnabled;
    obj[QStringLiteral("linkify")] = m_linkifyEnabled;
    obj[QStringLiteral("indent_first_line")] = m_indentFirstLineEnabled;
    obj[QStringLiteral("code_block_line_number")] = m_codeBlockLineNumberEnabled;
    obj[QStringLiteral("smart_table")] = m_smartTableEnabled;
    obj[QStringLiteral("smart_table_interval")] = m_smartTableInterval;
    obj[QStringLiteral("spell_check")] = m_spellCheckEnabled;
    obj[QStringLiteral("editor_overridden_font_family")] = m_editorOverriddenFontFamily;

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
        obj[QStringLiteral("inplace_preview_sources")] = srcs.join(QLatin1Char(';'));
    }

    obj[QStringLiteral("edit_view_mode")] = editViewModeToString(m_editViewMode);

    obj[QStringLiteral("rich_paste_by_default")] = m_richPasteByDefaultEnabled;

    return obj;
}

TextEditorConfig &MarkdownEditorConfig::getTextEditorConfig()
{
    return *m_textEditorConfig;
}

const TextEditorConfig &MarkdownEditorConfig::getTextEditorConfig() const
{
    return *m_textEditorConfig;
}

int MarkdownEditorConfig::revision() const
{
    return m_revision + m_textEditorConfig->revision();
}

void MarkdownEditorConfig::loadViewerResource(const QJsonObject &p_app, const QJsonObject &p_user)
{
    const QString name(QStringLiteral("viewer_resource"));

    if (MainConfig::isVersionChanged()) {
        bool needOverride = p_app[QStringLiteral("override_viewer_resource")].toBool();
        if (needOverride) {
            qInfo() << "override \"viewer_resource\" in user configuration due to version change";
            m_viewerResource.init(p_app[name].toObject());
            return;
        }
    }

    if (p_user.contains(name)) {
        m_viewerResource.init(p_user[name].toObject());
    } else {
        m_viewerResource.init(p_app[name].toObject());
    }
}

QJsonObject MarkdownEditorConfig::saveViewerResource() const
{
    return m_viewerResource.toJson();
}

void MarkdownEditorConfig::loadExportResource(const QJsonObject &p_app, const QJsonObject &p_user)
{
    const QString name(QStringLiteral("export_resource"));

    if (MainConfig::isVersionChanged()) {
        bool needOverride = p_app[QStringLiteral("override_viewer_resource")].toBool();
        if (needOverride) {
            qInfo() << "override \"viewer_resource\" in user configuration due to version change";
            m_exportResource.init(p_app[name].toObject());
            return;
        }
    }

    if (p_user.contains(name)) {
        m_exportResource.init(p_user[name].toObject());
    } else {
        m_exportResource.init(p_app[name].toObject());
    }
}

QJsonObject MarkdownEditorConfig::saveExportResource() const
{
    return m_exportResource.toJson();
}

const WebResource &MarkdownEditorConfig::getViewerResource() const
{
    return m_viewerResource;
}

const WebResource &MarkdownEditorConfig::getExportResource() const
{
    return m_exportResource;
}

bool MarkdownEditorConfig::getWebPlantUml() const
{
    return m_webPlantUml;
}

void MarkdownEditorConfig::setWebPlantUml(bool p_enabled)
{
    updateConfig(m_webPlantUml, p_enabled, this);
}

const QString &MarkdownEditorConfig::getPlantUmlJar() const
{
    return m_plantUmlJar;
}

void MarkdownEditorConfig::setPlantUmlJar(const QString &p_jar)
{
    updateConfig(m_plantUmlJar, p_jar, this);
}

const QString &MarkdownEditorConfig::getPlantUmlCommand() const
{
    return m_plantUmlCommand;
}

const QString &MarkdownEditorConfig::getPlantUmlWebService() const
{
    return m_plantUmlWebService;
}

void MarkdownEditorConfig::setPlantUmlWebService(const QString &p_service)
{
    updateConfig(m_plantUmlWebService, p_service, this);
}

bool MarkdownEditorConfig::getWebGraphviz() const
{
    return m_webGraphviz;
}

void MarkdownEditorConfig::setWebGraphviz(bool p_enabled)
{
    updateConfig(m_webGraphviz, p_enabled, this);
}

const QString &MarkdownEditorConfig::getGraphvizExe() const
{
    return m_graphvizExe;
}

void MarkdownEditorConfig::setGraphvizExe(const QString &p_exe)
{
    updateConfig(m_graphvizExe, p_exe, this);
}

const QString &MarkdownEditorConfig::getMathJaxScript() const
{
    return m_mathJaxScript;
}

void MarkdownEditorConfig::setMathJaxScript(const QString &p_script)
{
    updateConfig(m_mathJaxScript, p_script, this);
}

bool MarkdownEditorConfig::getPrependDotInRelativeLink() const
{
    return m_prependDotInRelativeLink;
}

bool MarkdownEditorConfig::getConfirmBeforeClearObsoleteImages() const
{
    return m_confirmBeforeClearObsoleteImages;
}

void MarkdownEditorConfig::setConfirmBeforeClearObsoleteImages(bool p_confirm)
{
    updateConfig(m_confirmBeforeClearObsoleteImages, p_confirm, this);
}

bool MarkdownEditorConfig::getInsertFileNameAsTitle() const
{
    return m_insertFileNameAsTitle;
}

void MarkdownEditorConfig::setInsertFileNameAsTitle(bool p_enabled)
{
    updateConfig(m_insertFileNameAsTitle, p_enabled, this);
}

bool MarkdownEditorConfig::getConstrainImageWidthEnabled() const
{
    return m_constrainImageWidthEnabled;
}

void MarkdownEditorConfig::setConstrainImageWidthEnabled(bool p_enabled)
{
    updateConfig(m_constrainImageWidthEnabled, p_enabled, this);
}

bool MarkdownEditorConfig::getImageAlignCenterEnabled() const
{
    return m_imageAlignCenterEnabled;
}

void MarkdownEditorConfig::setImageAlignCenterEnabled(bool p_enabled)
{
    updateConfig(m_imageAlignCenterEnabled, p_enabled, this);
}

bool MarkdownEditorConfig::getConstrainInplacePreviewWidthEnabled() const
{
    return m_constrainInplacePreviewWidthEnabled;
}

void MarkdownEditorConfig::setConstrainInplacePreviewWidthEnabled(bool p_enabled)
{
    updateConfig(m_constrainInplacePreviewWidthEnabled, p_enabled, this);
}

qreal MarkdownEditorConfig::getZoomFactorInReadMode() const
{
    return m_zoomFactorInReadMode;
}

void MarkdownEditorConfig::setZoomFactorInReadMode(qreal p_factor)
{
    updateConfig(m_zoomFactorInReadMode, p_factor, this);
}

bool MarkdownEditorConfig::getFetchImagesInParseAndPaste() const
{
    return m_fetchImagesInParseAndPaste;
}

void MarkdownEditorConfig::setFetchImagesInParseAndPaste(bool p_enabled)
{
    updateConfig(m_fetchImagesInParseAndPaste, p_enabled, this);
}

bool MarkdownEditorConfig::getProtectFromXss() const
{
    return m_protectFromXss;
}

bool MarkdownEditorConfig::getHtmlTagEnabled() const
{
    return m_htmlTagEnabled;
}

void MarkdownEditorConfig::setHtmlTagEnabled(bool p_enabled)
{
    updateConfig(m_htmlTagEnabled, p_enabled, this);
}

bool MarkdownEditorConfig::getAutoBreakEnabled() const
{
    return m_autoBreakEnabled;
}

void MarkdownEditorConfig::setAutoBreakEnabled(bool p_enabled)
{
    updateConfig(m_autoBreakEnabled, p_enabled, this);
}

bool MarkdownEditorConfig::getLinkifyEnabled() const
{
    return m_linkifyEnabled;
}

void MarkdownEditorConfig::setLinkifyEnabled(bool p_enabled)
{
    updateConfig(m_linkifyEnabled, p_enabled, this);
}

bool MarkdownEditorConfig::getIndentFirstLineEnabled() const
{
    return m_indentFirstLineEnabled;
}

void MarkdownEditorConfig::setIndentFirstLineEnabled(bool p_enabled)
{
    updateConfig(m_indentFirstLineEnabled, p_enabled, this);
}

bool MarkdownEditorConfig::getCodeBlockLineNumberEnabled() const
{
    return m_codeBlockLineNumberEnabled;
}

void MarkdownEditorConfig::setCodeBlockLineNumberEnabled(bool p_enabled)
{
    updateConfig(m_codeBlockLineNumberEnabled, p_enabled, this);
}

QString MarkdownEditorConfig::sectionNumberModeToString(SectionNumberMode p_mode) const
{
    switch (p_mode) {
    case SectionNumberMode::None:
        return QStringLiteral("none");

    case SectionNumberMode::Edit:
        return QStringLiteral("edit");

    default:
        return QStringLiteral("read");
    }
}

MarkdownEditorConfig::SectionNumberMode MarkdownEditorConfig::stringToSectionNumberMode(const QString &p_str) const
{
    auto mode = p_str.toLower();
    if (mode == QStringLiteral("none")) {
        return SectionNumberMode::None;
    } else if (mode == QStringLiteral("edit")) {
        return SectionNumberMode::Edit;
    } else {
        return SectionNumberMode::Read;
    }
}

QString MarkdownEditorConfig::sectionNumberStyleToString(SectionNumberStyle p_style) const
{
    switch (p_style) {
    case SectionNumberStyle::DigDotDig:
        return QStringLiteral("digdotdig");

    default:
        return QStringLiteral("digdotdigdot");
    }
}

MarkdownEditorConfig::SectionNumberStyle MarkdownEditorConfig::stringToSectionNumberStyle(const QString &p_str) const
{
    auto style = p_str.toLower();
    if (style == QStringLiteral("digdotdig")) {
        return SectionNumberStyle::DigDotDig;
    } else {
        return SectionNumberStyle::DigDotDigDot;
    }
}

QString MarkdownEditorConfig::inplacePreviewSourceToString(InplacePreviewSource p_src) const
{
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

MarkdownEditorConfig::InplacePreviewSource MarkdownEditorConfig::stringToInplacePreviewSource(const QString &p_str) const
{
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

MarkdownEditorConfig::SectionNumberMode MarkdownEditorConfig::getSectionNumberMode() const
{
    return m_sectionNumberMode;
}

void MarkdownEditorConfig::setSectionNumberMode(SectionNumberMode p_mode)
{
    updateConfig(m_sectionNumberMode, p_mode, this);
}

int MarkdownEditorConfig::getSectionNumberBaseLevel() const
{
    return m_sectionNumberBaseLevel;
}

void MarkdownEditorConfig::setSectionNumberBaseLevel(int p_level)
{
    updateConfig(m_sectionNumberBaseLevel, p_level, this);
}

MarkdownEditorConfig::SectionNumberStyle MarkdownEditorConfig::getSectionNumberStyle() const
{
    return m_sectionNumberStyle;
}

void MarkdownEditorConfig::setSectionNumberStyle(SectionNumberStyle p_style)
{
    updateConfig(m_sectionNumberStyle, p_style, this);
}

bool MarkdownEditorConfig::getSmartTableEnabled() const
{
    return m_smartTableEnabled;
}

void MarkdownEditorConfig::setSmartTableEnabled(bool p_enabled)
{
    updateConfig(m_smartTableEnabled, p_enabled, this);
}

int MarkdownEditorConfig::getSmartTableInterval() const
{
    return m_smartTableInterval;
}

bool MarkdownEditorConfig::isSpellCheckEnabled() const
{
    return m_spellCheckEnabled;
}

void MarkdownEditorConfig::setSpellCheckEnabled(bool p_enabled)
{
    updateConfig(m_spellCheckEnabled, p_enabled, this);
}

const QString &MarkdownEditorConfig::getEditorOverriddenFontFamily() const
{
    return m_editorOverriddenFontFamily;
}

void MarkdownEditorConfig::setEditorOverriddenFontFamily(const QString &p_family)
{
    updateConfig(m_editorOverriddenFontFamily, p_family, this);
}

MarkdownEditorConfig::InplacePreviewSources MarkdownEditorConfig::getInplacePreviewSources() const
{
    return m_inplacePreviewSources;
}

void MarkdownEditorConfig::setInplacePreviewSources(InplacePreviewSources p_src)
{
    updateConfig(m_inplacePreviewSources, p_src, this);
}

QString MarkdownEditorConfig::editViewModeToString(EditViewMode p_mode) const
{
    switch (p_mode) {
    case EditViewMode::EditPreview:
        return QStringLiteral("editpreview");

    default:
        return QStringLiteral("editonly");
    }
}

MarkdownEditorConfig::EditViewMode MarkdownEditorConfig::stringToEditViewMode(const QString &p_str) const
{
    auto mode = p_str.toLower();
    if (mode == QStringLiteral("editpreview")) {
        return EditViewMode::EditPreview;
    } else {
        return EditViewMode::EditOnly;
    }
}

MarkdownEditorConfig::EditViewMode MarkdownEditorConfig::getEditViewMode() const
{
    return m_editViewMode;
}

void MarkdownEditorConfig::setEditViewMode(EditViewMode p_mode)
{
    updateConfig(m_editViewMode, p_mode, this);
}

bool MarkdownEditorConfig::getRichPasteByDefaultEnabled() const
{
    return m_richPasteByDefaultEnabled;
}

void MarkdownEditorConfig::setRichPasteByDefaultEnabled(bool p_enabled)
{
    updateConfig(m_richPasteByDefaultEnabled, p_enabled, this);
}
