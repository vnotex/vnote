#ifndef SYNCOPS_H
#define SYNCOPS_H

#include <functional>

#include <QString>

#include <vxcore/vxcore_types.h>

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

} // namespace SyncOps

} // namespace vnotex

#endif // SYNCOPS_H
