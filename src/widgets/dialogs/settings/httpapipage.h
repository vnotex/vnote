#ifndef HTTPAPIPAGE_H
#define HTTPAPIPAGE_H

#include "settingspage.h"

class QCheckBox;
class QDateTimeEdit;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QTreeWidget;

namespace vnotex {
class HttpApiController;

class HttpApiPage : public SettingsPage {
  Q_OBJECT
public:
  explicit HttpApiPage(ServiceLocator &p_services, QWidget *p_parent = nullptr);

  QString title() const override;
  QString slug() const override;

protected:
  void loadInternal() override;
  bool saveInternal() override;

private:
  void setupUI();
  void refreshStatus();
  void refreshNotebooks();
  void refreshTokens();
  void issueToken();
  void revokeToken();

  HttpApiController *m_controller = nullptr;
  QCheckBox *m_enabled = nullptr;
  QLineEdit *m_address = nullptr;
  QLineEdit *m_port = nullptr;
  QLineEdit *m_externalUrl = nullptr;
  QLabel *m_status = nullptr;
  QLabel *m_configError = nullptr;
  QLineEdit *m_tokenLabel = nullptr;
  QListWidget *m_notebooks = nullptr;
  QCheckBox *m_read = nullptr;
  QCheckBox *m_readLive = nullptr;
  QCheckBox *m_write = nullptr;
  QCheckBox *m_trash = nullptr;
  QDateTimeEdit *m_expiry = nullptr;
  QTreeWidget *m_tokens = nullptr;
  QPushButton *m_issue = nullptr;
  QPushButton *m_revoke = nullptr;
  QLabel *m_tokenResult = nullptr;
  QLabel *m_issueResult = nullptr;
};

} // namespace vnotex

#endif // HTTPAPIPAGE_H
