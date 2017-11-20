#ifndef VCONFIGMANAGER_H
#define VCONFIGMANAGER_H

#include <QObject>
#include <QFont>
#include <QPalette>
#include <QVector>
#include <QSettings>
#include <QHash>
#include "vnotebook.h"
#include "hgmarkdownhighlighter.h"
#include "vmarkdownconverter.h"
#include "vconstants.h"
#include "vfilesessioninfo.h"
#include "utils/vmetawordmanager.h"


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

struct MarkdownitOption
{
    MarkdownitOption(bool p_html, bool p_breaks, bool p_linkify)
        : m_html(p_html), m_breaks(p_breaks), m_linkify(p_linkify)
    {
    }

    bool m_html;
    bool m_breaks;
    bool m_linkify;
};

// Type of heading sequence.
enum class HeadingSequenceType
{
    Disabled = 0,
    Enabled,

    // Enabled only for internal notes.
    EnabledNoteOnly,

    Invalid
};

class VConfigManager : public QObject
{
public:
    explicit VConfigManager(QObject *p_parent = NULL);

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

    QString getTemplateCodeBlockCssUrl();

    QString getEditorStyleUrl();

    const QString &getTemplateCss() const;
    void setTemplateCss(const QString &p_css);

    const QString &getTemplateCodeBlockCss() const;
    void setTemplateCodeBlockCss(const QString &p_css);

    const QString &getEditorStyle() const;
    void setEditorStyle(const QString &p_style);

    QFont getBaseEditFont() const;
    QPalette getBaseEditPalette() const;

    int getCurNotebookIndex() const;
    void setCurNotebookIndex(int index);

    // Read [notebooks] section from settings into @p_notebooks.
    void getNotebooks(QVector<VNotebook *> &p_notebooks, QObject *p_parent);

    // Write @p_notebooks to [notebooks] section into settings.
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

    const QByteArray &getNaviSplitterState() const;
    void setNaviSplitterState(const QByteArray &p_state);

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
    const QString &getEditorTrailingSpaceBg() const;
    const QString &getEditorSelectedWordBg() const;
    const QString &getEditorSearchedWordBg() const;
    const QString &getEditorSearchedWordCursorBg() const;
    const QString &getEditorIncrementalSearchedWordBg() const;

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

    const QString &getImageFolderExt() const;
    // Empty string to reset the default folder.
    void setImageFolderExt(const QString &p_folder);
    bool isCustomImageFolderExt() const;

    const QString &getAttachmentFolder() const;
    // Empty string to reset the default folder.
    void setAttachmentFolder(const QString &p_folder);
    bool isCustomAttachmentFolder() const;

    bool getEnableTrailingSpaceHighlight() const;
    void setEnableTrailingSapceHighlight(bool p_enabled);

    bool getEnableVimMode() const;
    void setEnableVimMode(bool p_enabled);

    bool getEnableSmartImInVimMode() const;
    void setEnableSmartImInVimMode(bool p_enabled);

    int getEditorLineNumber() const;
    void setEditorLineNumber(int p_mode);

    const QString &getEditorLineNumberBg() const;
    const QString &getEditorLineNumberFg() const;

    int getMinimizeToStystemTray() const;
    void setMinimizeToSystemTray(int p_val);

    void initDocSuffixes();
    const QHash<int, QList<QString>> &getDocSuffixes() const;

    int getMarkdownHighlightInterval() const;

    int getLineDistanceHeight() const;

    bool getInsertTitleFromNoteName() const;
    void setInsertTitleFromNoteName(bool p_enabled);

    OpenFileMode getNoteOpenMode() const;
    void setNoteOpenMode(OpenFileMode p_mode);

    HeadingSequenceType getHeadingSequenceType() const;
    void setHeadingSequenceType(HeadingSequenceType p_type);

    int getHeadingSequenceBaseLevel() const;
    void setHeadingSequenceBaseLevel(int p_level);

    int getColorColumn() const;
    void setColorColumn(int p_column);

    const QString &getEditorColorColumnBg() const;
    const QString &getEditorColorColumnFg() const;

    const QString &getEditorPreviewImageLineFg() const;

    bool getEnableCodeBlockLineNumber() const;
    void setEnableCodeBlockLineNumber(bool p_enabled);

    int getToolBarIconSize() const;

