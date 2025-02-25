#include "searchinfoprovider.h"

#include "mainwindow.h"
#include "notebookexplorer2.h"
#include "notebookmgr.h"
#include "viewarea.h"
#include <core/vnotex.h>

using namespace vnotex;

SearchInfoProvider::SearchInfoProvider(const ViewArea *p_viewArea,
                                       const NotebookExplorer2 *p_notebookExplorer,
                                       const NotebookMgr *p_notebookMgr)
    : m_viewArea(p_viewArea), m_notebookExplorer(p_notebookExplorer), m_notebookMgr(p_notebookMgr) {
}

QList<Buffer *> SearchInfoProvider::getBuffers() const {
  return m_viewArea->getAllBuffersInViewSplits();
}

Node *SearchInfoProvider::getCurrentFolder() const {
  // TODO: NotebookExplorer2 now uses NodeIdentifier instead of Node*.
  // The search system needs to be updated to use NodeIdentifier or
  // retrieve Node* from NotebookMgr using the NodeIdentifier.
  // For now, return nullptr to avoid compilation errors.
  // The search functionality will be limited until this is fully migrated.
  return nullptr;
}

Notebook *SearchInfoProvider::getCurrentNotebook() const {
  // TODO: Migrate search system to use notebook ID (QString) instead of Notebook*.
  // NotebookExplorer2 now uses currentNotebookId() returning QString.
  // NotebookMgr::findNotebookById takes ID (quint64), not QString.
  // This bridge would require converting QString to ID, which needs additional infrastructure.
  // For now, return nullptr until the search system is fully migrated.
  return nullptr;
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
