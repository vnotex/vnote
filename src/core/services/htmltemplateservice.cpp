#include "htmltemplateservice.h"

#include <QDebug>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QResource>

#include <core/configmgr2.h>
#include <core/markdowneditorconfig.h>
#include <core/markdownwebglobaloptions.h>
#include <core/mindmapeditorconfig.h>
#include <core/pdfviewerconfig.h>
#include <core/vxpdfscheme.h>
#include <core/webresource.h>
#include <utils/fileutils2.h>
#include <utils/pathutils.h>

using namespace vnotex;

HtmlTemplateService::HtmlTemplateService(ConfigMgr2 *p_configMgr, QObject *p_parent)
    : QObject(p_parent), m_configMgr(p_configMgr) {}

// ============ Helpers ============

QString HtmlTemplateService::resolveConfigFile(const QString &p_relativePath) const {
  return m_configMgr->getFileFromConfigFolder(p_relativePath);
}

QString HtmlTemplateService::readFile(const QString &p_filePath) const {
  QString text;
  Error err = FileUtils2::readTextFile(p_filePath, &text);
  if (err) {
    qWarning() << "HtmlTemplateService: failed to read file" << p_filePath << err.what();
    return QString();
  }
  return text;
}

QString HtmlTemplateService::errorPage() {
  return QStringLiteral(
      "Failed to load HTML template. Check the logs for details. "
      "Try deleting the user configuration file and the default configuration file.");
}

QString HtmlTemplateService::fillStyleTag(const QString &p_styleFile) {
  if (p_styleFile.isEmpty()) {
    return QString();
  }
  auto url = PathUtils::pathToUrl(p_styleFile);
  return QStringLiteral("<link rel=\"stylesheet\" type=\"text/css\" href=\"%1\">\n")
      .arg(url.toString());
}

QString HtmlTemplateService::fillScriptTag(const QString &p_scriptFile) {
  if (p_scriptFile.isEmpty()) {
    return QString();
  }
  auto url = PathUtils::pathToUrl(p_scriptFile);
  return QStringLiteral("<script type=\"text/javascript\" src=\"%1\"></script>\n")
      .arg(url.toString());
}

void HtmlTemplateService::fillThemeStyles(QString &p_template, const QString &p_webStyleSheetFile,
                                          const QString &p_highlightStyleSheetFile) {
  QString styles;
  styles += fillStyleTag(p_webStyleSheetFile);
  styles += fillStyleTag(p_highlightStyleSheetFile);

  if (!styles.isEmpty()) {
    p_template.replace(QStringLiteral("<!-- VX_THEME_STYLES_PLACEHOLDER -->"), styles);
  }
}

void HtmlTemplateService::fillThemeStylesWithContent(QString &p_template,
                                                     const QString &p_webStyleContent,
                                                     const QString &p_highlightStyleSheetFile) {
  QString styles;
  if (!p_webStyleContent.isEmpty()) {
    // Defensive: reject content containing </style> to prevent template injection.
    if (p_webStyleContent.contains(QStringLiteral("</style>"), Qt::CaseInsensitive)) {
      qWarning() << "HtmlTemplateService: web style content contains </style>, refusing to inline";
    } else {
      styles += QStringLiteral("<style type=\"text/css\">\n%1\n</style>\n").arg(p_webStyleContent);
    }
  }
  styles += fillStyleTag(p_highlightStyleSheetFile); // highlight CSS unchanged: still <link>
  if (!styles.isEmpty()) {
    p_template.replace(QStringLiteral("<!-- VX_THEME_STYLES_PLACEHOLDER -->"), styles);
  }
}

void HtmlTemplateService::fillGlobalStyles(QString &p_template, const WebResource &p_resource,
                                           const QString &p_additionalStyles) const {
  QString styles;
  for (const auto &ele : p_resource.m_resources) {
    if (ele.isGlobal()) {
      if (ele.m_enabled) {
        for (const auto &style : ele.m_styles) {
          auto styleFile = resolveConfigFile(style);
          auto content = readFile(styleFile);
          if (!content.isEmpty()) {
            styles += content;
          }
        }
      }
      break;
    }
  }

  styles += p_additionalStyles;

  if (!styles.isEmpty()) {
    p_template.replace(QStringLiteral("/* VX_GLOBAL_STYLES_PLACEHOLDER */"), styles);
  }
}

