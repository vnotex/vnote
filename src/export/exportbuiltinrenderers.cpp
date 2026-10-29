#include "exportrendererfactory.h"

#include "mindmapexporter.h"
#include "typstexporter.h"
#include "webviewexporter.h"

using namespace vnotex;

void vnotex::registerBuiltinExportRenderers(ExportRendererFactory &p_factory) {
  const auto webRenderer = [](ServiceLocator &p_services, QWidget *p_host, QObject *p_parent) {
    return new WebViewExporter(p_services, p_host, p_parent);
  };
  const auto webOptions = ExportOptionGroup::WebStyles | ExportOptionGroup::TransparentBackground;
  const auto pdfOptions =
      webOptions | ExportOptionGroup::PdfLayout | ExportOptionGroup::Wkhtmltopdf;
  const ExportCapabilities markdownCapabilities{{
      {ExportFormat::Markdown, {}, {}},
      {ExportFormat::HTML, QStringLiteral("html"), webOptions | ExportOptionGroup::Html},
      {ExportFormat::PDF, QStringLiteral("pdf"), pdfOptions | ExportOptionGroup::PdfToc},
      {ExportFormat::Custom, {}, webOptions | ExportOptionGroup::CustomScrollable},
  }};
  const ExportCapabilities textCapabilities{{
      {ExportFormat::Markdown, {}, {}},
      {ExportFormat::HTML, QStringLiteral("html"), webOptions | ExportOptionGroup::Html},
      {ExportFormat::PDF, QStringLiteral("pdf"), pdfOptions},
      {ExportFormat::Custom, {}, webOptions | ExportOptionGroup::CustomScrollable},
  }};
  const ExportCapabilities mindMapCapabilities{{
      {ExportFormat::MindMap, QStringLiteral("mmm"), {}},
      {ExportFormat::Markdown, QStringLiteral("md"), {}},
      {ExportFormat::HTML, QStringLiteral("html"), {}},
      {ExportFormat::PDF, QStringLiteral("pdf"),
       ExportOptionGroup::PdfLayout | ExportOptionGroup::Wkhtmltopdf},
      {ExportFormat::Custom, {}, {}},
  }};
  const ExportCapabilities typstCapabilities{{
      {ExportFormat::Typst, QStringLiteral("typst"), {}},
      {ExportFormat::HTML, QStringLiteral("html"), {}},
      {ExportFormat::PDF, QStringLiteral("pdf"), {}, ExportPdfBackend::Native},
      {ExportFormat::Custom, {}, {}},
  }};
  p_factory.registerRenderer(QStringLiteral("Typst"), typstCapabilities,
                             [](ServiceLocator &p_services, QWidget *p_host, QObject *p_parent) {
                               return new TypstExporter(p_services, p_host, p_parent);
                             });
  p_factory.registerRenderer(QStringLiteral("Markdown"), markdownCapabilities, webRenderer);
  p_factory.registerRenderer(QStringLiteral("Text"), textCapabilities, webRenderer);
  p_factory.registerRenderer(QStringLiteral("Others"), textCapabilities, webRenderer);
  p_factory.registerRenderer(QStringLiteral("MindMap"), mindMapCapabilities,
                             [](ServiceLocator &p_services, QWidget *p_host, QObject *p_parent) {
                               return new MindMapExporter(p_services, p_host, p_parent);
                             });
}
