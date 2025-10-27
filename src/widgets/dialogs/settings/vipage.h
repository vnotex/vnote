#ifndef VIPAGE_H
#define VIPAGE_H

#include "settingspage.h"

class QCheckBox;

namespace vnotex {
class ViPage : public SettingsPage {
  Q_OBJECT
public:
  explicit ViPage(QWidget *p_parent = nullptr);

  QString title() const Q_DECL_OVERRIDE;

protected:
  void loadInternal() Q_DECL_OVERRIDE;

  bool saveInternal() Q_DECL_OVERRIDE;

private:
  void setupUI();

  QCheckBox *m_controlCToCopyCheckBox = nullptr;
};
} // namespace vnotex

#endif // VIPAGE_H