void HtmlTemplateService::fillResources(QString &p_template, const WebResource &p_resource) const {
  QString styles;
  QString scripts;

  for (const auto &ele : p_resource.m_resources) {
    if (ele.m_enabled && !ele.isGlobal()) {
      for (const auto &style : ele.m_styles) {
        auto styleFile = resolveConfigFile(style);
        styles += fillStyleTag(styleFile);
      }
      for (const auto &script : ele.m_scripts) {
        auto scriptFile = resolveConfigFile(script);
        scripts += fillScriptTag(scriptFile);
      }
    }
  }

  if (!styles.isEmpty()) {
    p_template.replace(QStringLiteral("<!-- VX_STYLES_PLACEHOLDER -->"), styles);
  }
  if (!scripts.isEmpty()) {
    p_template.replace(QStringLiteral("<!-- VX_SCRIPTS_PLACEHOLDER -->"), scripts);
  }
}

QString HtmlTemplateService::fillStyleTagForUrl(const QString &p_url) {
  if (p_url.isEmpty()) {
    return QString();
  }
  return QStringLiteral("<link rel=\"stylesheet\" type=\"text/css\" href=\"%1\">\n").arg(p_url);
}

QString HtmlTemplateService::fillScriptTagForUrl(const QString &p_url, bool p_module) {
  if (p_url.isEmpty()) {
    return QString();
  }
  // A module script is deferred; a classic one is not. That ordering difference is
  // the contract pdfviewer.mjs relies on — see its header comment.
  return QStringLiteral("<script type=\"%1\" src=\"%2\"></script>\n")
      .arg(p_module ? QStringLiteral("module") : QStringLiteral("text/javascript"), p_url);
}

void HtmlTemplateService::fillPdfResources(QString &p_template, const WebResource &p_resource) {
  QString styles;
  QString scripts;

  for (const auto &ele : p_resource.m_resources) {
    if (!ele.m_enabled || ele.isGlobal()) {
      continue;
    }
    for (const auto &style : ele.m_styles) {
      styles += fillStyleTagForUrl(VxPdfScheme::assetUrl(style));
    }
    for (const auto &script : ele.m_scripts) {
      const bool isModule = script.endsWith(QStringLiteral(".mjs"), Qt::CaseInsensitive);
      scripts += fillScriptTagForUrl(VxPdfScheme::assetUrl(script), isModule);
    }
  }

  if (!styles.isEmpty()) {
    p_template.replace(QStringLiteral("<!-- VX_STYLES_PLACEHOLDER -->"), styles);
  }
  if (!scripts.isEmpty()) {
    p_template.replace(QStringLiteral("<!-- VX_SCRIPTS_PLACEHOLDER -->"), scripts);
  }
}

void HtmlTemplateService::fillResourcesByContent(QString &p_template,
                                                 const WebResource &p_resource) const {
  QString styles;
  QString scripts;

  for (const auto &ele : p_resource.m_resources) {
    if (ele.m_enabled && !ele.isGlobal()) {
      for (const auto &style : ele.m_styles) {
        auto styleFile = resolveConfigFile(style);
        auto content = readFile(styleFile);
        if (!content.isEmpty()) {
          styles += content;
        }
      }
      for (const auto &script : ele.m_scripts) {
        auto scriptFile = resolveConfigFile(script);
        auto content = readFile(scriptFile);
        if (!content.isEmpty()) {
          scripts += content;
        }
      }
    }
  }

  if (!styles.isEmpty()) {
    p_template.replace(QStringLiteral("/* VX_STYLES_PLACEHOLDER */"), styles);
  }
  if (!scripts.isEmpty()) {
    p_template.replace(QStringLiteral("/* VX_SCRIPTS_PLACEHOLDER */"), scripts);
  }
}

// ============ PDF Viewer Template ============

