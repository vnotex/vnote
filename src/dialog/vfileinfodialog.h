#ifndef VFILEINFODIALOG_H
#define VFILEINFODIALOG_H

#include <QDialog>

class QLabel;
class QLineEdit;
class QDialogButtonBox;
class QString;
class VDirectory;
class VFile;

class VFileInfoDialog : public QDialog
{
    Q_OBJECT
public:
    VFileInfoDialog(const QString &title, const QString &info,
                    VDirectory *directory, const VFile *file,
                    QWidget *parent = 0);
    QString getNameInput() const;

private slots:
    void handleInputChanged();

private:
    void setupUI();

    QLabel *infoLabel;
    QLabel *nameLabel;
    QLineEdit *nameEdit;
    QLabel *m_warnLabel;
    QDialogButtonBox *m_btnBox;

    QString title;
    QString info;

    VDirectory *m_directory;
    const VFile *m_file;
};

#endif // VFILEINFODIALOG_H
