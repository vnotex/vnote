#ifndef VCONFIGMANAGER_H
#define VCONFIGMANAGER_H

#include <QFont>
#include <QPalette>
#include <QVector>
#include <QSettings>
#include <QHash>
#include "vnotebook.h"
#include "hgmarkdownhighlighter.h"
#include "vmarkdownconverter.h"

class QJsonObject;
class QString;

enum MarkdownConverterType
{
    Hoedown = 0,
    Marked,
    MarkdownIt,
    Showdown
};

struct VColor
{
    QString name;
    QString rgb; // 'FFFFFF', without '#'
};

class VConfigManager
{
public:
    VConfigManager();
    ~VConfigManager();
    void initialize();

    // Read config from the directory config json file into a QJsonObject.
    // @path is the directory containing the config json file.
    static QJsonObject readDirectoryConfig(const QString &path);

    static bool writeDirectoryConfig(const QString &path, const QJsonObject &configJson);
    static bool directoryConfigExist(const QString &path);
    static bool deleteDirectoryConfig(const QString &path);

    static QString getLogFilePath();

    // Get the path of the folder used to store default notebook.
    static QString getVnoteNotebookFolderPath();

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

    QFont getMdEditFont() const;

    QPalette getMdEditPalette() const;

    QVector<HighlightingStyle> getMdHighlightingStyles() const;

    QHash<QString, QTextCharFormat> getCodeBlockStyles() const;

    QString getWelcomePagePath() const;

    QString getTemplateCssUrl();

    QString getEditorStyleUrl();

    const QString &getTemplateCss() const;
    void setTemplateCss(const QString &p_css);

    const QString &getEditorStyle() const;
    void setEditorStyle(const QString &p_style);

    QFont getBaseEditFont() const;
    QPalette getBaseEditPalette() const;

    int getCurNotebookIndex() const;
    void setCurNotebookIndex(int index);

    void getNotebooks(QVector<VNotebook *> &p_notebooks, QObject *parent);
    void setNotebooks(const QVector<VNotebook *> &p_notebooks);

    hoedown_extensions getMarkdownExtensions() const;
    MarkdownConverterType getMdConverterType() const;
    void setMarkdownConverterType(MarkdownConverterType type);

    int getTabStopWidth() const;
    void setTabStopWidth(int tabStopWidth);
    bool getIsExpandTab() const;
    void setIsExpandTab(bool isExpandTab);

    bool getHighlightCursorLine() const;
    void setHighlightCursorLine(bool p_cursorLine);

    bool getHighlightSelectedWord() const;
    void setHighlightSelectedWord(bool p_selectedWord);

    bool getHighlightSearchedWord() const;
    void setHighlightSearchedWord(bool p_searchedWord);

    bool getAutoIndent() const;
    void setAutoIndent(bool p_autoIndent);

    bool getAutoList() const;
    void setAutoList(bool p_autoList);

    const QVector<VColor> &getPredefinedColors() const;

    const QString &getCurBackgroundColor() const;
    void setCurBackgroundColor(const QString &colorName);

    const QString &getCurRenderBackgroundColor() const;
    void setCurRenderBackgroundColor(const QString &colorName);

    bool getToolsDockChecked() const;
    void setToolsDockChecked(bool p_checked);

    const QByteArray &getMainWindowGeometry() const;
    void setMainWindowGeometry(const QByteArray &p_geometry);

    const QByteArray &getMainWindowState() const;
    void setMainWindowState(const QByteArray &p_state);

    const QByteArray &getMainSplitterState() const;
    void setMainSplitterState(const QByteArray &p_state);

    bool getFindCaseSensitive() const;
    void setFindCaseSensitive(bool p_enabled);

    bool getFindWholeWordOnly() const;
    void setFindWholeWordOnly(bool p_enabled);

    bool getFindRegularExpression() const;
    void setFindRegularExpression(bool p_enabled);

    bool getFindIncrementalSearch() const;
    void setFindIncrementalSearch(bool p_enabled);

    QString getLanguage() const;
    void setLanguage(const QString &p_language);

    bool getEnableMermaid() const;
    void setEnableMermaid(bool p_enabled);

    bool getEnableFlowchart() const;
    void setEnableFlowchart(bool p_enabled);