void HtmlTemplateService::updatePdfViewerTemplate(const PdfViewerConfig &p_config,
                                                  const QString &p_commentColorsCss, bool p_force) {
  if (!p_force && p_config.revision() == m_pdfViewerTemplate.m_revision &&
      p_commentColorsCss == m_pdfViewerCommentColorsCss) {
    return;
  }

  m_pdfViewerTemplate.m_revision = p_config.revision();
  m_pdfViewerCommentColorsCss = p_commentColorsCss;
  generatePdfViewerTemplate(p_config, p_commentColorsCss, m_pdfViewerTemplate);
}

const QString &HtmlTemplateService::getPdfViewerTemplate() const {
  return m_pdfViewerTemplate.m_template;
}

const QString &HtmlTemplateService::getPdfViewerTemplatePath() const {
  return m_pdfViewerTemplate.m_templatePath;
}

void HtmlTemplateService::generatePdfViewerTemplate(const PdfViewerConfig &p_config,
                                                    const QString &p_commentColorsCss,
                                                    Template &p_template) const {
  const auto &viewerResource = p_config.getViewerResource();
  p_template.m_templatePath = resolveConfigFile(viewerResource.m_template);

  p_template.m_template = readFile(p_template.m_templatePath);
  if (p_template.m_template.isEmpty()) {
    p_template.m_template = errorPage();
    return;
  }

  if (!p_commentColorsCss.isEmpty()) {
    // Same defensive guard fillThemeStylesWithContent uses: content that closes
    // the style element would let a palette value inject markup.
    if (p_commentColorsCss.contains(QStringLiteral("</style>"), Qt::CaseInsensitive)) {
      qWarning() << "HtmlTemplateService: comment colors contain </style>, refusing to inline";
    } else {
      p_template.m_template.replace(
          QStringLiteral("<!-- VX_PDF_VARS_PLACEHOLDER -->"),
          QStringLiteral("<style type=\"text/css\">\n%1</style>").arg(p_commentColorsCss));
    }
  }

  fillPdfResources(p_template.m_template, viewerResource);
}

// ============ Markdown Viewer Template ============

void HtmlTemplateService::updateMarkdownViewerTemplate(const MarkdownEditorConfig &p_config,
                                                       const QString &p_webStyleContent,
                                                       const QString &p_highlightStyleSheetFile,
                                                       bool p_force) {
  if (!p_force && p_config.revision() == m_markdownViewerTemplate.m_revision) {
    return;
  }

  m_markdownViewerTemplate.m_revision = p_config.revision();

  const auto &viewerResource = p_config.getViewerResource();
  auto templateFile = resolveConfigFile(viewerResource.m_template);

  m_markdownViewerTemplate.m_templatePath = templateFile;
  m_markdownViewerTemplate.m_template = readFile(templateFile);
  if (m_markdownViewerTemplate.m_template.isEmpty()) {
    m_markdownViewerTemplate.m_template = errorPage();
    return;
  }

  fillGlobalStyles(m_markdownViewerTemplate.m_template, viewerResource, QString());
  fillThemeStylesWithContent(m_markdownViewerTemplate.m_template, p_webStyleContent,
                             p_highlightStyleSheetFile);

  {
    MarkdownWebGlobalOptions opts;
    opts.m_webPlantUml = p_config.getWebPlantUml();
    opts.m_plantUmlWebService = p_config.getPlantUmlWebService();
    opts.m_webGraphviz = p_config.getWebGraphviz();
    opts.m_mathRenderer = p_config.getMathRenderer();
    opts.m_mathJaxScript = p_config.getMathJaxScript();
    opts.m_constrainImageWidthEnabled = p_config.getConstrainImageWidthEnabled();
    opts.m_imageAlignCenterEnabled = p_config.getImageAlignCenterEnabled();
    opts.m_headingFoldingEnabled = p_config.getHeadingFoldingEnabled();
    opts.m_protectFromXss = p_config.getProtectFromXss();
    opts.m_htmlTagEnabled = p_config.getHtmlTagEnabled();
    opts.m_autoBreakEnabled = p_config.getAutoBreakEnabled();
    opts.m_linkifyEnabled = p_config.getLinkifyEnabled();
    opts.m_indentFirstLineEnabled = p_config.getIndentFirstLineEnabled();
    opts.m_codeBlockLineNumberEnabled = p_config.getCodeBlockLineNumberEnabled();
    opts.m_codeBlockLineWrapEnabled = p_config.getCodeBlockLineWrapEnabled();
    m_markdownViewerTemplate.m_template.replace(
        QStringLiteral("/* VX_GLOBAL_OPTIONS_PLACEHOLDER */"), opts.toJavascriptObject());
  }

  // Required viewer feature, after configured dependencies even for older resource lists.
  const auto scriptPlaceholder = QStringLiteral("<!-- VX_SCRIPTS_PLACEHOLDER -->");
  m_markdownViewerTemplate.m_template.replace(
      scriptPlaceholder, scriptPlaceholder + fillScriptTag(resolveConfigFile(
                                                 QStringLiteral("web/js/sectionnumber.js"))));
  fillResources(m_markdownViewerTemplate.m_template, viewerResource);
}

