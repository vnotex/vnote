#ifndef VDELETENOTEBOOKDIALOG_H
#define VDELETENOTEBOOKDIALOG_H

#include <QDialog>
#include <QMessageBox>

class QLabel;
class QLineEdit;
class QString;
class QCheckBox;
class QDialogButtonBox;

class VDeleteNotebookDialog : public QDialog
{
    Q_OBJECT
public:
    VDeleteNotebookDialog(const QString &p_title, const QString &p_name, const QString &p_path,
                          QWidget *p_parent = 0);
    bool getDeleteFiles() const;

private slots:
    void notDeleteCheckChanged(int p_state);

private:
    void setupUI(const QString &p_title, const QString &p_name);
    QPixmap standardIcon(QMessageBox::Icon p_icon);

    QString m_path;
    QLabel *m_warningLabel;
    QCheckBox *m_notDeleteCheck;
    QDialogButtonBox *m_btnBox;
};

#endif // VDELETENOTEBOOKDIALOG_H
