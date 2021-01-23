#ifndef BUILDMGR_H
#define BUILDMGR_H

#include <QObject>

#include <QVector>

#include "build.h"

namespace vnotex
{

    class BuildMgr : public QObject
    {
        Q_OBJECT
    public:
        struct BuildInfo
        {
            // Id.
            QString m_name;
            
            // Locale supported.
            QString m_displayName;
            
            QString m_filePath;
        };
        
        explicit BuildMgr(QObject *parent = nullptr);
        
        const QVector<BuildInfo> &getAllBuilds() const;
        
        static void addSearchPath(const QString &p_path);
        
    private:
        void loadAvailableBuilds();
        
        void loadBuilds(const QString &p_path);
        
        void checkAndAddBuildFile(const QString &p_file, const QString &p_locale);
        
        QVector<BuildInfo> m_builds;
        
        // List of path to search for themes.
        static QStringList s_searchPaths;
    };
}

Q_DECLARE_METATYPE(vnotex::BuildMgr::BuildInfo);


#endif // BUILDMGR_H
