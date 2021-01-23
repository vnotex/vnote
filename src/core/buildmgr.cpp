#include "buildmgr.h"

#include <QDir>
#include <QDebug>

#include "configmgr.h"
#include "coreconfig.h"
#include "utils/pathutils.h"

using namespace vnotex;

QStringList BuildMgr::s_searchPaths;

BuildMgr::BuildMgr(QObject *parent) 
    : QObject(parent)
{
    
}

const QVector<BuildMgr::BuildInfo> &BuildMgr::getAllBuilds() const
{
    return m_builds;
}

void BuildMgr::addSearchPath(const QString &p_path)
{
    s_searchPaths << p_path;
}

void BuildMgr::loadAvailableBuilds()
{
    m_builds.clear();
    
    for (const auto &pa : s_searchPaths) {
        loadBuilds(pa);
    }
}

void BuildMgr::loadBuilds(const QString &p_path)
{
    qDebug() << "search for builds in " << p_path;
    QDir dir(p_path);
    dir.setFilter(QDir::NoDotAndDotDot);
    auto buildEntrys = dir.entryList();
    const auto localeStr = ConfigMgr::getInst().getCoreConfig().getLocaleToUse();
    for (auto &entry : buildEntrys) {
        checkAndAddBuildFile(PathUtils::concatenateFilePath(p_path, entry), localeStr);
    }
}

void BuildMgr::checkAndAddBuildFile(const QString &p_file, const QString &p_locale)
{
    qDebug() << "Check Build Entry: " << p_file << ", locale: " << p_locale;
    
}