    MarkdownitOption getMarkdownitOption() const;
    void setMarkdownitOption(const MarkdownitOption &p_opt);

    const QString &getRecycleBinFolder() const;

    const QString &getRecycleBinFolderExt() const;

    bool getConfirmImagesCleanUp() const;
    void setConfirmImagesCleanUp(bool p_enabled);

    bool getConfirmReloadFolder() const;
    void setConfirmReloadFolder(bool p_enabled);

    const QString &getMathjaxJavascript() const;

    bool getDoubleClickCloseTab() const;

    // Whether user specify template_code_block_css_url directly.
    bool getUserSpecifyTemplateCodeBlockCssUrl() const;

    bool getEnableCompactMode() const;
    void setEnableCompactMode(bool p_enabled);

    StartupPageType getStartupPageType() const;
    void setStartupPageType(StartupPageType p_type);

    const QStringList &getStartupPages() const;
    void setStartupPages(const QStringList &p_pages);

    // Read last opened files from [last_opened_files] of session.ini.
    QVector<VFileSessionInfo> getLastOpenedFiles();

    // Write last opened files to [last_opened_files] of session.ini.
    void setLastOpenedFiles(const QVector<VFileSessionInfo> &p_files);

    // Read custom magic words from [magic_words] section.
    QVector<VMagicWord> getCustomMagicWords();

    // Return the configured key sequence of @p_operation.
    // Return empty if there is no corresponding config.
    QString getShortcutKeySequence(const QString &p_operation) const;

    // Return the configured key sequence in Captain mode.
    // Return empty if there is no corresponding config.
    QString getCaptainShortcutKeySequence(const QString &p_operation) const;

    // Get the folder the ini file exists.
    QString getConfigFolder() const;

    // Get the ini config file path.
    QString getConfigFilePath() const;

    // Get the folder c_styleConfigFolder in the config folder.
    QString getStyleConfigFolder() const;

    // Get the folder c_templateConfigFolder in the config folder.
    QString getTemplateConfigFolder() const;

    // Get the folder c_snippetConfigFolder in the config folder.
    QString getSnippetConfigFolder() const;

    QString getSnippetConfigFilePath() const;

    // Read all available css files in c_styleConfigFolder.
    QVector<QString> getCssStyles() const;

    // Read all available templates files in c_templateConfigFolder.
    QVector<QString> getNoteTemplates(DocType p_type = DocType::Unknown) const;

    // Get the folder c_codeBlockStyleConfigFolder in the config folder.
    QString getCodeBlockStyleConfigFolder() const;

    // Read all available css files in c_codeBlockStyleConfigFolder.
    QVector<QString> getCodeBlockCssStyles() const;

    // Read all available mdhl files in c_styleConfigFolder.
    QVector<QString> getEditorStyles() const;

    // Return the timer interval for checking file.
    int getFileTimerInterval() const;

    // Get the backup directory.
    const QString &getBackupDirectory() const;

    // Get the backup file extension.
    const QString &getBackupExtension() const;

    // Whether backup file is enabled.
    bool getEnableBackupFile() const;

    // Get defined external editors.
    QVector<QPair<QString, QString>> getExternalEditors() const;

    const QString &getVimExemptionKeys() const;

    const QString &getFlashPage() const;

private:
    // Look up a config from user and default settings.
    QVariant getConfigFromSettings(const QString &section, const QString &key) const;

    // Set a config to user settings.
    void setConfigToSettings(const QString &section, const QString &key, const QVariant &value);

    // Get default config from vnote.ini.
    QVariant getDefaultConfig(const QString &p_section, const QString &p_key) const;

    // Reset user config to default config and return the default config value.
    QVariant resetDefaultConfig(const QString &p_section, const QString &p_key);

    // Look up a config from session settings.
    QVariant getConfigFromSessionSettings(const QString &p_section, const QString &p_key) const;

    // Set a config to session settings.
    void setConfigToSessionSettings(const QString &p_section,
                                    const QString &p_key,
                                    const QVariant &p_value);

    // Init defaultSettings, userSettings, and m_sessionSettings.
    void initSettings();

    // Init from m_sessionSettings.
    void initFromSessionSettings();

    // Read [notebooks] section from @p_settings.
    void readNotebookFromSettings(QSettings *p_settings,
                                  QVector<VNotebook *> &p_notebooks,
                                  QObject *parent);

