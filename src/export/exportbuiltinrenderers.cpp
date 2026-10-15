#include "exportrendererfactory.h"

#include "mindmapexporter.h"
#include "webviewexporter.h"

using namespace vnotex;

void vnotex::registerBuiltinExportRenderers(ExportRendererFactory &p_factory) {
  const auto webRenderer = [](ServiceLocator &p_services, QWidget *p_host, QObject *p_parent) {
    return new WebViewExporter(p_services, p_host, p_parent);
  };
  p_factory.registerRenderer(QStringLiteral("Markdown"), webRenderer);
  p_factory.registerRenderer(QStringLiteral("Text"), webRenderer);
  p_factory.registerRenderer(QStringLiteral("Others"), webRenderer);
  p_factory.registerRenderer(QStringLiteral("MindMap"),
                             [](ServiceLocator &p_services, QWidget *p_host, QObject *p_parent) {
                               return new MindMapExporter(p_services, p_host, p_parent);
                             });
}
