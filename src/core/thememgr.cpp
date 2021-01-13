#include "thememgr.h"

#include <QRegularExpression>
#include <QDir>
#include <QDebug>

#include <utils/pathutils.h>
#include "exception.h"
#include <utils/iconutils.h>
#include <vtextedit/vtexteditor.h>
#include "configmgr.h"
#include "coreconfig.h"

using namespace vnotex;

QStringList ThemeMgr::s_searchPaths;

QStringList ThemeMgr::s_webStylesSearchPaths;

ThemeMgr::ThemeMgr(const QString &p_currentThemeName, QObject *p_parent)
    : QObject(p_parent)
{
    loadAvailableThemes();

    loadCurrentTheme(p_currentThemeName);

    IconUtils::setDefaultIconForeground(paletteColor("base#icon#fg"), paletteColor("base#icon#disabled#fg"));
}

QString ThemeMgr::getIconFile(const QString &p_icon) const
{
    Q_ASSERT(!p_icon.isEmpty());

    QRegularExpression sep("[/\\\\]");
    if (p_icon.indexOf(sep) != -1) {
        return p_icon;
    }

    return ":/vnotex/data/core/icons/" + p_icon;
}

void ThemeMgr::loadAvailableThemes()
{
    m_themes.clear();

    for (const auto &pa : s_searchPaths) {
        loadThemes(pa);
    }

    if (m_themes.isEmpty()) {
        Exception::throwOne(Exception::Type::EssentialFileMissing,
                            QString("no available themes found in paths: %1").arg(s_searchPaths.join(QLatin1Char(';'))));
    }
}

void ThemeMgr::loadThemes(const QString &p_path)
{
    qDebug() << "search for themes in" << p_path;
    QDir dir(p_path);
    dir.setFilter(QDir::Dirs | QDir::NoDotAndDotDot);
    auto themeFolders = dir.entryList();
    const auto localeStr = ConfigMgr::getInst().getCoreConfig().getLocaleToUse();
    for (auto &folder : themeFolders) {
        checkAndAddThemeFolder(PathUtils::concatenateFilePath(p_path, folder), localeStr);
    }
}

void ThemeMgr::checkAndAddThemeFolder(const QString &p_folder, const QString &p_locale)
{
    if (Theme::isValidThemeFolder(p_folder)) {
        ThemeInfo info;
        info.m_name = PathUtils::dirName(p_folder);
        info.m_displayName = Theme::getDisplayName(p_folder, p_locale);
        info.m_folderPath = p_folder;
        m_themes.push_back(info);
        qDebug() << "add theme" << info.m_name << info.m_displayName << info.m_folderPath;
    }
}

const Theme &ThemeMgr::getCurrentTheme() const
{
    return *m_currentTheme;
}

void ThemeMgr::loadCurrentTheme(const QString &p_themeName)
{
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
}

Theme *ThemeMgr::loadTheme(const QString &p_themeFolder)
{
    if (p_themeFolder.isEmpty()) {
        qWarning("failed to load theme from empty folder");
        return nullptr;
    }

    try {
        return Theme::fromFolder(p_themeFolder);
    } catch (Exception &p_e) {
        qWarning("failed to load theme from folder %s (%s)",
                 p_themeFolder.toStdString().c_str(),
                 p_e.what());
        return nullptr;
    }
}

QString ThemeMgr::findThemeFolder(const QString &p_name) const
{
    auto theme = findTheme(p_name);
    if (theme) {
        return theme->m_folderPath;
    }
    return QString();
}

const ThemeMgr::ThemeInfo *ThemeMgr::findTheme(const QString &p_name) const
{
    for (const auto &info : m_themes) {
        if (info.m_name == p_name) {
            return &info;
        }
    }

    return nullptr;
}

QString ThemeMgr::fetchQtStyleSheet() const
{
    Q_ASSERT(m_currentTheme);
    if (!m_currentTheme) {
        return QString();
    }

    return m_currentTheme->fetchQtStyleSheet();
}

QString ThemeMgr::paletteColor(const QString &p_name) const
{
    Q_ASSERT(m_currentTheme);
    return m_currentTheme->paletteColor(p_name);
}

void ThemeMgr::addSearchPath(const QString &p_path)
{
    s_searchPaths << p_path;
}

QString ThemeMgr::getFile(Theme::File p_fileType) const
{
    return m_currentTheme->getFile(p_fileType);
}

QString ThemeMgr::getEditorHighlightTheme() const
{
    return m_currentTheme->getEditorHighlightTheme();
}

QString ThemeMgr::getMarkdownEditorHighlightTheme() const
{
    return m_currentTheme->getMarkdownEditorHighlightTheme();
}

void ThemeMgr::addSyntaxHighlightingSearchPaths(const QStringList &p_paths)
{
    vte::VTextEditor::addSyntaxCustomSearchPaths(p_paths);
}

const QColor &ThemeMgr::getBaseBackground() const
{
    return m_baseBackground;
}

void ThemeMgr::setBaseBackground(const QColor &p_bg)
{
    m_baseBackground = p_bg;
}

const QVector<ThemeMgr::ThemeInfo> &ThemeMgr::getAllThemes() const
{
    return m_themes;
}

QPixmap ThemeMgr::getThemePreview(const QString &p_name) const
{
    auto theme = findTheme(p_name);
    if (theme) {
        return Theme::getCover(theme->m_folderPath);
    }
    return QPixmap();
}

void ThemeMgr::refresh()
{
    loadAvailableThemes();
}

void ThemeMgr::addWebStylesSearchPath(const QString &p_path)
{
    s_webStylesSearchPaths << p_path;
}

QVector<QPair<QString, QString>> ThemeMgr::getWebStyles() const
{
    QVector<QPair<QString, QString>> styles;

    // From themes.
    for (const auto &th : m_themes) {
        auto filePath = Theme::getFile(th.m_folderPath, Theme::File::WebStyleSheet);
        if (!filePath.isEmpty()) {
            styles.push_back(qMakePair(tr("[Theme] %1 %2").arg(th.m_displayName, PathUtils::fileName(filePath)),
                                       filePath));
        }

        filePath = Theme::getFile(th.m_folderPath, Theme::File::HighlightStyleSheet);
        if (!filePath.isEmpty()) {
            styles.push_back(qMakePair(tr("[Theme] %1 %2").arg(th.m_displayName, PathUtils::fileName(filePath)),
                                       filePath));
        }
    }

    // From search paths.
    for (const auto &pa : s_webStylesSearchPaths) {
        QDir dir(pa);
        auto styleFiles = dir.entryList({"*.css"}, QDir::Files);
        for (const auto &file : styleFiles) {
            styles.push_back(qMakePair(file, dir.filePath(file)));
        }
    }

    return styles;
}
