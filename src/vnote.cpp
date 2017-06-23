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

extern VConfigManager vconfig;

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

const QString VNote::c_mathjaxJsFile = "https://cdn.mathjax.org/mathjax/latest/MathJax.js?config=TeX-MML-AM_CHTML";

const QString VNote::c_shortcutsDocFile_en = ":/resources/docs/shortcuts_en.md";
const QString VNote::c_shortcutsDocFile_zh = ":/resources/docs/shortcuts_zh.md";

VNote::VNote(QObject *parent)
    : QObject(parent), m_mainWindow(dynamic_cast<VMainWindow *>(parent))
{
    initTemplate();
    vconfig.getNotebooks(m_notebooks, this);
}

void VNote::initPalette(QPalette palette)
{
    m_palette.clear();

    m_palette.append(QPair<QString, QString>("base-background",
                                             palette.background().color().name()));
    m_palette.append(QPair<QString, QString>("base-foreground",
                                             palette.background().color().name()));
    m_palette.append(QPair<QString, QString>("hover-color", "#42A5F5"));
    m_palette.append(QPair<QString, QString>("base-color", "#BDBDBD"));
    m_palette.append(QPair<QString, QString>("focus-color", "#75C5B5"));
    m_palette.append(QPair<QString, QString>("logo-base", "#D6EACE"));
    m_palette.append(QPair<QString, QString>("logo-max", "#15AE67"));
    m_palette.append(QPair<QString, QString>("logo-min", "#75C5B5"));

    // Material Design Colors
    m_palette.append(QPair<QString, QString>("Teal0", "#E0F2F1"));
    m_palette.append(QPair<QString, QString>("Teal1", "#B2DFDB"));
    m_palette.append(QPair<QString, QString>("Teal2", "#80CBC4"));
    m_palette.append(QPair<QString, QString>("Teal3", "#4DB6AC"));
    m_palette.append(QPair<QString, QString>("Teal4", "#26A69A"));
    m_palette.append(QPair<QString, QString>("Teal5", "#009688"));

    m_palette.append(QPair<QString, QString>("Indigo0", "#E8EAF6"));
    m_palette.append(QPair<QString, QString>("Indigo1", "#C5CAE9"));
    m_palette.append(QPair<QString, QString>("Indigo2", "#9FA8DA"));
    m_palette.append(QPair<QString, QString>("Indigo3", "#7986CB"));
    m_palette.append(QPair<QString, QString>("Indigo4", "#5C6BC0"));

    m_palette.append(QPair<QString, QString>("Grey0", "#FAFAFA"));
    m_palette.append(QPair<QString, QString>("Grey1", "#F5F5F5"));
    m_palette.append(QPair<QString, QString>("Grey2", "#EEEEEE"));
    m_palette.append(QPair<QString, QString>("Grey3", "#E0E0E0"));
    m_palette.append(QPair<QString, QString>("Grey4", "#BDBDBD"));
    m_palette.append(QPair<QString, QString>("Grey5", "#9E9E9E"));
    m_palette.append(QPair<QString, QString>("Grey6", "#757575"));
    m_palette.append(QPair<QString, QString>("Grey7", "#616161"));
    m_palette.append(QPair<QString, QString>("Grey8", "#424242"));

    m_palette.append(QPair<QString, QString>("Green0", "#E8F5E9"));
    m_palette.append(QPair<QString, QString>("Green1", "#C8E6C9"));
    m_palette.append(QPair<QString, QString>("Green2", "#A5D6A7"));
    m_palette.append(QPair<QString, QString>("Green3", "#81C784"));
    m_palette.append(QPair<QString, QString>("Green4", "#66BB6A"));
    m_palette.append(QPair<QString, QString>("Green5", "#4CAF50"));
    m_palette.append(QPair<QString, QString>("Green6", "#43A047"));
    m_palette.append(QPair<QString, QString>("Green7", "#388E3C"));
    m_palette.append(QPair<QString, QString>("Green8", "#2E7D32"));
    m_palette.append(QPair<QString, QString>("Green9", "#1B5E20"));

    m_palette.append(QPair<QString, QString>("DeepPurple0", "#EDE7F6"));
    m_palette.append(QPair<QString, QString>("DeepPurple1", "#D1C4E9"));
    m_palette.append(QPair<QString, QString>("DeepPurple2", "#B39DDB"));
    m_palette.append(QPair<QString, QString>("DeepPurple3", "#9575CD"));
    m_palette.append(QPair<QString, QString>("DeepPurple4", "#7E57C2"));
    m_palette.append(QPair<QString, QString>("DeepPurple5", "#673AB7"));
    m_palette.append(QPair<QString, QString>("DeepPurple6", "#5E35B1"));
    m_palette.append(QPair<QString, QString>("DeepPurple7", "#512DA8"));
    m_palette.append(QPair<QString, QString>("DeepPurple8", "#4527A0"));
    m_palette.append(QPair<QString, QString>("DeepPurple9", "#311B92"));

    m_palette.append(QPair<QString, QString>("Purple0", "#F3E5F5"));
    m_palette.append(QPair<QString, QString>("Purple1", "#E1BEE7"));
    m_palette.append(QPair<QString, QString>("Purple2", "#CE93D8"));
    m_palette.append(QPair<QString, QString>("Purple3", "#BA68C8"));
    m_palette.append(QPair<QString, QString>("Purple4", "#AB47BC"));
    m_palette.append(QPair<QString, QString>("Purple5", "#9C27B0"));
    m_palette.append(QPair<QString, QString>("Purple6", "#8E24AA"));
    m_palette.append(QPair<QString, QString>("Purple7", "#7B1FA2"));
    m_palette.append(QPair<QString, QString>("Purple8", "#6A1B9A"));
    m_palette.append(QPair<QString, QString>("Purple9", "#4A148C"));

    m_palette.append(QPair<QString, QString>("Red0", "#FFEBEE"));
    m_palette.append(QPair<QString, QString>("Red1", "#FFCDD2"));
    m_palette.append(QPair<QString, QString>("Red2", "#EF9A9A"));
    m_palette.append(QPair<QString, QString>("Red3", "#E57373"));
    m_palette.append(QPair<QString, QString>("Red4", "#EF5350"));
    m_palette.append(QPair<QString, QString>("Red5", "#F44336"));
    m_palette.append(QPair<QString, QString>("Red6", "#E53935"));
    m_palette.append(QPair<QString, QString>("Red7", "#D32F2F"));
    m_palette.append(QPair<QString, QString>("Red8", "#C62828"));
    m_palette.append(QPair<QString, QString>("Red9", "#B71C1C"));
}