const QString &HtmlTemplateService::getMarkdownViewerTemplate() const {
  return m_markdownViewerTemplate.m_template;
}

QString HtmlTemplateService::protectedContentSecurityPolicy(const QString &p_nonce) {
  return QStringLiteral("default-src 'none'; script-src 'nonce-%1'; "
                        "style-src 'unsafe-inline' vxnote:; img-src vxnote: data:; "
                        "font-src vxnote: data:; connect-src 'none'; media-src 'none'; "
                        "frame-src 'none'; object-src 'none'; base-uri 'none'; form-action 'none'")
      .arg(p_nonce);
}

QString HtmlTemplateService::protectedMarkdownViewerTemplate(
    const QString &p_token, const QString &p_nonce, QHash<QString, QByteArray> &p_resources) const {
  p_resources.clear();
  static const QRegularExpression c_token(QStringLiteral("^[a-f0-9]{32}$"));
  if (!c_token.match(p_token).hasMatch() || !c_token.match(p_nonce).hasMatch()) {
    return QString();
  }

  // Mount only the shipped RCC, under a private root. Never consult the editable
  // config copy, custom templates, user.css, or a URL-derived filesystem path.
  struct Bundle {
    QString file = QStringLiteral("app:vnote_extra.rcc");
    QString root;
    bool registered = false;
    ~Bundle() {
      if (registered) {
        QResource::unregisterResource(file, root);
      }
    }
  } bundle;
  bundle.root = QStringLiteral("/vxnote-template-") + p_token;
  bundle.registered = QResource::registerResource(bundle.file, bundle.root);
  if (!bundle.registered) {
    return QString();
  }
  const auto bundleRoot = QLatin1Char(':') + bundle.root + QStringLiteral("/vnotex/data/extra/");
  auto readBundled = [&bundleRoot](const QString &p_path) {
    QFile file(bundleRoot + p_path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
  };
  auto html = QString::fromUtf8(readBundled(QStringLiteral("web/markdown-viewer-template.html")));
  if (html.isEmpty()) {
    return QString();
  }
  const auto origin = QStringLiteral("vxnote://") + p_token;
  // Ordering follows the built-in Markdown viewer resource definition. This is
  // a fixed execution allowlist, intentionally independent of user config.
  const QStringList scripts = {
      QStringLiteral("web/js/qwebchannel.js"),
      QStringLiteral("web/js/eventemitter.js"),
      QStringLiteral("web/js/vxcore.js"),
      QStringLiteral("web/js/utils.js"),
      QStringLiteral("web/js/nodelinemapper.js"),
      QStringLiteral("web/js/lrucache.js"),
      QStringLiteral("web/js/graphcache.js"),
      QStringLiteral("web/js/graphpreviewer.js"),
      QStringLiteral("web/js/markdownviewercore.js"),
      QStringLiteral("web/js/vxworker.js"),
      QStringLiteral("web/js/graphrenderer.js"),
      QStringLiteral("web/js/svg-to-image.js"),
      QStringLiteral("web/js/computed-style-to-inline-style.js"),
      QStringLiteral("web/js/imageviewer.js"),
      QStringLiteral("web/js/easyaccess.js"),
      QStringLiteral("web/js/crosscopy.js"),
      QStringLiteral("web/js/markdownviewer.js"),
      QStringLiteral("web/js/markdown-it/markdown-it.min.js"),
      QStringLiteral("web/js/markdown-it/markdown-it-container.min.js"),
      QStringLiteral("web/js/markdown-it/markdown-it-emoji.min.js"),
      QStringLiteral("web/js/markdown-it/markdown-it-footnote.min.js"),
      QStringLiteral("web/js/markdown-it/markdown-it-front-matter.js"),
      QStringLiteral("web/js/markdown-it/markdown-it-imsize.min.js"),
      QStringLiteral("web/js/markdown-it/markdown-it-sub.min.js"),
      QStringLiteral("web/js/markdown-it/markdown-it-sup.min.js"),
      QStringLiteral("web/js/markdown-it/markdown-it-task-lists.js"),
      QStringLiteral("web/js/markdown-it/markdown-it-texmath.js"),
      QStringLiteral("web/js/markdown-it/markdown-it-inject-linenumbers.js"),
      QStringLiteral("web/js/markdown-it/markdownItAnchor.umd.js"),
      QStringLiteral("web/js/markdown-it/markdownItTocDoneRight.umd.js"),
      QStringLiteral("web/js/markdown-it/markdown-it-implicit-figure.js"),
      QStringLiteral("web/js/markdown-it/markdown-it-mark.min.js"),
      QStringLiteral("web/js/markdownit.js"),
      QStringLiteral("web/js/prism.js"),
      QStringLiteral("web/js/codeblockactions.js"),
      QStringLiteral("web/js/mermaid.js"),
      QStringLiteral("web/js/flowchart.js/raphael.min.js"),
      QStringLiteral("web/js/flowchart.js/flowchart.min.js"),
      QStringLiteral("web/js/flowchartjs.js"),
      QStringLiteral("web/js/wavedrom.js"),
      QStringLiteral("web/js/mathjax.js"),
      QStringLiteral("web/js/plantuml.js"),
      QStringLiteral("web/js/graphviz.js"),
      QStringLiteral("web/js/turndown/turndown.js"),
      QStringLiteral("web/js/turndown/turndown-plugin-gfm.js"),
      QStringLiteral("web/js/turndown.js"),
      QStringLiteral("web/js/mark.js/mark.min.js"),
      QStringLiteral("web/js/markjs.js")};
  const QStringList lazyScripts = {QStringLiteral("web/js/prism/prism.min.js"),
                                   QStringLiteral("web/js/mermaid/mermaid.min.js"),
                                   QStringLiteral("web/js/wavedrom/theme-default.js"),
                                   QStringLiteral("web/js/wavedrom/wavedrom.min.js"),
                                   QStringLiteral("web/js/viz.js/viz.js"),
                                   QStringLiteral("web/js/viz.js/lite.render.js")};
  const QStringList styles = {
      QStringLiteral("web/css/globalstyles.css"),  QStringLiteral("themes/pure/web.css"),
      QStringLiteral("themes/pure/highlight.css"), QStringLiteral("web/css/imageviewer.css"),
      QStringLiteral("web/css/markdownit.css"),    QStringLiteral("web/css/codeblockactions.css")};
  QHash<QString, QByteArray> resources;
  for (const auto &path : scripts + lazyScripts + styles) {
    auto bytes = readBundled(path);
    if (bytes.isEmpty()) {
      return QString();
    }
    if (path == QStringLiteral("web/js/prism.js")) {
      // This tag is made by our bundled loader, never by note-generated HTML.
      bytes.replace("let scriptNode = document.createElement('script');",
                    "let scriptNode = document.createElement('script');\n"
                    "    scriptNode.nonce = document.currentScript.nonce;");
    }
    resources.insert(QStringLiteral("/app/") + path, bytes);
  }

  QJsonArray scriptUrls;
  for (const auto &path : scripts + lazyScripts) {
    scriptUrls.append(origin + QStringLiteral("/app/") + path);
  }
  // Only this application-owned loader may apply the nonce to dynamic scripts;
  // no DOM observer ever blesses a script inserted by a note or diagram.
  const auto loader =
      QStringLiteral(R"VX((function() {
    const nonce = document.currentScript.nonce;
    const allowed = new Set(%1);
    Utils.loadScript = function(src, callback) {
      let url;
      try { url = new URL(src, document.URL).href; } catch (_) { url = ''; }
      if (!allowed.has(url)) {
        if (callback) { callback(); }
        return;
      }
      const script = document.createElement('script');
      script.nonce = nonce;
      script.type = 'text/javascript';
      script.src = url;
      if (callback) { script.onload = callback; script.onerror = callback; }
      document.head.appendChild(script);
    };
})(); )VX")
          .arg(QString::fromUtf8(QJsonDocument(scriptUrls).toJson(QJsonDocument::Compact)));
  QString scriptTags;
  for (const auto &path : scripts) {
    scriptTags += QStringLiteral("<script nonce=\"%1\" src=\"%2/app/%3\"></script>\n")
                      .arg(p_nonce, origin, path);
    if (path == QStringLiteral("web/js/utils.js")) {
      scriptTags += QStringLiteral("<script nonce=\"%1\">%2</script>\n").arg(p_nonce, loader);
    }
  }
  QString styleTags;
  for (const auto &path : styles) {
    styleTags += fillStyleTagForUrl(origin + QStringLiteral("/app/") + path);
  }

  MarkdownWebGlobalOptions options;
  options.m_webPlantUml = false;
  options.m_webGraphviz = true;
  options.m_protectFromXss = true;
  const auto globalOptions =
      options.toJavascriptObject() + QStringLiteral("\nwindow.vxOptions.protectedView = true;\n");
  html.replace(
      QStringLiteral("<head>"),
      QStringLiteral("<head>\n<meta http-equiv=\"Content-Security-Policy\" content=\"%1\">\n")
          .arg(protectedContentSecurityPolicy(p_nonce).toHtmlEscaped()));
  // This replacement touches the bundled template only, before any note data.
  html.replace(QStringLiteral("<script type=\"text/javascript\">"),
               QStringLiteral("<script nonce=\"%1\" type=\"text/javascript\">").arg(p_nonce));
  html.replace(QStringLiteral("/* VX_GLOBAL_STYLES_PLACEHOLDER */"), QString());
  html.replace(QStringLiteral("<!-- VX_THEME_STYLES_PLACEHOLDER -->"), QString());
  html.replace(QStringLiteral("/* VX_GLOBAL_OPTIONS_PLACEHOLDER */"), globalOptions);
  html.replace(QStringLiteral("<!-- VX_STYLES_PLACEHOLDER -->"), styleTags);
  html.replace(QStringLiteral("<!-- VX_SCRIPTS_PLACEHOLDER -->"), scriptTags);
  p_resources.swap(resources);
  return html;
}

