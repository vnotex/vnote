#ifndef NEWHTTPAPITOKENDIALOG_H
#define NEWHTTPAPITOKENDIALOG_H

#include "../scrolldialog.h"

class QCheckBox;
class QComboBox;
class QLineEdit;
class QListWidget;

namespace vnotex {
class HttpApiController;
class ServiceLocator;

class NewHttpApiTokenDialog : public ScrollDialog {
  Q_OBJECT
public:
  explicit NewHttpApiTokenDialog(ServiceLocator &p_services, QWidget *p_parent = nullptr);

protected:
  void acceptedButtonClicked() override;

private:
  void setupUI();

  HttpApiController *m_controller = nullptr;
  QLineEdit *m_tokenLabel = nullptr;
  QListWidget *m_notebooks = nullptr;
  QCheckBox *m_read = nullptr;
  QCheckBox *m_readLive = nullptr;
  QCheckBox *m_write = nullptr;
  QCheckBox *m_trash = nullptr;
  QComboBox *m_expiry = nullptr;
};
} // namespace vnotex

#endif // NEWHTTPAPITOKENDIALOG_H
