#include <QSettings>
#include <QDebug>
#include <QJsonObject>
#include <QJsonArray>
#include <QDir>
#include <QFont>
#include <QFontMetrics>
#include <QStringList>
#include <QFontDatabase>
#include "vnote.h"
#include "utils/vutils.h"
#include "vconfigmanager.h"
#include "vorphanfile.h"
#include "vnotefile.h"
#include "vpalette.h"

extern VConfigManager *g_config;

extern VPalette *g_palette;

// Meta word manager.
VMetaWordManager *g_mwMgr;

QString VNote::s_simpleHtmlTemplate;

QString VNote::s_markdownTemplate;

QString VNote::s_sloganTemplate = "<div class=\"typewriter\"><h3>Hi Markdown, I'm VNote</h3></div>";

const QString VNote::c_hoedownJsFile = ":/resources/hoedown.js";

const QString VNote::c_markedJsFile = ":/resources/marked.js";
const QString VNote::c_markedExtraFile = ":/utils/marked/marked.min.js";

const QString VNote::c_markdownitJsFile = ":/resources/markdown-it.js";
const QString VNote::c_markdownitExtraFile = ":/utils/markdown-it/markdown-it.min.js";
const QString VNote::c_markdownitAnchorExtraFile = ":/utils/markdown-it/markdown-it-headinganchor.js";
const QString VNote::c_markdownitTaskListExtraFile = ":/utils/markdown-it/markdown-it-task-lists.min.js";
const QString VNote::c_markdownitSubExtraFile = ":/utils/markdown-it/markdown-it-sub.min.js";
const QString VNote::c_markdownitSupExtraFile = ":/utils/markdown-it/markdown-it-sup.min.js";
const QString VNote::c_markdownitFootnoteExtraFile = ":/utils/markdown-it/markdown-it-footnote.min.js";
const QString VNote::c_markdownitFrontMatterExtraFile = ":/utils/markdown-it/markdown-it-front-matter.js";
const QString VNote::c_markdownitImsizeExtraFile = ":/utils/markdown-it/markdown-it-imsize.min.js";
const QString VNote::c_markdownitEmojiExtraFile = ":/utils/markdown-it/markdown-it-emoji.min.js";
const QString VNote::c_markdownitTexMathExtraFile = ":/utils/markdown-it/markdown-it-texmath.js";
const QString VNote::c_markdownitContainerExtraFile = ":/utils/markdown-it/markdown-it-container.min.js";

const QString VNote::c_showdownJsFile = ":/resources/showdown.js";
const QString VNote::c_showdownExtraFile = ":/utils/showdown/showdown.min.js";
const QString VNote::c_showdownAnchorExtraFile = ":/utils/showdown/showdown-headinganchor.js";

const QString VNote::c_turndownJsFile = ":/utils/turndown/turndown.js";
const QString VNote::c_turndownGfmExtraFile = ":/utils/turndown/turndown-plugin-gfm.js";

const QString VNote::c_mermaidApiJsFile = ":/utils/mermaid/mermaid.min.js";
const QString VNote::c_mermaidForestCssFile = ":/utils/mermaid/mermaid.forest.css";

const QString VNote::c_flowchartJsFile = ":/utils/flowchart.js/flowchart.min.js";
const QString VNote::c_raphaelJsFile = ":/utils/flowchart.js/raphael.min.js";

const QString VNote::c_wavedromJsFile = ":/utils/wavedrom/wavedrom.min.js";
const QString VNote::c_wavedromThemeFile = ":/utils/wavedrom/wavedrom-theme.js";

const QString VNote::c_plantUMLJsFile = ":/utils/plantuml/synchro2.js";
const QString VNote::c_plantUMLZopfliJsFile = ":/utils/plantuml/zopfli.raw.min.js";

const QString VNote::c_highlightjsLineNumberExtraFile = ":/utils/highlightjs/highlightjs-line-numbers.min.js";

const QString VNote::c_docFileFolder = ":/resources/docs";

const QString VNote::c_shortcutsDocFile = "shortcuts.md";

const QString VNote::c_markdownGuideDocFile = "markdown_guide.md";

VNote::VNote(QObject *parent)
    : QObject(parent)
{
    initTemplate();

    g_config->getNotebooks(m_notebooks, this);

    g_mwMgr = &m_metaWordMgr;
}

void VNote::initTemplate()
{
    if (s_markdownTemplate.isEmpty()) {
        updateSimpletHtmlTemplate();

        updateTemplate();
    }
}

