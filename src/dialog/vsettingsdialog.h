#ifndef VSETTINGSDIALOG_H
#define VSETTINGSDIALOG_H

#include <QDialog>
#include <QVector>
#include <QString>

class QDialogButtonBox;
class QComboBox;
class QGroupBox;
class QDoubleSpinBox;
class QCheckBox;
class QLineEdit;
class QStackedLayout;
class QListWidget;

class VGeneralTab : public QWidget
{
    Q_OBJECT
public:
    explicit VGeneralTab(QWidget *p_parent = 0);
    bool loadConfiguration();
    bool saveConfiguration();

private:
    bool loadLanguage();
    bool saveLanguage();

    bool loadSystemTray();
    bool saveSystemTray();

    // Language
    QComboBox *m_langCombo;

    // System tray
    QCheckBox *m_systemTray;

    static const QVector<QString> c_availableLangs;
};

class VReadEditTab : public QWidget
{
    Q_OBJECT
public:
    explicit VReadEditTab(QWidget *p_parent = 0);
    bool loadConfiguration();
    bool saveConfiguration();

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

    QGroupBox *m_noteBox;
    QGroupBox *m_externalBox;

    // Image folder.
    QCheckBox *m_customImageFolder;
    QLineEdit *m_imageFolderEdit;

    // Image folder of External File.
    QCheckBox *m_customImageFolderExt;
    QLineEdit *m_imageFolderEditExt;

private slots:
    void customImageFolderChanged(int p_state);
    void customImageFolderExtChanged(int p_state);

private:
    bool loadImageFolder();
    bool saveImageFolder();

    bool loadImageFolderExt();
    bool saveImageFolderExt();
};

class VMarkdownTab : public QWidget
{
    Q_OBJECT
public:
    explicit VMarkdownTab(QWidget *p_parent = 0);
    bool loadConfiguration();
    bool saveConfiguration();

    // Default note open mode for markdown.
    QComboBox *m_openModeCombo;

    // Whether enable heading sequence.
    QCheckBox *m_headingSequence;
    QComboBox *m_headingSequenceCombo;

    // Web zoom factor.
    QCheckBox *m_customWebZoom;
    QDoubleSpinBox *m_webZoomFactorSpin;

    // Color column in code block.
    QLineEdit *m_colorColumnEdit;

private:
    bool loadOpenMode();
    bool saveOpenMode();

    bool loadHeadingSequence();
    bool saveHeadingSequence();

    bool loadWebZoomFactor();
    bool saveWebZoomFactor();

    bool loadColorColumn();
    bool saveColorColumn();
};

class VSettingsDialog : public QDialog
{
    Q_OBJECT
public:
    explicit VSettingsDialog(QWidget *p_parent = 0);

private slots:
    void saveConfiguration();

private:
    void loadConfiguration();

    void addTab(QWidget *p_widget, const QString &p_label);

    QStackedLayout *m_tabs;
    QListWidget *m_tabList;
    QDialogButtonBox *m_btnBox;
};

#endif // VSETTINGSDIALOG_H
