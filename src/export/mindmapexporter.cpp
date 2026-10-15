#include "mindmapexporter.h"

#include <QApplication>
#include <QEventLoop>
#include <QFileInfo>
#include <QPageLayout>
#include <QSaveFile>
#include <QScopeGuard>
#include <QThread>
#include <QTimer>
#include <QXmlStreamReader>
#include <cmath>
#include <memory>
#include <stdexcept>

#include <m3/m3.h>
#include <m3/qt/editor.h>

#include <core/configmgr2.h>
#include <core/editorconfig.h>
#include <core/mindmapeditorconfig.h>
#include <core/servicelocator.h>
#include <gui/services/mindmapimageloader.h>

using namespace vnotex;

MindMapExporter::MindMapExporter(ServiceLocator &p_services, QWidget *p_widgetParent,
                                 QObject *p_parent)
    : ExportRenderer(p_services, p_widgetParent, p_parent) {}

MindMapExporter::~MindMapExporter() {
  clearRequests();
  delete m_editor.data();
}

bool MindMapExporter::renderMarkdown(const ExportInput &p_input, ExportMarkdown &p_output) {
  m_stopped = false;
  const auto bytes = p_input.content.toUtf8();
  // The C API accepts NUL-terminated text; never silently truncate a supplied snapshot.
  if (bytes.contains('\0')) {
    emit logRequested(tr("Invalid NUL in MindMap source."));
    return false;
  }
  bool blank = true;
  for (const auto ch : bytes) {
    if (ch != ' ' && ch != '\t' && ch != '\r' && ch != '\n') {
      blank = false;
      break;
    }
  }
  M3Mindmap *raw = nullptr;
  const auto status = blank ? m3_mindmap_create("root", "Central topic", &raw)
                            : m3_mindmap_from_json(bytes.constData(), &raw);
  const std::unique_ptr<M3Mindmap, decltype(&m3_mindmap_destroy)> map(raw, m3_mindmap_destroy);
  if (status != M3_OK) {
    emit logRequested(tr("Failed to parse MindMap: %1").arg(QString::fromUtf8(m3_last_error())));
    return false;
  }
  char *text = nullptr;
  const auto projected = m3_mindmap_to_markdown(map.get(), &text);
  const std::unique_ptr<char, decltype(&m3_string_free)> markdown(text, m3_string_free);
  if (projected != M3_OK) {
    emit logRequested(tr("Failed to project MindMap: %1").arg(QString::fromUtf8(m3_last_error())));
    return false;
  }
  p_output.content = QString::fromUtf8(markdown.get());
  const auto name = p_input.file.fileName.isEmpty() ? p_input.file.filePath : p_input.file.fileName;
  p_output.fileName = QFileInfo(name).completeBaseName() + QStringLiteral(".md");
  p_output.copyMedia = true;
  return !m_stopped;
}

void MindMapExporter::clearRequests() {
  disconnect(m_imageRequestConnection);
  disconnect(m_imageReadyConnection);
  disconnect(m_errorConnection);
  if (m_images) {
    m_images->cancel();
  }
  m_pendingImages.clear();
}

void MindMapExporter::stop() {
  m_stopped = true;
  clearRequests();
  if (m_waitLoop) {
    m_waitLoop->quit();
  }
}

