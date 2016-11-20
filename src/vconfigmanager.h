#ifndef VCONFIGMANAGER_H
#define VCONFIGMANAGER_H

#include <QFont>
#include <QPalette>
#include <QVector>
#include <QSettings>
#include "vnotebook.h"

#include "hgmarkdownhighlighter.h"
#include "vmarkdownconverter.h"

class QJsonObject;
class QString;

enum MarkdownConverterType
{
    Hoedown = 0,
    Marked
};

struct VColor
{
    QString name;
    QString rgb; // 'FFFFFF', ithout '#'
};

class VConfigManager
{
public:
    VConfigManager();
    ~VConfigManager();
    void initialize();

    // Static helper functions
    // Read config from the directory config json file into a QJsonObject
    static QJsonObject readDirectoryConfig(const QString &path);
    static bool writeDirectoryConfig(const QString &path, const QJsonObject &configJson);
    static bool deleteDirectoryConfig(const QString &path);

    // Constants
    static const QString orgName;
    static const QString appName;

    inline QFont getMdEditFont() const;

    inline QPalette getMdEditPalette() const;

    inline QVector<HighlightingStyle> getMdHighlightingStyles() const;

    inline QString getWelcomePagePath() const;

    inline QString getTemplatePath() const;

    inline QString getTemplateCssUrl() const;

    inline QFont getBaseEditFont() const;
    inline QPalette getBaseEditPalette() const;

    inline int getCurNotebookIndex() const;
    inline void setCurNotebookIndex(int index);

    inline const QVector<VNotebook>& getNotebooks() const;
    inline void setNotebooks(const QVector<VNotebook> &notebooks);

    inline hoedown_extensions getMarkdownExtensions() const;
    inline MarkdownConverterType getMdConverterType() const;
    inline void setMarkdownConverterType(MarkdownConverterType type);

    inline QString getPreTemplatePath() const;
    inline QString getPostTemplatePath() const;

    inline int getTabStopWidth() const;
    inline void setTabStopWidth(int tabStopWidth);
    inline bool getIsExpandTab() const;
    inline void setIsExpandTab(bool isExpandTab);

    inline const QVector<VColor> &getPredefinedColors() const;

    inline const QString &getCurBackgroundColor() const;
    inline void setCurBackgroundColor(const QString &colorName);

    inline const QString &getCurRenderBackgroundColor() const;
    inline void setCurRenderBackgroundColor(const QString &colorName);

    inline bool getToolsDockChecked() const;
    inline void setToolsDockChecked(bool p_checked);

    inline const QByteArray &getMainWindowGeometry() const;
    inline void setMainWindowGeometry(const QByteArray &p_geometry);

    inline const QByteArray &getMainWindowState() const;
    inline void setMainWindowState(const QByteArray &p_state);

private:
    void updateMarkdownEditStyle();
    QVariant getConfigFromSettings(const QString &section, const QString &key);
    void setConfigToSettings(const QString &section, const QString &key, const QVariant &value);
    void readNotebookFromSettings();
    void writeNotebookToSettings();
    void readPredefinedColorsFromSettings();
    // Update baseEditPalette according to curBackgroundColor
    void updatePaletteColor();

    QFont baseEditFont;
    QPalette baseEditPalette;
    QFont mdEditFont;
    QPalette mdEditPalette;
    QVector<HighlightingStyle> mdHighlightingStyles;
    QString welcomePagePath;
    QString templatePath;
    QString preTemplatePath;
    QString postTemplatePath;
    QString templateCssUrl;
    int curNotebookIndex;
    QVector<VNotebook> notebooks;

    // Markdown Converter
    hoedown_extensions markdownExtensions;
    MarkdownConverterType mdConverterType;

    // Num of spaces
    int tabStopWidth;
    // Expand tab to @tabStopWidth spaces
    bool isExpandTab;

    // App defined color
    QVector<VColor> predefinedColors;
    QString curBackgroundColor;
    QString curRenderBackgroundColor;

    bool m_toolsDockChecked;

    QByteArray m_mainWindowGeometry;
    QByteArray m_mainWindowState;

    // The name of the config file in each directory
    static const QString dirConfigFileName;
    // The name of the default configuration file
    static const QString defaultConfigFilePath;
    // QSettings for the user configuration
    QSettings *userSettings;
    // Qsettings for @defaultConfigFileName
    QSettings *defaultSettings;
};


inline QFont VConfigManager::getMdEditFont() const
{
    return mdEditFont;
}

inline QPalette VConfigManager::getMdEditPalette() const
{
    return mdEditPalette;
}

