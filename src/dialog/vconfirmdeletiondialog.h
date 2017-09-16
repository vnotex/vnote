#ifndef VCONFIRMDELETIONDIALOG_H
#define VCONFIRMDELETIONDIALOG_H

#include <QDialog>
#include <QVector>

class QLabel;
class QPushButton;
class QDialogButtonBox;
class QListWidget;
class QListWidgetItem;
class ConfirmItemWidget;
class QCheckBox;

class VConfirmDeletionDialog : public QDialog
{
    Q_OBJECT
public:
    VConfirmDeletionDialog(const QString &p_title,
                           const QString &p_info,
                           const QVector<QString> &p_files,
                           QWidget *p_parent = 0);

    QVector<QString> getConfirmedFiles() const;

private slots:
    void currentFileChanged(int p_row);

private:
    void setupUI(const QString &p_title, const QString &p_info);

    void initFileItems(const QVector<QString> &p_files);

    ConfirmItemWidget *getItemWidget(QListWidgetItem *p_item) const;

    QListWidget *m_listWidget;
    QLabel *m_previewer;
    QDialogButtonBox *m_btnBox;
    QCheckBox *m_askAgainCB;
};

#endif // VCONFIRMDELETIONDIALOG_H
