#ifndef GENERALPAGE_H
#define GENERALPAGE_H

#include "settingspage.h"

class QComboBox;
class QCheckBox;

namespace vnotex {
class GeneralPage : public SettingsPage {
  Q_OBJECT
public:
  explicit GeneralPage(QWidget *p_parent = nullptr);

  QString title() const Q_DECL_OVERRIDE;

protected:
  void loadInternal() Q_DECL_OVERRIDE;

  bool saveInternal() Q_DECL_OVERRIDE;

private:
  void setupUI();

  QComboBox *m_localeComboBox = nullptr;

  QComboBox *m_openGLComboBox = nullptr;

  QCheckBox *m_systemTrayCheckBox = nullptr;

  QCheckBox *m_recoverLastSessionCheckBox = nullptr;

  QCheckBox *m_checkForUpdatesCheckBox = nullptr;
};
} // namespace vnotex

#endif // GENERALPAGE_H