void VNote::updateSimpletHtmlTemplate()
{
    const QString c_simpleHtmlTemplatePath(":/resources/simple_template.html");

    const QString cssHolder("CSS_PLACE_HOLDER");

    s_simpleHtmlTemplate = VUtils::readFileFromDisk(c_simpleHtmlTemplatePath);
    g_palette->fillStyle(s_simpleHtmlTemplate);

    s_simpleHtmlTemplate.replace(cssHolder, g_config->getCssStyleUrl());
}

QString VNote::generateHtmlTemplate(const QString &p_renderBg,
                                    const QString &p_renderStyleUrl,
                                    const QString &p_codeBlockStyleUrl,
                                    bool p_isPDF)
{
    const QString c_markdownTemplatePath(":/resources/markdown_template.html");

    QString cssStyle;
    if (!p_renderBg.isEmpty()) {
        cssStyle += "body { background-color: " + p_renderBg + " !important; }\n";
    }

    if (g_config->getEnableImageConstraint()) {
        // Constain the image width.
        cssStyle += "img { max-width: 100% !important; height: auto !important; }\n";
    }

    QString templ = VUtils::readFileFromDisk(c_markdownTemplatePath);
    g_palette->fillStyle(templ);

    // Must replace the code block holder first.
    templ.replace(HtmlHolder::c_commonCssHolder, g_config->getCommonCssUrl());
    templ.replace(HtmlHolder::c_codeBlockCssHolder, p_codeBlockStyleUrl);
    templ.replace(HtmlHolder::c_cssHolder, p_renderStyleUrl);

    if (p_isPDF) {
        // Shoudl not display scrollbar in PDF.
        cssStyle += "pre { white-space: pre-wrap !important; "
                          "word-break: break-all !important; }\n"
                    "pre code { white-space: pre-wrap !important; "
                               "word-break: break-all !important; }\n"
                    "code { word-break: break-all !important; }\n"
                    "div.flowchart-diagram { overflow: hidden !important; }\n"
                    "div.mermaid-diagram { overflow: hidden !important; }\n"
                    "div.plantuml-diagram { overflow: hidden !important; }\n"
                    "a { word-break: break-all !important; }\n"
                    "td.hljs-ln-code { white-space: pre-wrap !important; "
                                      "word-break: break-all !important; }\n";
        if (!g_config->getEnableImageConstraint()) {
            // Constain the image width by force in PDF, otherwise, the PDF will
            // be cut off.
            cssStyle += "img { max-width: 100% !important; height: auto !important; }\n";
        }
    }

    if (!cssStyle.isEmpty()) {
        templ.replace(HtmlHolder::c_globalStyleHolder, cssStyle);
    }

    return templ;
}

QString VNote::generateExportHtmlTemplate(const QString &p_renderBg)
{
    const QString c_exportTemplatePath(":/resources/export/export_template.html");

    QString cssStyle;
    if (!p_renderBg.isEmpty()) {
        cssStyle += "body { background-color: " + p_renderBg + " !important; }\n";
    }

    if (g_config->getEnableImageConstraint()) {
        // Constain the image width.
        cssStyle += "img { max-width: 100% !important; height: auto !important; }\n";
    }

    QString templ = VUtils::readFileFromDisk(c_exportTemplatePath);
    g_palette->fillStyle(templ);

    if (!cssStyle.isEmpty()) {
        templ.replace(HtmlHolder::c_globalStyleHolder, cssStyle);
    }

    return templ;
}

QString VNote::generateMathJaxPreviewTemplate()
{
    const QString c_templatePath(":/resources/mathjax_preview_template.html");
    QString templ = VUtils::readFileFromDisk(c_templatePath);
    g_palette->fillStyle(templ);

    QString cssStyle;
    cssStyle += "div.flowchart-diagram { margin: 0px !important; "
                "                        padding: 0px 5px 0px 5px !important; }\n"
                "div.mermaid-diagram { margin: 0px !important; "
                "                      padding: 0px 5px 0px 5px !important; }\n";

    templ.replace(HtmlHolder::c_globalStyleHolder, cssStyle);

    templ.replace(HtmlHolder::c_cssHolder, g_config->getCssStyleUrl());

    templ.replace(HtmlHolder::c_scaleFactorHolder, QString::number(VUtils::calculateScaleFactor()));

    return templ;
}

void VNote::updateTemplate()
{
    QString renderBg = g_config->getRenderBackgroundColor(g_config->getCurRenderBackgroundColor());

    s_markdownTemplate = generateHtmlTemplate(renderBg,
                                              g_config->getCssStyleUrl(),
                                              g_config->getCodeBlockCssStyleUrl(),
                                              false);
}

const QVector<VNotebook *> &VNote::getNotebooks() const
{
    return m_notebooks;
}

