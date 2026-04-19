#include "themeservice.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QRegularExpression>

#include "core/exception.h"
#include "core/hookevents.h"
#include "core/hooknames.h"
#include "core/services/hookmanager.h"
#include "core/theme.h"
#include <gui/utils/themeutils.h>
#include <gui/utils/iconutils.h>
#include <utils/pathutils.h>

using namespace vnotex;

ThemeService::ThemeService(const ThemeServiceConfig &p_config, QObject *p_parent)
    : QObject(p_parent),
      m_locale(p_config.locale),
      m_themeSearchPaths(PathUtils::concatenateFilePath(p_config.appDataPath, "themes")),
      m_webStylesSearchPaths(PathUtils::concatenateFilePath(p_config.appDataPath, "web_styles")) {
  loadAvailableThemes();
  loadCurrentTheme(p_config.themeName);
}

QString ThemeService::getIconFile(const QString &p_icon) const {
  Q_ASSERT(!p_icon.isEmpty());

  QRegularExpression sep("[/\\\\]");
  if (p_icon.indexOf(sep) != -1) {
    return p_icon;
  }

  // If there is an ICONS folder in the theme configuration, use the custom ICONS from it.
  QString customIcon = getFile(Theme::File::Icon) + "/" + p_icon;
  if (QFile::exists(customIcon)) {
    return customIcon;
  } else {
    return ":/vnotex/data/core/icons/" + p_icon;
  }
}

void ThemeService::loadAvailableThemes() {
  m_themes.clear();

  for (const auto &pa : m_themeSearchPaths) {
    loadThemes(pa);
  }

  if (m_themes.isEmpty()) {
    Exception::throwOne(Exception::Type::EssentialFileMissing,
                        QStringLiteral("no available themes found in paths: %1")
                            .arg(m_themeSearchPaths.join(QLatin1Char(';'))));
  }
}

void ThemeService::loadThemes(const QString &p_path) {
  qDebug() << "search for themes in" << p_path;
  QDir dir(p_path);
  dir.setFilter(QDir::Dirs | QDir::NoDotAndDotDot);
  auto themeFolders = dir.entryList();
  for (auto &folder : themeFolders) {
    checkAndAddThemeFolder(PathUtils::concatenateFilePath(p_path, folder));
  }
}

void ThemeService::checkAndAddThemeFolder(const QString &p_folder) {
  if (Theme::isValidThemeFolder(p_folder)) {
    ThemeInfo info;
    info.m_name = PathUtils::dirName(p_folder);
    info.m_displayName = Theme::getDisplayName(p_folder, m_locale);
    info.m_folderPath = p_folder;
    m_themes.push_back(info);
    qDebug() << "add theme" << info.m_name << info.m_displayName << info.m_folderPath;
  }
}

const Theme &ThemeService::getCurrentTheme() const { return *m_currentTheme; }

void ThemeService::loadCurrentTheme(const QString &p_themeName) {
  emit themeAboutToChange(p_themeName);
  QCoreApplication::processEvents();

  m_currentTheme.reset();
  auto themeFolder = findThemeFolder(p_themeName);
  if (themeFolder.isNull()) {
    qWarning() << "failed to locate theme" << p_themeName;
  } else {
    qInfo() << "using theme" << p_themeName;
    m_currentTheme.reset(loadTheme(themeFolder));
  }

  if (!m_currentTheme) {
    const QString defaultTheme("pure");
    qWarning() << "fall back to default theme" << defaultTheme;
    m_currentTheme.reset(loadTheme(findThemeFolder(defaultTheme)));
  }

  IconUtils::setDefaultIconForeground(paletteColor("base#icon#fg"),
                                      paletteColor("base#icon#disabled#fg"));

  emit themeChanged(p_themeName);
  if (m_hookMgr) {
    ThemeSwitchEvent event;
    event.themeName = p_themeName;
    m_hookMgr->doAction(HookNames::ThemeAfterSwitch, event);
  }
}

