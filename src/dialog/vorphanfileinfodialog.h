#ifndef VORPHANFILEINFODIALOG_H
#define VORPHANFILEINFODIALOG_H

#include <QDialog>

class VOrphanFile;
class QDialogButtonBox;
class VLineEdit;

class VOrphanFileInfoDialog : public QDialog
{
    Q_OBJECT

public:
    VOrphanFileInfoDialog(const VOrphanFile *p_file, QWidget *p_parent = 0);

    // Get the custom image folder for this external file.
    // Empty string indicates using global config.
    QString getImageFolder() const;

private slots:
    // Handle the change of the image folder input.
    void handleInputChanged();

private:
    void setupUI();

    const VOrphanFile *m_file;

    QDialogButtonBox *m_btnBox;
    VLineEdit *m_imageFolderEdit;
};

#endif // VORPHANFILEINFODIALOG_H
