#ifndef VNOTEX_APIAUTH_H
#define VNOTEX_APIAUTH_H

#include "apitypes.h"
#include "iapicredentialstore.h"
#include <optional>

class QThread;
namespace vnotex {
class ApiServer;
class ApiAuth {
public:
  enum class IssueResult { Issued, Rejected, Unavailable };

  explicit ApiAuth(IApiCredentialStore &p_store);
  ~ApiAuth();
  ApiAuth(const ApiAuth &) = delete;
  ApiAuth &operator=(const ApiAuth &) = delete;

  bool load(QString *p_error);
  IssueResult issue(const ApiTokenGrant &p_grant, ApiIssuedToken *p_token, QString *p_error);
  bool revoke(const QString &p_id, QString *p_error);
  QJsonArray tokens() const;
  std::optional<ApiPrincipal> authenticate(const QByteArray &p_bearerValue) const;
  bool isAuthorized(const ApiPrincipal &p_principal, const ApiCommand &p_command) const;

private:
  friend class ApiServer;
  struct State;
  std::function<bool()> authorizer(const ApiPrincipal &p_principal,
                                   const ApiCommand &p_command) const;
  bool adminThread(QString *p_error) const;
  IApiCredentialStore &m_store;
  QThread *m_owner;
  std::shared_ptr<State> m_state;
};
} // namespace vnotex
#endif
