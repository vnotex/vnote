#include <QSettings>
#include <QDebug>
#include <QJsonObject>
#include <QJsonArray>
#include <QDir>
#include "vnote.h"
#include "utils/vutils.h"
#include "vconfigmanager.h"
#include "vmainwindow.h"

extern VConfigManager vconfig;

QString VNote::s_markdownTemplate;
const QString VNote::c_hoedownJsFile = ":/resources/hoedown.js";
const QString VNote::c_markedJsFile = ":/resources/marked.js";
const QString VNote::c_markedExtraFile = ":/utils/marked/marked.min.js";
const QString VNote::c_markdownitJsFile = ":/resources/markdown-it.js";
const QString VNote::c_markdownitExtraFile = ":/utils/markdown-it/markdown-it.min.js";
const QString VNote::c_markdownitAnchorExtraFile = ":/utils/markdown-it/markdown-it-headinganchor.js";
const QString VNote::c_markdownitTaskListExtraFile = ":/utils/markdown-it/markdown-it-task-lists.min.js";
const QString VNote::c_mermaidApiJsFile = ":/utils/mermaid/mermaidAPI.min.js";
const QString VNote::c_mermaidCssFile = ":/utils/mermaid/mermaid.css";
const QString VNote::c_mermaidDarkCssFile = ":/utils/mermaid/mermaid.dark.css";
const QString VNote::c_mermaidForestCssFile = ":/utils/mermaid/mermaid.forest.css";

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
    m_palette.append(QPair<QString, QString>("focus-color", "#15AE67"));
    m_palette.append(QPair<QString, QString>("logo-base", "#D6EACE"));
    m_palette.append(QPair<QString, QString>("logo-max", "#15AE67"));
    m_palette.append(QPair<QString, QString>("logo-min", "#75C5B5"));

    // Material Design Colors
    m_palette.append(QPair<QString, QString>("Teal0", "#E0F2F1"));
    m_palette.append(QPair<QString, QString>("Teal1", "#B2DFDB"));
    m_palette.append(QPair<QString, QString>("Teal2", "#80CBC4"));
    m_palette.append(QPair<QString, QString>("Teal3", "#4DB6AC"));
    m_palette.append(QPair<QString, QString>("Teal4", "#26A69A"));

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
        cssStyle = "body { background-color: #" + rgb + "; }";
    }
    QString styleHolder("<!-- BACKGROUND_PLACE_HOLDER -->");
    QString cssHolder("CSS_PLACE_HOLDER");

    s_markdownTemplate = VUtils::readFileFromDisk(c_markdownTemplatePath);
    s_markdownTemplate.replace(cssHolder, vconfig.getTemplateCssUrl());
    if (!cssStyle.isEmpty()) {
        s_markdownTemplate.replace(styleHolder, cssStyle);
    }
}

const QVector<VNotebook *> &VNote::getNotebooks() const
{
    return m_notebooks;
}

QVector<VNotebook *> &VNote::getNotebooks()
{
    return m_notebooks;
}
