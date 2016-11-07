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

VNote::VNote() : QObject()
{
    vconfig.initialize();
    initTemplate();
    notebooks = vconfig.getNotebooks();
    emit notebooksChanged(notebooks);
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

const QVector<VNotebook>& VNote::getNotebooks()
{
    return notebooks;
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
    notebooks.prepend(VNotebook(name, path));
    vconfig.setNotebooks(notebooks);

    // Set current index to the new notebook
    vconfig.setCurNotebookIndex(0);

    emit notebooksAdded(notebooks, 0);
}

void VNote::removeNotebook(const QString &name)
{
    // Update notebooks settings
    QString path;
    int curIndex = vconfig.getCurNotebookIndex();
    int index;
    for (index = 0; index < notebooks.size(); ++index) {
        if (notebooks[index].getName() == name) {
            path = notebooks[index].getPath();
            break;
        }
    }
    if (index == notebooks.size()) {
        return;
    }
    notebooks.remove(index);
    vconfig.setNotebooks(notebooks);
    if (notebooks.isEmpty()) {
        vconfig.setCurNotebookIndex(-1);
    } else if (index == curIndex) {
        vconfig.setCurNotebookIndex(0);
    }

    // Delete the directory
    QDir dir(path);
    if (!dir.removeRecursively()) {
        qWarning() << "error: fail to delete" << path << "recursively";
    } else {
        qDebug() << "delete" << path << "recursively";
    }

    notebookPathHash.remove(name);
    emit notebooksDeleted(notebooks, name);
}

void VNote::renameNotebook(const QString &name, const QString &newName)
{
    QString path;
    int index;
    for (index = 0; index < notebooks.size(); ++index) {
        if (notebooks[index].getName() == name) {
            path = notebooks[index].getPath();
            break;
        }
    }
    if (index == notebooks.size()) {
        return;
    }

    notebooks[index].setName(newName);
    vconfig.setNotebooks(notebooks);

    notebookPathHash.remove(name);
    emit notebooksRenamed(notebooks, name, newName);
}

QString VNote::getNotebookPath(const QString &name)
{
    QString path = notebookPathHash.value(name);
    if (path.isEmpty()) {
        for (int i = 0; i < notebooks.size(); ++i) {
            if (notebooks[i].getName() == name) {
                path = notebooks[i].getPath();
                break;
            }
        }
        if (!path.isEmpty()) {
            notebookPathHash.insert(name, path);
        }
    }
    return path;
}
