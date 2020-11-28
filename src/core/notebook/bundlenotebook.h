#ifndef BUNDLENOTEBOOK_H
#define BUNDLENOTEBOOK_H

#include "notebook.h"
#include "global.h"

namespace vnotex
{
    class BundleNotebookConfigMgr;

    class BundleNotebook : public Notebook
    {
        Q_OBJECT
    public:
        BundleNotebook(const NotebookParameters &p_paras,
                       QObject *p_parent = nullptr);

        ID getNextNodeId() const Q_DECL_OVERRIDE;

        ID getAndUpdateNextNodeId() Q_DECL_OVERRIDE;

        void updateNotebookConfig() Q_DECL_OVERRIDE;

        void removeNotebookConfig() Q_DECL_OVERRIDE;

        void remove() Q_DECL_OVERRIDE;

    private:
        BundleNotebookConfigMgr *getBundleNotebookConfigMgr() const;

        ID m_nextNodeId = 1;
    };
} // ns vnotex

#endif // BUNDLENOTEBOOK_H
