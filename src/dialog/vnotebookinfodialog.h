#ifndef VNOTEBOOKINFODIALOG_H
#define VNOTEBOOKINFODIALOG_H

#include <QDialog>
#include <QVector>

class QLabel;
class QLineEdit;
class VLineEdit;
class QDialogButtonBox;
class QString;
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

    VLineEdit *m_nameEdit;
    QLineEdit *m_pathEdit;
    QLineEdit *m_imageFolderEdit;
    // Read-only.
    QLineEdit *m_attachmentFolderEdit;
    QLabel *m_warnLabel;
    QDialogButtonBox *m_btnBox;
    const QVector<VNotebook *> &m_notebooks;
};

#endif // VNOTEBOOKINFODIALOG_H
