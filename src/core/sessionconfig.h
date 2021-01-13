#ifndef SESSIONCONFIG_H
#define SESSIONCONFIG_H

#include "iconfig.h"

#include <QString>
#include <QVector>

#include <export/exportdata.h>

namespace vnotex
{
    class SessionConfig : public IConfig
    {
    public:
        struct NotebookItem
        {
            NotebookItem() = default;

            bool operator==(const NotebookItem &p_other) const;

            void fromJson(const QJsonObject &p_jobj);

            QJsonObject toJson() const;

            QString m_type;
            QString m_rootFolderPath;
            QString m_backend;
        };

        struct MainWindowStateGeometry
        {
            bool operator==(const MainWindowStateGeometry &p_other) const
            {
                return m_mainState == p_other.m_mainState
                       && m_mainGeometry == p_other.m_mainGeometry;
            }

            QByteArray m_mainState;
            QByteArray m_mainGeometry;
        };

        enum OpenGL
        {
            None,
            Desktop,
            Angle,
            Software
        };

        explicit SessionConfig(ConfigMgr *p_mgr);

        ~SessionConfig();

        void init() Q_DECL_OVERRIDE;

        const QString &getNewNotebookDefaultRootFolderPath() const;
        void setNewNotebookDefaultRootFolderPath(const QString &p_path);

        const QString &getCurrentNotebookRootFolderPath() const;
        void setCurrentNotebookRootFolderPath(const QString &p_path);

        const QVector<SessionConfig::NotebookItem> &getNotebooks();
        void setNotebooks(const QVector<SessionConfig::NotebookItem> &p_notebooks);

        void writeToSettings() const Q_DECL_OVERRIDE;

        QJsonObject toJson() const Q_DECL_OVERRIDE;

        SessionConfig::MainWindowStateGeometry getMainWindowStateGeometry() const;
        void setMainWindowStateGeometry(const SessionConfig::MainWindowStateGeometry &p_state);

        OpenGL getOpenGL() const;
        void setOpenGL(OpenGL p_option);

        bool getSystemTitleBarEnabled() const;
        void setSystemTitleBarEnabled(bool p_enabled);

        static OpenGL getOpenGLAtBootstrap();

        static QString openGLToString(OpenGL p_option);
        static OpenGL stringToOpenGL(const QString &p_str);

        int getMinimizeToSystemTray() const;
        void setMinimizeToSystemTray(bool p_enabled);

        const ExportOption &getExportOption() const;
        void setExportOption(const ExportOption &p_option);

    private:
        void loadCore(const QJsonObject &p_session);

        QJsonObject saveCore() const;

        void loadNotebooks(const QJsonObject &p_session);

        QJsonArray saveNotebooks() const;

        void loadStateAndGeometry(const QJsonObject &p_session);

        QJsonObject saveStateAndGeometry() const;

        void doVersionSpecificOverride();

        QString m_newNotebookDefaultRootFolderPath;

        // Use root folder to identify a notebook uniquely.
        QString m_currentNotebookRootFolderPath;

        QVector<SessionConfig::NotebookItem> m_notebooks;

        MainWindowStateGeometry m_mainWindowStateGeometry;

        OpenGL m_openGL = OpenGL::None;

        // Whether use system's title bar or not.
        bool m_systemTitleBarEnabled = false;

        // Whether to minimize to tray.
        // -1 for prompting for user;
        // 0 for disabling minimizing to system tray;
        // 1 for enabling minimizing to system tray.
        int m_minimizeToSystemTray = -1;

        ExportOption m_exportOption;
    };
} // ns vnotex

#endif // SESSIONCONFIG_H
