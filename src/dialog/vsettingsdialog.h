#ifndef VSETTINGSDIALOG_H
#define VSETTINGSDIALOG_H

#include <QDialog>
#include <QVector>
#include <QString>

class QDialogButtonBox;
class QTabWidget;
class QComboBox;
class QGroupBox;
class QDoubleSpinBox;
class QCheckBox;
class QLineEdit;

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

    // Web zoom factor.
    QCheckBox *m_customWebZoom;
    QDoubleSpinBox *m_webZoomFactorSpin;

    // Default note open mode for markdown.
    QComboBox *m_openModeCombo;

private slots:
    void customWebZoomChanged(int p_state);

private:
    bool loadWebZoomFactor();
    bool saveWebZoomFactor();

    bool loadOpenMode();
    bool saveOpenMode();
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

class VSettingsDialog : public QDialog
{
    Q_OBJECT
public:
    explicit VSettingsDialog(QWidget *p_parent = 0);

private slots:
    void saveConfiguration();

private:
    void loadConfiguration();

    QTabWidget *m_tabs;
    QDialogButtonBox *m_btnBox;
};

#endif // VSETTINGSDIALOG_H
