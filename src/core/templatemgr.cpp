#include "templatemgr.h"

#include <QDir>

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
    return QDir(getTemplateFolder()).filePath(p_name);
}
