#ifndef ISEARCHENGINE_H
#define ISEARCHENGINE_H

#include <QList>
#include <QObject>
#include <QSharedPointer>
#include <QVector>

#include <core/global.h>

#include "searchdata.h"

namespace vnotex {
struct SearchResultItem;

class SearchToken;

// Deprecated: Use SearchService with ServiceLocator pattern instead.
// NOTE: Cannot use VNOTEX_DEPRECATED on this struct because MSVC propagates
// C4996 through QList/QVector template instantiations, causing cascading warnings.
struct SearchSecondPhaseItem {
  SearchSecondPhaseItem() = default;

  SearchSecondPhaseItem(const QString &p_filePath, const QString &p_displayPath)
      : m_filePath(p_filePath), m_displayPath(p_displayPath) {}

  QString m_filePath;

  QString m_displayPath;
};

class VNOTEX_DEPRECATED("Use SearchService with ServiceLocator pattern instead") ISearchEngine
    : public QObject {
  Q_OBJECT
public:
  ISearchEngine() = default;

  virtual ~ISearchEngine() = default;

  virtual void search(const QSharedPointer<SearchOption> &p_option, const SearchToken &p_token,
                      const QVector<SearchSecondPhaseItem> &p_items) = 0;

  virtual void stop() = 0;

  virtual void clear() = 0;

signals:
  void finished(SearchState p_state);

  void resultItemsAdded(const QVector<QSharedPointer<SearchResultItem>> &p_items);

  void logRequested(const QString &p_log);
};
} // namespace vnotex
#endif // ISEARCHENGINE_H