QVector<VNotebook *> &VNote::getNotebooks()
{
    return m_notebooks;
}

QString VNote::getNavigationLabelStyle(const QString &p_str, bool p_small) const
{
    static int lastLen = -1;
    static int pxWidth = 24;
    static int pxHeight = 24;
    const int fontPt = p_small ? 12 : 15;

    QString fontFamily = getMonospaceFont();

    if (p_str.size() != lastLen) {
        QFont font(fontFamily, fontPt);
        font.setBold(true);
        QFontMetrics fm(font);
        pxWidth = fm.width(p_str);
        pxHeight = fm.capHeight() + 5;
        lastLen = p_str.size();
    }

    QColor bg(g_palette->color("navigation_label_bg"));
    bg.setAlpha(200);

    QString style = QString("background-color: %1;"
                            "color: %2;"
                            "font-size: %3pt;"
                            "font: bold;"
                            "font-family: %4;"
                            "border-radius: 3px;"
                            "min-width: %5px;"
                            "max-width: %5px;")
                           .arg(bg.name(QColor::HexArgb))
                           .arg(g_palette->color("navigation_label_fg"))
                           .arg(fontPt)
                           .arg(fontFamily)
                           .arg(pxWidth);

    if (p_small) {
        style += QString("margin: 0px;"
                         "padding: 0px;"
                         "min-height: %1px;"
                         "max-height: %1px;")
                        .arg(pxHeight);
    }

    return style;
}

const QString &VNote::getMonospaceFont()
{
    static QString font;
    if (font.isNull()) {
        QStringList candidates;
        candidates << "Consolas" << "Monaco" << "Andale Mono" << "Monospace" << "Courier New";
        QStringList availFamilies = QFontDatabase().families();

        for (int i = 0; i < candidates.size(); ++i) {
            QString family = candidates[i].trimmed().toLower();
            for (int j = 0; j < availFamilies.size(); ++j) {
                QString availFamily = availFamilies[j];
                availFamily.remove(QRegExp("\\[.*\\]"));
                if (family == availFamily.trimmed().toLower()) {
                    font = availFamily;
                    return font;
                }
            }
        }

        // Fallback to current font.
        font = QFont().family();
    }
    return font;
}

VOrphanFile *VNote::getOrphanFile(const QString &p_path, bool p_modifiable, bool p_systemFile)
{
    if (p_path.isEmpty()) {
        return NULL;
    }

    QString path = QDir::cleanPath(p_path);
    // See if the file has already been opened before.
    for (auto const &file : m_externalFiles) {
        if (VUtils::equalPath(QDir::cleanPath(file->fetchPath()), path)) {
            Q_ASSERT(file->isModifiable() == p_modifiable);
            Q_ASSERT(file->isSystemFile() == p_systemFile);
            return file;
        }
    }

    freeOrphanFiles();

    // Create a VOrphanFile for path.
    VOrphanFile *file = new VOrphanFile(this, path, p_modifiable, p_systemFile);
    m_externalFiles.append(file);
    return file;
}

VNoteFile *VNote::getInternalFile(const QString &p_path)
{
    VNoteFile *file = NULL;
    for (auto & nb : m_notebooks) {
        file = nb->tryLoadFile(p_path);
        if (file) {
            break;
        }
    }

    return file;
}

VFile *VNote::getFile(const QString &p_path, bool p_forceOrphan)
{
    VFile *file = NULL;
    if (!p_forceOrphan) {
        file = getInternalFile(p_path);
    }

    if (!file) {
        QFileInfo fi(p_path);
        if (fi.isNativePath()) {
            file = getOrphanFile(p_path, true, false);
        } else {
            // File in Qt resource system.
            file = getOrphanFile(p_path, false, true);
        }
    }

    return file;
}

VDirectory *VNote::getInternalDirectory(const QString &p_path)
{
    VDirectory *dir = NULL;
    for (auto & nb : m_notebooks) {
        dir = nb->tryLoadDirectory(p_path);
        if (dir) {
            break;
        }
    }

    return dir;

}

VNotebook *VNote::getNotebook(const QString &p_path)
{
    for (auto & nb : m_notebooks) {
        if (VUtils::equalPath(nb->getPath(), p_path)) {
            return nb;
        }
    }

    return NULL;
}

void VNote::freeOrphanFiles()
{
    for (int i = 0; i < m_externalFiles.size();) {
        VOrphanFile *file = m_externalFiles[i];
        if (!file->isOpened()) {
            qDebug() << "release orphan file" << file;
            m_externalFiles.removeAt(i);
            delete file;
        } else {
            ++i;
        }
    }
}
