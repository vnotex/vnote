#ifndef APPEARANCEPAGE_H
#define APPEARANCEPAGE_H

#include "settingspage.h"

#include <QPair>
#include <QVector>

class QCheckBox;
class QSpinBox;

namespace vnotex {
class MainWindow2;

class AppearancePage : public SettingsPage {
  Q_OBJECT
public:
  AppearancePage(ServiceLocator &p_services, MainWindow2 *p_mainWindow,
                 QWidget *p_parent = nullptr);

  QString title() const Q_DECL_OVERRIDE;

  QString slug() const Q_DECL_OVERRIDE;

protected:
  void loadInternal() Q_DECL_OVERRIDE;

  bool saveInternal() Q_DECL_OVERRIDE;

private:
  void setupUI();

  MainWindow2 *m_mainWindow = nullptr;

  QCheckBox *m_systemTitleBarCheckBox = nullptr;

  QSpinBox *m_toolBarIconSizeSpinBox = nullptr;

  // <CheckBox, ObjectName>.
  QVector<QPair<QCheckBox *, QString>> m_keepDocksExpandingContentArea;
};
} // namespace vnotex

#endif // APPEARANCEPAGE_H
