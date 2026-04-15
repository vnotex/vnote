#include "sessionconfig.h"

#include <QDebug>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>

#include <utils/fileutils.h>
#include <utils/pathutils.h>
#include <utils/vxurlutils.h>

#include "historymgr.h"
#include "iconfigmgr.h"
#include "mainconfig.h"

using namespace vnotex;

bool SessionConfig::QuickNoteScheme::operator==(const QuickNoteScheme &p_other) const {
  return m_name == p_other.m_name && m_folderPath == p_other.m_folderPath &&
         m_noteName == p_other.m_noteName && m_template == p_other.m_template;
}

void SessionConfig::QuickNoteScheme::fromJson(const QJsonObject &p_jobj) {
  m_name = p_jobj[QStringLiteral("name")].toString();
  m_folderPath = p_jobj[QStringLiteral("folderPath")].toString();
  m_noteName = p_jobj[QStringLiteral("noteName")].toString();
  m_template = p_jobj[QStringLiteral("template")].toString();
}

QJsonObject SessionConfig::QuickNoteScheme::toJson() const {
  QJsonObject jobj;

  jobj[QStringLiteral("name")] = m_name;
  jobj[QStringLiteral("folderPath")] = m_folderPath;
  jobj[QStringLiteral("noteName")] = m_noteName;
  jobj[QStringLiteral("template")] = m_template;

  return jobj;
}

bool SessionConfig::QuickAccessItem::operator==(const QuickAccessItem &p_other) const {
  return m_path == p_other.m_path && m_openMode == p_other.m_openMode;
}

void SessionConfig::QuickAccessItem::fromJson(const QJsonObject &p_jobj) {
  m_path = p_jobj[QStringLiteral("path")].toString();
  m_openMode = stringToOpenMode(p_jobj[QStringLiteral("openMode")].toString());
  m_uuid = p_jobj[QStringLiteral("uuid")].toString();
}

QJsonObject SessionConfig::QuickAccessItem::toJson() const {
  QJsonObject jobj;
  jobj[QStringLiteral("path")] = m_path;
  jobj[QStringLiteral("openMode")] = openModeToString(m_openMode);
  if (!m_uuid.isEmpty()) {
    jobj[QStringLiteral("uuid")] = m_uuid;
  }
  return jobj;
}

void SessionConfig::ExternalProgram::fromJson(const QJsonObject &p_jobj) {
  m_name = p_jobj[QStringLiteral("name")].toString();
  m_command = p_jobj[QStringLiteral("command")].toString();
  m_shortcut = p_jobj[QStringLiteral("shortcut")].toString();
}

QJsonObject SessionConfig::ExternalProgram::toJson() const {
  QJsonObject jobj;

  jobj[QStringLiteral("name")] = m_name;
  jobj[QStringLiteral("command")] = m_command;
  jobj[QStringLiteral("shortcut")] = m_shortcut;

  return jobj;
}

QString SessionConfig::ExternalProgram::fetchCommand(const QString &p_file) const {
  auto command(m_command);
  command.replace(QStringLiteral("%1"), QStringLiteral("\"%1\"").arg(p_file));
  return command;
}

SessionConfig::SessionConfig(IConfigMgr *p_mgr) : IConfig(p_mgr, nullptr) {}

SessionConfig::~SessionConfig() {}

void SessionConfig::fromJson(const QJsonObject &p_jobj) {
  // p_jobj is already merged (defaults + user overrides)
  loadCore(p_jobj);

  loadStateAndGeometry(p_jobj);

  loadExportOption(p_jobj);

  m_searchOption.fromJson(p_jobj[QStringLiteral("searchOption")].toObject());

  m_searchHistory = readStringList(p_jobj, QStringLiteral("searchHistory"));

  m_viewAreaSession = readByteArray(p_jobj, QStringLiteral("viewareaSession"));

  m_viewAreaLayout = p_jobj[QStringLiteral("viewAreaLayout")].toObject();

  m_notebookExplorerSession = readByteArray(p_jobj, QStringLiteral("notebookExplorerSession"));

  m_tagExplorerSession = readByteArray(p_jobj, QStringLiteral("tagExplorerSession"));

  loadExternalPrograms(p_jobj);

  loadHistory(p_jobj);

  loadQuickNoteSchemes(p_jobj);
}

