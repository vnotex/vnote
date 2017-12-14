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
#include "vmainwindow.h"
#include "vorphanfile.h"
#include "vnotefile.h"
#include "vpalette.h"

extern VConfigManager *g_config;

extern VPalette *g_palette;

// Meta word manager.
VMetaWordManager *g_mwMgr;

QString VNote::s_markdownTemplate;
QString VNote::s_markdownTemplatePDF;

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

const QString VNote::c_showdownJsFile = ":/resources/showdown.js";
const QString VNote::c_showdownExtraFile = ":/utils/showdown/showdown.min.js";
const QString VNote::c_showdownAnchorExtraFile = ":/utils/showdown/showdown-headinganchor.js";

const QString VNote::c_mermaidApiJsFile = ":/utils/mermaid/mermaidAPI.min.js";
const QString VNote::c_mermaidCssFile = ":/utils/mermaid/mermaid.css";
const QString VNote::c_mermaidDarkCssFile = ":/utils/mermaid/mermaid.dark.css";
const QString VNote::c_mermaidForestCssFile = ":/utils/mermaid/mermaid.forest.css";

const QString VNote::c_flowchartJsFile = ":/utils/flowchart.js/flowchart.min.js";
const QString VNote::c_raphaelJsFile = ":/utils/flowchart.js/raphael.min.js";

const QString VNote::c_highlightjsLineNumberExtraFile = ":/utils/highlightjs/highlightjs-line-numbers.min.js";

const QString VNote::c_shortcutsDocFile_en = ":/resources/docs/shortcuts_en.md";
const QString VNote::c_shortcutsDocFile_zh = ":/resources/docs/shortcuts_zh.md";

const QString VNote::c_markdownGuideDocFile_en = ":/resources/docs/markdown_guide_en.md";
const QString VNote::c_markdownGuideDocFile_zh = ":/resources/docs/markdown_guide_zh.md";

VNote::VNote(QObject *parent)
    : QObject(parent)
{
    initTemplate();

    g_config->getNotebooks(m_notebooks, this);

    m_metaWordMgr.init();

    g_mwMgr = &m_metaWordMgr;
}

void VNote::initTemplate()
{
    if (s_markdownTemplate.isEmpty()) {
        updateTemplate();
    }
}

void VNote::updateTemplate()
{
    const QString c_markdownTemplatePath(":/resources/markdown_template.html");

    // Get background color
    QString rgb;
    const QString &curRenderBg = g_config->getCurRenderBackgroundColor();
    const QVector<VColor> &predefinedColors = g_config->getPredefinedColors();
    if (curRenderBg != "System") {
        for (int i = 0; i < predefinedColors.size(); ++i) {
            if (predefinedColors[i].name == curRenderBg) {
                rgb = predefinedColors[i].rgb;
                break;
            }
        }
    }

    QString cssStyle;
    if (!rgb.isEmpty()) {
        cssStyle += "body { background-color: #" + rgb + " !important; }\n";
    }

    if (g_config->getEnableImageConstraint()) {
        // Constain the image width.
        cssStyle += "img { max-width: 100% !important; height: auto !important; }\n";
    }

    const QString styleHolder("<!-- BACKGROUND_PLACE_HOLDER -->");
    const QString cssHolder("CSS_PLACE_HOLDER");
    const QString codeBlockCssHolder("HIGHLIGHTJS_CSS_PLACE_HOLDER");

    s_markdownTemplate = VUtils::readFileFromDisk(c_markdownTemplatePath);
    g_palette->fillStyle(s_markdownTemplate);

    // Must replace the code block holder first.
    s_markdownTemplate.replace(codeBlockCssHolder, g_config->getCodeBlockCssStyleUrl());
    s_markdownTemplate.replace(cssHolder, g_config->getCssStyleUrl());

    s_markdownTemplatePDF = s_markdownTemplate;

    if (!cssStyle.isEmpty()) {
        s_markdownTemplate.replace(styleHolder, cssStyle);
    }

    // Shoudl not display scrollbar in PDF.
    cssStyle += "pre code { white-space: pre-wrap !important; "
                           "word-break: break-all !important; }\n";
    if (!g_config->getEnableImageConstraint()) {
        // Constain the image width by force in PDF, otherwise, the PDF will
        // be cut off.
        cssStyle += "img { max-width: 100% !important; height: auto !important; }\n";
    }

    s_markdownTemplatePDF.replace(styleHolder, cssStyle);
}

const QVector<VNotebook *> &VNote::getNotebooks() const
{
    return m_notebooks;
}

QVector<VNotebook *> &VNote::getNotebooks()
{
    return m_notebooks;
}

QString VNote::getNavigationLabelStyle(const QString &p_str) const
{
    static int lastLen = -1;
    static int pxWidth = 24;
    const int fontPt = 15;

    QString fontFamily = getMonospacedFont();

    if (p_str.size() != lastLen) {
        QFont font(fontFamily, fontPt);
        font.setBold(true);
        QFontMetrics fm(font);
        pxWidth = fm.width(p_str);
        lastLen = p_str.size();
    }

    QColor bg(g_palette->color("navigation_label_bg"));
    bg.setAlpha(200);

    return QString("background-color: %1;"
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
}

const QString &VNote::getMonospacedFont() const
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

    for (int i = 0; i < m_externalFiles.size(); ++i) {
        VOrphanFile *file = m_externalFiles[i];
        if (!file->isOpened()) {
            qDebug() << "release orphan file" << file;
            m_externalFiles.removeAt(i);
            delete file;
            --i;
        }
    }

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

VFile *VNote::getFile(const QString &p_path)
{
    VFile *file = getInternalFile(p_path);
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
