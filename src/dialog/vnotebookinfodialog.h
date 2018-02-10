#ifndef VNOTEBOOKINFODIALOG_H
#define VNOTEBOOKINFODIALOG_H

#include <QDialog>
#include <QVector>

class QLabel;
class VLineEdit;
class VMetaWordLineEdit;
class QDialogButtonBox;
class VNotebook;

class VNotebookInfoDialog : public QDialog
{
    Q_OBJECT
public:
    VNotebookInfoDialog(const QString &p_title,
                        const QString &p_info,
                        const VNotebook *p_notebook,
                        const QVector<VNotebook *> &p_notebooks,
                        QWidget *p_parent = 0);

    QString getName() const;

    // Get the custom image folder for this notebook.
    // Empty string indicates using global config.
    QString getImageFolder() const;

private slots:
    // Handle the change of the name and path input.
    void handleInputChanged();

protected:
    void showEvent(QShowEvent *p_event) Q_DECL_OVERRIDE;

private:
    void setupUI(const QString &p_title, const QString &p_info);

    const VNotebook *m_notebook;

    VMetaWordLineEdit *m_nameEdit;
    VLineEdit *m_pathEdit;
    VLineEdit *m_imageFolderEdit;
    // Read-only.
    VLineEdit *m_attachmentFolderEdit;
    QLabel *m_warnLabel;
    QDialogButtonBox *m_btnBox;
    const QVector<VNotebook *> &m_notebooks;
};

#endif // VNOTEBOOKINFODIALOG_H