// ============ MindMap Editor Template ============

void HtmlTemplateService::updateMindMapEditorTemplate(const MindMapEditorConfig &p_config,
                                                      const QString &p_webStyleSheetFile,
                                                      bool p_force) {
  if (!p_force && p_config.revision() == m_mindMapEditorTemplate.m_revision) {
    return;
  }

  m_mindMapEditorTemplate.m_revision = p_config.revision();
  generateMindMapEditorTemplate(p_config, p_webStyleSheetFile, m_mindMapEditorTemplate);
}

const QString &HtmlTemplateService::getMindMapEditorTemplate() const {
  return m_mindMapEditorTemplate.m_template;
}

void HtmlTemplateService::generateMindMapEditorTemplate(const MindMapEditorConfig &p_config,
                                                        const QString &p_webStyleSheetFile,
                                                        Template &p_template) const {
  const auto &editorResource = p_config.getEditorResource();
  p_template.m_templatePath = resolveConfigFile(editorResource.m_template);

  p_template.m_template = readFile(p_template.m_templatePath);
  if (p_template.m_template.isEmpty()) {
    p_template.m_template = errorPage();
    return;
  }

  fillThemeStyles(p_template.m_template, p_webStyleSheetFile, QString());
  fillResources(p_template.m_template, editorResource);
}
