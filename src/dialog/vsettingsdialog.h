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

    // Language
    QComboBox *m_langCombo;

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

private slots:
    void customWebZoomChanged(int p_state);

private:
    bool loadWebZoomFactor();
    bool saveWebZoomFactor();
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
