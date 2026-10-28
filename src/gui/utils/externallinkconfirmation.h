#ifndef EXTERNALLINKCONFIRMATION_H
#define EXTERNALLINKCONFIRMATION_H

#include <QPointer>

class QUrl;
class QWidget;

namespace vnotex {
class ConfigMgr2;

class [[nodiscard]] ExternalLinkConfirmation {
public:
  // Shows the warning when required, without changing preferences or opening the URL.
  static ExternalLinkConfirmation ask(const QUrl &p_url, ConfigMgr2 *p_configMgr,
                                      QWidget *p_parent);

  // Call only after revalidating the caller's lifetime and authorization. Dropping
  // the result instead leaves preferences unchanged, even after choosing Never Ask.
  bool accept() const;

private:
  ExternalLinkConfirmation() = default;

  QPointer<ConfigMgr2> m_configMgr;
  bool m_accepted = false;
  bool m_neverAsk = false;
};
} // namespace vnotex

#endif // EXTERNALLINKCONFIRMATION_H
