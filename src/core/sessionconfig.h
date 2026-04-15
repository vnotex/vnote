#ifndef SESSIONCONFIG_H
#define SESSIONCONFIG_H

#include "iconfig.h"

#include <QString>
#include <QVector>

#include "historyitem.h"
#include <export/exportdata.h>
#include <search/searchdata.h>

namespace vnotex {
class IConfigMgr;

enum class QuickAccessOpenMode { Default, Read, Edit };

class SessionConfig : public IConfig {
public:
  struct MainWindowStateGeometry {
    bool operator==(const MainWindowStateGeometry &p_other) const {
      return m_mainState == p_other.m_mainState && m_mainGeometry == p_other.m_mainGeometry &&
             m_visibleDocksBeforeExpand == p_other.m_visibleDocksBeforeExpand;
    }

    QByteArray m_mainState;

    QByteArray m_mainGeometry;

    QStringList m_visibleDocksBeforeExpand;
  };

  struct QuickNoteScheme {
    bool operator==(const QuickNoteScheme &p_other) const;

    void fromJson(const QJsonObject &p_jobj);

    QJsonObject toJson() const;

    QString m_name;

    // Where to create the quick note.
    QString m_folderPath;

    // Name of the quick note. Snippet is supported.
    QString m_noteName;

    QString m_template;
  };

  struct QuickAccessItem {
    bool operator==(const QuickAccessItem &p_other) const;

    void fromJson(const QJsonObject &p_jobj);

    QJsonObject toJson() const;

    QString m_path;

    QuickAccessOpenMode m_openMode = QuickAccessOpenMode::Default;

    QString m_uuid;
  };

  enum OpenGL { None, Desktop, Angle, Software };

  struct ExternalProgram {
    void fromJson(const QJsonObject &p_jobj);

    QJsonObject toJson() const;

    QString fetchCommand(const QString &p_file) const;

    QString m_name;

    // %1: the file paths to open.
    QString m_command;

    QString m_shortcut;
  };

  explicit SessionConfig(IConfigMgr *p_mgr);

  ~SessionConfig();

  void fromJson(const QJsonObject &p_jobj) Q_DECL_OVERRIDE;

  const QString &getNewNotebookDefaultRootFolderPath() const;
  void setNewNotebookDefaultRootFolderPath(const QString &p_path);

  const QString &getExternalMediaDefaultPath() const;
  void setExternalMediaDefaultPath(const QString &p_path);

  void update() Q_DECL_OVERRIDE;

  QJsonObject toJson() const Q_DECL_OVERRIDE;

  SessionConfig::MainWindowStateGeometry getMainWindowStateGeometry() const;
  void setMainWindowStateGeometry(const SessionConfig::MainWindowStateGeometry &p_state);

  OpenGL getOpenGL() const;
  void setOpenGL(OpenGL p_option);

  bool getSystemTitleBarEnabled() const;
  void setSystemTitleBarEnabled(bool p_enabled);

  static QString openGLToString(OpenGL p_option);
  static OpenGL stringToOpenGL(const QString &p_str);

  static QString openModeToString(QuickAccessOpenMode p_mode);
  static QuickAccessOpenMode stringToOpenMode(const QString &p_str);

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

  // Get the view area layout (new architecture, JSON-based).
  QJsonObject getViewAreaLayout() const;
  void setViewAreaLayout(const QJsonObject &p_layout);

  QByteArray getNotebookExplorerSession() const;
  void setNotebookExplorerSession(const QByteArray &p_bytes);

  QByteArray getTagExplorerSession() const;
  void setTagExplorerSession(const QByteArray &p_bytes);

  const QVector<QuickAccessItem> &getQuickAccessItems() const;
  void setQuickAccessItems(const QVector<QuickAccessItem> &p_items);

  QStringList getQuickAccessFiles() const;
  void setQuickAccessFiles(const QStringList &p_files);
  void removeQuickAccessFile(const QString &p_file);

  const QVector<ExternalProgram> &getExternalPrograms() const;
  const ExternalProgram *findExternalProgram(const QString &p_name) const;

  const QVector<HistoryItem> &getHistory() const;
  void addHistory(const HistoryItem &p_item);
  void removeHistory(const QString &p_itemPath);
  void clearHistory();

  const QVector<QuickNoteScheme> &getQuickNoteSchemes() const;
  void setQuickNoteSchemes(const QVector<QuickNoteScheme> &p_schemes);

  const QString &getCurrentNotebook() const;
  void setCurrentNotebook(const QString &p_guid);

  const QStringList &getSearchHistory() const;
  void addSearchHistory(const QString &p_keyword);
  void setSearchHistory(const QStringList &p_history);

private:
  void loadCore(const QJsonObject &p_session);

  QJsonObject saveCore() const;

  void loadStateAndGeometry(const QJsonObject &p_session);

  QJsonObject saveStateAndGeometry() const;

  void loadExternalPrograms(const QJsonObject &p_session);

  QJsonArray saveExternalPrograms() const;

  void loadQuickNoteSchemes(const QJsonObject &p_session);

  QJsonArray saveQuickNoteSchemes() const;

  void loadQuickAccessItems(const QJsonObject &p_session);

  QJsonArray saveQuickAccessItems() const;

  void doVersionSpecificOverride();

  void loadHistory(const QJsonObject &p_session);

  QJsonArray saveHistory() const;

  void loadExportOption(const QJsonObject &p_session);

  QJsonObject saveExportOption() const;

  QString m_newNotebookDefaultRootFolderPath;

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

  QVector<ExportCustomOption> m_customExportOptions;

  SearchOption m_searchOption;

  QByteArray m_viewAreaSession;

  // New architecture: JSON-based view area layout (splitter tree + workspace mapping).
  QJsonObject m_viewAreaLayout;

  QByteArray m_notebookExplorerSession;

  QByteArray m_tagExplorerSession;

  QVector<QuickAccessItem> m_quickAccessItems;

  QVector<ExternalProgram> m_externalPrograms;

  QVector<HistoryItem> m_history;

  // Default folder path to open for external media like images and files.
  QString m_externalMediaDefaultPath;

  QVector<QuickNoteScheme> m_quickNoteSchemes;

  // GUID of the currently active notebook.
  QString m_currentNotebook;

  // Keyword search history (most recent first, capped at 20).
  QStringList m_searchHistory;
};
} // namespace vnotex

#endif // SESSIONCONFIG_H