    // Write to [notebooks] section to @p_settings.
    void writeNotebookToSettings(QSettings *p_settings,
                                 const QVector<VNotebook *> &p_notebooks);

    void readPredefinedColorsFromSettings();

    // 1. Update styles common in HTML and Markdown;
    // 2. Update styles for Markdown.
    void updateEditStyle();

    void updateMarkdownEditStyle();

    // Output pre-defined CSS styles to style folder.
    bool outputDefaultCssStyle() const;

    bool outputDefaultCodeBlockCssStyle() const;

    bool outputDefaultEditorStyle() const;

    // See if the old c_obsoleteDirConfigFile exists. If so, rename it to
    // the new one; if not, use the c_dirConfigFile.
    static QString fetchDirConfigFilePath(const QString &p_path);

    // Read the [shortcuts] section in settings to init m_shortcuts.
    // Will remove invalid config items.
    // First read the config in default settings;
    // Second read the config in user settings and overwrite the default ones;
    // If there is any config in deafult settings that is absent in user settings,
    // write the combined configs to user settings.
    void readShortcutsFromSettings();

    // Read the [captain_mode_shortcuts] section in the settings to init
    // m_captainShortcuts.
    void readCaptainShortcutsFromSettings();

    QHash<QString, QString> readShortcutsFromSettings(QSettings *p_settings,
                                                      const QString &p_group);

    void writeShortcutsToSettings(QSettings *p_settings,
                                  const QString &p_group,
                                  const QHash<QString, QString> &p_shortcuts);

    // Whether @p_seq is a valid key sequence for shortcuts.
    bool isValidKeySequence(const QString &p_seq);

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

    // CSS style for Markdown template.
    QString m_templateCss;

    // Code block CSS style for Markdown template.
    QString m_templateCodeBlockCss;

    // Code block CSS style file URL for Markdown template.
    // If not empty, VNote will ignore m_templateCodeBlockCss.
    QString m_templateCodeBlockCssUrl;

    QString m_editorStyle;

    // Index of current notebook.
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
    QByteArray m_naviSplitterState;

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

    // Background color of selected word in editor.
    QString m_editorSelectedWordBg;

    // Background color of searched word in editor.
    QString m_editorSearchedWordBg;

    // Background color of searched word under cursor in editor.
    QString m_editorSearchedWordCursorBg;

    // Background color of incremental searched word in editor.
    QString m_editorIncrementalSearchedWordBg;

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

    // Global default folder name to store images of all external files.
    // Each file can specify its custom folder.
    QString m_imageFolderExt;

    // Global default folder name to store attachments of all the notes.
    // Each notebook can specify its custom folder.
    QString m_attachmentFolder;

    // Enable trailing-space highlight.
    bool m_enableTrailingSpaceHighlight;

    // Enable Vim mode.
    bool m_enableVimMode;

    // Enable smart input method in Vim mode.
    bool m_enableSmartImInVimMode;

    // Editor line number mode.
    int m_editorLineNumber;

    // The background color of the line number area.
    QString m_editorLineNumberBg;

    // The foreground color of the line number area.
    QString m_editorLineNumberFg;

    // Shortcuts config.
    // Operation -> KeySequence.
    QHash<QString, QString> m_shortcuts;

    // Shortcuts config in Captain mode.
    // Operation -> KeySequence.
    QHash<QString, QString> m_captainShortcuts;

    // Whether minimize to system tray icon when closing the app.
    // -1: uninitialized;
    // 0: do not minimize to the tay;
    // 1: minimize to the tray.
    int m_minimizeToSystemTray;

    // Suffixes of different doc type.
    // [DocType] -> { Suffixes }.
    QHash<int, QList<QString>> m_docSuffixes;

    // Interval for HGMarkdownHighlighter highlight timer (milliseconds).
    int m_markdownHighlightInterval;

    // Line distance height in pixel.
    int m_lineDistanceHeight;

    // Whether insert the note name as a title when creating a new note.
    bool m_insertTitleFromNoteName;

    // Default mode when opening a note.
    OpenFileMode m_noteOpenMode;

    // Whether auto genearte heading sequence.
    HeadingSequenceType m_headingSequenceType;

    // Heading sequence base level.
    int m_headingSequenceBaseLevel;

