#ifndef SESSIONCONFIG_H
#define SESSIONCONFIG_H

#include "iconfig.h"

#include <QString>
#include <QVector>

#include <export/exportdata.h>
#include <search/searchdata.h>
#include "historyitem.h"

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
                       && m_mainGeometry == p_other.m_mainGeometry
                       && m_visibleDocksBeforeExpand == p_other.m_visibleDocksBeforeExpand
                       && m_tagExplorerState == p_other.m_tagExplorerState
                       && m_notebookExplorerState == p_other.m_notebookExplorerState
                       && m_locationListState == p_other.m_locationListState;
            }

            QByteArray m_mainState;

            QByteArray m_mainGeometry;

            QStringList m_visibleDocksBeforeExpand;

            QByteArray m_tagExplorerState;

            QByteArray m_notebookExplorerState;

            QByteArray m_locationListState;
        };

        enum OpenGL
        {
            None,
            Desktop,
            Angle,
            Software
        };

        struct ExternalProgram
        {
            void fromJson(const QJsonObject &p_jobj);

            QJsonObject toJson() const;

            QString fetchCommand(const QString &p_file) const;

            QString m_name;

            // %1: the file paths to open.
            QString m_command;

            QString m_shortcut;
        };

        explicit SessionConfig(ConfigMgr *p_mgr);

        ~SessionConfig();

        void init() Q_DECL_OVERRIDE;

        const QString &getNewNotebookDefaultRootFolderPath() const;
        void setNewNotebookDefaultRootFolderPath(const QString &p_path);

        const QString &getExternalMediaDefaultPath() const;
        void setExternalMediaDefaultPath(const QString &p_path);

        const QString &getCurrentNotebookRootFolderPath() const;
        void setCurrentNotebookRootFolderPath(const QString &p_path);

        const QVector<SessionConfig::NotebookItem> &getNotebooks() const;
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

        const QVector<ExportCustomOption> &getCustomExportOptions() const;
        void setCustomExportOptions(const QVector<ExportCustomOption> &p_options);

        const SearchOption &getSearchOption() const;
        void setSearchOption(const SearchOption &p_option);

        QByteArray getViewAreaSessionAndClear();
        void setViewAreaSession(const QByteArray &p_bytes);

        QByteArray getNotebookExplorerSessionAndClear();
        void setNotebookExplorerSession(const QByteArray &p_bytes);

        const QString &getFlashPage() const;
        void setFlashPage(const QString &p_file);

        const QStringList &getQuickAccessFiles() const;
        void setQuickAccessFiles(const QStringList &p_files);

        void removeQuickAccessFile(const QString &p_file);

        const QVector<ExternalProgram> &getExternalPrograms() const;
        const ExternalProgram *findExternalProgram(const QString &p_name) const;

        const QVector<HistoryItem> &getHistory() const;
        void addHistory(const HistoryItem &p_item);
        void clearHistory();

    private:
        void loadCore(const QJsonObject &p_session);

        QJsonObject saveCore() const;

        void loadNotebooks(const QJsonObject &p_session);

        QJsonArray saveNotebooks() const;

        void loadStateAndGeometry(const QJsonObject &p_session);

        QJsonObject saveStateAndGeometry() const;

        void loadExternalPrograms(const QJsonObject &p_session);

        QJsonArray saveExternalPrograms() const;

        void doVersionSpecificOverride();

        void loadHistory(const QJsonObject &p_session);

        QJsonArray saveHistory() const;

        void loadExportOption(const QJsonObject &p_session);

        QJsonObject saveExportOption() const;

        QString m_newNotebookDefaultRootFolderPath;

        // Use root folder to identify a notebook uniquely.
        QString m_currentNotebookRootFolderPath;

        QVector<SessionConfig::NotebookItem> m_notebooks;

        MainWindowStateGeometry m_mainWindowStateGeometry;

        OpenGL m_openGL = OpenGL::None;

        // Whether use system's title bar or not.
        bool m_systemTitleBarEnabled = true;

        // Whether to minimize to tray.
        // -1 for prompting for user;
        // 0 for disabling minimizing to system tray;
        // 1 for enabling minimizing to system tray.
        int m_minimizeToSystemTray = -1;

        ExportOption m_exportOption;

        QVector<ExportCustomOption> m_customExportOptions;

        SearchOption m_searchOption;

        QByteArray m_viewAreaSession;

        QByteArray m_notebookExplorerSession;

        QString m_flashPage;

        QStringList m_quickAccessFiles;

        QVector<ExternalProgram> m_externalPrograms;

        QVector<HistoryItem> m_history;

        // Default folder path to open for external media like images and files.
        QString m_externalMediaDefaultPath;;
    };
} // ns vnotex

#endif // SESSIONCONFIG_H
