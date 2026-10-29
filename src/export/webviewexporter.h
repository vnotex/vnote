#ifndef WEBVIEWEXPORTER_H
#define WEBVIEWEXPORTER_H

#include "exportrenderer.h"

#include <atomic>
#include <memory>

class QWidget;

namespace vnotex {
class MarkdownViewer;

class WebViewExporter : public ExportRenderer {
  Q_OBJECT
public:
  enum WebViewState { Started = 0, LoadFinished = 0x1, WorkFinished = 0x2, Failed = 0x4 };
  Q_DECLARE_FLAGS(WebViewStates, WebViewState);

  // What the live page must flatten before its DOM is serialized (or printed), because the export
  // target cannot render it faithfully. One decision per route, so a route can never enable half
  // of what it needs.
  enum RasterFlag {
    // MathJax SVG -> PNG: Qt WebEngine's printToPdf picks the wrong font for <use>-referenced
    // glyphs (issue #2681), and Pandoc without rsvg-convert cannot embed the SVG at all.
    RasterizeMath = 0x1,
    // Mermaid SVG -> PNG: wkhtmltopdf re-shapes SVG text with a substituted font and clips the
    // labels (see wkhtmltopdfhtmlpatch.h).
    RasterizeDiagrams = 0x2
  };
  Q_DECLARE_FLAGS(RasterFlags, RasterFlag);

  WebViewExporter(ServiceLocator &p_services, QWidget *p_widgetParent, QObject *p_parent);

  ~WebViewExporter() override;

  bool renderSource(const ExportInput &p_input, ExportFormat p_format,
                    const QString &p_outputFile) override;

  bool renderHtml(const ExportInput &p_input, const ExportHtmlRequest &p_request,
                  const QString &p_outputFile) override;

  void stop() override;

private:
  enum class ExportState { Busy = 0, Finished, Failed };

  // Resolution a rasterized diagram is produced at, over the box it is PRINTED in. 384 dpi (4x
  // the 96 CSS px/inch wkhtmltopdf lays out at) is where the returns flatten out: measured against
  // the same diagram, 2x is visibly soft, 4x is clean at print and at a 6x on-screen zoom, and 6x
  // costs another 75% in file size for a difference that only shows under magnification.
  static constexpr int c_diagramRasterDpi = 384;

  void prepare(const ExportHtmlRequest &p_request);

  void clear();

  bool isWebViewReady() const;

  bool isWebViewFailed() const;

  bool doExportHtml(const ExportHtmlOption &p_htmlOption, const QString &p_outputFile,
                    const QUrl &p_baseUrl, RasterFlags p_rasterFlags = RasterFlags(),
                    const QPageLayout *p_layout = nullptr);

  // Ask the live page to flatten everything named by p_flags and block (bounded) until it reports
  // it is done. Returns false when the export was asked to stop, or when diagram rasterization -
  // which rewrites the live DOM - did not finish in time (serializing then would capture a random
  // mix of vector and raster diagrams).
  //
  // p_layout is the target page layout, used to rasterize diagrams at the size they will actually
  // be PRINTED at rather than at the size they happen to occupy on screen. Without it the page
  // rasterizes at its natural size, which is then resampled a second time by the CSS clamp - the
  // two resamplings are what made the diagrams look soft. Pass nullptr when no diagram is being
  // rasterized, or when the printed geometry is unknown.
  bool prepareLivePageForExport(RasterFlags p_flags, const QPageLayout *p_layout = nullptr);

  bool writeHtmlFile(const QString &p_file, const QUrl &p_baseUrl, const QString &p_headContent,
                     QString p_styleContent, const QString &p_content,
                     const QString &p_bodyClassList, bool p_embedStyles, bool p_completePage,
                     bool p_embedImages);

  bool embedStyleResources(QString &p_html) const;

  bool embedBodyResources(const QUrl &p_baseUrl, QString &p_html);

  bool fixBodyResources(const QUrl &p_baseUrl, const QString &p_folder, QString &p_html);

  QSize pageLayoutSize(const QPageLayout &p_layout) const;

  bool m_askedToStop = false;
  quint64 m_nativeGeneration = 0;
  std::shared_ptr<std::atomic_bool> m_nativeCancelled;

  bool m_exportOngoing = false;

  WebViewStates m_webViewStates = WebViewState::Started;

  // Managed by QObject.
  QPointer<MarkdownViewer> m_viewer;

  QString m_htmlTemplate;

  QString m_exportHtmlTemplate;

  // Resolved theme syntax-highlight stylesheet path (holds Prism .token colors AND the
  // div.code-toolbar/.toolbar positioning used by the code-block copy button). Embedded
  // directly into the exported HTML so a static file is self-contained even when
  // Utils.fetchStyleContent() fails to capture the highlight <link>.
  QString m_syntaxHighlightStyleFile;
};
} // namespace vnotex

Q_DECLARE_OPERATORS_FOR_FLAGS(vnotex::WebViewExporter::WebViewStates)
Q_DECLARE_OPERATORS_FOR_FLAGS(vnotex::WebViewExporter::RasterFlags)

#endif // WEBVIEWEXPORTER_H
