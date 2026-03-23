#ifndef EDITORPAGE_H
#define EDITORPAGE_H

#include "settingspage.h"

class QComboBox;
class QSpinBox;

namespace vnotex {
class HookManager;

class EditorPage : public SettingsPage {
  Q_OBJECT
public:
  explicit EditorPage(ServiceLocator &p_services, QWidget *p_parent = nullptr);

  QString title() const Q_DECL_OVERRIDE;

  // Should be called by all editors setting page when saved.
  static void notifyEditorConfigChange(HookManager *p_hookMgr);

protected:
  void loadInternal() Q_DECL_OVERRIDE;

  bool saveInternal() Q_DECL_OVERRIDE;

private:
  void setupUI();

  QComboBox *m_autoSavePolicyComboBox = nullptr;

  QSpinBox *m_toolBarIconSizeSpinBox = nullptr;

  QComboBox *m_spellCheckDictComboBox = nullptr;

  QComboBox *m_lineEndingComboBox = nullptr;

  QComboBox *m_layoutModeComboBox = nullptr;

  QSpinBox *m_readableWidthSpinBox = nullptr;
};
} // namespace vnotex

#endif // EDITORPAGE_H
