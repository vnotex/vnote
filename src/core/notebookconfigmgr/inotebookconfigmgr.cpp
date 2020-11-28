#include "inotebookconfigmgr.h"

#include <notebookbackend/inotebookbackend.h>

using namespace vnotex;

INotebookConfigMgr::INotebookConfigMgr(const QSharedPointer<INotebookBackend> &p_backend,
                                       QObject *p_parent)
    : QObject(p_parent),
      m_backend(p_backend)
{
}

INotebookConfigMgr::~INotebookConfigMgr()
{
}

const QSharedPointer<INotebookBackend> &INotebookConfigMgr::getBackend() const
{
    return m_backend;
}

QString INotebookConfigMgr::getCodeVersion() const
{
    const QString version("1");
    return version;
}

Notebook *INotebookConfigMgr::getNotebook() const
{
    return m_notebook;
}

void INotebookConfigMgr::setNotebook(Notebook *p_notebook)
{
    m_notebook = p_notebook;
}
