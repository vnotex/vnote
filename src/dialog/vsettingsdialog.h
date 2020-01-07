#ifndef VSETTINGSDIALOG_H
#define VSETTINGSDIALOG_H

#include <QDialog>
#include <QVector>
#include <QString>
#include <QTabWidget>

class QDialogButtonBox;
class QComboBox;
class QGroupBox;
class QDoubleSpinBox;
class QSpinBox;
class QCheckBox;
class VLineEdit;
class QStackedLayout;
class QListWidget;
class QPlainTextEdit;
class QVBoxLayout;
class QFontComboBox;

class VSettingsDialog;

class VGeneralTab : public QWidget
{
    Q_OBJECT
public:
    explicit VGeneralTab(QWidget *p_parent = 0);
    bool loadConfiguration();
    bool saveConfiguration();

private:
    QLayout *setupStartupPagesLayout();

    bool loadLanguage();
    bool saveLanguage();

    bool loadSystemTray();
    bool saveSystemTray();

    bool loadStartupPageType();
    bool saveStartupPageType();

    bool loadQuickAccess();
    bool saveQuickAccess();

    bool loadKeyboardLayoutMapping();
    bool saveKeyboardLayoutMapping();

    bool loadOpenGL();
    bool saveOpenGL();

    // Language
    QComboBox *m_langCombo;

    // System tray
    QCheckBox *m_systemTray;

    // Startup page type.
    QComboBox *m_startupPageTypeCombo;

    // Startup pages.
    QPlainTextEdit *m_startupPagesEdit;

    // Startup pages add files button.
    QPushButton *m_startupPagesAddBtn;

    // Quick access note path.
    VLineEdit *m_quickAccessEdit;

    // Keyboard layout mappings.
    QComboBox *m_keyboardLayoutCombo;

#if defined(Q_OS_WIN)
    // Windows OpenGL.
    QComboBox *m_openGLCombo;
#endif

    static const QVector<QString> c_availableLangs;
};

class VLookTab: public QWidget
{
    Q_OBJECT
public:
    explicit VLookTab(QWidget *p_parent = 0);
    bool loadConfiguration();
    bool saveConfiguration();

private:
    bool loadToolBarIconSize();
    bool saveToolBarIconSize();

    // Tool bar icon size.
    QSpinBox *m_tbIconSizeSpin;
};

class VReadEditTab : public QWidget
{
    Q_OBJECT
public:
    explicit VReadEditTab(VSettingsDialog *p_dlg, QWidget *p_parent = 0);

    bool loadConfiguration();
    bool saveConfiguration();

private:
    bool loadWebZoomFactor();
    bool saveWebZoomFactor();

    bool loadSwapFile();
    bool saveSwapFile();

    bool loadAutoSave();
    bool saveAutoSave();

    void showTipsAboutAutoSave();

    bool loadKeyMode();
    bool saveKeyMode();

    bool loadFlashAnchor();
    bool saveFlashAnchor();

    bool loadEditorZoomDelta();
    bool saveEditorZoomDelta();

    bool loadEditorFontFamily();
    bool saveEditorFontFamily();

    VSettingsDialog *m_settingsDlg;

    // Web zoom factor.
    QCheckBox *m_customWebZoom;
    QDoubleSpinBox *m_webZoomFactorSpin;

    // Web flash anchor.
    QCheckBox *m_flashAnchor;

    // Swap file.
    QCheckBox *m_swapFile;

    // Auto save.
    QCheckBox *m_autoSave;

    // Key mode.
    QComboBox *m_keyModeCB;

    // Smart IM in Vim mode.
    QCheckBox *m_smartIM;

    // Editor zoom delta.
    QSpinBox *m_editorZoomDeltaSpin;

    // Editor font family.
    QCheckBox *m_customEditorFont;
    QFontComboBox *m_editorFontFamilyCB;

    QGroupBox *m_readBox;
    QGroupBox *m_editBox;
};

class VNoteManagementTab : public QWidget
{
    Q_OBJECT
public:
    explicit VNoteManagementTab(QWidget *p_parent = 0);
    bool loadConfiguration();
    bool saveConfiguration();

private slots:
    void customImageFolderChanged(int p_state);
    void customImageFolderExtChanged(int p_state);
    void customAttachmentFolderChanged(int p_state);

private:
    bool loadImageFolder();
    bool saveImageFolder();

    bool loadImageFolderExt();
    bool saveImageFolderExt();

    bool loadAttachmentFolder();
    bool saveAttachmentFolder();

    bool loadSingleClickOpen();
    bool saveSingleClickOpen();

    QGroupBox *m_noteBox;
    QGroupBox *m_externalBox;

    // Image folder.
    QCheckBox *m_customImageFolder;
    VLineEdit *m_imageFolderEdit;

    // Image folder of External File.
    QCheckBox *m_customImageFolderExt;
    VLineEdit *m_imageFolderEditExt;

    // Attachment folder.
    QCheckBox *m_customAttachmentFolder;
    VLineEdit *m_attachmentFolderEdit;

    // Single click to open note in current tab.
    QCheckBox *m_singleClickOpen;
};

class VMarkdownTab : public QWidget
{
    Q_OBJECT
public:
    explicit VMarkdownTab(QWidget *p_parent = 0);
    bool loadConfiguration();
    bool saveConfiguration();

private:
    bool loadOpenMode();
    bool saveOpenMode();

    bool loadHeadingSequence();
    bool saveHeadingSequence();

    bool loadColorColumn();
    bool saveColorColumn();

    bool loadCodeBlockCopyButton();
    bool saveCodeBlockCopyButton();

    bool loadMathJax();
    bool saveMathJax();