void SessionConfig::loadCore(const QJsonObject &p_session) {
  const auto coreObj = p_session.value(QStringLiteral("core")).toObject();
  m_newNotebookDefaultRootFolderPath =
      readString(coreObj, QStringLiteral("newNotebookDefaultRootFolderPath"));
  if (m_newNotebookDefaultRootFolderPath.isEmpty()) {
    m_newNotebookDefaultRootFolderPath = QDir::homePath();
  }

  {
    auto option = readString(coreObj, QStringLiteral("opengl"));
    m_openGL = stringToOpenGL(option);
  }

  if (!isUndefinedKey(coreObj, QStringLiteral("systemTitleBar"))) {
    m_systemTitleBarEnabled = readBool(coreObj, QStringLiteral("systemTitleBar"));
  } else {
    m_systemTitleBarEnabled = false;
  }

  if (!isUndefinedKey(coreObj, QStringLiteral("minimizeToSystemTray"))) {
    m_minimizeToSystemTray = readBool(coreObj, QStringLiteral("minimizeToSystemTray")) ? 1 : 0;
  }

  {
    const auto quickAccessArr = coreObj.value(QStringLiteral("quickAccess")).toArray();
    m_quickAccessItems.resize(quickAccessArr.size());
    for (int i = 0; i < quickAccessArr.size(); ++i) {
      m_quickAccessItems[i].fromJson(quickAccessArr[i].toObject());
    }
  }

  m_externalMediaDefaultPath = readString(coreObj, QStringLiteral("externalMediaDefaultPath"));
  if (m_externalMediaDefaultPath.isEmpty()) {
    m_externalMediaDefaultPath = QDir::homePath();
  }

  m_currentNotebook = readString(coreObj, QStringLiteral("currentNotebook"));
}

QJsonObject SessionConfig::saveCore() const {
  QJsonObject coreObj;
  coreObj[QStringLiteral("newNotebookDefaultRootFolderPath")] = m_newNotebookDefaultRootFolderPath;
  coreObj[QStringLiteral("opengl")] = openGLToString(m_openGL);
  coreObj[QStringLiteral("systemTitleBar")] = m_systemTitleBarEnabled;
  if (m_minimizeToSystemTray != -1) {
    coreObj[QStringLiteral("minimizeToSystemTray")] = m_minimizeToSystemTray > 0;
  }
  coreObj[QStringLiteral("quickAccess")] = saveQuickAccessItems();
  coreObj[QStringLiteral("externalMediaDefaultPath")] = m_externalMediaDefaultPath;
  coreObj[QStringLiteral("currentNotebook")] = m_currentNotebook;
  return coreObj;
}

const QString &SessionConfig::getNewNotebookDefaultRootFolderPath() const {
  return m_newNotebookDefaultRootFolderPath;
}

void SessionConfig::setNewNotebookDefaultRootFolderPath(const QString &p_path) {
  updateConfig(m_newNotebookDefaultRootFolderPath, p_path, this);
}

const QString &SessionConfig::getExternalMediaDefaultPath() const {
  return m_externalMediaDefaultPath;
}

void SessionConfig::setExternalMediaDefaultPath(const QString &p_path) {
  updateConfig(m_externalMediaDefaultPath, p_path, this);
}

const QString &SessionConfig::getCurrentNotebook() const { return m_currentNotebook; }

void SessionConfig::setCurrentNotebook(const QString &p_guid) {
  updateConfig(m_currentNotebook, p_guid, this);
}

void SessionConfig::update() { getMgr()->updateSessionConfig(toJson()); }

QJsonObject SessionConfig::toJson() const {
  QJsonObject obj;
  obj[QStringLiteral("core")] = saveCore();
  obj[QStringLiteral("stateGeometry")] = saveStateAndGeometry();
  obj[QStringLiteral("export")] = saveExportOption();
  obj[QStringLiteral("searchOption")] = m_searchOption.toJson();
  writeStringList(obj, QStringLiteral("searchHistory"), m_searchHistory);
  writeByteArray(obj, QStringLiteral("viewareaSession"), m_viewAreaSession);
  if (!m_viewAreaLayout.isEmpty()) {
    obj[QStringLiteral("viewAreaLayout")] = m_viewAreaLayout;
  }
  writeByteArray(obj, QStringLiteral("notebookExplorerSession"), m_notebookExplorerSession);
  writeByteArray(obj, QStringLiteral("tagExplorerSession"), m_tagExplorerSession);
  obj[QStringLiteral("externalPrograms")] = saveExternalPrograms();
  obj[QStringLiteral("history")] = saveHistory();
  obj[QStringLiteral("quickNoteSchemes")] = saveQuickNoteSchemes();
  return obj;
}

