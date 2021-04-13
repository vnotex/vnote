#ifndef SEARCHINFOPROVIDER_H
#define SEARCHINFOPROVIDER_H

#include "searchpanel.h"

namespace vnotex
{
    class ViewArea;
    class NotebookExplorer;
    class NotebookMgr;

    class SearchInfoProvider : public ISearchInfoProvider
    {
    public:
        SearchInfoProvider(const ViewArea *p_viewArea,
                           const NotebookExplorer *p_notebookExplorer,
                           const NotebookMgr *p_notebookMgr);

        QList<Buffer *> getBuffers() const Q_DECL_OVERRIDE;

        Node *getCurrentFolder() const Q_DECL_OVERRIDE;

        Notebook *getCurrentNotebook() const Q_DECL_OVERRIDE;

        QVector<Notebook *> getNotebooks() const Q_DECL_OVERRIDE;

    private:
        const ViewArea *m_viewArea = nullptr;

        const NotebookExplorer *m_notebookExplorer = nullptr;

        const NotebookMgr *m_notebookMgr = nullptr;
    };
}

#endif // SEARCHINFOPROVIDER_H
