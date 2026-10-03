#ifndef VNOTEX_MINDMAPEDITORCONFIG_H
#define VNOTEX_MINDMAPEDITORCONFIG_H

#include "iconfig.h"

namespace vnotex {
class MainConfig;
class IConfigMgr;

class MindMapEditorConfig : public IConfig {
public:
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

  const QJsonObject &getShortcuts() const;

  void fromJson(const QJsonObject &p_jobj) override;
  QJsonObject toJson() const override;

private:
  friend class MainConfig;

  QString m_fontFamily;
  int m_fontPointSize = 0;
  int m_undoLimit = 100;
  bool m_confirmSubtreeDeletion = true;
  bool m_autoRandomBranchColor = true;

  // Keep user overrides intact; MainConfig filters only the load-time runtime copy.
  QJsonObject m_shortcuts;
  QJsonObject m_effectiveShortcuts;
};
} // namespace vnotex

#endif // VNOTEX_MINDMAPEDITORCONFIG_H
