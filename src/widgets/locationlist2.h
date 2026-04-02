#ifndef LOCATIONLIST2_H
#define LOCATIONLIST2_H

#include <QFrame>

#include <core/noncopyable.h>

class QLabel;
class QModelIndex;
class QPoint;
class QStackedWidget;

namespace vnotex {

class SearchResultDelegate;
class SearchResultModel;
class SearchResultView;
class ServiceLocator;

class LocationList2 : public QFrame, private Noncopyable {
  Q_OBJECT

public:
  explicit LocationList2(ServiceLocator &p_services, QWidget *p_parent = nullptr);
  ~LocationList2() override;

  SearchResultModel *getModel();
  void clear();

signals:
  void resultActivated(const QModelIndex &p_index);
  void contextMenuRequested(const QModelIndex &p_index, const QPoint &p_globalPos);

private:
  void setupUI();
  void setupConnections();
  void updatePlaceholder();

  ServiceLocator &m_services;

  SearchResultModel *m_model = nullptr;
  SearchResultView *m_view = nullptr;
  SearchResultDelegate *m_delegate = nullptr;

  QStackedWidget *m_stackedWidget = nullptr;
  QLabel *m_placeholderLabel = nullptr;
  QLabel *m_truncatedBanner = nullptr;
};

} // namespace vnotex

#endif // LOCATIONLIST2_H
