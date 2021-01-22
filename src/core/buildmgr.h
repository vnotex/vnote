#ifndef BUILDMGR_H
#define BUILDMGR_H

#include <QObject>

#include <QVector>

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
        
    private:
        void loadAvailableBuilds();
        
        void loadBuilds(const QString &p_path);
        
        void checkAndAddBuildEntry(const QString &p_entry, const QString &p_locale);
        
        QVector<BuildInfo> m_builds;
        
        // List of path to search for themes.
        static QStringList s_searchPaths;
    };
}


#endif // BUILDMGR_H
