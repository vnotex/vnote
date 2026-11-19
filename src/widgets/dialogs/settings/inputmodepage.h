#ifndef INPUTMODEPAGE_H
#define INPUTMODEPAGE_H

#include "settingspage.h"

class QCheckBox;
class QComboBox;

namespace vnotex {
class InputModePage : public SettingsPage {
  Q_OBJECT
public:
  explicit InputModePage(ServiceLocator &p_services, QWidget *p_parent = nullptr);

  QString title() const Q_DECL_OVERRIDE;

  QString slug() const Q_DECL_OVERRIDE;

protected:
  void loadInternal() Q_DECL_OVERRIDE;

  bool saveInternal() Q_DECL_OVERRIDE;

private:
  void setupUI();

  QComboBox *m_inputModeComboBox = nullptr;

  QCheckBox *m_controlCToCopyCheckBox = nullptr;
};
} // namespace vnotex

#endif // INPUTMODEPAGE_H
