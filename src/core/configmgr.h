#ifndef CONFIGMGR_H
#define CONFIGMGR_H

#include <QObject>
#include <QSharedPointer>
#include <QJsonObject>
#include <QScopedPointer>

namespace vnotex
{
    class MainConfig;
    class SessionConfig;
    class CoreConfig;
    class EditorConfig;
    class WidgetConfig;

    class ConfigMgr : public QObject
    {
        Q_OBJECT
    public:
        enum class Source
        {
            Default,
            App,
            User,
            Session
        };

        class Settings
        {
        public:
            Settings() = default;

            Settings(const QJsonObject &p_jobj)
                : m_jobj(p_jobj)
            {
            }

            const QJsonObject &getJson() const;

            void writeToFile(const QString &p_jsonFilePath) const;

            static QSharedPointer<Settings> fromFile(const QString &p_jsonFilePath);

        private:
            QJsonObject m_jobj;
        };

        static ConfigMgr &getInst()
        {
            static ConfigMgr inst;
            return inst;
        }

        ~ConfigMgr();

        ConfigMgr(const ConfigMgr &) = delete;
        void operator=(const ConfigMgr &) = delete;

        MainConfig &getConfig();

        SessionConfig &getSessionConfig();

        CoreConfig &getCoreConfig();

        EditorConfig &getEditorConfig();

        WidgetConfig &getWidgetConfig();

        QString getAppFolder() const;

        QString getUserFolder() const;

        QString getLogFile() const;

        QString getAppThemeFolder() const;

        QString getUserThemeFolder() const;

        QString getAppWebStylesFolder() const;

        QString getUserWebStylesFolder() const;

        QString getAppDocsFolder() const;

        QString getUserDocsFolder() const;

        QString getAppSyntaxHighlightingFolder() const;

        QString getUserSyntaxHighlightingFolder() const;

        // If @p_filePath is absolute, just return it.
        // Otherwise, first try to find it in user folder, then in app folder.
        QString getUserOrAppFile(const QString &p_filePath) const;

        QString getConfigFilePath(Source p_src) const;

        // Called at boostrap without QApplication instance.
        static QString locateSessionConfigFilePathAtBootstrap();

        static QString getApplicationFilePath();

        static QString getApplicationDirPath();

        static QString getDocumentOrHomePath();

        static const QString c_orgName;

        static const QString c_appName;

    public:
        // Used by IConfig.
        QSharedPointer<Settings> getSettings(Source p_src) const;

        void writeUserSettings(const QJsonObject &p_jobj);

        void writeSessionSettings(const QJsonObject &p_jobj);

    signals:
        void editorConfigChanged();

    private:
        explicit ConfigMgr(QObject *p_parent = nullptr);

        // Locate the folder path where the config file exists.
        void locateConfigFolder();

        // Check if app config exists and is updated.
        // Update it if in need.
        void checkAppConfig();

        QScopedPointer<MainConfig> m_config;;

        // Session config.
        QScopedPointer<SessionConfig> m_sessionConfig;

        // Absolute path of the app config folder.
        QString m_appConfigFolderPath;

        // Absolute path of the user config folder.
        QString m_userConfigFolderPath;

        // Name of the core config file.
        static const QString c_configFileName;

        // Name of the session config file.
        static const QString c_sessionFileName;
    };
} // ns vnotex

#endif // CONFIGMGR_H
