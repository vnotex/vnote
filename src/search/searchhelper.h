#ifndef SEARCHHELPER_H
#define SEARCHHELPER_H

#include <QSharedPointer>

#include <core/global.h>

#include "searchdata.h"
#include "searcher.h"

namespace vnotex {
class ISearchInfoProvider;

class VNOTEX_DEPRECATED("Use SearchController with ServiceLocator pattern instead") SearchHelper {
public:
  SearchHelper() = delete;

  static SearchState searchOnProvider(Searcher *p_searcher,
                                      const QSharedPointer<SearchOption> &p_option,
                                      const QSharedPointer<ISearchInfoProvider> &p_provider,
                                      QString &p_msg);

private:
  static bool isSearchOptionValid(const SearchOption &p_option, QString &p_msg);
};
} // namespace vnotex

#endif // SEARCHHELPER_H