    // The column to style in code block.
    int m_colorColumn;

    // Whether display line number of code block in read mode.
    bool m_enableCodeBlockLineNumber;

    // The background color of the color column.
    QString m_editorColorColumnBg;

    // The foreground color of the color column.
    QString m_editorColorColumnFg;

    // The foreground color of the preview image line.
    QString m_editorPreviewImageLineFg;

    // Icon size of tool bar in pixels.
    int m_toolBarIconSize;

    // Eanble HTML tags in source.
    bool m_markdownitOptHtml;

    // Convert '\n' in paragraphs into <br>.
    bool m_markdownitOptBreaks;

    // Auto-convert URL-like text to links.
    bool m_markdownitOptLinkify;

    // Default name of the recycle bin folder of notebook.
    QString m_recycleBinFolder;

    // Default name of the recycle bin folder of external files.
    QString m_recycleBinFolderExt;

    // Confirm before deleting unused images.
    bool m_confirmImagesCleanUp;

    // Confirm before reloading folder from disk.
    bool m_confirmReloadFolder;

    // Location and configuration for Mathjax.
    QString m_mathjaxJavascript;

    // Whether double click on a tab to close it.
    bool m_doubleClickCloseTab;

    // Whether put folder and note panel in one single column.
    bool m_enableCompactMode;

    // Type of the pages to open on startup.
    StartupPageType m_startupPageType;

    // File paths to open on startup.
    QStringList m_startupPages;

    // Timer interval to check file in milliseconds.
    int m_fileTimerInterval;

    // Directory for the backup file (relative or absolute path).
    QString m_backupDirectory;

    // Extension of the backup file.
    QString m_backupExtension;

    // Whether enable backup file.
    bool m_enableBackupFile;

    // Skipped keys in Vim mode.
    // c: Ctrl+C
    // v: Ctrl+V
    QString m_vimExemptionKeys;

    // Absolute path of flash page.
    QString m_flashPage;

    // The name of the config file in each directory, obsolete.
    // Use c_dirConfigFile instead.
    static const QString c_obsoleteDirConfigFile;

    // The name of the config file in each directory.
    static const QString c_dirConfigFile;

    // The path of the default configuration file
    static const QString c_defaultConfigFilePath;

    // The name of the config file.
    static const QString c_defaultConfigFile;

    // The name of the config file for session information.
    static const QString c_sessionConfigFile;

    // The name of the config file for snippets folder.
    static const QString c_snippetConfigFile;

    // QSettings for the user configuration
    QSettings *userSettings;

    // Qsettings for @c_defaultConfigFilePath.
    QSettings *defaultSettings;

    // QSettings for the session configuration, such as notebooks,
    // geometry, last opened files.
    QSettings *m_sessionSettings;

    // The folder name of style files.
    static const QString c_styleConfigFolder;

    // The folder name of code block style files.
    static const QString c_codeBlockStyleConfigFolder;

    // The folder name of template files.
    static const QString c_templateConfigFolder;

    // The folder name of snippet files.
    static const QString c_snippetConfigFolder;

    // Default CSS file in resource system.
    static const QString c_defaultCssFile;

    // Default code block CSS file in resource system.
    static const QString c_defaultCodeBlockCssFile;

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
    setConfigToSessionSettings("global", "current_notebook", index);
}

inline void VConfigManager::getNotebooks(QVector<VNotebook *> &p_notebooks,
                                         QObject *p_parent)
{
    // We used to store it in vnote.ini. For now, we store it in session.ini.
    readNotebookFromSettings(m_sessionSettings, p_notebooks, p_parent);

    // Migration.
    if (p_notebooks.isEmpty()) {
        readNotebookFromSettings(userSettings, p_notebooks, p_parent);

        if (!p_notebooks.isEmpty()) {
            // Clear and save it in another place.
            userSettings->beginGroup("notebooks");
            userSettings->remove("");
            userSettings->endGroup();

            writeNotebookToSettings(m_sessionSettings, p_notebooks);
        }
    }
}

inline void VConfigManager::setNotebooks(const QVector<VNotebook *> &p_notebooks)
{
    writeNotebookToSettings(m_sessionSettings, p_notebooks);
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
    setConfigToSettings("global", "tools_dock_checked",
                        m_toolsDockChecked);
}

