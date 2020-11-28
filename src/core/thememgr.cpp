#include "thememgr.h"

#include <QRegularExpression>
#include <QDir>
#include <QDebug>

#include <utils/pathutils.h>
#include "exception.h"
#include <utils/iconutils.h>
#include <vtextedit/vtexteditor.h>

using namespace vnotex;

QStringList ThemeMgr::s_searchPaths;

ThemeMgr::ThemeMgr(const QString &p_currentThemeName, QObject *p_parent)
    : QObject(p_parent)
{
    loadAvailableThemes();

    loadCurrentTheme(p_currentThemeName);

    IconUtils::setDefaultIconForeground(paletteColor("base#icon#fg"), paletteColor("base#icon#disabled_fg"));
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
    for (const auto &pa : s_searchPaths) {
        loadThemes(pa);
    }

    if (m_availableThemes.isEmpty()) {
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
    for (auto &folder : themeFolders) {
        checkAndAddThemeFolder(PathUtils::concatenateFilePath(p_path, folder));
    }
}

void ThemeMgr::checkAndAddThemeFolder(const QString &p_folder)
{
    if (Theme::isValidThemeFolder(p_folder)) {
        QString themeName = PathUtils::dirName(p_folder);
        m_availableThemes.insert(themeName, p_folder);
        qDebug() << "add theme" << themeName << p_folder;
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
        qCritical() << "fail to locate theme" << p_themeName;
    } else {
        m_currentTheme.reset(loadTheme(themeFolder));
    }

    if (!m_currentTheme) {
        const QString defaultTheme("native");
        qInfo() << "fall back to default theme" << defaultTheme;
        m_currentTheme.reset(loadTheme(findThemeFolder(defaultTheme)));
    }
}

Theme *ThemeMgr::loadTheme(const QString &p_themeFolder)
{
    if (p_themeFolder.isEmpty()) {
        qCritical("fail to load theme from empty folder");
        return nullptr;
    }

    try {
        return Theme::fromFolder(p_themeFolder);
    } catch (Exception &p_e) {
        qCritical("fail to load theme from folder %s (%s)",
                  p_themeFolder.toStdString().c_str(),
                  p_e.what());
        return nullptr;
    }
}

QString ThemeMgr::findThemeFolder(const QString &p_name) const
{
    auto it = m_availableThemes.find(p_name);
    if (it != m_availableThemes.end()) {
        return it.value();
    }

    return QString();
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
