#ifndef MINDMAPEDITORPAGE_H
#define MINDMAPEDITORPAGE_H

#include "settingspage.h"

class QCheckBox;
class QLineEdit;
class QSpinBox;

namespace vnotex {
class MindMapEditorPage : public SettingsPage {
  Q_OBJECT
public:
  explicit MindMapEditorPage(ServiceLocator &p_services, QWidget *p_parent = nullptr);

  QString title() const override;
  QString slug() const override;

protected:
  void loadInternal() override;
  bool saveInternal() override;

private:
  void setupUI();

  QLineEdit *m_fontFamily = nullptr;
  QSpinBox *m_fontSize = nullptr;
  QSpinBox *m_undoLimit = nullptr;
  QCheckBox *m_confirmSubtreeDeletion = nullptr;
  QCheckBox *m_autoRandomBranchColor = nullptr;
};
} // namespace vnotex

#endif // MINDMAPEDITORPAGE_H
