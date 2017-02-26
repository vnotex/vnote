#ifndef VDIRINFODIALOG_H
#define VDIRINFODIALOG_H

#include <QDialog>

class QLabel;
class QLineEdit;
class QDialogButtonBox;
class QString;

class VDirInfoDialog : public QDialog
{
    Q_OBJECT
public:
    VDirInfoDialog(const QString &title, const QString &info, const QString &defaultName,
                   QWidget *parent = 0);
    QString getNameInput() const;

private slots:
    void enableOkButton();

private:
    void setupUI();

    QLabel *infoLabel;
    QLabel *nameLabel;
    QLineEdit *nameEdit;
    QDialogButtonBox *m_btnBox;

    QString title;
    QString info;
    QString defaultName;
};
#endif // VDIRINFODIALOG_H
