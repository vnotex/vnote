#ifndef VNEWNOTEBOOKDIALOG_H
#define VNEWNOTEBOOKDIALOG_H

#include <QDialog>

class QLabel;
class QLineEdit;
class QPushButton;
class QString;
class QCheckBox;
class QDialogButtonBox;

class VNewNotebookDialog : public QDialog
{
    Q_OBJECT
public:
    VNewNotebookDialog(const QString &title, const QString &info, const QString &defaultName,
                       const QString &defaultPath, QWidget *parent = 0);
    QString getNameInput() const;
    QString getPathInput() const;
    bool getImportCheck() const;

private slots:
    void enableOkButton();
    void handleBrowseBtnClicked();
    void handlePathChanged(const QString &p_text);

protected:
    void showEvent(QShowEvent *event) Q_DECL_OVERRIDE;

private:
    void setupUI();

    QLabel *infoLabel;
    QLabel *nameLabel;
    QLineEdit *nameEdit;
    QLineEdit *pathEdit;
    QCheckBox *importCheck;
    QPushButton *browseBtn;
    QDialogButtonBox *m_btnBox;
    QLabel *m_warnLabel;

    QString title;
    QString info;
    QString defaultName;
    QString defaultPath;
};

#endif // VNEWNOTEBOOKDIALOG_H
