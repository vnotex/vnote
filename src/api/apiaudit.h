#ifndef VNOTEX_APIAUDIT_H
#define VNOTEX_APIAUDIT_H

#include "apitypes.h"
#include <http/httptypes.h>

namespace vnotex {

// Project only trusted metadata, never request/response payloads or error messages.
void logApiAccess(const HttpAccessRecord &p_record);
void logApiOperation(const HttpAuditContext &p_context, const QString &p_operationId,
                     qint64 p_durationMs, const QString &p_state, const ApiResult &p_result,
                     bool p_outcomeKnown, bool p_interrupted = false);

} // namespace vnotex
#endif // VNOTEX_APIAUDIT_H