inline QVector<HighlightingStyle> VConfigManager::getMdHighlightingStyles() const
{
    return mdHighlightingStyles;
}

inline QString VConfigManager::getWelcomePagePath() const
{
    return welcomePagePath;
}

inline QString VConfigManager::getTemplatePath() const
{
    return templatePath;
}

inline QString VConfigManager::getTemplateCssUrl() const
{
    return templateCssUrl;
}

inline QFont VConfigManager::getBaseEditFont() const
{
    return baseEditFont;
}

inline QPalette VConfigManager::getBaseEditPalette() const
{
    return baseEditPalette;
}

inline int VConfigManager::getCurNotebookIndex() const
{
    return curNotebookIndex;
}

inline void VConfigManager::setCurNotebookIndex(int index)
{
    if (index == curNotebookIndex) {
        return;
    }
    curNotebookIndex = index;
    setConfigToSettings("global", "current_notebook", index);
}

inline const QVector<VNotebook>& VConfigManager::getNotebooks() const
{
    return notebooks;
}

inline void VConfigManager::setNotebooks(const QVector<VNotebook> &notebooks)
{
    this->notebooks = notebooks;
    writeNotebookToSettings();
}

inline hoedown_extensions VConfigManager::getMarkdownExtensions() const
{
    return markdownExtensions;
}

inline MarkdownConverterType VConfigManager::getMdConverterType() const
{
    return mdConverterType;
}

inline QString VConfigManager::getPreTemplatePath() const
{
    return preTemplatePath;
}

inline QString VConfigManager::getPostTemplatePath() const
{
    return postTemplatePath;
}

inline void VConfigManager::setMarkdownConverterType(MarkdownConverterType type)
{
    if (mdConverterType == type) {
        return;
    }
    mdConverterType = type;
    setConfigToSettings("global", "markdown_converter", type);
}

inline int VConfigManager::getTabStopWidth() const
{
    return tabStopWidth;
}

inline bool VConfigManager::getIsExpandTab() const
{
    return isExpandTab;
}

inline void VConfigManager::setTabStopWidth(int tabStopWidth)
{
    if (tabStopWidth == this->tabStopWidth) {
        return;
    }
    this->tabStopWidth = tabStopWidth;
    setConfigToSettings("global", "tab_stop_width", tabStopWidth);
}

inline void VConfigManager::setIsExpandTab(bool isExpandTab)
{
    if (isExpandTab == this->isExpandTab) {
        return;
    }
    this->isExpandTab = isExpandTab;
    setConfigToSettings("global", "is_expand_tab", this->isExpandTab);
}

inline const QVector<VColor>& VConfigManager::getPredefinedColors() const
{
    return predefinedColors;
}

inline const QString& VConfigManager::getCurBackgroundColor() const
{
    return curBackgroundColor;
}

inline void VConfigManager::setCurBackgroundColor(const QString &colorName)
{
    if (curBackgroundColor == colorName) {
        return;
    }
    curBackgroundColor = colorName;
    setConfigToSettings("global", "current_background_color",
                        curBackgroundColor);
    updatePaletteColor();
}

inline const QString& VConfigManager::getCurRenderBackgroundColor() const
{
    return curRenderBackgroundColor;
}

inline void VConfigManager::setCurRenderBackgroundColor(const QString &colorName)
{
    if (curRenderBackgroundColor == colorName) {
        return;
    }
    curRenderBackgroundColor = colorName;
    setConfigToSettings("global", "current_render_background_color",
                        curRenderBackgroundColor);
}

inline bool VConfigManager::getToolsDockChecked() const
{
    return m_toolsDockChecked;
}

inline void VConfigManager::setToolsDockChecked(bool p_checked)
{
    m_toolsDockChecked = p_checked;
    setConfigToSettings("session", "tools_dock_checked",
                        m_toolsDockChecked);
}

inline const QByteArray& VConfigManager::getMainWindowGeometry() const
{
    return m_mainWindowGeometry;
}

inline void VConfigManager::setMainWindowGeometry(const QByteArray &p_geometry)
{
    m_mainWindowGeometry = p_geometry;
    setConfigToSettings("session", "main_window_geometry",
                        m_mainWindowGeometry);
}

inline const QByteArray& VConfigManager::getMainWindowState() const
{
    return m_mainWindowState;
}

inline void VConfigManager::setMainWindowState(const QByteArray &p_state)
{
    m_mainWindowState = p_state;
    setConfigToSettings("session", "main_window_state",
                        m_mainWindowState);
}

#endif // VCONFIGMANAGER_H
