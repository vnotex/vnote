#ifndef MINDMAPEDITORCONFIG_H
#define MINDMAPEDITORCONFIG_H

#include "iconfig.h"

#include "webresource.h"

namespace vnotex {
class IConfigMgr;

class MindMapEditorConfig : public IConfig {
public:
  MindMapEditorConfig(IConfigMgr *p_mgr, IConfig *p_topConfig);

  void fromJson(const QJsonObject &p_jobj) Q_DECL_OVERRIDE;

  QJsonObject toJson() const Q_DECL_OVERRIDE;

  const WebResource &getEditorResource() const;

private:
  friend class MainConfig;

  void loadEditorResource(const QJsonObject &p_jobj);
  QJsonObject saveEditorResource() const;

  void initDefaults();
  static WebResource defaultEditorResource();

  WebResource m_editorResource;
};
} // namespace vnotex

#endif // MINDMAPEDITORCONFIG_H
