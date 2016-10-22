#ifndef VNEWNOTEBOOKDIALOG_H
#define VNEWNOTEBOOKDIALOG_H

#include <QDialog>

class QLabel;
class QLineEdit;
class QPushButton;
class QString;

class VNewNotebookDialog : public QDialog
{
    Q_OBJECT
public:
    VNewNotebookDialog(const QString &title, const QString &info, const QString &defaultName,
                       const QString &defaultPath, QWidget *parent = 0);
    QString getNameInput() const;
    QString getPathInput() const;

private slots:
    void enableOkButton();
    void handleBrowseBtnClicked();

private:
    void setupUI();

    QLabel *infoLabel;
    QLabel *nameLabel;
    QLineEdit *nameEdit;
    QLineEdit *pathEdit;
    QPushButton *browseBtn;
    QPushButton *okBtn;
    QPushButton *cancelBtn;

    QString title;
    QString info;
    QString defaultName;
    QString defaultPath;
};

#endif // VNEWNOTEBOOKDIALOG_H
