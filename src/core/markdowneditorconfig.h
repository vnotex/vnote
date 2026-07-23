#ifndef MARKDOWNEDITORCONFIG_H
#define MARKDOWNEDITORCONFIG_H

#include "iconfig.h"

#include "webresource.h"

#include <QSharedPointer>
#include <QVector>

namespace vnotex {
class TextEditorConfig;
class IConfigMgr;

class MarkdownEditorConfig : public IConfig {
public:
  enum InplacePreviewSource { NoInplacePreview = 0, ImageLink = 0x1, CodeBlock = 0x2, Math = 0x4 };
  Q_DECLARE_FLAGS(InplacePreviewSources, InplacePreviewSource);

  enum EditViewMode { EditOnly, EditPreview };

  MarkdownEditorConfig(IConfigMgr *p_mgr, IConfig *p_topConfig,
                       const QSharedPointer<TextEditorConfig> &p_textEditorConfig);

  void fromJson(const QJsonObject &p_jobj) Q_DECL_OVERRIDE;

  QJsonObject toJson() const Q_DECL_OVERRIDE;

  TextEditorConfig &getTextEditorConfig();
  const TextEditorConfig &getTextEditorConfig() const;

  const WebResource &getViewerResource() const;

  const WebResource &getExportResource() const;

  bool getWebPlantUml() const;
  void setWebPlantUml(bool p_enabled);

  const QString &getPlantUmlJar() const;
  void setPlantUmlJar(const QString &p_jar);

  const QString &getPlantUmlCommand() const;

  const QString &getPlantUmlWebService() const;
  void setPlantUmlWebService(const QString &p_service);

  bool getWebGraphviz() const;
  void setWebGraphviz(bool p_enabled);

  const QString &getGraphvizExe() const;
  void setGraphvizExe(const QString &p_exe);

  const QString &getMathJaxScript() const;
  void setMathJaxScript(const QString &p_script);

  bool getPrependDotInRelativeLink() const;

  bool getConfirmBeforeClearObsoleteImages() const;
  void setConfirmBeforeClearObsoleteImages(bool p_confirm);

  bool getInsertFileNameAsTitle() const;
  void setInsertFileNameAsTitle(bool p_enabled);

  bool getConstrainImageWidthEnabled() const;
  void setConstrainImageWidthEnabled(bool p_enabled);

  bool getImageAlignCenterEnabled() const;
  void setImageAlignCenterEnabled(bool p_enabled);

  bool getConstrainInplacePreviewWidthEnabled() const;
  void setConstrainInplacePreviewWidthEnabled(bool p_enabled);

  qreal getZoomFactorInReadMode() const;
  void setZoomFactorInReadMode(qreal p_factor);

  bool getFetchImagesInParseAndPaste() const;
  void setFetchImagesInParseAndPaste(bool p_enabled);

  bool getProtectFromXss() const;

  bool getHtmlTagEnabled() const;
  void setHtmlTagEnabled(bool p_enabled);

  bool getAutoBreakEnabled() const;
  void setAutoBreakEnabled(bool p_enabled);

  bool getLinkifyEnabled() const;
  void setLinkifyEnabled(bool p_enabled);

  bool getIndentFirstLineEnabled() const;
  void setIndentFirstLineEnabled(bool p_enabled);

  bool getCodeBlockLineNumberEnabled() const;
  void setCodeBlockLineNumberEnabled(bool p_enabled);

  bool getCodeBlockLineWrapEnabled() const;
  void setCodeBlockLineWrapEnabled(bool p_enabled);

  bool getSmartTableEnabled() const;
  void setSmartTableEnabled(bool p_enabled);

  int getSmartTableInterval() const;

  bool isSpellCheckEnabled() const;
  void setSpellCheckEnabled(bool p_enabled);

  InplacePreviewSources getInplacePreviewSources() const;
  void setInplacePreviewSources(InplacePreviewSources p_src);

  EditViewMode getEditViewMode() const;
  void setEditViewMode(EditViewMode p_mode);

  bool getRichPasteByDefaultEnabled() const;
  void setRichPasteByDefaultEnabled(bool p_enabled);

private:
  friend class MainConfig;

  void loadViewerResource(const QJsonObject &p_jobj);
  QJsonObject saveViewerResource() const;

  void loadExportResource(const QJsonObject &p_jobj);
  QJsonObject saveExportResource() const;

  QString inplacePreviewSourceToString(InplacePreviewSource p_src) const;
  InplacePreviewSource stringToInplacePreviewSource(const QString &p_str) const;

  QString editViewModeToString(EditViewMode p_mode) const;
  EditViewMode stringToEditViewMode(const QString &p_str) const;

  void initDefaults();
  static WebResource defaultViewerResource();
  static WebResource defaultExportResource();

  QSharedPointer<TextEditorConfig> m_textEditorConfig;

  WebResource m_viewerResource;

  WebResource m_exportResource;

  // Whether use javascript or external program to render PlantUML.
  bool m_webPlantUml = true;

  // File path of the JAR to render PlantUmL.
  QString m_plantUmlJar;

  // Command to render PlantUml. If set, will ignore m_plantUmlJar.
  // %1: the format to render in.
  QString m_plantUmlCommand;

  // PlantUml Web service to override that in plantuml.js file.
  QString m_plantUmlWebService;

  bool m_webGraphviz = true;

  // Graphviz executable file.
  QString m_graphvizExe;

  // MathJax script to override that in mathjax.js file.
  QString m_mathJaxScript;

  // Whether prepend a dot in front of the relative link, like images.
  bool m_prependDotInRelativeLink = false;

  // Whether ask for user confirmation before clearing obsolete images.
  bool m_confirmBeforeClearObsoleteImages = true;

  // Whether insert the name of the new file as title.
  bool m_insertFileNameAsTitle = true;

  // Whether enable image width constraint.
  bool m_constrainImageWidthEnabled = true;

  bool m_imageAlignCenterEnabled = false;

  // Whether enable in-place preview width constraint.
  bool m_constrainInplacePreviewWidthEnabled = false;

  qreal m_zoomFactorInReadMode = 1.0;

  // Whether fetch images to local in Parse To Markdown And Paste.
  bool m_fetchImagesInParseAndPaste = true;

  // Whether protect from Cross-Site Scripting.
  bool m_protectFromXss = true;

  // Whether allow HTML tag in Markdown source.
  bool m_htmlTagEnabled = true;

  // Whether auto break a line with `\n`.
  bool m_autoBreakEnabled = true;

  // Whether convert URL-like text to links.
  bool m_linkifyEnabled = true;

  // Whether indent the first line of a paragraph.
  bool m_indentFirstLineEnabled = false;

  // Whether enable code block line number in read mode.
  bool m_codeBlockLineNumberEnabled = true;

  // Whether wrap long lines in code blocks in read mode.
  bool m_codeBlockLineWrapEnabled = false;

  bool m_smartTableEnabled = true;

  // Interval time to do smart table format.
  int m_smartTableInterval = 1000;

  // Override the config in TextEditorConfig.
  bool m_spellCheckEnabled = false;

  InplacePreviewSources m_inplacePreviewSources = InplacePreviewSources(InplacePreviewSource::ImageLink | InplacePreviewSource::CodeBlock | InplacePreviewSource::Math);

  // View mode in edit mode.
  EditViewMode m_editViewMode = EditViewMode::EditOnly;

  bool m_richPasteByDefaultEnabled = true;
};
} // namespace vnotex

Q_DECLARE_OPERATORS_FOR_FLAGS(vnotex::MarkdownEditorConfig::InplacePreviewSources)

#endif // MARKDOWNEDITORCONFIG_H
