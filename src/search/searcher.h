#ifndef SEARCHER_H
#define SEARCHER_H

#include <QObject>
#include <QRegularExpression>
#include <QScopedPointer>
#include <QSharedPointer>
#include <QThread>

#include "isearchengine.h"
#include "searchdata.h"
#include "searchtoken.h"

namespace vnotex {
class Buffer;
class File;
struct SearchResultItem;
class Node;
class Notebook;
class AsyncWorkerWithFunctor;

class Searcher : public QObject {
  Q_OBJECT
public:
  explicit Searcher(QObject *p_parent = nullptr);

  void clear();

  void stop();

  SearchState search(const QSharedPointer<SearchOption> &p_option,
                     const QList<Buffer *> &p_buffers);

  SearchState search(const QSharedPointer<SearchOption> &p_option, Node *p_folder);

  SearchState search(const QSharedPointer<SearchOption> &p_option,
                     const QVector<Notebook *> &p_notebooks);

  const SearchToken &getToken() const;

signals:
  void progressUpdated(int p_val, int p_maximum);

  void logRequested(const QString &p_log);

  void resultItemAdded(const QSharedPointer<SearchResultItem> &p_item);

  void resultItemsAdded(const QVector<QSharedPointer<SearchResultItem>> &p_items);

  void finished(SearchState p_state);

private:
  bool isAskedToStop() const;

  bool prepare(const QSharedPointer<SearchOption> &p_option);

  // Return false if there is failure.
  // Always search content at first phase.
  bool firstPhaseSearch(const File *p_file);

  // Return false if there is failure.
  bool firstPhaseSearchFolder(Node *p_node, QVector<SearchSecondPhaseItem> &p_secondPhaseItems);

  // Return false if there is failure.
  bool firstPhaseSearch(Node *p_node, QVector<SearchSecondPhaseItem> &p_secondPhaseItems);

  // Return false if there is failure.
  bool firstPhaseSearch(Notebook *p_notebook, QVector<SearchSecondPhaseItem> &p_secondPhaseItems);

  // Return false if there is failure.
  bool secondPhaseSearch(const QVector<SearchSecondPhaseItem> &p_secondPhaseItems);

  bool isFilePatternMatched(const QString &p_name) const;

  bool testTarget(SearchTarget p_target) const;

  bool testObject(SearchObject p_object) const;

  bool isTokenMatched(const QString &p_text) const;

  bool searchContent(const File *p_file);

  // Return true if matched.
  bool searchTag(const Node *p_node) const;

  void createSearchEngine();

  // Will be called after m_firstPhaseWorker finished.
  void doSecondPhaseSearch();

  QSharedPointer<SearchOption> m_option;

  SearchToken m_token;

  QRegularExpression m_filePattern;

  bool m_askedToStop = false;

  QScopedPointer<ISearchEngine> m_engine;

  AsyncWorkerWithFunctor *m_firstPhaseWorker = nullptr;

  // Pending second phase items.
  QVector<SearchSecondPhaseItem> m_secondPhaseItems;
};
} // namespace vnotex

#endif // SEARCHER_H
