#ifndef VSETTINGSDIALOG_H
#define VSETTINGSDIALOG_H

#include <QDialog>
#include <QVector>
#include <QString>

class QDialogButtonBox;
class QTabWidget;
class QComboBox;

class VGeneralTab : public QWidget
{
    Q_OBJECT
public:
    explicit VGeneralTab(QWidget *p_parent = 0);
    bool loadConfiguration();
    bool saveConfiguration();

private slots:
    void handleIndexChange(int p_index);

private:
    bool loadLanguage();
    bool saveLanguage();

    // Language
    QComboBox *m_langCombo;
    // Whether language changes.
    bool m_langChanged;

    static const QVector<QString> c_availableLangs;
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
