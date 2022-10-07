#ifndef NOTEBOOKSELECTOR_H
#define NOTEBOOKSELECTOR_H

#include <QModelIndex>

#include "global.h"
#include "navigationmode.h"
#include "combobox.h"
#include "notebooknodeexplorer.h"

namespace vnotex
{
    class Notebook;

    class NotebookSelector : public ComboBox, public NavigationMode
    {
        Q_OBJECT
    public:
        explicit NotebookSelector(QWidget *p_parent = nullptr);

        void loadNotebooks();

        void reloadNotebook(const Notebook *p_notebook);

        void setCurrentNotebook(ID p_id);

        void setViewOrder(int p_order);

    signals:
        void newNotebookRequested();

    // NavigationMode.
    protected:
        QVector<void *> getVisibleNavigationItems() Q_DECL_OVERRIDE;

        void placeNavigationLabel(int p_idx, void *p_item, QLabel *p_label) Q_DECL_OVERRIDE;

        void handleTargetHit(void *p_item) Q_DECL_OVERRIDE;

        void clearNavigation() Q_DECL_OVERRIDE;

    protected:
        bool eventFilter(QObject *p_obj, QEvent *p_event) Q_DECL_OVERRIDE;

        void mousePressEvent(QMouseEvent *p_event) Q_DECL_OVERRIDE;

    private:
        void addNotebookItem(const QSharedPointer<Notebook> &p_notebook);

        QIcon generateItemIcon(const Notebook *p_notebook);

        QString generateItemToolTip(const Notebook *p_notebook);

        QString getItemToolTip(int p_idx) const;
        void setItemToolTip(int p_idx, const QString &p_tooltip);

        int findNotebook(ID p_id) const;

        void sortNotebooks(QVector<QSharedPointer<Notebook>> &p_notebooks) const;

        static void fetchIconColor(const QString &p_name, QString &p_fg, QString &p_bg);

        bool m_notebooksInitialized = false;

        QVector<QModelIndex> m_navigationIndexes;

        ViewOrder m_viewOrder = ViewOrder::OrderedByConfiguration;
    };
} // ns vnotex

#endif // NOTEBOOKSELECTOR_H
