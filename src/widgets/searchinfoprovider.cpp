#include "searchinfoprovider.h"

#include "mainwindow.h"
#include "notebookexplorer.h"
#include "notebookmgr.h"
#include "viewarea.h"
#include <core/vnotex.h>

using namespace vnotex;

SearchInfoProvider::SearchInfoProvider(const ViewArea *p_viewArea,
                                       const NotebookExplorer *p_notebookExplorer,
                                       const NotebookMgr *p_notebookMgr)
    : m_viewArea(p_viewArea), m_notebookExplorer(p_notebookExplorer), m_notebookMgr(p_notebookMgr) {
}

QList<Buffer *> SearchInfoProvider::getBuffers() const {
  return m_viewArea->getAllBuffersInViewSplits();
}

Node *SearchInfoProvider::getCurrentFolder() const {
  return m_notebookExplorer->currentExploredFolderNode();
}

Notebook *SearchInfoProvider::getCurrentNotebook() const {
  return m_notebookExplorer->currentNotebook().data();
}

QVector<Notebook *> SearchInfoProvider::getNotebooks() const {
  auto notebooks = m_notebookMgr->getNotebooks();
  QVector<Notebook *> nbs;
  nbs.reserve(notebooks.size());
  for (const auto &nb : notebooks) {
    nbs.push_back(nb.data());
  }

  return nbs;
}

QSharedPointer<SearchInfoProvider> SearchInfoProvider::create(const MainWindow *p_mainWindow) {
  return QSharedPointer<SearchInfoProvider>::create(p_mainWindow->getViewArea(),
                                                    p_mainWindow->getNotebookExplorer(),
                                                    &VNoteX::getInst().getNotebookMgr());
}
