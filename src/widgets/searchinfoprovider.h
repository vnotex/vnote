#ifndef SEARCHINFOPROVIDER_H
#define SEARCHINFOPROVIDER_H

#include <search/isearchinfoprovider.h>

#include <QSharedPointer>

#include <core/global.h>

namespace vnotex {
class ViewArea;
class NotebookExplorer2;
class NotebookMgr;
class MainWindow;

class VNOTEX_DEPRECATED("Use SearchCoreService with ServiceLocator pattern instead")
    SearchInfoProvider : public ISearchInfoProvider {
public:
  SearchInfoProvider(const ViewArea *p_viewArea, const NotebookExplorer2 *p_notebookExplorer,
                     const NotebookMgr *p_notebookMgr);

  QList<Buffer *> getBuffers() const Q_DECL_OVERRIDE;

  Node *getCurrentFolder() const Q_DECL_OVERRIDE;

  Notebook *getCurrentNotebook() const Q_DECL_OVERRIDE;

  QVector<Notebook *> getNotebooks() const Q_DECL_OVERRIDE;

  static QSharedPointer<SearchInfoProvider> create(const MainWindow *p_mainWindow);

private:
  const ViewArea *m_viewArea = nullptr;

  const NotebookExplorer2 *m_notebookExplorer = nullptr;

  const NotebookMgr *m_notebookMgr = nullptr;
};
} // namespace vnotex

#endif // SEARCHINFOPROVIDER_H