Theme *ThemeService::loadTheme(const QString &p_themeFolder) {
  if (p_themeFolder.isEmpty()) {
    qWarning("failed to load theme from empty folder");
    return nullptr;
  }

  try {
    return Theme::fromFolder(p_themeFolder, ThemeUtils::backfillSystemPalette);
  } catch (Exception &p_e) {
    qWarning("failed to load theme from folder %s (%s)", p_themeFolder.toStdString().c_str(),
             p_e.what());
    return nullptr;
  }
}

QString ThemeService::findThemeFolder(const QString &p_name) const {
  auto theme = findTheme(p_name);
  if (theme) {
    return theme->m_folderPath;
  }
  return QString();
}

const ThemeService::ThemeInfo *ThemeService::findTheme(const QString &p_name) const {
  for (const auto &info : m_themes) {
    if (info.m_name == p_name) {
      return &info;
    }
  }

  return nullptr;
}

QString ThemeService::fetchQtStyleSheet() const {
  Q_ASSERT(m_currentTheme);
  if (!m_currentTheme) {
    return QString();
  }

  return m_currentTheme->fetchQtStyleSheet();
}

QString ThemeService::paletteColor(const QString &p_name) const {
  Q_ASSERT(m_currentTheme);
  return m_currentTheme->paletteColor(p_name);
}

QString ThemeService::getFile(Theme::File p_fileType) const {
  return m_currentTheme->getFile(p_fileType);
}

QString ThemeService::getEditorHighlightTheme() const {
  return m_currentTheme->getEditorHighlightTheme();
}

QString ThemeService::getMarkdownEditorHighlightTheme() const {
  return m_currentTheme->getMarkdownEditorHighlightTheme();
}

const QColor &ThemeService::getBaseBackground() const { return m_baseBackground; }

void ThemeService::setBaseBackground(const QColor &p_bg) { m_baseBackground = p_bg; }

const QVector<ThemeService::ThemeInfo> &ThemeService::getAllThemes() const { return m_themes; }

QPixmap ThemeService::getThemePreview(const QString &p_name) const {
  auto theme = findTheme(p_name);
  if (theme) {
    return ThemeUtils::getCover(theme->m_folderPath);
  }
  return QPixmap();
}

void ThemeService::refresh() {
  loadAvailableThemes();
  refreshCurrentTheme();
}

void ThemeService::refreshCurrentTheme() {
  if (m_currentTheme) {
    loadCurrentTheme(m_currentTheme->name());
  }
}

void ThemeService::switchTheme(const QString &p_name) { loadCurrentTheme(p_name); }

void ThemeService::setHookManager(HookManager *p_hookMgr) { m_hookMgr = p_hookMgr; }

QVector<QPair<QString, QString>> ThemeService::getWebStyles() const {
  QVector<QPair<QString, QString>> styles;

  // From themes.
  for (const auto &th : m_themes) {
    auto filePath = Theme::getFile(th.m_folderPath, Theme::File::WebStyleSheet);
    if (!filePath.isEmpty()) {
      styles.push_back(qMakePair(
          tr("[Theme] %1 %2").arg(th.m_displayName, PathUtils::fileName(filePath)), filePath));
    }

    filePath = Theme::getFile(th.m_folderPath, Theme::File::HighlightStyleSheet);
    if (!filePath.isEmpty()) {
      styles.push_back(qMakePair(
          tr("[Theme] %1 %2").arg(th.m_displayName, PathUtils::fileName(filePath)), filePath));
    }
  }

  // From search paths.
  for (const auto &pa : m_webStylesSearchPaths) {
    QDir dir(pa);
    auto styleFiles = dir.entryList({"*.css"}, QDir::Files);
    for (const auto &file : styleFiles) {
      styles.push_back(qMakePair(file, dir.filePath(file)));
    }
  }

  return styles;
}
