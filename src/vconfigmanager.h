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
    Marked,
    MarkdownIt
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
    static bool directoryConfigExist(const QString &path);
    static bool deleteDirectoryConfig(const QString &path);

    // Constants
    static const QString orgName;
    static const QString appName;
    static const QString c_version;

    // CSS style for Warning texts.
    static const QString c_warningTextStyle;

    // CSS style for data in label.
    static const QString c_dataTextStyle;

    // QStylesheet for danger button. Should keep identical with DangerBtn in QSS.
    static const QString c_dangerBtnStyle;

    inline QFont getMdEditFont() const;

    inline QPalette getMdEditPalette() const;

    inline QVector<HighlightingStyle> getMdHighlightingStyles() const;

    inline QMap<QString, QTextCharFormat> getCodeBlockStyles() const;

    inline QString getWelcomePagePath() const;

    QString getTemplateCssUrl();

    QString getEditorStyleUrl();

    const QString &getTemplateCss() const;
    void setTemplateCss(const QString &p_css);

    const QString &getEditorStyle() const;
    void setEditorStyle(const QString &p_style);

    inline QFont getBaseEditFont() const;
    inline QPalette getBaseEditPalette() const;

    inline int getCurNotebookIndex() const;
    inline void setCurNotebookIndex(int index);

    inline void getNotebooks(QVector<VNotebook *> &p_notebooks, QObject *parent);
    inline void setNotebooks(const QVector<VNotebook *> &p_notebooks);

    inline hoedown_extensions getMarkdownExtensions() const;
    inline MarkdownConverterType getMdConverterType() const;
    inline void setMarkdownConverterType(MarkdownConverterType type);

    inline int getTabStopWidth() const;
    inline void setTabStopWidth(int tabStopWidth);
    inline bool getIsExpandTab() const;
    inline void setIsExpandTab(bool isExpandTab);

    inline bool getHighlightCursorLine() const;
    inline void setHighlightCursorLine(bool p_cursorLine);

    inline bool getHighlightSelectedWord() const;
    inline void setHighlightSelectedWord(bool p_selectedWord);

    inline bool getHighlightSearchedWord() const;
    inline void setHighlightSearchedWord(bool p_searchedWord);

    inline bool getAutoIndent() const;
    inline void setAutoIndent(bool p_autoIndent);

    inline bool getAutoList() const;
    inline void setAutoList(bool p_autoList);

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

    inline const QByteArray &getMainSplitterState() const;
    inline void setMainSplitterState(const QByteArray &p_state);

    inline bool getFindCaseSensitive() const;
    inline void setFindCaseSensitive(bool p_enabled);

    inline bool getFindWholeWordOnly() const;
    inline void setFindWholeWordOnly(bool p_enabled);

    inline bool getFindRegularExpression() const;
    inline void setFindRegularExpression(bool p_enabled);

    inline bool getFindIncrementalSearch() const;
    inline void setFindIncrementalSearch(bool p_enabled);

    inline QString getLanguage() const;
    inline void setLanguage(const QString &p_language);

    inline bool getEnableMermaid() const;
    inline void setEnableMermaid(bool p_enabled);

    inline bool getEnableMathjax() const;
    inline void setEnableMathjax(bool p_enabled);

    inline qreal getWebZoomFactor() const;
    void setWebZoomFactor(qreal p_factor);
    inline bool isCustomWebZoomFactor();

    inline QString getEditorCurrentLineBackground() const;
    inline QString getEditorCurrentLineVimBackground() const;

    inline bool getEnableCodeBlockHighlight() const;
    inline void setEnableCodeBlockHighlight(bool p_enabled);

    inline bool getEnablePreviewImages() const;
    inline void setEnablePreviewImages(bool p_enabled);

    inline bool getEnablePreviewImageConstraint() const;
    inline void setEnablePreviewImageConstraint(bool p_enabled);

    // Get the folder the ini file exists.
    QString getConfigFolder() const;

    // Get the folder c_styleConfigFolder in the config folder.
    QString getStyleConfigFolder() const;

    // Read all available css files in c_styleConfigFolder.
    QVector<QString> getCssStyles() const;

    // Read all available mdhl files in c_styleConfigFolder.
    QVector<QString> getEditorStyles() const;

