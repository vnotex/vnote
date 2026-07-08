#ifndef SYNCPAGE_H
#define SYNCPAGE_H

#include "settingspage.h"

class QSpinBox;

namespace vnotex {
class SyncPage : public SettingsPage {
  Q_OBJECT
public:
  explicit SyncPage(ServiceLocator &p_services, QWidget *p_parent = nullptr);

  QString title() const Q_DECL_OVERRIDE;

  QString slug() const Q_DECL_OVERRIDE;

protected:
  void loadInternal() Q_DECL_OVERRIDE;

  bool saveInternal() Q_DECL_OVERRIDE;

private:
  void setupUI();

  QSpinBox *m_autoSyncDebounceSpinBox = nullptr;
};
} // namespace vnotex

#endif // SYNCPAGE_H
