#ifndef UNITEDENTRYPAGE_H
#define UNITEDENTRYPAGE_H

#include "settingspage.h"

class QCheckBox;

namespace vnotex {
class UnitedEntryPage : public SettingsPage {
  Q_OBJECT
public:
  explicit UnitedEntryPage(ServiceLocator &p_services, QWidget *p_parent = nullptr);

  QString title() const Q_DECL_OVERRIDE;

  QString slug() const Q_DECL_OVERRIDE;

protected:
  void loadInternal() Q_DECL_OVERRIDE;

  bool saveInternal() Q_DECL_OVERRIDE;

private:
  void setupUI();

  QCheckBox *m_expandAllCheckBox = nullptr;
};
} // namespace vnotex

#endif // UNITEDENTRYPAGE_H
