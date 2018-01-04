#ifndef VDELETENOTEBOOKDIALOG_H
#define VDELETENOTEBOOKDIALOG_H

#include <QDialog>
#include <QMessageBox>

class QLabel;
class QString;
class QCheckBox;
class QDialogButtonBox;
class VNotebook;

class VDeleteNotebookDialog : public QDialog
{
    Q_OBJECT
public:
    VDeleteNotebookDialog(const QString &p_title,
                          const VNotebook *p_notebook,
                          QWidget *p_parent = 0);

    // Whether delete files from disk.
    bool getDeleteFiles() const;

private slots:
    void deleteCheckChanged(int p_state);

private:
    void setupUI(const QString &p_title, const QString &p_name);
    QPixmap standardIcon(QMessageBox::Icon p_icon);

    const VNotebook *m_notebook;
    QLabel *m_warningLabel;
    QCheckBox *m_deleteCheck;
    QDialogButtonBox *m_btnBox;
};

#endif // VDELETENOTEBOOKDIALOG_H
