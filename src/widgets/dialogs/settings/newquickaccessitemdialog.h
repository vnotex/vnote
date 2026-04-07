#ifndef NEWQUICKACCESSITEMDIALOG_H
#define NEWQUICKACCESSITEMDIALOG_H

#include "../scrolldialog.h"

#include <core/sessionconfig.h>

class QComboBox;

namespace vnotex {

class LocationInputWithBrowseButton;
class ServiceLocator;

class NewQuickAccessItemDialog : public ScrollDialog {
  Q_OBJECT
public:
  explicit NewQuickAccessItemDialog(ServiceLocator &p_services, QWidget *p_parent = nullptr);

  SessionConfig::QuickAccessItem getItem() const;

protected:
  void acceptedButtonClicked() Q_DECL_OVERRIDE;

private:
  void setupUI();

  bool validateInputs();

  ServiceLocator &m_services;

  LocationInputWithBrowseButton *m_pathInput = nullptr;

  QComboBox *m_openModeComboBox = nullptr;
};

} // namespace vnotex

#endif // NEWQUICKACCESSITEMDIALOG_H
