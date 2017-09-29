#ifndef VSORTDIALOG_H
#define VSORTDIALOG_H

#include <QDialog>
#include <QVector>
#include <QTreeWidget>

class QPushButton;
class QDialogButtonBox;
class QTreeWidget;
class QDropEvent;

// QTreeWidget won't emit the rowsMoved() signal after drag-and-drop.
// VTreeWidget will emit rowsMoved() signal.
class VTreeWidget : public QTreeWidget
{
    Q_OBJECT
public:
    explicit VTreeWidget(QWidget *p_parent = 0)
        : QTreeWidget(p_parent)
    {
    }

protected:
    void dropEvent(QDropEvent *p_event) Q_DECL_OVERRIDE;

signals:
    // Rows [@p_first, @p_last] were moved to @p_row.
    void rowsMoved(int p_first, int p_last, int p_row);

};

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

    // Get user data of column 0 from sorted items.
    QVector<QVariant> getSortedData() const;

private:
    enum MoveOperation { Top, Up, Down, Bottom };

private slots:
    void handleMoveOperation(MoveOperation p_op);

private:
    void setupUI(const QString &p_title, const QString &p_info);

    VTreeWidget *m_treeWidget;
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
