#ifndef VSORTDIALOG_H
#define VSORTDIALOG_H

#include <QDialog>
#include <QVector>

class QPushButton;
class QDialogButtonBox;
class QTreeWidget;

class VSortDialog : public QDialog
{
    Q_OBJECT
public:
    VSortDialog(const QString &p_title,
                const QString &p_info,
                QWidget *p_parent = 0);

    QTreeWidget *getTreeWidget() const;

    // Called after updating the m_treeWidget.
    void treeUpdated();

private:
    enum MoveOperation { Top, Up, Down, Bottom };

private slots:
    void handleMoveOperation(MoveOperation p_op);

private:
    void setupUI(const QString &p_title, const QString &p_info);

    QTreeWidget *m_treeWidget;
    QPushButton *m_topBtn;
    QPushButton *m_upBtn;
    QPushButton *m_downBtn;
    QPushButton *m_bottomBtn;
    QDialogButtonBox *m_btnBox;
};

inline QTreeWidget *VSortDialog::getTreeWidget() const
{
    return m_treeWidget;
}

#endif // VSORTDIALOG_H