bool MindMapExporter::renderHtml(const ExportInput &p_input, const ExportHtmlRequest &p_request,
                                 const QString &p_outputFile) {
  if (!qApp || QThread::currentThread() != qApp->thread()) {
    emit logRequested(tr("MindMap HTML export requires the GUI thread."));
    return false;
  }
  if (p_request.options.m_htmlOption.m_useMimeHtmlFormat) {
    emit logRequested(tr("MIME HTML export is not supported for MindMap."));
    return false;
  }
  clearRequests();
  m_stopped = false;
  m_failed = false;
  const auto cleanup = qScopeGuard([this] {
    clearRequests();
    m_waitLoop.clear();
  });
  try {
    if (!m_editor) {
      m3::qt::EditorConfig config;
      config.initialVisibleLevels = 0;
      config.resourceBasePath = p_input.file.resourcePath;
      config.resolveRelativeUrls = false;
      if (auto *manager = m_services.get<ConfigMgr2>()) {
        const auto &settings = manager->getEditorConfig().getMindMapEditorConfig();
        config.style = {settings.getFontFamily(), settings.getFontPointSize()};
      }
      m_editor = new m3::qt::MindMapEditor(config, m_widgetParent.data());
      m_editor->hide();
      m_editor->setPalette(m_widgetParent ? m_widgetParent->palette() : QApplication::palette());
    }
    if (!m_images) {
      m_images = new MindMapImageLoader(m_services, this);
    }
    m_errorConnection = connect(m_editor, &m3::qt::MindMapEditor::errorOccurred, this,
                                [this](const QString &p_error) {
                                  m_failed = true;
                                  emit logRequested(p_error);
                                  if (m_waitLoop) {
                                    m_waitLoop->quit();
                                  }
                                });
    m_imageRequestConnection =
        connect(m_editor, &m3::qt::MindMapEditor::imageRequested, this,
                [this, base = p_input.file.resourcePath](const QString &p_url, quint64 p_id) {
                  if (m_stopped || m_failed) {
                    return;
                  }
                  m_pendingImages.insert(p_id);
                  m_images->requestImage(p_url, p_id, base);
                });
    m_imageReadyConnection =
        connect(m_images, &MindMapImageLoader::imageReady, this,
                [this](const QString &p_url, quint64 p_id, const QImage &p_image) {
                  if (m_stopped || !m_editor || !m_pendingImages.remove(p_id)) {
                    return;
                  }
                  if (p_image.isNull()) {
                    emit logRequested(tr("MindMap image unavailable: %1").arg(p_url));
                  }
                  m_editor->provideImage(p_url, p_id, p_image);
                  if (m_pendingImages.isEmpty() && m_waitLoop) {
                    m_waitLoop->quit();
                  }
                });
    // Rebase and load synchronously: a queued request must never see the prior document's base.
    m_editor->setResourceBasePath(p_input.file.resourcePath);
    if (!m_editor->loadJson(p_input.content.toUtf8())) {
      emit logRequested(tr("Failed to load MindMap: %1").arg(m_editor->lastError()));
      return false;
    }

    QEventLoop loop;
    QTimer deadline;
    deadline.setSingleShot(true);
    connect(&deadline, &QTimer::timeout, &loop, &QEventLoop::quit);
    deadline.start(30000);
    // Native requests are queued. Drain their initial dispatch before testing the pending set.
    QEventLoop dispatch;
    QTimer::singleShot(0, &dispatch, &QEventLoop::quit);
    dispatch.exec();
    m_waitLoop = &loop;
    if (!m_pendingImages.isEmpty() && !m_stopped && !m_failed && deadline.isActive()) {
      loop.exec();
    }
    m_waitLoop.clear();
    if (m_stopped || m_failed) {
      return false;
    }
    if (!m_pendingImages.isEmpty() || !deadline.isActive()) {
      emit logRequested(tr("Timed out waiting for MindMap export images."));
      return false;
    }
    auto html = m_editor->toHtml();
    if (html.isEmpty() || m_failed) {
      emit logRequested(tr("Failed to render MindMap: %1").arg(m_editor->lastError()));
      return false;
    }
    if (!adaptHtml(html, p_request) || m_stopped) {
      return false;
    }
    QSaveFile output(p_outputFile);
    const auto bytes = html.toUtf8();
    if (!output.open(QIODevice::WriteOnly) || output.write(bytes) != bytes.size() || m_stopped ||
        !output.commit()) {
      emit logRequested(
          tr("Failed to write MindMap HTML (%1): %2").arg(p_outputFile, output.errorString()));
      return false;
    }
    return true;
  } catch (const std::exception &error) {
    emit logRequested(tr("Failed to render MindMap: %1").arg(QString::fromUtf8(error.what())));
    return false;
  }
}