private:
    QVariant getConfigFromSettings(const QString &section, const QString &key);
    void setConfigToSettings(const QString &section, const QString &key, const QVariant &value);
    void readNotebookFromSettings(QVector<VNotebook *> &p_notebooks, QObject *parent);
    void writeNotebookToSettings(const QVector<VNotebook *> &p_notebooks);
    void readPredefinedColorsFromSettings();
    // 1. Update styles common in HTML and Markdown;
    // 2. Update styles for Markdown.
    void updateEditStyle();
    void updateMarkdownEditStyle();

    // Migrate ini file from tamlok/vnote.ini to vnote/vnote.ini.
    // This is for the change of org name.
    void migrateIniFile();

    bool outputDefaultCssStyle() const;
    bool outputDefaultEditorStyle() const;

    // See if the old c_obsoleteDirConfigFile exists. If so, rename it to
    // the new one; if not, use the c_dirConfigFile.
    static QString fetchDirConfigFilePath(const QString &p_path);

    int m_editorFontSize;
    QFont baseEditFont;
    QPalette baseEditPalette;
    QFont mdEditFont;
    QPalette mdEditPalette;
    QVector<HighlightingStyle> mdHighlightingStyles;
    QMap<QString, QTextCharFormat> m_codeBlockStyles;
    QString welcomePagePath;
    QString m_templateCss;
    QString m_editorStyle;
    int curNotebookIndex;

    // Markdown Converter
    hoedown_extensions markdownExtensions;
    MarkdownConverterType mdConverterType;

    // Num of spaces
    int tabStopWidth;
    // Expand tab to @tabStopWidth spaces
    bool isExpandTab;

    // Highlight current cursor line.
    bool m_highlightCursorLine;

    // Highlight selected word.
    bool m_highlightSelectedWord;

    // Highlight searched word.
    bool m_highlightSearchedWord;

    // Auto Indent.
    bool m_autoIndent;

    // Auto List.
    bool m_autoList;

    // App defined color
    QVector<VColor> predefinedColors;
    QString curBackgroundColor;
    QString curRenderBackgroundColor;

    bool m_toolsDockChecked;

    QByteArray m_mainWindowGeometry;
    QByteArray m_mainWindowState;
    QByteArray m_mainSplitterState;

    // Find/Replace dialog options
    bool m_findCaseSensitive;
    bool m_findWholeWordOnly;
    bool m_findRegularExpression;
    bool m_findIncrementalSearch;

    // Language
    QString m_language;

    // Enable Mermaid.
    bool m_enableMermaid;

    // Enable Mathjax.
    bool m_enableMathjax;

    // Zoom factor of the QWebEngineView.
    qreal m_webZoomFactor;

    // Current line background color in editor.
    QString m_editorCurrentLineBackground;
    // Current line background color in editor in Vim mode.
    QString m_editorCurrentLineVimBackground;

    // Enable colde block syntax highlight.
    bool m_enableCodeBlockHighlight;

    // Preview images in edit mode.
    bool m_enablePreviewImages;

    // Constrain the width of image preview in edit mode.
    bool m_enablePreviewImageConstraint;

    // The name of the config file in each directory, obsolete.
    // Use c_dirConfigFile instead.
    static const QString c_obsoleteDirConfigFile;

    // The name of the config file in each directory.
    static const QString c_dirConfigFile;

    // The name of the default configuration file
    static const QString defaultConfigFilePath;
    // QSettings for the user configuration
    QSettings *userSettings;
    // Qsettings for @defaultConfigFileName
    QSettings *defaultSettings;
    // The folder name of style files.
    static const QString c_styleConfigFolder;
    static const QString c_defaultCssFile;

    // MDHL files for editor styles.
    static const QString c_defaultMdhlFile;
    static const QString c_solarizedDarkMdhlFile;
    static const QString c_solarizedLightMdhlFile;
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

