#ifndef VNEWDIRDIALOG_H
#define VNEWDIRDIALOG_H

#include <QDialog>

class QLabel;
class VMetaWordLineEdit;
class QDialogButtonBox;
class QString;
class VDirectory;

class VNewDirDialog : public QDialog
{
    Q_OBJECT
public:
    VNewDirDialog(const QString &title,
                  const QString &info,
                  const QString &defaultName,
                  VDirectory *directory,
                  QWidget *parent = 0);

    QString getNameInput() const;

private slots:
    void handleInputChanged();

private:
    void setupUI();

    VMetaWordLineEdit *m_nameEdit;
    QDialogButtonBox *m_btnBox;

    QLabel *m_warnLabel;

    QString title;
    QString info;
    QString defaultName;

    VDirectory *m_directory;
};

#endif // VNEWDIRDIALOG_H
