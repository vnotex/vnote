#ifndef THEMEPAGE_H
#define THEMEPAGE_H

#include "settingspage.h"

class QListWidget;

namespace vnotex {
class ThemePage : public SettingsPage {
  Q_OBJECT
public:
  explicit ThemePage(ServiceLocator &p_services, QWidget *p_parent = nullptr);

  ~ThemePage() override;

  QString title() const Q_DECL_OVERRIDE;

  QString slug() const Q_DECL_OVERRIDE;

protected:
  void loadInternal() Q_DECL_OVERRIDE;

  bool saveInternal() Q_DECL_OVERRIDE;

private:
  void setupUI();

  void loadThemes();

  QString currentTheme() const;

  void applyThemePreview(const QString &p_themeName);

  void revertThemePreview();

  QWidget *findSettingsWidget();

  QListWidget *m_themeListWidget = nullptr;

  QString m_originalStyleSheet;

  bool m_previewActive = false;

  QWidget *m_settingsWidget = nullptr;
};
} // namespace vnotex

#endif // THEMEPAGE_H
