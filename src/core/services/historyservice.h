#ifndef HISTORYSERVICE_H
#define HISTORYSERVICE_H

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
  NotebookCoreService *m_notebookService = nullptr;
};

} // namespace vnotex

#endif // HISTORYSERVICE_H