inline QMap<QString, QTextCharFormat> VConfigManager::getCodeBlockStyles() const
{
    return m_codeBlockStyles;
}

inline QString VConfigManager::getWelcomePagePath() const
{
    return welcomePagePath;
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

inline void VConfigManager::getNotebooks(QVector<VNotebook *> &p_notebooks, QObject *parent)
{
    readNotebookFromSettings(p_notebooks, parent);
}

inline void VConfigManager::setNotebooks(const QVector<VNotebook *> &p_notebooks)
{
    writeNotebookToSettings(p_notebooks);
}

inline hoedown_extensions VConfigManager::getMarkdownExtensions() const
{
    return markdownExtensions;
}

inline MarkdownConverterType VConfigManager::getMdConverterType() const
{
    return mdConverterType;
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

inline bool VConfigManager::getHighlightCursorLine() const
{
    return m_highlightCursorLine;
}

inline void VConfigManager::setHighlightCursorLine(bool p_cursorLine)
{
    if (p_cursorLine == m_highlightCursorLine) {
        return;
    }
    m_highlightCursorLine = p_cursorLine;
    setConfigToSettings("global", "highlight_cursor_line", m_highlightCursorLine);
}

inline bool VConfigManager::getHighlightSelectedWord() const
{
    return m_highlightSelectedWord;
}

inline void VConfigManager::setHighlightSelectedWord(bool p_selectedWord)
{
    if (p_selectedWord == m_highlightSelectedWord) {
        return;
    }
    m_highlightSelectedWord = p_selectedWord;
    setConfigToSettings("global", "highlight_selected_word",
                        m_highlightSelectedWord);
}

inline bool VConfigManager::getHighlightSearchedWord() const
{
    return m_highlightSearchedWord;
}

inline void VConfigManager::setHighlightSearchedWord(bool p_searchedWord)
{
    if (p_searchedWord == m_highlightSearchedWord) {
        return;
    }
    m_highlightSearchedWord = p_searchedWord;
    setConfigToSettings("global", "highlight_searched_word",
                        m_highlightSearchedWord);
}

inline bool VConfigManager::getAutoIndent() const
{
    return m_autoIndent;
}

inline void VConfigManager::setAutoIndent(bool p_autoIndent)
{
    if (m_autoIndent == p_autoIndent) {
        return;
    }
    m_autoIndent = p_autoIndent;
    setConfigToSettings("global", "auto_indent",
                        m_autoIndent);
}

inline bool VConfigManager::getAutoList() const
{
    return m_autoList;
}

inline void VConfigManager::setAutoList(bool p_autoList)
{
    if (m_autoList == p_autoList) {
        return;
    }
    m_autoList = p_autoList;
    setConfigToSettings("global", "auto_list",
                        m_autoList);
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
    updateEditStyle();
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

inline const QByteArray& VConfigManager::getMainSplitterState() const
{
    return m_mainSplitterState;
}

inline void VConfigManager::setMainSplitterState(const QByteArray &p_state)
{
    m_mainSplitterState = p_state;
    setConfigToSettings("session", "main_splitter_state", m_mainSplitterState);
}

inline bool VConfigManager::getFindCaseSensitive() const
{
    return m_findCaseSensitive;
}

inline void VConfigManager::setFindCaseSensitive(bool p_enabled)
{
    if (m_findCaseSensitive == p_enabled) {
        return;
    }
    m_findCaseSensitive = p_enabled;
    setConfigToSettings("global", "find_case_sensitive", m_findCaseSensitive);
}

inline bool VConfigManager::getFindWholeWordOnly() const
{
    return m_findWholeWordOnly;
}

inline void VConfigManager::setFindWholeWordOnly(bool p_enabled)
{
    if (m_findWholeWordOnly == p_enabled) {
        return;
    }
    m_findWholeWordOnly = p_enabled;
    setConfigToSettings("global", "find_whole_word_only", m_findWholeWordOnly);
}

inline bool VConfigManager::getFindRegularExpression() const
{
    return m_findRegularExpression;
}

inline void VConfigManager::setFindRegularExpression(bool p_enabled)
{
    if (m_findRegularExpression == p_enabled) {
        return;
    }
    m_findRegularExpression = p_enabled;
    setConfigToSettings("global", "find_regular_expression",
                        m_findRegularExpression);
}

inline bool VConfigManager::getFindIncrementalSearch() const
{
    return m_findIncrementalSearch;
}

inline void VConfigManager::setFindIncrementalSearch(bool p_enabled)
{
    if (m_findIncrementalSearch == p_enabled) {
        return;
    }
    m_findIncrementalSearch = p_enabled;
    setConfigToSettings("global", "find_incremental_search",
                        m_findIncrementalSearch);
}

inline QString VConfigManager::getLanguage() const
{
    return m_language;
}

inline void VConfigManager::setLanguage(const QString &p_language)
{
    if (m_language == p_language) {
        return;
    }
    m_language = p_language;
    setConfigToSettings("global", "language",
                        m_language);
}

inline bool VConfigManager::getEnableMermaid() const
{
    return m_enableMermaid;
}

inline void VConfigManager::setEnableMermaid(bool p_enabled)
{
    if (m_enableMermaid == p_enabled) {
        return;
    }
    m_enableMermaid = p_enabled;
    setConfigToSettings("global", "enable_mermaid", m_enableMermaid);
}

inline bool VConfigManager::getEnableMathjax() const
{
    return m_enableMathjax;
}

inline void VConfigManager::setEnableMathjax(bool p_enabled)
{
    if (m_enableMathjax == p_enabled) {
        return;
    }
    m_enableMathjax = p_enabled;
    setConfigToSettings("global", "enable_mathjax", m_enableMathjax);
}

inline qreal VConfigManager::getWebZoomFactor() const
{
    return m_webZoomFactor;
}

inline bool VConfigManager::isCustomWebZoomFactor()
{
    qreal factorFromIni = getConfigFromSettings("global", "web_zoom_factor").toReal();
    // -1 indicates let system automatically calculate the factor.
    return factorFromIni > 0;
}

inline QString VConfigManager::getEditorCurrentLineBackground() const
{
    return m_editorCurrentLineBackground;
}

inline QString VConfigManager::getEditorCurrentLineVimBackground() const
{
    return m_editorCurrentLineVimBackground;
}

inline bool VConfigManager::getEnableCodeBlockHighlight() const
{
    return m_enableCodeBlockHighlight;
}

inline void VConfigManager::setEnableCodeBlockHighlight(bool p_enabled)
{
    if (m_enableCodeBlockHighlight == p_enabled) {
        return;
    }
    m_enableCodeBlockHighlight = p_enabled;
    setConfigToSettings("global", "enable_code_block_highlight",
                        m_enableCodeBlockHighlight);
}

inline bool VConfigManager::getEnablePreviewImages() const
{
    return m_enablePreviewImages;
}

inline void VConfigManager::setEnablePreviewImages(bool p_enabled)
{
    if (m_enablePreviewImages == p_enabled) {
        return;
    }

    m_enablePreviewImages = p_enabled;
    setConfigToSettings("global", "enable_preview_images",
                        m_enablePreviewImages);
}

inline bool VConfigManager::getEnablePreviewImageConstraint() const
{
    return m_enablePreviewImageConstraint;
}

inline void VConfigManager::setEnablePreviewImageConstraint(bool p_enabled)
{
    if (m_enablePreviewImageConstraint == p_enabled) {
        return;
    }

    m_enablePreviewImageConstraint = p_enabled;
    setConfigToSettings("global", "enable_preview_image_constraint",
                        m_enablePreviewImageConstraint);
}

#endif // VCONFIGMANAGER_H
