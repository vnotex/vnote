#ifndef VNEWNOTEBOOKDIALOG_H
#define VNEWNOTEBOOKDIALOG_H

#include <QDialog>
#include <QVector>

class QLabel;
class VLineEdit;
class VMetaWordLineEdit;
class QPushButton;
class QDialogButtonBox;
class VNotebook;
class QCheckBox;

class VNewNotebookDialog : public QDialog
{
    Q_OBJECT
public:
    VNewNotebookDialog(const QString &title, const QString &info, const QString &defaultName,
                       const QString &defaultPath, const QVector<VNotebook *> &p_notebooks,
                       QWidget *parent = 0);

    QString getNameInput() const;

    QString getPathInput() const;

    // Whether import existing notebook by reading the config file.
    bool isImportExistingNotebook() const;

    // Get the custom image folder for this notebook.
    // Empty string indicates using global config.
    QString getImageFolder() const;

    // Get the custom attachment folder for this notebook.
    // Empty string indicates using global config.
    QString getAttachmentFolder() const;

private slots:
    void handleBrowseBtnClicked();

    // Handle the change of the name and path input.
    void handleInputChanged();

protected:
    void showEvent(QShowEvent *event) Q_DECL_OVERRIDE;

private:
    void setupUI(const QString &p_title, const QString &p_info);

    // Should be called before enableOkButton() when path changed.
    void checkRootFolder(const QString &p_path);

    // Try to figure out name and path.
    // Returns true if name or path is modified.
    bool autoComplete();

    // Whether relative path will be used in config file.
    bool isUseRelativePath() const;

    VMetaWordLineEdit *m_nameEdit;
    VLineEdit *m_pathEdit;
    QPushButton *browseBtn;
    QCheckBox *m_relativePathCB;
    QLabel *m_warnLabel;
    VLineEdit *m_imageFolderEdit;
    VLineEdit *m_attachmentFolderEdit;
    QDialogButtonBox *m_btnBox;

    QString defaultName;
    QString defaultPath;

    // Whether import existing notebook config file.
    bool m_importNotebook;

    // True if user has change the content of the path edit.
    bool m_manualPath;

    // True if user has change the content of the name edit.
    bool m_manualName;

    // All existing notebooks in VNote.
    const QVector<VNotebook *> &m_notebooks;
};

#endif // VNEWNOTEBOOKDIALOG_H
