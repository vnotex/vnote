#include <QSettings>
#include <QDebug>
#include <QJsonObject>
#include <QJsonArray>
#include <QDir>
#include "vnote.h"
#include "utils/vutils.h"
#include "vconfigmanager.h"

VConfigManager vconfig;

QString VNote::templateHtml;
QString VNote::preTemplateHtml;
QString VNote::postTemplateHtml;

VNote::VNote(QObject *parent)
    : QObject(parent)
{
    vconfig.initialize();
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
}

void VNote::initTemplate()
{
    if (templateHtml.isEmpty() || preTemplateHtml.isEmpty()
        || postTemplateHtml.isEmpty()) {
        updateTemplate();
    }
}

void VNote::updateTemplate()
{
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
    templateHtml = VUtils::readFileFromDisk(vconfig.getTemplatePath());
    templateHtml.replace(cssHolder, vconfig.getTemplateCssUrl());
    if (!cssStyle.isEmpty()) {
        templateHtml.replace(styleHolder, cssStyle);
    }

    preTemplateHtml = VUtils::readFileFromDisk(vconfig.getPreTemplatePath());
    preTemplateHtml.replace(cssHolder, vconfig.getTemplateCssUrl());
    if (!cssStyle.isEmpty()) {
        preTemplateHtml.replace(styleHolder, cssStyle);
    }

    postTemplateHtml = VUtils::readFileFromDisk(vconfig.getPostTemplatePath());
}

const QVector<VNotebook *> &VNote::getNotebooks()
{
    return m_notebooks;
}

void VNote::createNotebook(const QString &name, const QString &path)
{
    // Create a directory config file in @path
    QJsonObject configJson;
    configJson["version"] = "1";
    configJson["sub_directories"] = QJsonArray();
    configJson["files"] = QJsonArray();
    if (!vconfig.writeDirectoryConfig(path, configJson)) {
        return;
    }

    // Update notebooks settings
    VNotebook *nb = new VNotebook(name, path, this);
    m_notebooks.prepend(nb);
    vconfig.setNotebooks(m_notebooks);

    // Set current index to the new notebook
    vconfig.setCurNotebookIndex(0);

    emit notebookAdded(nb, 0);
}

void VNote::removeNotebook(int idx)
{
    Q_ASSERT(idx >= 0 && idx < m_notebooks.size());
    VNotebook *nb = m_notebooks[idx];
    QString name = nb->getName();
    QString path = nb->getPath();

    // Update notebook settings
    int curIndex = vconfig.getCurNotebookIndex();
    m_notebooks.remove(idx);
    vconfig.setNotebooks(m_notebooks);
    if (m_notebooks.isEmpty()) {
        vconfig.setCurNotebookIndex(-1);
    } else if (idx == curIndex) {
        vconfig.setCurNotebookIndex(0);
    }

    // Close all the directory and files
    notebookPathHash.remove(name);
    nb->close();
    delete nb;
    qDebug() << "notebook" << name << "deleted";

    // Delete the directory
    QDir dir(path);
    if (!dir.removeRecursively()) {
        qWarning() << "failed to delete" << path << "recursively";
    }
    emit notebookDeleted(idx);
}

void VNote::renameNotebook(int idx, const QString &newName)
{
    Q_ASSERT(idx >= 0 && idx < m_notebooks.size());
    VNotebook *nb = m_notebooks[idx];
    QString name = nb->getName();
    notebookPathHash.remove(name);
    nb->setName(newName);
    vconfig.setNotebooks(m_notebooks);

    emit notebookRenamed(nb, idx);
}

QString VNote::getNotebookPath(const QString &name)
{
    QString path = notebookPathHash.value(name);
    if (path.isEmpty()) {
        for (int i = 0; i < m_notebooks.size(); ++i) {
            if (m_notebooks[i]->getName() == name) {
                path = m_notebooks[i]->getPath();
                break;
            }
        }
        if (!path.isEmpty()) {
            notebookPathHash.insert(name, path);
        }
    }
    return path;
}
