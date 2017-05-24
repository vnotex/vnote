#ifndef VNOTEBOOKINFODIALOG_H
#define VNOTEBOOKINFODIALOG_H

#include <QDialog>

class QLabel;
class QLineEdit;
class QDialogButtonBox;
class QString;
class VNotebook;

class VNotebookInfoDialog : public QDialog
{
    Q_OBJECT
public:
    VNotebookInfoDialog(const QString &p_title, const QString &p_info,
                        const VNotebook *p_notebook, QWidget *p_parent = 0);

    QString getName() const;
    QString getImageFolder() const;

private slots:
    void enableOkButton();

protected:
    void showEvent(QShowEvent *p_event) Q_DECL_OVERRIDE;

private:
    void setupUI(const QString &p_title, const QString &p_info);

    const VNotebook *m_notebook;

    QLabel *m_infoLabel;
    QLineEdit *m_nameEdit;
    QLineEdit *m_pathEdit;
    QLineEdit *m_imageFolderEdit;
    QDialogButtonBox *m_btnBox;
};

#endif // VNOTEBOOKINFODIALOG_H
