#include "templatemgr.h"

#include <QDir>

#include <utils/fileutils.h>

#include "configmgr.h"

using namespace vnotex;

QString TemplateMgr::getTemplateFolder() const
{
    return ConfigMgr::getInst().getUserTemplateFolder();
}

QStringList TemplateMgr::getTemplates() const
{
    QDir dir(getTemplateFolder());
    dir.setFilter(QDir::Files | QDir::NoSymLinks);
    return dir.entryList();
}

QString TemplateMgr::getTemplateFilePath(const QString &p_name) const
{
    if (p_name.isEmpty()) {
        return QString();
    }
    return QDir(getTemplateFolder()).filePath(p_name);
}

QString TemplateMgr::getTemplateContent(const QString &p_name) const
{
    const auto filePath = getTemplateFilePath(p_name);
    if (filePath.isEmpty()) {
        return QString();
    }
    return FileUtils::readTextFile(filePath);
}
