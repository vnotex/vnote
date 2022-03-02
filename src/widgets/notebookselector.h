#ifndef NOTEBOOKSELECTOR_H
#define NOTEBOOKSELECTOR_H

#include <QModelIndex>

#include "global.h"
#include "navigationmode.h"
#include "combobox.h"

namespace vnotex
{
    class Notebook;

    class NotebookSelector : public ComboBox, public NavigationMode
    {
        Q_OBJECT
    public:
        explicit NotebookSelector(QWidget *p_parent = nullptr);

        void setNotebooks(const QVector<QSharedPointer<Notebook>> &p_notebooks);

        void reloadNotebook(const Notebook *p_notebook);

        void setCurrentNotebook(ID p_id);

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

        bool m_notebooksInitialized = false;

        QVector<QModelIndex> m_navigationIndexes;
    };
} // ns vnotex

#endif // NOTEBOOKSELECTOR_H
