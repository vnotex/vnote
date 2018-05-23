#ifndef VFIXNOTEBOOKDIALOG_H
#define VFIXNOTEBOOKDIALOG_H

#include <QDialog>

class VNotebook;
class VLineEdit;
class QLabel;
class QPushButton;
class QDialogButtonBox;
class QCheckBox;

class VFixNotebookDialog : public QDialog
{
    Q_OBJECT
public:
    VFixNotebookDialog(const VNotebook *p_notebook,
                       const QVector<VNotebook *> &p_notebooks,
                       QWidget *p_parent = nullptr);

    QString getPathInput() const;

private slots:
    void handleBrowseBtnClicked();

    void handleInputChanged();

private:
    void setupUI();

    // Whether relative path will be used in config file.
    bool isUseRelativePath() const;

    const VNotebook *m_notebook;

    const QVector<VNotebook *> &m_notebooks;

    VLineEdit *m_pathEdit;

    QPushButton *m_browseBtn;

    QCheckBox *m_relativePathCB;

    QLabel *m_warnLabel;

    QDialogButtonBox *m_btnBox;
};

#endif // VFIXNOTEBOOKDIALOG_H
