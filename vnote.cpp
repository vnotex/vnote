#include <QSettings>
#include <QDebug>
#include <QJsonObject>
#include <QJsonArray>
#include "vnote.h"
#include "utils/vutils.h"
#include "vconfigmanager.h"

VConfigManager vconfig;

QString VNote::templateHtml;

VNote::VNote() : QObject()
{
    vconfig.initialize();
    decorateTemplate();
    notebooks = vconfig.getNotebooks();
    emit notebooksChanged(notebooks);
}

void VNote::decorateTemplate()
{
    templateHtml = VUtils::readFileFromDisk(vconfig.getTemplatePath());
    templateHtml.replace("CSS_PLACE_HOLDER", vconfig.getTemplateCssUrl());
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
    configJson["name"] = name;
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

    emit notebooksChanged(notebooks);
}