QString VNote::getColorFromPalette(const QString &p_name) const
{
    for (int i = 0; i < m_palette.size(); ++i) {
        if (m_palette[i].first == p_name) {
            return m_palette[i].second;
        }
    }
    return "White";
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
    const QString &curRenderBg = vconfig.getCurRenderBackgroundColor();
    const QVector<VColor> &predefinedColors = vconfig.getPredefinedColors();
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

    if (vconfig.getEnableImageConstraint()) {
        // Constain the image width.
        cssStyle += "img { max-width: 100% !important; height: auto !important; }\n";
    }

    const QString styleHolder("<!-- BACKGROUND_PLACE_HOLDER -->");
    const QString cssHolder("CSS_PLACE_HOLDER");

    s_markdownTemplate = VUtils::readFileFromDisk(c_markdownTemplatePath);
    s_markdownTemplate.replace(cssHolder, vconfig.getTemplateCssUrl());

    s_markdownTemplatePDF = s_markdownTemplate;

    if (!cssStyle.isEmpty()) {
        s_markdownTemplate.replace(styleHolder, cssStyle);
    }

    // Shoudl not display scrollbar in PDF.
    cssStyle += "pre code { white-space: pre-wrap !important; "
                           "word-break: break-all !important; }\n";
    if (!vconfig.getEnableImageConstraint()) {
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
    int fontPt = 15;
    QString fontFamily = getMonospacedFont();

    if (p_str.size() != lastLen) {
        QFont font(fontFamily, fontPt);
        font.setBold(true);
        QFontMetrics fm(font);
        pxWidth = fm.width(p_str);
        lastLen = p_str.size();
    }

    return QString("background-color: %1;"
                   "color: %2;"
                   "font-size: %3pt;"
                   "font: bold;"
                   "font-family: %4;"
                   "border-radius: 3px;"
                   "min-width: %5px;"
                   "max-width: %5px;")
                   .arg(getColorFromPalette("logo-base"))
                   .arg(getColorFromPalette("logo-max"))
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

VFile *VNote::getOrphanFile(const QString &p_path)
{
    if (p_path.isEmpty()) {
        return NULL;
    }

    // See if the file has already been opened before.
    for (auto const &file : m_externalFiles) {
        if (file->getType() == FileType::Orphan && file->retrivePath() == p_path) {
            qDebug() << "find a VFile for path" << p_path;
            return file;
        }
    }

    // TODO: Clean up unopened file here.

    // Create a VOrphanFile for p_path.
    VOrphanFile *file = new VOrphanFile(p_path, this);
    m_externalFiles.append(file);
    return file;
}
