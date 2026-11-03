#ifndef VNOTEX_IAPIBACKEND_H
#define VNOTEX_IAPIBACKEND_H

#include "apitypes.h"

namespace vnotex {
class IApiBackend {
public:
  using Completion = std::function<void(ApiResult)>;
  virtual ~IApiBackend() = default;
  // execute/stop run only on ApiServer's owner thread. Completion may run on any thread.
  virtual void execute(ApiCommand p_command, ApiRequestContext p_context,
                       Completion p_complete) = 0;
  virtual void stop() = 0;
};
} // namespace vnotex
#endif
