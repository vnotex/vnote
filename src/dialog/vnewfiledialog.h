#ifndef VNEWFILEDIALOG_H
#define VNEWFILEDIALOG_H

#include <QDialog>

class QLabel;
class QLineEdit;
class QDialogButtonBox;
class QString;

class VNewFileDialog : public QDialog
{
    Q_OBJECT
public:
    VNewFileDialog(const QString &title, const QString &info, const QString &name,
                   const QString &defaultName, QWidget *parent = 0);
    QString getNameInput() const;

private slots:
    void enableOkButton(const QString &editText);

private:
    void setupUI();

    QLabel *nameLabel;
    QLineEdit *nameEdit;
    QPushButton *okBtn;
    QDialogButtonBox *m_btnBox;

    QString title;
    QString info;
    QString name;
    QString defaultName;
};

#endif // VNEWFILEDIALOG_H
