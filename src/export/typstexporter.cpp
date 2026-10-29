#include "typstexporter.h"

#include <QDir>
#include <QEventLoop>
#include <QFileInfo>
#include <QSaveFile>
#include <QScopeGuard>
#include <QTimer>
#include <QUrl>

#include <vtextedit/theme.h>
#include <vtextedit/typstcompiler.h>
#include <vtextedit/typstruntime.h>

#include <core/servicelocator.h>
#include <gui/services/themeservice.h>
#include <utils/contentmediautils.h>

using namespace vnotex;

TypstExporter::TypstExporter(ServiceLocator &p_services, QWidget *p_widgetParent, QObject *p_parent)
    : ExportRenderer(p_services, p_widgetParent, p_parent) {}

TypstExporter::~TypstExporter() { stop(); }

bool TypstExporter::writeOutput(const QString &p_outputFile, const QByteArray &p_bytes) {
  if (m_stopped) {
    return false;
  }
  QSaveFile output(p_outputFile);
  if (!output.open(QIODevice::WriteOnly) || output.write(p_bytes) != p_bytes.size() ||
      !output.commit()) {
    emit logRequested(tr("Failed to write file (%1): %2").arg(p_outputFile, output.errorString()));
    return false;
  }
  return true;
}

bool TypstExporter::renderSource(const ExportInput &p_input, ExportFormat p_format,
                                 const QString &p_outputFile) {
  if (m_waitLoop || p_format != ExportFormat::Typst) {
    emit logRequested(tr("Source export format is not supported by this renderer."));
    return false;
  }
  m_stopped = false;
  QString error;
  const auto scans = vte::TypstCompiler::collectImageReferences(
      {p_input.content}, m_services.get<vte::TypstRuntime>(), &error);
  if (scans.size() != 1) {
    emit logRequested(error);
    return false;
  }
  const auto &scan = scans.first();
  if (!scan.m_complete) {
    emit logRequested(
        tr("Typst image references may be incomplete; dynamic references and imported "
           "sources were not copied."));
  }
  QStringList paths;
  paths.reserve(scan.m_images.size());
  for (const auto &path : scan.m_images) {
    // Scanner values are literal filenames, not URL-encoded destinations. Keep %, # and ?.
    if (path.isEmpty() || QDir::isAbsolutePath(path) || path.startsWith(QLatin1Char('/')) ||
        path.startsWith(QLatin1Char('\\')) || !QUrl(path).scheme().isEmpty() ||
        (path.size() > 1 && path.at(1) == QLatin1Char(':'))) {
      continue;
    }
    paths.append(path);
  }
  QStringList warnings;
  const bool copied = ContentMediaUtils::copyRelativeImageFiles(paths, p_input.file.resourcePath,
                                                                p_outputFile, &warnings);
  for (const auto &warning : warnings) {
    emit logRequested(warning);
  }
  return copied && !m_stopped && writeOutput(p_outputFile, p_input.content.toUtf8());
}

bool TypstExporter::renderHtml(const ExportInput &p_input, const ExportHtmlRequest &p_request,
                               const QString &p_outputFile) {
  if (p_request.options.m_htmlOption.m_useMimeHtmlFormat) {
    emit logRequested(tr("MIME HTML export is not supported for Typst."));
    return false;
  }
  return compile(p_input, false, p_outputFile);
}

bool TypstExporter::renderPdf(const ExportInput &p_input, const ExportOption &p_option,
                              const QString &p_outputFile) {
  Q_UNUSED(p_option);
  return compile(p_input, true, p_outputFile);
}

bool TypstExporter::compile(const ExportInput &p_input, bool p_pdf, const QString &p_outputFile) {
  if (m_waitLoop) {
    emit logRequested(tr("Typst export is already in progress."));
    return false;
  }
  m_stopped = false;
  const QFileInfo file(p_input.file.filePath);
  const auto root = file.dir().canonicalPath();
  auto *runtime = m_services.get<vte::TypstRuntime>();
  if (!runtime || !runtime->isAvailable()) {
    emit logRequested(tr("Typst runtime is unavailable."));
    return false;
  }
  if (!file.isFile() || root.isEmpty() || file.fileName().isEmpty()) {
    emit logRequested(tr("Typst export requires an existing source file and project directory."));
    return false;
  }
  vte::TypstEditorConfig config;
  config.m_runtime = runtime;
  config.m_projectRoot = root;
  config.m_mainFile = file.fileName();
  if (auto *themeService = m_services.get<ThemeService>()) {
    config.m_textEditorConfig->m_theme =
        vte::Theme::createThemeFromContent(themeService->fetchTextEditorStyle());
  }
  m_compiler = new vte::TypstCompiler(config, this);
  QEventLoop loop;
  m_waitLoop = &loop;
  const auto cleanup = qScopeGuard([this] {
    m_waitLoop.clear();
    delete m_compiler.data();
  });
  bool completed = false;
  vte::TypstCompiler::Result result;
  connect(m_compiler, &vte::TypstCompiler::finished, &loop,
          [&](const vte::TypstCompiler::Result &p_result) {
            result = p_result;
            completed = true;
            loop.quit();
          });
  QTimer timeout;
  timeout.setSingleShot(true);
  connect(&timeout, &QTimer::timeout, &loop, [this] {
    emit logRequested(tr("Typst export timed out."));
    stop();
  });
  const bool accepted = p_pdf ? m_compiler->compileToPdf(p_input.content)
                              : m_compiler->compileToHtml(p_input.content);
  if (!accepted) {
    emit logRequested(tr("Typst compiler is busy."));
    return false;
  }
  timeout.start(config.m_compileTimeoutMs * 3);
  if (!completed && !m_stopped) {
    loop.exec();
  }
  timeout.stop();
  if (m_stopped || !completed) {
    return false;
  }
  for (const auto &diagnostic : result.m_diagnostics) {
    emit logRequested(diagnostic);
  }
  if (!result.isSuccess()) {
    emit logRequested(result.m_error);
    return false;
  }
  return !m_stopped && writeOutput(p_outputFile, p_pdf ? result.m_pdf : result.m_html);
}

void TypstExporter::stop() {
  m_stopped = true;
  if (m_waitLoop) {
    m_waitLoop->quit();
  }
  delete m_compiler.data();
}