bool MindMapExporter::adaptHtml(QString &p_html, const ExportHtmlRequest &p_request) {
  if (p_request.purpose == ExportHtmlPurpose::Standalone) {
    return true;
  }
  if (!p_html.contains(QStringLiteral("</head>")) ||
      !p_html.contains(QStringLiteral("<section id=\"visual-map\">")) ||
      !p_html.contains(QStringLiteral("<section id=\"articles\" hidden=\"hidden\">"))) {
    emit logRequested(tr("The native MindMap HTML structure is incompatible with export."));
    return false;
  }
  // Read only the native export's controlled geometry marker, before any node content.
  QSizeF naturalSize;
  bool viewportFound = false;
  QXmlStreamReader reader(p_html);
  while (!reader.atEnd()) {
    if (reader.readNext() != QXmlStreamReader::StartElement) {
      continue;
    }
    const auto id = reader.attributes().value(QStringLiteral("id"));
    if (id == QStringLiteral("map-viewport")) {
      viewportFound = true;
    }
    if (id != QStringLiteral("mindmap")) {
      continue;
    }
    bool widthValid = false, heightValid = false;
    const auto width =
        reader.attributes().value(QStringLiteral("data-width")).toDouble(&widthValid);
    const auto height =
        reader.attributes().value(QStringLiteral("data-height")).toDouble(&heightValid);
    if (widthValid && heightValid && std::isfinite(width) && std::isfinite(height) && width > 0 &&
        height > 0) {
      naturalSize = QSizeF(width, height);
    }
    break;
  }
  if (!viewportFound || naturalSize.isEmpty()) {
    emit logRequested(tr("The native MindMap HTML geometry is incompatible with export."));
    return false;
  }
  QString style = QStringLiteral("#view-toggle { display:none !important; }\n"
                                 "#articles[hidden] { display:block !important; }\n");
  if (p_request.purpose == ExportHtmlPurpose::Pdf) {
    const auto layout = p_request.options.m_pdfOption.m_layout;
    if (!layout) {
      emit logRequested(tr("MindMap PDF export requires a page layout."));
      return false;
    }
    const auto size = layout->paintRect(QPageLayout::Millimeter).size();
    if (size.width() <= 1 || size.height() <= 1) {
      emit logRequested(tr("MindMap PDF printable area is too small."));
      return false;
    }
    constexpr qreal pixelsPerMillimeter = 96.0 / 25.4;
    const auto scale =
        qMin(qreal(1), qMin((size.width() - 1) * pixelsPerMillimeter / naturalSize.width(),
                            (size.height() - 1) * pixelsPerMillimeter / naturalSize.height()));
    // Layout zoom, unlike a paint-only transform, keeps Chromium's print shrink-to-fit from
    // shrinking every page to the natural diagram width. The viewport alone reserves flow space.
    // Important rules also prevent the standalone resize handler from undoing paper fitting.
    style += QStringLiteral(
                 "html, body { margin:0; padding:0; }\n"
                 "main { margin:0; padding:0; max-width:none; }\n"
                 "main > h1, #visual-map > h2, #visual-map > p { display:none; }\n"
                 "#visual-map { margin:0; padding:0; page-break-after:always; break-after:page; }\n"
                 "#map-viewport { position:relative !important; overflow:hidden !important; "
                 "margin:0 !important; padding:0 !important; border:0 !important; "
                 "width:%1px !important; height:%2px !important; max-width:none !important; }\n"
                 "#mindmap { position:absolute !important; left:0 !important; top:0 !important; "
                 "transform:none !important; -webkit-transform:none !important; "
                 "zoom:%3 !important; }\n"
                 "#articles, #articles ul, #articles li, #articles article, #articles p "
                 "{ page-break-inside:auto; break-inside:auto; }\n"
                 "#articles ul { margin-left:0; padding-left:0; padding-inline-start:0; "
                 "list-style:none; }\n"
                 "#articles h2, #articles h3 { page-break-after:avoid; break-after:avoid; }\n"
                 "#articles p { orphans:2; widows:2; }\n")
                 .arg(QString::number(naturalSize.width() * scale, 'g', 17),
                      QString::number(naturalSize.height() * scale, 'g', 17),
                      QString::number(scale, 'g', 17));
  }
  p_html.insert(p_html.indexOf(QStringLiteral("</head>")),
                QStringLiteral("<style id=\"vnote-export-layout\">\n") + style +
                    QStringLiteral("</style>\n"));
  return true;
}
