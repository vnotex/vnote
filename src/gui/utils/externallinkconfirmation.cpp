#include "externallinkconfirmation.h"

#include <QUrl>

#include <core/configmgr2.h>
#include <core/coreconfig.h>
#include <widgets/messageboxhelper.h>

using namespace vnotex;

ExternalLinkConfirmation ExternalLinkConfirmation::ask(const QUrl &p_url, ConfigMgr2 *p_configMgr,
                                                       QWidget *p_parent) {
  ExternalLinkConfirmation result;
  result.m_configMgr = p_configMgr;

  int answer = QMessageBox::Yes;
  if (!result.m_configMgr || result.m_configMgr->getCoreConfig().isConfirmExternalLinksEnabled()) {
    answer = MessageBoxHelper::questionOpenLink(p_url, p_parent);
  }
  result.m_neverAsk = answer == QMessageBox::YesToAll;
  result.m_accepted = answer == QMessageBox::Yes || result.m_neverAsk;
  return result;
}

bool ExternalLinkConfirmation::accept() const {
  if (m_neverAsk && m_configMgr) {
    m_configMgr->getCoreConfig().setConfirmExternalLinksEnabled(false);
  }
  return m_accepted;
}
