#ifndef VNOTEX_MINDMAPEDITORCONFIG_H
#define VNOTEX_MINDMAPEDITORCONFIG_H

#include "iconfig.h"

namespace vnotex {
class MainConfig;
class IConfigMgr;

class MindMapEditorConfig : public IConfig {
public:
  // Persisted names are independent of the native editor's enum values.
  enum class LayoutDirection { Balanced, Right, Left, Outline };

  MindMapEditorConfig(IConfigMgr *p_mgr, IConfig *p_topConfig);

  const QString &getFontFamily() const;
  void setFontFamily(const QString &p_family);

  int getFontPointSize() const;
  void setFontPointSize(int p_size);

  int getUndoLimit() const;
  void setUndoLimit(int p_limit);

  bool getConfirmSubtreeDeletion() const;
  void setConfirmSubtreeDeletion(bool p_enabled);

  bool getAutoRandomBranchColor() const;
  void setAutoRandomBranchColor(bool p_enabled);

  bool getAutoSectionNumbering() const;
  void setAutoSectionNumbering(bool p_enabled);

  bool getSeparateNodePropertiesPanel() const;
  void setSeparateNodePropertiesPanel(bool p_enabled);

  LayoutDirection getDefaultLayoutDirection() const;
  void setDefaultLayoutDirection(LayoutDirection p_direction);

  const QJsonObject &getShortcuts() const;

  void fromJson(const QJsonObject &p_jobj) override;
  QJsonObject toJson() const override;

private:
  friend class MainConfig;

  static LayoutDirection stringToLayoutDirection(const QString &p_direction);
  static QString layoutDirectionToString(LayoutDirection p_direction);

  QString m_fontFamily;
  int m_fontPointSize = 0;
  int m_undoLimit = 100;
  bool m_confirmSubtreeDeletion = true;
  bool m_autoRandomBranchColor = true;
  bool m_autoSectionNumbering = true;
  bool m_separateNodePropertiesPanel = true;
  LayoutDirection m_defaultLayoutDirection = LayoutDirection::Balanced;

  // Keep user overrides intact; MainConfig filters only the load-time runtime copy.
  QJsonObject m_shortcuts;
  QJsonObject m_effectiveShortcuts;
};
} // namespace vnotex

#endif // VNOTEX_MINDMAPEDITORCONFIG_H
