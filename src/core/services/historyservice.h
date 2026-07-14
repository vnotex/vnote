#ifndef HISTORYSERVICE_H
#define HISTORYSERVICE_H

#include <QDate>
#include <QObject>
#include <QVector>

#include <core/nodeidentifier.h>
#include <core/nodeinfo.h>

namespace vnotex {

class NotebookCoreService;

// Service that centralizes cross-notebook history logic. Aggregates the
// per-notebook history recorded by vxcore into a single, sorted list and
// exposes preview access. Wraps NotebookCoreService (does NOT hold a
// VxCoreContextHandle of its own).
class HistoryService : public QObject {
  Q_OBJECT

public:
  explicit HistoryService(NotebookCoreService *p_notebookService, QObject *p_parent = nullptr);
  ~HistoryService() override = default;

  // Aggregate history across all open notebooks. Skips entries with an empty
  // notebook id, and sorts by modified (opened) time descending.
  QVector<NodeInfo> getAllHistory() const;

  // Aggregate history capped to the p_limit most-recent entries. A p_limit
  // <= 0 means no cap (returns the full list).
  QVector<NodeInfo> getRecentHistory(int p_limit) const;

  // Aggregate history filtered to entries opened on the given local calendar
  // day (modifiedTimeUtc converted to local time), then capped to p_limit.
  // A p_limit <= 0 means no cap.
  QVector<NodeInfo> getHistoryForDate(const QDate &p_date, int p_limit) const;

  // Fetch a short preview for a history entry (delegates to
  // NotebookCoreService::peekFile). Returns empty string if the service is
  // unavailable or the file cannot be read.
  QString previewFor(const NodeIdentifier &p_id) const;

signals:
  // Reserved for future consumers that want to react to history changes.
  // NOTE: there is NO emitter yet — history writes still happen inside vxcore
  // on buffer open. Do not wire connections to this signal until an emitter
  // exists.
  void historyChanged();

private:
  // Build the full, sorted-descending aggregate history across all open
  // notebooks. Shared by getAllHistory / getRecentHistory / getHistoryForDate.
  QVector<NodeInfo> buildAllHistory() const;

  NotebookCoreService *m_notebookService = nullptr;
};

} // namespace vnotex

#endif // HISTORYSERVICE_H
