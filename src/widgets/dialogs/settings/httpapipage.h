#ifndef HTTPAPIPAGE_H
#define HTTPAPIPAGE_H

#include "settingspage.h"

class QCheckBox;
class QLabel;
class QLineEdit;
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
  void refreshTokens();
  void newToken();
  void revokeToken();

  HttpApiController *m_controller = nullptr;
  QCheckBox *m_enabled = nullptr;
  QLineEdit *m_address = nullptr;
  QLineEdit *m_port = nullptr;
  QLabel *m_status = nullptr;
  QLabel *m_configError = nullptr;
  QTreeWidget *m_tokens = nullptr;
  QPushButton *m_revoke = nullptr;
};

} // namespace vnotex

#endif // HTTPAPIPAGE_H
