#ifndef SELECTDIALOG_H
#define SELECTDIALOG_H

#include <QDialog>
#include <QMap>

class QPushButton;
class QMouseEvent;
class QListWidget;
class QListWidgetItem;
class QShowEvent;
class QKeyEvent;

namespace vnotex
{
    class SelectDialog : public QDialog
    {
        Q_OBJECT
    public:
        SelectDialog(const QString &p_title, QWidget *p_parent = nullptr);

        // @p_selectID should >= 0.
        void addSelection(const QString &p_selectStr, int p_selectID);

        int getSelection() const;

    protected:
        void showEvent(QShowEvent *p_event) Q_DECL_OVERRIDE;

        void keyPressEvent(QKeyEvent *p_event) Q_DECL_OVERRIDE;

    private slots:
        void selectionChosen(QListWidgetItem *p_item);

    private:
        enum { CANCEL_ID = -1 };

        void setupUI(const QString &p_title);

        void updateSize();

        int m_choice = CANCEL_ID;

        QListWidget *m_list = nullptr;
    };
} // ns vnotex

#endif // SELECTDIALOG_H
