#ifndef MAINCONFIG_H
#define MAINCONFIG_H

#include "iconfig.h"

#include <QtGlobal>
#include <QString>

class QJsonObject;

namespace vnotex
{
    class CoreConfig;
    class EditorConfig;
    class WidgetConfig;

    class MainConfig : public IConfig
    {
    public:
        explicit MainConfig(ConfigMgr *p_mgr);

        ~MainConfig();

        void init() Q_DECL_OVERRIDE;

        const QString &getVersion() const;

        CoreConfig &getCoreConfig();

        EditorConfig &getEditorConfig();

        WidgetConfig &getWidgetConfig();

        void writeToSettings() const Q_DECL_OVERRIDE;

        QJsonObject toJson() const Q_DECL_OVERRIDE;

        static QString getVersion(const QJsonObject &p_jobj);

        static bool isVersionChanged();

    private:
        void loadMetadata(const QJsonObject &p_app, const QJsonObject &p_user);

        QJsonObject saveMetaData() const;

        void doVersionSpecificOverride();

        // Version of VNoteX.
        QString m_version;

        // Version of user's configuration.
        QString m_userVersion;

        QScopedPointer<CoreConfig> m_coreConfig;

        QScopedPointer<EditorConfig> m_editorConfig;

        QScopedPointer<WidgetConfig> m_widgetConfig;

        static bool s_versionChanged;
    };
} // ns vnotex

#endif // MAINCONFIG_H
