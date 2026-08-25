#ifndef PDFVIEWERCONFIG_H
#define PDFVIEWERCONFIG_H

#include "iconfig.h"

#include "webresource.h"

namespace vnotex {
class IConfigMgr;

class PdfViewerConfig : public IConfig {
public:
  PdfViewerConfig(IConfigMgr *p_mgr, IConfig *p_topConfig);

  void fromJson(const QJsonObject &p_jobj) Q_DECL_OVERRIDE;

  QJsonObject toJson() const Q_DECL_OVERRIDE;

  const WebResource &getViewerResource() const;

  // Last colour the user picked for a highlight / ink stroke / text box. A
  // semantic CommentColor token, never a literal colour. Persisted so the
  // toolbar comes back armed the way it was left.
  const QString &getCommentColor() const;

  void setCommentColor(const QString &p_color);

private:
  friend class MainConfig;

  void loadViewerResource(const QJsonObject &p_jobj);
  QJsonObject saveViewerResource() const;

  void initDefaults();
  static WebResource defaultViewerResource();

  WebResource m_viewerResource;

  QString m_commentColor;
};
} // namespace vnotex

#endif // PDFVIEWERCONFIG_H
