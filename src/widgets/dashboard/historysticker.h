#ifndef HISTORYSTICKER_H
#define HISTORYSTICKER_H

#include "sticker.h"

#include <QDate>
#include <QVariantMap>

class QListView;
class QModelIndex;
class QShowEvent;

namespace vnotex {

class HistoryListModel;
class FileNodeDelegate;
class HookManager;

// Dashboard sticker that lists recently opened files. Recent-by-default; when
// the Calendar sticker broadcasts HookNames::DashboardCalendarDateChanged, it
// switches to showing files opened on that local calendar day. It is a thin
// view: fetch logic lives in HistoryService, data in HistoryListModel.
class HistorySticker : public Sticker {
  Q_OBJECT
public:
  explicit HistorySticker(ServiceLocator &p_services, QWidget *p_parent = nullptr);
  ~HistorySticker() override;

  QString typeId() const override;
  QString titleText() const override;

  // Bounded number of entries shown in both recent and day-filter modes.
  static constexpr int kHistoryLimit = 20;

protected:
  void showEvent(QShowEvent *p_event) override;

private slots:
  void onItemActivated(const QModelIndex &p_index);

private:
  // React to the calendar-date-changed hook. Parses the raw string payload and
  // switches to day-filter mode only for a valid date; otherwise preserves the
  // current mode.
  void onCalendarDateChanged(const QVariantMap &p_args);

  // Reload whichever mode is currently active.
  void reloadCurrentMode();

  HistoryListModel *m_model = nullptr;
  QListView *m_listView = nullptr;
  FileNodeDelegate *m_delegate = nullptr;

  // Invalid = recent mode; valid = day-filter mode for that local date.
  QDate m_activeDate;

  HookManager *m_hookManager = nullptr;
  int m_dateHookId = -1;
};

} // namespace vnotex

#endif // HISTORYSTICKER_H
