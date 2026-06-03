#ifndef ISYNCNOTEBOOKSERVICE_H
#define ISYNCNOTEBOOKSERVICE_H

#include <QString>

#include <vxcore/vxcore_types.h>

struct VxCoreSyncCancellation_;
typedef struct VxCoreSyncCancellation_ VxCoreSyncCancellation;

namespace vnotex {

// Narrow interface exposing only the staged-sync entry points that
// SyncOps::triggerSync needs. NotebookCoreService implements it; tests
// can substitute a mock without inheriting the full notebook service.
class ISyncNotebookService {
public:
  virtual ~ISyncNotebookService() = default;

  // Stage + local commit phase. Caller MUST hold the per-notebook IO gate
  // around this call. On success, *p_didCommit is set to true iff a new
  // commit was produced (false means working tree was clean).
  virtual VxCoreError syncStageOnly(const QString &p_notebookId,
                                    VxCoreSyncCancellation *p_cancellationToken,
                                    bool *p_didCommit) = 0;

  // Network phase (fetch / rebase / push). MUST be invoked WITHOUT holding
  // the per-notebook IO gate so concurrent saves are not blocked across
  // network latency.
  virtual VxCoreError syncNetworkPhase(const QString &p_notebookId,
                                       VxCoreSyncCancellation *p_cancellationToken) = 0;
};

} // namespace vnotex

#endif // ISYNCNOTEBOOKSERVICE_H