    bool getEnableMathjax() const;
    void setEnableMathjax(bool p_enabled);

    qreal getWebZoomFactor() const;
    void setWebZoomFactor(qreal p_factor);
    bool isCustomWebZoomFactor();

    const QString &getEditorCurrentLineBg() const;
    QString getEditorTrailingSpaceBackground() const;

    const QString &getEditorVimNormalBg() const;
    const QString &getEditorVimInsertBg() const;
    const QString &getEditorVimVisualBg() const;
    const QString &getEditorVimReplaceBg() const;

    bool getEnableCodeBlockHighlight() const;
    void setEnableCodeBlockHighlight(bool p_enabled);

    bool getEnablePreviewImages() const;
    void setEnablePreviewImages(bool p_enabled);

    bool getEnablePreviewImageConstraint() const;
    void setEnablePreviewImageConstraint(bool p_enabled);

    bool getEnableImageConstraint() const;
    void setEnableImageConstraint(bool p_enabled);

    bool getEnableImageCaption() const;
    void setEnableImageCaption(bool p_enabled);

    const QString &getImageFolder() const;
    // Empty string to reset the default folder.
    void setImageFolder(const QString &p_folder);
    bool isCustomImageFolder() const;

    bool getEnableTrailingSpaceHighlight() const;
    void setEnableTrailingSapceHighlight(bool p_enabled);

    bool getEnableVimMode() const;
    void setEnableVimMode(bool p_enabled);

    bool getEnableSmartImInVimMode() const;
    void setEnableSmartImInVimMode(bool p_enabled);

    // Get the folder the ini file exists.
    QString getConfigFolder() const;

    // Get the folder c_styleConfigFolder in the config folder.
    QString getStyleConfigFolder() const;

    // Read all available css files in c_styleConfigFolder.
    QVector<QString> getCssStyles() const;

    // Read all available mdhl files in c_styleConfigFolder.
    QVector<QString> getEditorStyles() const;

private:
    QVariant getConfigFromSettings(const QString &section, const QString &key) const;
    void setConfigToSettings(const QString &section, const QString &key, const QVariant &value);

    // Get default config from vnote.ini.
    QVariant getDefaultConfig(const QString &p_section, const QString &p_key) const;

    // Reset user config to default config and return the default config value.
    QVariant resetDefaultConfig(const QString &p_section, const QString &p_key);

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

    // Default font and palette.
    QFont m_defaultEditFont;
    QPalette m_defaultEditPalette;

    // Font and palette used for non-markdown editor.
    QFont baseEditFont;
    QPalette baseEditPalette;

    // Font and palette used for markdown editor.
    QFont mdEditFont;
    QPalette mdEditPalette;

    QVector<HighlightingStyle> mdHighlightingStyles;
    QHash<QString, QTextCharFormat> m_codeBlockStyles;

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

    // Enable flowchart.js.
    bool m_enableFlowchart;

    // Enable Mathjax.
    bool m_enableMathjax;

    // Zoom factor of the QWebEngineView.
    qreal m_webZoomFactor;

    // Current line background color in editor.
    QString m_editorCurrentLineBg;

    // Current line background color in editor in Vim normal mode.
    QString m_editorVimNormalBg;

    // Current line background color in editor in Vim insert mode.
    QString m_editorVimInsertBg;

    // Current line background color in editor in Vim visual mode.
    QString m_editorVimVisualBg;

    // Current line background color in editor in Vim replace mode.
    QString m_editorVimReplaceBg;

    // Trailing space background color in editor.
    QString m_editorTrailingSpaceBg;

    // Enable colde block syntax highlight.
    bool m_enableCodeBlockHighlight;

    // Preview images in edit mode.
    bool m_enablePreviewImages;

    // Constrain the width of image preview in edit mode.
    bool m_enablePreviewImageConstraint;

    // Constrain the width of image in read mode.
    bool m_enableImageConstraint;

    // Center image and add the alt text as caption.
    bool m_enableImageCaption;

    // Global default folder name to store images of all the notes.
    // Each notebook can specify its custom folder.
    QString m_imageFolder;

    // Enable trailing-space highlight.
    bool m_enableTrailingSpaceHighlight;

    // Enable Vim mode.
    bool m_enableVimMode;

