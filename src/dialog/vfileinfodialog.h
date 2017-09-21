#ifndef VFILEINFODIALOG_H
#define VFILEINFODIALOG_H

#include <QDialog>

class QLabel;
class QLineEdit;
class QDialogButtonBox;
class QString;
class VDirectory;
class VNoteFile;

// File information dialog for internal note file.
class VFileInfoDialog : public QDialog
{
    Q_OBJECT
public:
    VFileInfoDialog(const QString &title,
                    const QString &info,
                    VDirectory *directory,
                    const VNoteFile *file,
                    QWidget *parent = 0);

    QString getNameInput() const;

private slots:
    void handleInputChanged();

private:
    void setupUI(const QString &p_title, const QString &p_info);

    QLineEdit *nameEdit;
    QLabel *m_warnLabel;
    QDialogButtonBox *m_btnBox;

    VDirectory *m_directory;
    const VNoteFile *m_file;
};

#endif // VFILEINFODIALOG_H