QJsonObject SessionConfig::saveStateAndGeometry() const {
  QJsonObject obj;
  writeByteArray(obj, QStringLiteral("mainWindowState"), m_mainWindowStateGeometry.m_mainState);
  writeByteArray(obj, QStringLiteral("mainWindowGeometry"),
                 m_mainWindowStateGeometry.m_mainGeometry);
  writeStringList(obj, QStringLiteral("visibleDocksBeforeExpand"),
                  m_mainWindowStateGeometry.m_visibleDocksBeforeExpand);
  return obj;
}

SessionConfig::MainWindowStateGeometry SessionConfig::getMainWindowStateGeometry() const {
  return m_mainWindowStateGeometry;
}

void SessionConfig::setMainWindowStateGeometry(
    const SessionConfig::MainWindowStateGeometry &p_state) {
  updateConfig(m_mainWindowStateGeometry, p_state, this);
}

SessionConfig::OpenGL SessionConfig::getOpenGL() const { return m_openGL; }

void SessionConfig::setOpenGL(OpenGL p_option) { updateConfig(m_openGL, p_option, this); }

QString SessionConfig::openGLToString(OpenGL p_option) {
  switch (p_option) {
  case OpenGL::Desktop:
    return QStringLiteral("desktop");

  case OpenGL::Angle:
    return QStringLiteral("angle");

  case OpenGL::Software:
    return QStringLiteral("software");

  default:
    return QStringLiteral("none");
  }
}

SessionConfig::OpenGL SessionConfig::stringToOpenGL(const QString &p_str) {
  auto option = p_str.toLower();
  if (option == QStringLiteral("software")) {
    return OpenGL::Software;
  } else if (option == QStringLiteral("desktop")) {
    return OpenGL::Desktop;
  } else if (option == QStringLiteral("angle")) {
    return OpenGL::Angle;
  } else {
    return OpenGL::None;
  }
}

QString SessionConfig::openModeToString(QuickAccessOpenMode p_mode) {
  switch (p_mode) {
  case QuickAccessOpenMode::Read:
    return QStringLiteral("read");
  case QuickAccessOpenMode::Edit:
    return QStringLiteral("edit");
  default:
    return QStringLiteral("default");
  }
}

QuickAccessOpenMode SessionConfig::stringToOpenMode(const QString &p_str) {
  auto s = p_str.toLower();
  if (s == QStringLiteral("read")) {
    return QuickAccessOpenMode::Read;
  } else if (s == QStringLiteral("edit")) {
    return QuickAccessOpenMode::Edit;
  }
  return QuickAccessOpenMode::Default;
}

bool SessionConfig::getSystemTitleBarEnabled() const { return m_systemTitleBarEnabled; }

void SessionConfig::setSystemTitleBarEnabled(bool p_enabled) {
  updateConfig(m_systemTitleBarEnabled, p_enabled, this);
}

int SessionConfig::getMinimizeToSystemTray() const { return m_minimizeToSystemTray; }

void SessionConfig::setMinimizeToSystemTray(bool p_enabled) {
  updateConfig(m_minimizeToSystemTray, p_enabled ? 1 : 0, this);
}

void SessionConfig::doVersionSpecificOverride() {
  // In a new version, we may want to change one value by force.
  // SHOULD set the in memory variable only, or will override the notebook list.
}

const ExportOption &SessionConfig::getExportOption() const { return m_exportOption; }

void SessionConfig::setExportOption(const ExportOption &p_option) {
  updateConfig(m_exportOption, p_option, this);
}

const QVector<ExportCustomOption> &SessionConfig::getCustomExportOptions() const {
  return m_customExportOptions;
}

void SessionConfig::setCustomExportOptions(const QVector<ExportCustomOption> &p_options) {
  updateConfig(m_customExportOptions, p_options, this);
}

const SearchOption &SessionConfig::getSearchOption() const { return m_searchOption; }

