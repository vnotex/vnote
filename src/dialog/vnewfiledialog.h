#ifndef VNEWFILEDIALOG_H
#define VNEWFILEDIALOG_H

#include <QDialog>

class QLabel;
class VLineEdit;
class QDialogButtonBox;
class QCheckBox;
class VDirectory;

class VNewFileDialog : public QDialog
{
    Q_OBJECT
public:
    VNewFileDialog(const QString &title, const QString &info,
                   const QString &defaultName, VDirectory *directory,
                   QWidget *parent = 0);

    QString getNameInput() const;

    bool getInsertTitleInput() const;

private slots:
    void handleInputChanged();

private:
    void setupUI();

    VLineEdit *m_nameEdit;
    QCheckBox *m_insertTitleCB;

    QPushButton *okBtn;
    QDialogButtonBox *m_btnBox;

    QLabel *m_warnLabel;

    QString title;
    QString info;
    QString defaultName;

    VDirectory *m_directory;
};

#endif // VNEWFILEDIALOG_H
