#include "pdftooloptionsrouter.h"

#include <core/pdfviewerconfig.h>
#include <core/services/commenttypes.h>

#include "editors/pdfvieweradapter.h"

using namespace vnotex;

namespace {

void push(const PdfViewerConfig &p_config, PdfViewerAdapter *p_adapter, const QString &p_tool) {
  if (!p_adapter) {
    return;
  }
  p_adapter->setToolOptions(
      PdfViewerAdapter::toolFromString(p_tool),
      PdfViewerConfig::toolOptionsToJson(p_tool, p_config.getToolOptions(p_tool)));
}

} // namespace

void PdfToolOptionsRouter::hydrate(const PdfViewerConfig &p_config, PdfViewerAdapter *p_adapter) {
  if (!p_adapter) {
    return;
  }
  for (const auto &tool : PdfViewerConfig::toolNames()) {
    push(p_config, p_adapter, tool);
  }
}

QString PdfToolOptionsRouter::applyColor(PdfViewerConfig &p_config, PdfViewerAdapter *p_adapter,
                                         const QString &p_tool, const QString &p_token) {
  if (!PdfToolOptions::isValidTool(p_tool)) {
    return QString();
  }

  auto options = p_config.getToolOptions(p_tool);
  options.m_color = p_token;
  p_config.setToolOptions(p_tool, options);

  // Read BACK, so the adapter gets what was actually stored rather than what
  // was asked for.
  push(p_config, p_adapter, p_tool);
  return p_config.getToolOptions(p_tool).m_color;
}

double PdfToolOptionsRouter::applyScalar(PdfViewerConfig &p_config, PdfViewerAdapter *p_adapter,
                                         const QString &p_tool, double p_value) {
  auto options = p_config.getToolOptions(p_tool);
  if (PdfToolOptions::hasWidth(p_tool)) {
    options.m_width = p_value;
  } else if (PdfToolOptions::hasFontSize(p_tool)) {
    options.m_fontSize = p_value;
  } else {
    return 0.0;
  }

  p_config.setToolOptions(p_tool, options);
  push(p_config, p_adapter, p_tool);

  const auto stored = p_config.getToolOptions(p_tool);
  return PdfToolOptions::hasWidth(p_tool) ? stored.m_width : stored.m_fontSize;
}

void PdfToolOptionsRouter::captureHighlight(PdfViewerConfig &p_config, PdfViewerAdapter *p_adapter,
                                            const QString &p_token) {
  const auto stored = applyColor(p_config, p_adapter, PdfToolOptions::highlightTool(), p_token);
  if (p_adapter) {
    // The SAME normalized token the store now holds -- capturing with the raw
    // pick would let the highlight painted on the page differ from the one the
    // toolbar shows.
    p_adapter->captureSelection(stored);
  }
}