void SessionConfig::setSearchOption(const SearchOption &p_option) {
  updateConfig(m_searchOption, p_option, this);
}

void SessionConfig::loadStateAndGeometry(const QJsonObject &p_session) {
  const auto obj = p_session.value(QStringLiteral("stateGeometry")).toObject();
  m_mainWindowStateGeometry.m_mainState = readByteArray(obj, QStringLiteral("mainWindowState"));
  m_mainWindowStateGeometry.m_mainGeometry =
      readByteArray(obj, QStringLiteral("mainWindowGeometry"));
  m_mainWindowStateGeometry.m_visibleDocksBeforeExpand =
      readStringList(obj, QStringLiteral("visibleDocksBeforeExpand"));
}

QByteArray SessionConfig::getViewAreaSessionAndClear() {
  QByteArray bytes;
  m_viewAreaSession.swap(bytes);
  return bytes;
}

void SessionConfig::setViewAreaSession(const QByteArray &p_bytes) {
  updateConfigWithoutCheck(m_viewAreaSession, p_bytes, this);
}

QByteArray SessionConfig::getNotebookExplorerSession() const { return m_notebookExplorerSession; }

void SessionConfig::setNotebookExplorerSession(const QByteArray &p_bytes) {
  updateConfigWithoutCheck(m_notebookExplorerSession, p_bytes, this);
}

QByteArray SessionConfig::getTagExplorerSession() const { return m_tagExplorerSession; }

void SessionConfig::setTagExplorerSession(const QByteArray &p_bytes) {
  updateConfigWithoutCheck(m_tagExplorerSession, p_bytes, this);
}

QJsonObject SessionConfig::getViewAreaLayout() const { return m_viewAreaLayout; }

void SessionConfig::setViewAreaLayout(const QJsonObject &p_layout) {
  updateConfigWithoutCheck(m_viewAreaLayout, p_layout, this);
}

QStringList SessionConfig::getQuickAccessFiles() const {
  QStringList files;
  files.reserve(m_quickAccessItems.size());
  for (const auto &item : m_quickAccessItems) {
    files << item.m_path;
  }
  return files;
}

void SessionConfig::setQuickAccessFiles(const QStringList &p_files) {
  QVector<QuickAccessItem> items;
  for (const auto &file : p_files) {
    auto fi = file.trimmed();
    if (!fi.isEmpty()) {
      QuickAccessItem item;
      item.m_path = fi;
      item.m_openMode = QuickAccessOpenMode::Default;
      items << item;
    }
  }
  updateConfig(m_quickAccessItems, items, this);
}

void SessionConfig::removeQuickAccessFile(const QString &p_file) {
  for (int i = m_quickAccessItems.size() - 1; i >= 0; --i) {
    if (m_quickAccessItems[i].m_path == p_file) {
      m_quickAccessItems.removeAt(i);
    }
  }
  update();
}

void SessionConfig::loadExternalPrograms(const QJsonObject &p_session) {
  const auto arr = p_session.value(QStringLiteral("externalPrograms")).toArray();
  m_externalPrograms.resize(arr.size());
  for (int i = 0; i < arr.size(); ++i) {
    m_externalPrograms[i].fromJson(arr[i].toObject());
  }
}

QJsonArray SessionConfig::saveExternalPrograms() const {
  QJsonArray arr;
  for (const auto &pro : m_externalPrograms) {
    arr.append(pro.toJson());
  }
  return arr;
}

void SessionConfig::loadQuickNoteSchemes(const QJsonObject &p_session) {
  const auto arr = p_session.value(QStringLiteral("quickNoteSchemes")).toArray();
  m_quickNoteSchemes.resize(arr.size());
  for (int i = 0; i < arr.size(); ++i) {
    m_quickNoteSchemes[i].fromJson(arr[i].toObject());
  }
}

QJsonArray SessionConfig::saveQuickNoteSchemes() const {
  QJsonArray arr;
  for (const auto &scheme : m_quickNoteSchemes) {
    arr.append(scheme.toJson());
  }
  return arr;
}

void SessionConfig::loadQuickAccessItems(const QJsonObject &p_session) {
  const auto arr = p_session.value(QStringLiteral("quickAccess")).toArray();
  m_quickAccessItems.resize(arr.size());
  for (int i = 0; i < arr.size(); ++i) {
    m_quickAccessItems[i].fromJson(arr[i].toObject());
  }
}

