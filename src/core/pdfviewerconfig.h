#ifndef PDFVIEWERCONFIG_H
#define PDFVIEWERCONFIG_H

#include "iconfig.h"

#include "webresource.h"

#include <QHash>

namespace vnotex {
class IConfigMgr;

class PdfViewerConfig : public IConfig {
public:
  // Per-tool authoring options for the three annotation tools. Each tool owns
  // its OWN colour, so switching from a yellow highlight to a blue pen and back
  // does not mean re-picking every time.
  //
  // @m_width and @m_opacity apply to "ink" only and @m_fontSize to "freetext"
  // only; the serialized object omits the key for the tools that do not carry
  // it.
  struct ToolOptions {
    // A semantic CommentColor token, never a literal colour.
    QString m_color = QStringLiteral("yellow");

    // PDF units.
    double m_width = 1.5;

    double m_fontSize = 12.0;

    // Ink stroke opacity, 0.1 - 1.0. Ink only.
    double m_opacity = 1.0;

    bool operator==(const ToolOptions &p_other) const {
      return m_color == p_other.m_color && qFuzzyCompare(m_width, p_other.m_width) &&
             qFuzzyCompare(m_fontSize, p_other.m_fontSize) &&
             qFuzzyCompare(m_opacity, p_other.m_opacity);
    }

    bool operator!=(const ToolOptions &p_other) const { return !(*this == p_other); }
  };

  PdfViewerConfig(IConfigMgr *p_mgr, IConfig *p_topConfig);

  void fromJson(const QJsonObject &p_jobj) Q_DECL_OVERRIDE;

  QJsonObject toJson() const Q_DECL_OVERRIDE;

  const WebResource &getViewerResource() const;

  // "highlight", "ink", "freetext" — the SAME vocabulary
  // PdfViewerAdapter::toolToString() and the web side use.
  static QStringList toolNames();

  // Options the toolbar/adapter should start from. Normalized on the way in and
  // on the way out of JSON, so an unknown tool yields the defaults rather than
  // an empty object.
  ToolOptions getToolOptions(const QString &p_tool) const;

  void setToolOptions(const QString &p_tool, const ToolOptions &p_options);

  // Value <-> JSON, applying the shared PdfToolOptions normalization. Exposed
  // so the toolbar and the adapter can speak the same normalized object.
  static QJsonObject toolOptionsToJson(const QString &p_tool, const ToolOptions &p_options);

  static ToolOptions toolOptionsFromJson(const QString &p_tool, const QJsonObject &p_obj);

private:
  friend class MainConfig;

  void loadViewerResource(const QJsonObject &p_jobj);
  QJsonObject saveViewerResource() const;

  void initDefaults();
  static WebResource defaultViewerResource();

  WebResource m_viewerResource;

  QHash<QString, ToolOptions> m_toolOptions;
};
} // namespace vnotex

#endif // PDFVIEWERCONFIG_H