    // Enable smart input method in Vim mode.
    bool m_enableSmartImInVimMode;

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

    // The folder name to store all notebooks if user does not specify one.
    static const QString c_vnoteNotebookFolderName;
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

inline QHash<QString, QTextCharFormat> VConfigManager::getCodeBlockStyles() const
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

inline bool VConfigManager::getEnableFlowchart() const
{
    return m_enableFlowchart;
}

inline void VConfigManager::setEnableFlowchart(bool p_enabled)
{
    if (m_enableFlowchart == p_enabled) {
        return;
    }

    m_enableFlowchart = p_enabled;
    setConfigToSettings("global", "enable_flowchart", m_enableFlowchart);
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

inline const QString &VConfigManager::getEditorCurrentLineBg() const
{
    return m_editorCurrentLineBg;
}

inline QString VConfigManager::getEditorTrailingSpaceBackground() const
{
    return m_editorTrailingSpaceBg;
}

inline const QString &VConfigManager::getEditorVimNormalBg() const
{
    return m_editorVimNormalBg;
}

inline const QString &VConfigManager::getEditorVimInsertBg() const
{
    return m_editorVimInsertBg;
}

inline const QString &VConfigManager::getEditorVimVisualBg() const
{
    return m_editorVimVisualBg;
}

inline const QString &VConfigManager::getEditorVimReplaceBg() const
{
    return m_editorVimReplaceBg;
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

inline bool VConfigManager::getEnableImageConstraint() const
{
    return m_enableImageConstraint;
}

inline void VConfigManager::setEnableImageConstraint(bool p_enabled)
{
    if (m_enableImageConstraint == p_enabled) {
        return;
    }

    m_enableImageConstraint = p_enabled;
    setConfigToSettings("global", "enable_image_constraint",
                        m_enableImageConstraint);
}

inline bool VConfigManager::getEnableImageCaption() const
{
    return m_enableImageCaption;
}

inline void VConfigManager::setEnableImageCaption(bool p_enabled)
{
    if (m_enableImageCaption == p_enabled) {
        return;
    }
    m_enableImageCaption = p_enabled;
    setConfigToSettings("global", "enable_image_caption",
                        m_enableImageCaption);
}

inline const QString &VConfigManager::getImageFolder() const
{
    return m_imageFolder;
}

inline void VConfigManager::setImageFolder(const QString &p_folder)
{
    if (p_folder.isEmpty()) {
        // Reset the default folder.
        m_imageFolder = resetDefaultConfig("global", "image_folder").toString();
        return;
    }

    if (m_imageFolder == p_folder) {
        return;
    }

    m_imageFolder = p_folder;
    setConfigToSettings("global", "image_folder", m_imageFolder);
}

inline bool VConfigManager::isCustomImageFolder() const
{
    return m_imageFolder != getDefaultConfig("global", "image_folder").toString();
}

inline bool VConfigManager::getEnableTrailingSpaceHighlight() const
{
    return m_enableTrailingSpaceHighlight;
}

inline void VConfigManager::setEnableTrailingSapceHighlight(bool p_enabled)
{
    if (m_enableTrailingSpaceHighlight == p_enabled) {
        return;
    }

    m_enableTrailingSpaceHighlight = p_enabled;
    setConfigToSettings("global", "enable_trailing_space_highlight",
                        m_enableTrailingSpaceHighlight);
}

inline bool VConfigManager::getEnableVimMode() const
{
    return m_enableVimMode;
}

inline void VConfigManager::setEnableVimMode(bool p_enabled)
{
    if (m_enableVimMode == p_enabled) {
        return;
    }

    m_enableVimMode = p_enabled;
    setConfigToSettings("global", "enable_vim_mode",
                        m_enableVimMode);
}

inline bool VConfigManager::getEnableSmartImInVimMode() const
{
    return m_enableSmartImInVimMode;
}

inline void VConfigManager::setEnableSmartImInVimMode(bool p_enabled)
{
    if (m_enableSmartImInVimMode == p_enabled) {
        return;
    }

    m_enableSmartImInVimMode = p_enabled;
    setConfigToSettings("global", "enable_smart_im_in_vim_mode",
                        m_enableSmartImInVimMode);
}

#endif // VCONFIGMANAGER_H
