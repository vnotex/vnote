#include "syncops.h"

#include <utility>

#include <core/services/notebookcoreservice.h>

namespace vnotex {
namespace SyncOps {

void disableSync(NotebookCoreService *p_svc, QString p_notebookId,
                 std::function<void(VxCoreError)> p_onFinished) {
  if (!p_svc) {
    if (p_onFinished) {
      p_onFinished(VXCORE_ERR_NULL_POINTER);
    }
    return;
  }

  const VxCoreError code = p_svc->disableSync(p_notebookId);
  if (p_onFinished) {
    p_onFinished(code);
  }
}

} // namespace SyncOps
} // namespace vnotex
