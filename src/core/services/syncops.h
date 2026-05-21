#ifndef SYNCOPS_H
#define SYNCOPS_H

#include <functional>

#include <QString>

#include <vxcore/vxcore_types.h>

// Forward declaration of the opaque cancellation token (defined in
// vxcore/vxcore.h). Tests + SyncService include the full header.
struct VxCoreSyncCancellation_;
typedef struct VxCoreSyncCancellation_ VxCoreSyncCancellation;

namespace vnotex {

class NotebookCoreService;

// Stateless free-function callables that wrap vxcore sync operations.
// Designed to be invoked from any thread (typically a SyncWorkQueueManager
// pool worker). No QObject, no signals: results are delivered via the
// p_onFinished std::function, which is invoked on the calling thread
// exactly once — even on early-return error paths (null inputs, etc.).
namespace SyncOps {

// Disable sync for the given notebook. p_onFinished is ALWAYS invoked once.
// If p_svc is null, p_onFinished is invoked synchronously with
// VXCORE_ERR_NULL_POINTER and the function returns without touching vxcore.
void disableSync(NotebookCoreService *p_svc, QString p_notebookId,
                 std::function<void(VxCoreError)> p_onFinished);

// Update credentials for the given notebook. p_onFinished is ALWAYS invoked
// once. If p_svc is null, p_onFinished is invoked synchronously with
// VXCORE_ERR_NULL_POINTER and the function returns without touching vxcore.
// The PAT inside p_credentialsJson MUST NOT be logged; the value is captured
// by-value and destroyed when this function returns.
void setCredentials(NotebookCoreService *p_svc, QString p_notebookId, QString p_credentialsJson,
                    std::function<void(VxCoreError)> p_onFinished);

// Enable sync for the given notebook. Calls vxcore_sync_enable (via
// NotebookCoreService) with the provided config + credentials JSON. The
// p_onFinished callback is ALWAYS invoked exactly once with (code, message).
// If p_svc is null, fires synchronously with VXCORE_ERR_NULL_POINTER and
// a descriptive message. Does NOT persist config to disk — that is the
// responsibility of SyncService::bootstrapAndPersist.
void enableSync(NotebookCoreService *p_svc, QString p_notebookId, QString p_configJson,
                QString p_credentialsJson, std::function<void(VxCoreError, QString)> p_onFinished);

// Trigger a sync run for the given notebook. If @p_cancel is non-null, routes
// through vxcore_sync_trigger_cancellable (via NotebookCoreService); otherwise
// falls back to vxcore_sync_trigger. The token lifetime is owned by the
// CALLER (typically SyncService) — this function NEVER frees it.
// p_onFinished is ALWAYS invoked exactly once, even on null-service early
// return (with VXCORE_ERR_NULL_POINTER).
void triggerSync(NotebookCoreService *p_svc, QString p_notebookId, VxCoreSyncCancellation *p_cancel,
                 std::function<void(VxCoreError)> p_onFinished);

} // namespace SyncOps

} // namespace vnotex

#endif // SYNCOPS_H
