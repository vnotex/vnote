#ifndef EDITORPAGE_H
#define EDITORPAGE_H

#include "settingspage.h"

class QComboBox;
class QSpinBox;

namespace vnotex {
class EditorPage : public SettingsPage {
  Q_OBJECT
public:
  explicit EditorPage(QWidget *p_parent = nullptr);

  QString title() const Q_DECL_OVERRIDE;

  // Should be called by all editors setting page when saved.
  static void notifyEditorConfigChange();

protected:
  void loadInternal() Q_DECL_OVERRIDE;

  bool saveInternal() Q_DECL_OVERRIDE;

private:
  void setupUI();

  QComboBox *m_autoSavePolicyComboBox = nullptr;

  QSpinBox *m_toolBarIconSizeSpinBox = nullptr;

  QComboBox *m_spellCheckDictComboBox = nullptr;

  QComboBox *m_lineEndingComboBox = nullptr;
};
} // namespace vnotex

#endif // EDITORPAGE_H