    bool loadPlantUML();
    bool savePlantUML();

    bool loadGraphviz();
    bool saveGraphviz();

    // Default note open mode for markdown.
    QComboBox *m_openModeCombo;

    // Whether enable heading sequence.
    QComboBox *m_headingSequenceTypeCombo;
    QComboBox *m_headingSequenceLevelCombo;

    // Color column in code block.
    VLineEdit *m_colorColumnEdit;

    // Copy button in code block.
    QCheckBox *m_codeBlockCopyButtonCB;

    // MathJax.
    VLineEdit *m_mathjaxConfigEdit;

    // PlantUML.
    QComboBox *m_plantUMLModeCombo;
    VLineEdit *m_plantUMLServerEdit;
    VLineEdit *m_plantUMLJarEdit;

    // Graphviz.
    QCheckBox *m_graphvizCB;
    VLineEdit *m_graphvizDotEdit;
};

class VMiscTab : public QWidget
{
    Q_OBJECT
public:
    explicit VMiscTab(QWidget *p_parent = 0);
    bool loadConfiguration();
    bool saveConfiguration();

private:
    bool loadMatchesInPage();
    bool saveMatchesInPage();

    // Highlight matches in page.
    QCheckBox *m_matchesInPageCB;
};

class VImageHostingTab : public QWidget
{
    Q_OBJECT
public:
    explicit VImageHostingTab(QWidget *p_parent = 0);
    bool loadConfiguration();
    bool saveConfiguration();

private:
    bool loadGithubPersonalAccessToken();
    bool saveGithubPersonalAccessToken();

    bool loadGithubReposName();
    bool saveGithubReposName();

    bool loadGithubUserName();
    bool saveGithubUserName();

    bool loadGithubKeepImgScale();
    bool saveGithubKeepImgScale();

    bool loadGithubDoNotReplaceLink();
    bool saveGithubDoNotReplaceLink();

    bool loadWechatAppid();
    bool saveWechatAppid();

    bool loadWechatSecret();
    bool saveWechatSecret();

    bool loadMarkdown2WechatToolUrl();
    bool saveMarkdown2WechatToolUrl();

    bool loadWechatKeepImgScale();
    bool saveWechatKeepImgScale();

    bool loadWechatDoNotReplaceLink();
    bool saveWechatDoNotReplaceLink();

    bool loadTencentAccessDomainName();
    bool saveTencentAccessDomainName();

    bool loadTencentSecretId();
    bool saveTencentSecretId();

    bool loadTencentSecretKey();
    bool saveTencentSecretKey();

    bool loadTencentKeepImgScale();
    bool saveTencentKeepImgScale();

    bool loadTencentDoNotReplaceLink();
    bool saveTencentDoNotReplaceLink();

    bool loadGiteePersonalAccessToken();
    bool saveGiteePersonalAccessToken();

    bool loadGiteeReposName();
    bool saveGiteeReposName();

    bool loadGiteeUserName();
    bool saveGiteeUserName();

    bool loadGiteeKeepImgScale();
    bool saveGiteeKeepImgScale();

    bool loadGiteeDoNotReplaceLink();
    bool saveGiteeDoNotReplaceLink();

    // Github configuration edit.
    VLineEdit *m_githubPersonalAccessTokenEdit;
    VLineEdit *m_githubRepoNameEdit;
    VLineEdit *m_githubUserNameEdit;
    QCheckBox *m_githubKeepImgScaleCB;
    QCheckBox *m_githubDoNotReplaceLinkCB;

    // Gitee configuration edit.
    VLineEdit *m_giteePersonalAccessTokenEdit;
    VLineEdit *m_giteeRepoNameEdit;
    VLineEdit *m_giteeUserNameEdit;
    QCheckBox *m_giteeKeepImgScaleCB;
    QCheckBox *m_giteeDoNotReplaceLinkCB;

    // Wechat configuration edit.
    VLineEdit *m_wechatAppidEdit;
    VLineEdit *m_wechatSecretEdit;
    VLineEdit *m_markdown2WechatToolUrlEdit;
    QCheckBox *m_wechatKeepImgScaleCB;
    QCheckBox *m_wechatDoNotReplaceLinkCB;

    // Tencent configuration edit.
    VLineEdit *m_tencentAccessDomainNameEdit;
    VLineEdit *m_tencentSecretIdEdit;
    VLineEdit *m_tencentSecretKeyEdit;
    QCheckBox *m_tencentKeepImgScaleCB;
    QCheckBox *m_tencentDoNotReplaceLinkCB;
};

class VSettingsDialog : public QDialog
{
    Q_OBJECT
public:
    explicit VSettingsDialog(QWidget *p_parent = 0);

    void setNeedUpdateEditorFont(bool p_need);
    bool getNeedUpdateEditorFont() const;

private slots:
    void saveConfiguration();

    void resetVNote();

    void resetLayout();

private:
    void loadConfiguration();

    void addTab(QWidget *p_widget, const QString &p_label);

    QStackedLayout *m_tabs;
    QListWidget *m_tabList;
    QDialogButtonBox *m_btnBox;

    // Reset all the configuration of VNote.
    QPushButton *m_resetVNoteBtn;

    // Reset the layout.
    QPushButton *m_resetLayoutBtn;

    bool m_needUpdateEditorFont;
};

inline void VSettingsDialog::setNeedUpdateEditorFont(bool p_need)
{
    m_needUpdateEditorFont = p_need;
}

inline bool VSettingsDialog::getNeedUpdateEditorFont() const
{
    return m_needUpdateEditorFont;
}
#endif // VSETTINGSDIALOG_H