QJsonArray SessionConfig::saveQuickAccessItems() const {
  QJsonArray arr;
  for (const auto &item : m_quickAccessItems) {
    arr.append(item.toJson());
  }
  return arr;
}

const QVector<SessionConfig::QuickAccessItem> &SessionConfig::getQuickAccessItems() const {
  return m_quickAccessItems;
}

void SessionConfig::setQuickAccessItems(const QVector<QuickAccessItem> &p_items) {
  QVector<QuickAccessItem> items;
  for (const auto &item : p_items) {
    auto path = item.m_path.trimmed();
    if (!path.isEmpty()) {
      QuickAccessItem cleanItem;
      cleanItem.m_path = path;
      cleanItem.m_openMode = item.m_openMode;
      cleanItem.m_uuid = item.m_uuid;
      items << cleanItem;
    }
  }
  updateConfig(m_quickAccessItems, items, this);
}

const QVector<SessionConfig::ExternalProgram> &SessionConfig::getExternalPrograms() const {
  return m_externalPrograms;
}

const SessionConfig::ExternalProgram *
SessionConfig::findExternalProgram(const QString &p_name) const {
  for (const auto &pro : m_externalPrograms) {
    if (pro.m_name == p_name) {
      return &pro;
    }
  }
  return nullptr;
}

const QVector<HistoryItem> &SessionConfig::getHistory() const { return m_history; }

void SessionConfig::addHistory(const HistoryItem &p_item) {
  HistoryMgr::insertHistoryItem(m_history, p_item);
  update();
}

void SessionConfig::removeHistory(const QString &p_itemPath) {
  HistoryMgr::removeHistoryItem(m_history, p_itemPath);
  update();
}

void SessionConfig::clearHistory() {
  m_history.clear();
  update();
}

void SessionConfig::loadHistory(const QJsonObject &p_session) {
  auto arr = p_session[QStringLiteral("history")].toArray();
  m_history.resize(arr.size());
  for (int i = 0; i < arr.size(); ++i) {
    m_history[i].fromJson(arr[i].toObject());
  }
}

QJsonArray SessionConfig::saveHistory() const {
  QJsonArray arr;
  for (const auto &item : m_history) {
    arr.append(item.toJson());
  }
  return arr;
}

void SessionConfig::loadExportOption(const QJsonObject &p_session) {
  auto exportObj = p_session[QStringLiteral("export")].toObject();

  m_exportOption.fromJson(exportObj[QStringLiteral("exportOption")].toObject());

  auto customArr = exportObj[QStringLiteral("customOptions")].toArray();
  m_customExportOptions.resize(customArr.size());
  for (int i = 0; i < customArr.size(); ++i) {
    m_customExportOptions[i].fromJson(customArr[i].toObject());
  }
}

QJsonObject SessionConfig::saveExportOption() const {
  QJsonObject obj;

  obj[QStringLiteral("exportOption")] = m_exportOption.toJson();

  QJsonArray customArr;
  for (int i = 0; i < m_customExportOptions.size(); ++i) {
    customArr.push_back(m_customExportOptions[i].toJson());
  }
  obj[QStringLiteral("customOptions")] = customArr;

  return obj;
}

const QVector<SessionConfig::QuickNoteScheme> &SessionConfig::getQuickNoteSchemes() const {
  return m_quickNoteSchemes;
}

void SessionConfig::setQuickNoteSchemes(const QVector<QuickNoteScheme> &p_schemes) {
  updateConfig(m_quickNoteSchemes, p_schemes, this);
}

const QStringList &SessionConfig::getSearchHistory() const { return m_searchHistory; }

void SessionConfig::addSearchHistory(const QString &p_keyword) {
  auto trimmed = p_keyword.trimmed();
  if (trimmed.isEmpty()) {
    return;
  }

  // Remove existing occurrence (case-sensitive dedup).
  m_searchHistory.removeAll(trimmed);

  // Prepend to front.
  m_searchHistory.prepend(trimmed);

  // Cap at 20.
  while (m_searchHistory.size() > 20) {
    m_searchHistory.removeLast();
  }

  update();
}

void SessionConfig::setSearchHistory(const QStringList &p_history) {
  updateConfig(m_searchHistory, p_history, this);
}