inline const QByteArray& VConfigManager::getMainWindowGeometry() const
{
    return m_mainWindowGeometry;
}

inline void VConfigManager::setMainWindowGeometry(const QByteArray &p_geometry)
{
    m_mainWindowGeometry = p_geometry;
    setConfigToSessionSettings("geometry",
                               "main_window_geometry",
                               m_mainWindowGeometry);
}

inline const QByteArray& VConfigManager::getMainWindowState() const
{
    return m_mainWindowState;
}

inline void VConfigManager::setMainWindowState(const QByteArray &p_state)
{
    m_mainWindowState = p_state;
    setConfigToSessionSettings("geometry",
                               "main_window_state",
                               m_mainWindowState);
}

inline const QByteArray& VConfigManager::getMainSplitterState() const
{
    return m_mainSplitterState;
}

inline void VConfigManager::setMainSplitterState(const QByteArray &p_state)
{
    m_mainSplitterState = p_state;
    setConfigToSessionSettings("geometry",
                               "main_splitter_state",
                               m_mainSplitterState);
}

inline const QByteArray& VConfigManager::getNaviSplitterState() const
{
    return m_naviSplitterState;
}

inline void VConfigManager::setNaviSplitterState(const QByteArray &p_state)
{
    m_naviSplitterState = p_state;
    setConfigToSessionSettings("geometry",
                               "navi_splitter_state",
                               m_naviSplitterState);
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

inline const QString &VConfigManager::getEditorTrailingSpaceBg() const
{
    return m_editorTrailingSpaceBg;
}

inline const QString &VConfigManager::getEditorSelectedWordBg() const
{
    return m_editorSelectedWordBg;
}

inline const QString &VConfigManager::getEditorSearchedWordBg() const
{
    return m_editorSearchedWordBg;
}

inline const QString &VConfigManager::getEditorSearchedWordCursorBg() const
{
    return m_editorSearchedWordCursorBg;
}

inline const QString &VConfigManager::getEditorIncrementalSearchedWordBg() const
{
    return m_editorIncrementalSearchedWordBg;
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

inline const QString &VConfigManager::getImageFolderExt() const
{
    return m_imageFolderExt;
}

inline void VConfigManager::setImageFolderExt(const QString &p_folder)
{
    if (p_folder.isEmpty()) {
        // Reset the default folder.
        m_imageFolderExt = resetDefaultConfig("global", "external_image_folder").toString();
        return;
    }

    if (m_imageFolderExt == p_folder) {
        return;
    }

    m_imageFolderExt = p_folder;
    setConfigToSettings("global", "external_image_folder", m_imageFolderExt);
}

inline bool VConfigManager::isCustomImageFolderExt() const
{
    return m_imageFolderExt != getDefaultConfig("global", "external_image_folder").toString();
}

inline const QString &VConfigManager::getAttachmentFolder() const
{
    return m_attachmentFolder;
}

inline void VConfigManager::setAttachmentFolder(const QString &p_folder)
{
    if (p_folder.isEmpty()) {
        // Reset the default folder.
        m_attachmentFolder = resetDefaultConfig("global", "attachment_folder").toString();
        return;
    }

    if (m_attachmentFolder == p_folder) {
        return;
    }

    m_attachmentFolder = p_folder;
    setConfigToSettings("global", "attachment_folder", m_attachmentFolder);
}

inline bool VConfigManager::isCustomAttachmentFolder() const
{
    return m_attachmentFolder != getDefaultConfig("global", "attachment_folder").toString();
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

inline int VConfigManager::getEditorLineNumber() const
{
    return m_editorLineNumber;
}

inline void VConfigManager::setEditorLineNumber(int p_mode)
{
    if (m_editorLineNumber == p_mode) {
        return;
    }

    m_editorLineNumber = p_mode;
    setConfigToSettings("global", "editor_line_number",
                        m_editorLineNumber);
}

inline const QString &VConfigManager::getEditorLineNumberBg() const
{
    return m_editorLineNumberBg;
}

inline const QString &VConfigManager::getEditorLineNumberFg() const
{
    return m_editorLineNumberFg;
}

inline int VConfigManager::getMinimizeToStystemTray() const
{
    return m_minimizeToSystemTray;
}

inline void VConfigManager::setMinimizeToSystemTray(int p_val)
{
    if (m_minimizeToSystemTray == p_val) {
        return;
    }

    m_minimizeToSystemTray = p_val;
    setConfigToSettings("global", "minimize_to_system_tray",
                        m_minimizeToSystemTray);
}

inline const QHash<int, QList<QString>> &VConfigManager::getDocSuffixes() const
{
    return m_docSuffixes;
}

inline int VConfigManager::getMarkdownHighlightInterval() const
{
    return m_markdownHighlightInterval;
}

inline int VConfigManager::getLineDistanceHeight() const
{
    return m_lineDistanceHeight;
}

inline bool VConfigManager::getInsertTitleFromNoteName() const
{
    return m_insertTitleFromNoteName;
}

inline void VConfigManager::setInsertTitleFromNoteName(bool p_enabled)
{
    if (p_enabled == m_insertTitleFromNoteName) {
        return;
    }

    m_insertTitleFromNoteName = p_enabled;
    setConfigToSettings("global", "insert_title_from_note_name",
                        m_insertTitleFromNoteName);
}

inline OpenFileMode VConfigManager::getNoteOpenMode() const
{
    return m_noteOpenMode;
}

inline void VConfigManager::setNoteOpenMode(OpenFileMode p_mode)
{
    if (m_noteOpenMode == p_mode) {
        return;
    }

    m_noteOpenMode = p_mode;
    setConfigToSettings("global", "note_open_mode",
                        m_noteOpenMode == OpenFileMode::Read ? 0 : 1);
}

inline HeadingSequenceType VConfigManager::getHeadingSequenceType() const
{
    return m_headingSequenceType;
}

inline void VConfigManager::setHeadingSequenceType(HeadingSequenceType p_type)
{
    if (m_headingSequenceType == p_type) {
        return;
    }

    m_headingSequenceType = p_type;
    setConfigToSettings("global",
                        "heading_sequence_type",
                        (int)m_headingSequenceType);
}

inline int VConfigManager::getHeadingSequenceBaseLevel() const
{
    return m_headingSequenceBaseLevel;
}

inline void VConfigManager::setHeadingSequenceBaseLevel(int p_level)
{
    if (m_headingSequenceBaseLevel == p_level) {
        return;
    }

    m_headingSequenceBaseLevel = p_level;
    setConfigToSettings("global",
                        "heading_sequence_base_level",
                        m_headingSequenceBaseLevel);
}

inline int VConfigManager::getColorColumn() const
{
    return m_colorColumn;
}

inline void VConfigManager::setColorColumn(int p_column)
{
    if (m_colorColumn == p_column) {
        return;
    }

    m_colorColumn = p_column;
    setConfigToSettings("global", "color_column", m_colorColumn);
}

inline const QString &VConfigManager::getEditorColorColumnBg() const
{
    return m_editorColorColumnBg;
}

inline const QString &VConfigManager::getEditorColorColumnFg() const
{
    return m_editorColorColumnFg;
}

inline const QString &VConfigManager::getEditorPreviewImageLineFg() const
{
    return m_editorPreviewImageLineFg;
}

inline bool VConfigManager::getEnableCodeBlockLineNumber() const
{
    return m_enableCodeBlockLineNumber;
}

inline void VConfigManager::setEnableCodeBlockLineNumber(bool p_enabled)
{
    if (m_enableCodeBlockLineNumber == p_enabled) {
        return;
    }

    m_enableCodeBlockLineNumber = p_enabled;
    setConfigToSettings("global",
                        "enable_code_block_line_number",
                        m_enableCodeBlockLineNumber);
}

inline int VConfigManager::getToolBarIconSize() const
{
    return m_toolBarIconSize;
}

inline MarkdownitOption VConfigManager::getMarkdownitOption() const
{
    return MarkdownitOption(m_markdownitOptHtml,
                            m_markdownitOptBreaks,
                            m_markdownitOptLinkify);
}

inline void VConfigManager::setMarkdownitOption(const MarkdownitOption &p_opt)
{
    if (m_markdownitOptHtml != p_opt.m_html) {
        m_markdownitOptHtml = p_opt.m_html;
        setConfigToSettings("global",
                            "markdownit_opt_html",
                            m_markdownitOptHtml);
    }

    if (m_markdownitOptBreaks != p_opt.m_breaks) {
        m_markdownitOptBreaks = p_opt.m_breaks;
        setConfigToSettings("global",
                            "markdownit_opt_breaks",
                            m_markdownitOptBreaks);
    }

    if (m_markdownitOptLinkify != p_opt.m_linkify) {
        m_markdownitOptLinkify = p_opt.m_linkify;
        setConfigToSettings("global",
                            "markdownit_opt_linkify",
                            m_markdownitOptLinkify);
    }
}

inline const QString &VConfigManager::getRecycleBinFolder() const
{
    return m_recycleBinFolder;
}

inline const QString &VConfigManager::getRecycleBinFolderExt() const
{
    return m_recycleBinFolderExt;
}

inline bool VConfigManager::getConfirmImagesCleanUp() const
{
    return m_confirmImagesCleanUp;
}

inline void VConfigManager::setConfirmImagesCleanUp(bool p_enabled)
{
    if (m_confirmImagesCleanUp == p_enabled) {
        return;
    }

    m_confirmImagesCleanUp = p_enabled;
    setConfigToSettings("global",
                        "confirm_images_clean_up",
                        m_confirmImagesCleanUp);
}

inline bool VConfigManager::getConfirmReloadFolder() const
{
    return m_confirmReloadFolder;
}

inline void VConfigManager::setConfirmReloadFolder(bool p_enabled)
{
    if (m_confirmReloadFolder == p_enabled) {
        return;
    }

    m_confirmReloadFolder = p_enabled;
    setConfigToSettings("global",
                        "confirm_reload_folder",
                        m_confirmReloadFolder);
}

inline const QString &VConfigManager::getMathjaxJavascript() const
{
    return m_mathjaxJavascript;
}

inline bool VConfigManager::getDoubleClickCloseTab() const
{
    return m_doubleClickCloseTab;
}

inline const QString &VConfigManager::getTemplateCss() const
{
    return m_templateCss;
}

inline void VConfigManager::setTemplateCss(const QString &p_css)
{
    if (m_templateCss == p_css) {
        return;
    }

    m_templateCss = p_css;
    setConfigToSettings("global", "template_css", m_templateCss);
}

inline const QString &VConfigManager::getTemplateCodeBlockCss() const
{
    return m_templateCodeBlockCss;
}

inline void VConfigManager::setTemplateCodeBlockCss(const QString &p_css)
{
    if (m_templateCodeBlockCss == p_css) {
        return;
    }

    m_templateCodeBlockCss = p_css;
    setConfigToSettings("global", "template_code_block_css", m_templateCodeBlockCss);
}

inline bool VConfigManager::getUserSpecifyTemplateCodeBlockCssUrl() const
{
    return !m_templateCodeBlockCssUrl.isEmpty();
}

inline bool VConfigManager::getEnableCompactMode() const
{
    return m_enableCompactMode;
}

inline void VConfigManager::setEnableCompactMode(bool p_enabled)
{
    if (m_enableCompactMode == p_enabled) {
        return;
    }

    m_enableCompactMode = p_enabled;
    setConfigToSettings("global", "enable_compact_mode", m_enableCompactMode);
}

inline StartupPageType VConfigManager::getStartupPageType() const
{
    return m_startupPageType;
}

inline void VConfigManager::setStartupPageType(StartupPageType p_type)
{
    if (m_startupPageType == p_type) {
        return;
    }

    m_startupPageType = p_type;
    setConfigToSettings("global", "startup_page_type", (int)m_startupPageType);
}

inline const QStringList &VConfigManager::getStartupPages() const
{
    return m_startupPages;
}

inline void VConfigManager::setStartupPages(const QStringList &p_pages)
{
    if (m_startupPages == p_pages) {
        return;
    }

    m_startupPages = p_pages;
    setConfigToSettings("global", "startup_pages", m_startupPages);
}

inline int VConfigManager::getFileTimerInterval() const
{
    return m_fileTimerInterval;
}

inline const QString &VConfigManager::getBackupDirectory() const
{
    return m_backupDirectory;
}

inline const QString &VConfigManager::getBackupExtension() const
{
    return m_backupExtension;
}

inline bool VConfigManager::getEnableBackupFile() const
{
    return m_enableBackupFile;
}

inline const QString &VConfigManager::getVimExemptionKeys() const
{
    return m_vimExemptionKeys;
}

#endif // VCONFIGMANAGER_H
