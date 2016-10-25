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
    decorateTemplate();
    notebooks = vconfig.getNotebooks();
    emit notebooksChanged(notebooks);
}

void VNote::decorateTemplate()
{
    if (templateHtml.isEmpty()) {
        templateHtml = VUtils::readFileFromDisk(vconfig.getTemplatePath());
        templateHtml.replace("CSS_PLACE_HOLDER", vconfig.getTemplateCssUrl());
    }
    if (preTemplateHtml.isEmpty()) {
        preTemplateHtml = VUtils::readFileFromDisk(vconfig.getPreTemplatePath());
        preTemplateHtml.replace("CSS_PLACE_HOLDER", vconfig.getTemplateCssUrl());
    }
    if (postTemplateHtml.isEmpty()) {
        postTemplateHtml = VUtils::readFileFromDisk(vconfig.getPostTemplatePath());
    }
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

    emit notebooksRenamed(notebooks, name, newName);
}
