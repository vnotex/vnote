#ifndef FINDUNITEDENTRY_H
#define FINDUNITEDENTRY_H

#include "iunitedentry.h"

#include <QCommandLineParser>
#include <QSharedPointer>

#include <functional>

#include <controllers/searchcontroller.h>
#include <unitedentry/unitedentryhelper.h>

class QTreeWidget;
class QTreeWidgetItem;
class QTimer;

namespace vnotex {
class ServiceLocator;
class SearchResultModel;

class FindUnitedEntry : public IUnitedEntry {
  Q_OBJECT
public:
  struct SearchParams {
    QString keyword;
    int scope = SearchController::CurrentNotebook;
    int searchMode = SearchController::ContentSearch;
    bool caseSensitive = false;
    bool useRegex = false;
    QString filePattern;
  };

  FindUnitedEntry(ServiceLocator &p_services, UnitedEntryMgr *p_mgr, QObject *p_parent = nullptr);

  static SearchParams mapArgsToSearchParams(const QCommandLineParser &p_parser);

  void stop() Q_DECL_OVERRIDE;

  QSharedPointer<QWidget> currentPopupWidget() const Q_DECL_OVERRIDE;

protected:
  void initOnFirstProcess() Q_DECL_OVERRIDE;

  void
  processInternal(const QString &p_args,
                  const std::function<void(const QSharedPointer<QWidget> &)> &p_popupWidgetFunc)
      Q_DECL_OVERRIDE;

private:
  ServiceLocator &m_services;

  QString getHelpText() const;

  void prepareResultTree();

  void populateResultTree();

  void showMessageInResultTree(const QString &p_message);

  void doProcessInternal();

  void finish();

  void handleItemActivated(QTreeWidgetItem *p_item, int p_column);

  QCommandLineParser m_parser;

  SearchController *m_searchController = nullptr;

  SearchResultModel *m_resultModel = nullptr;

  QSharedPointer<QTreeWidget> m_resultTree;

  QTimer *m_processTimer = nullptr;

  SearchParams m_lastSearchParams;

  bool m_hasLastSearchParams = false;

  bool m_searchActive = false;

  UnitedEntryHelper m_helper;
};
} // namespace vnotex

#endif // FINDUNITEDENTRY_H
