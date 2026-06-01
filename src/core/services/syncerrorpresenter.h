#ifndef SYNCERRORPRESENTER_H
#define SYNCERRORPRESENTER_H

#include <QString>

#include <vxcore/vxcore_types.h>

namespace vnotex {

class SyncErrorPresenter {
public:
  enum class Context {
    CredentialWrite,
    CredentialRead,
    CredentialDelete,
    EnableSync,
    DisableSync,
    TriggerSync,
  };

  struct PresentedError {
    QString primary; // user-facing, tr()-translated
    QString details; // raw backend message
  };

  static PresentedError present(Context p_ctx, VxCoreError p_code, const QString &p_rawMessage);
};

} // namespace vnotex

#endif // SYNCERRORPRESENTER_H
