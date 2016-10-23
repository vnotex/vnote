#ifndef VNOTEBOOKINFODIALOG_H
#define VNOTEBOOKINFODIALOG_H

#include <QDialog>

class QLabel;
class QLineEdit;
class QPushButton;
class QString;

class VNotebookInfoDialog : public QDialog
{
    Q_OBJECT
public:
    VNotebookInfoDialog(const QString &title, const QString &info, const QString &defaultName,
                        const QString &defaultPath, QWidget *parent = 0);
    QString getNameInput() const;

private slots:
    void enableOkButton();

private:
    void setupUI();

    QLabel *infoLabel;
    QLabel *nameLabel;
    QLineEdit *nameEdit;
    QLineEdit *pathEdit;
    QPushButton *okBtn;
    QPushButton *cancelBtn;

    QString title;
    QString info;
    QString defaultName;
    QString defaultPath;
};

#endif // VNOTEBOOKINFODIALOG_H
