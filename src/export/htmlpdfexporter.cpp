#include "htmlpdfexporter.h"

#include "exportdata.h"
#include "exportfontresolver.h"
#include "wkhtmltopdfhtmlpatch.h"

#include <QDebug>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QPageLayout>
#include <QPointer>
#include <QProcess>
#include <QSaveFile>
#include <QTemporaryDir>
#include <QTimer>
#include <QUrl>
#include <QVariant>
#include <QWebEnginePage>
#include <QWebEngineProfile>
#include <QWebEngineScript>
#include <QWebEngineSettings>
#include <QWebEngineView>

#include <utils/htmlutils.h>
#include <utils/processutils.h>

using namespace vnotex;

struct HtmlPdfExporter::Operation {
  bool active = true;
  bool cancelled = false;
  bool loadFinished = false;
  bool loadSucceeded = false;
  bool resourcesFinished = false;
  bool resourcesSucceeded = false;
  bool resourceQueryPending = false;
  bool printFinished = false;
  bool processStarted = false;
  bool processFinished = false;
  bool processStartTimedOut = false;
  QByteArray pdf;
  QPointer<QWebEnginePage> page;
  QPointer<QProcess> process;
  QPointer<QEventLoop> waitLoop;

  void wake() const {
    if (waitLoop) {
      waitLoop->quit();
    }
  }
};

namespace {
constexpr int c_phaseTimeoutMs = 30000;

// The asynchronous callbacks only touch their own heap-owned operation. The local loop is
// guarded and cleared before returning, so cancellation cannot leave a dangling completion target.
template <typename Operation>
bool waitForPhase(const std::shared_ptr<Operation> &p_operation, bool Operation::*p_finished) {
  if (p_operation->cancelled || p_operation.get()->*p_finished) {
    return !p_operation->cancelled && p_operation.get()->*p_finished;
  }

  QEventLoop loop;
  QTimer deadline;
  deadline.setSingleShot(true);
  deadline.setTimerType(Qt::PreciseTimer);
  QObject::connect(&deadline, &QTimer::timeout, &loop, &QEventLoop::quit);
  p_operation->waitLoop = &loop;
  deadline.start(c_phaseTimeoutMs);
  loop.exec();
  p_operation->waitLoop.clear();
  return !p_operation->cancelled && p_operation.get()->*p_finished;
}

// Run in an isolated world: generated HTML keeps its own scripts and cannot overwrite this state.
// Failed images settle too; a decoded image is not required to be successful to finish readiness.
const char c_beginResourceWait[] = R"JS(
(function () {
  window.__vnotePdfResourcesReady = 0;
  try {
    var images = Array.prototype.slice.call(document.images);
    var pending = images.map(function (image) {
      image.loading = 'eager';
      return new Promise(function (resolve) {
        var settled = false;
        function finish() {
          if (settled) { return; }
          settled = true;
          image.removeEventListener('load', finish);
          image.removeEventListener('error', finish);
          if (image.complete && image.naturalWidth && typeof image.decode === 'function') {
            image.decode().then(resolve, resolve);
          } else {
            resolve();
          }
        }
        image.addEventListener('load', finish);
        image.addEventListener('error', finish);
        if (image.complete) { finish(); }
      });
    });
    pending.push(document.fonts.ready);
    Promise.all(pending).then(function () {
      window.__vnotePdfResourcesReady = 1;
    }, function () {
      window.__vnotePdfResourcesReady = -1;
    });
    return 0;
  } catch (error) {
    window.__vnotePdfResourcesReady = -1;
    return -1;
  }
})()
)JS";
} // namespace

HtmlPdfExporter::HtmlPdfExporter(ServiceLocator &p_services, QObject *p_parent)
    : QObject(p_parent) {
  // Transport has no application-global page/profile or source-specific service dependency.
  Q_UNUSED(p_services);
}

HtmlPdfExporter::~HtmlPdfExporter() {
  stop();
  if (m_operation) {
    m_operation->active = false;
  }
}

bool HtmlPdfExporter::exportFiles(const ExportPdfOption &p_option, const QStringList &p_htmlFiles,
                                  const QString &p_outputFile) {
  if (m_operation) {
    emit logRequested(tr("A PDF conversion is already running."));
    return false;
  }
  if (p_htmlFiles.isEmpty() || p_outputFile.isEmpty() || !p_option.m_layout) {
    emit logRequested(tr("PDF conversion requires input HTML, an output file and a page layout."));
    return false;
  }
  if (!p_option.m_useWkhtmltopdf && p_htmlFiles.size() != 1) {
    emit logRequested(tr("Qt PDF conversion requires exactly one HTML file. Use wkhtmltopdf for "
                         "multiple documents."));
    return false;
  }

  const auto operation = std::make_shared<Operation>();
  m_operation = operation;
  const bool succeeded =
      p_option.m_useWkhtmltopdf
          ? htmlToPdfViaWkhtmltopdf(p_option, p_htmlFiles, p_outputFile, operation)
          : exportViaQt(p_option, p_htmlFiles.first(), p_outputFile, operation);
  operation->active = false;
  m_operation.reset();
  return succeeded && !operation->cancelled;
}

void HtmlPdfExporter::stop() {
  const auto operation = m_operation;
  if (!operation) {
    return;
  }
  operation->cancelled = true;
  if (operation->page) {
    operation->page->triggerAction(QWebEnginePage::Stop);
  }
  if (operation->process) {
    operation->process->kill();
  }
  operation->wake();
}

bool HtmlPdfExporter::exportViaQt(const ExportPdfOption &p_option, const QString &p_htmlFile,
                                  const QString &p_outputFile,
                                  const std::shared_ptr<Operation> &p_operation) {
  const QFileInfo input(p_htmlFile);
  if (!input.isFile() || !input.isReadable()) {
    emit logRequested(tr("Failed to load HTML for PDF conversion (%1). The file is not readable.")
                          .arg(p_htmlFile));
    return false;
  }

  // The unnamed profile is off the record. Destruction order keeps it alive until its page dies.
  QWebEngineProfile profile;
  QWebEnginePage page(&profile);
  // Chromium needs a render surface to settle image.decode(), even for a completed local image.
  // A scoped offscreen view drives that work without exposing a window or persistent storage.
  QWebEngineView view;
  view.setAttribute(Qt::WA_DontShowOnScreen);
  view.resize(p_option.m_layout->paintRectPixels(96).size());
  view.setPage(&page);
  view.show();
  p_operation->page = &page;
  page.settings()->setAttribute(QWebEngineSettings::LocalContentCanAccessFileUrls, true);
  // Unprotected MarkdownViewer permits remote resources too; non-embedded export images must
  // retain that policy when the completed HTML is reloaded for printing.
  page.settings()->setAttribute(QWebEngineSettings::LocalContentCanAccessRemoteUrls, true);
  const auto loaded =
      connect(&page, &QWebEnginePage::loadFinished, &page, [p_operation](bool p_success) {
        if (!p_operation->active || p_operation->cancelled) {
          return;
        }
        p_operation->loadSucceeded = p_success;
        p_operation->loadFinished = true;
        p_operation->wake();
      });
  page.load(QUrl::fromLocalFile(input.absoluteFilePath()));
  const bool loadFinished = waitForPhase(p_operation, &Operation::loadFinished);
  disconnect(loaded);
  if (!loadFinished || !p_operation->loadSucceeded) {
    if (!p_operation->cancelled) {
      emit logRequested(
          loadFinished ? tr("Failed to load HTML for PDF conversion (%1).").arg(p_htmlFile)
                       : tr("Timed out loading HTML for PDF conversion (%1).").arg(p_htmlFile));
    }
    return false;
  }

  const auto resourceResult = [p_operation](const QVariant &p_result) {
    if (!p_operation->active || p_operation->cancelled) {
      return;
    }
    p_operation->resourceQueryPending = false;
    bool valid = false;
    const int status = p_result.toInt(&valid);
    if (!valid || status != 0) {
      p_operation->resourcesSucceeded = valid && status == 1;
      p_operation->resourcesFinished = true;
      p_operation->wake();
    }
  };
  p_operation->resourceQueryPending = true;
  page.runJavaScript(QString::fromLatin1(c_beginResourceWait), QWebEngineScript::ApplicationWorld,
                     resourceResult);
  QTimer resourcePoll;
  connect(&resourcePoll, &QTimer::timeout, &page, [p_operation, resourceResult]() {
    if (!p_operation->active || p_operation->cancelled || p_operation->resourcesFinished ||
        p_operation->resourceQueryPending || !p_operation->page) {
      return;
    }
    p_operation->resourceQueryPending = true;
    p_operation->page->runJavaScript(QStringLiteral("window.__vnotePdfResourcesReady"),
                                     QWebEngineScript::ApplicationWorld, resourceResult);
  });
  resourcePoll.start(25);
  const bool resourcesFinished = waitForPhase(p_operation, &Operation::resourcesFinished);
  resourcePoll.stop();
  if (!resourcesFinished || !p_operation->resourcesSucceeded) {
    if (!p_operation->cancelled) {
      emit logRequested(
          resourcesFinished
              ? tr("Failed while waiting for PDF images and fonts (%1).").arg(p_htmlFile)
              : tr("Timed out waiting for PDF images and fonts (%1).").arg(p_htmlFile));
    }
    return false;
  }

  page.printToPdf(
      [p_operation](const QByteArray &p_result) {
        if (!p_operation->active || p_operation->cancelled) {
          return;
        }
        p_operation->pdf = p_result;
        p_operation->printFinished = true;
        p_operation->wake();
      },
      *p_option.m_layout);
  if (!waitForPhase(p_operation, &Operation::printFinished) || p_operation->pdf.isEmpty()) {
    if (!p_operation->cancelled) {
      emit logRequested(p_operation->printFinished
                            ? tr("Failed to print HTML to PDF (%1).").arg(p_htmlFile)
                            : tr("Timed out printing HTML to PDF (%1).").arg(p_htmlFile));
    }
    return false;
  }

  QSaveFile output(p_outputFile);
  if (!output.open(QIODevice::WriteOnly) ||
      output.write(p_operation->pdf) != p_operation->pdf.size()) {
    emit logRequested(
        tr("Failed to write PDF file (%1): %2").arg(p_outputFile, output.errorString()));
    return false;
  }
  if (p_operation->cancelled) {
    return false;
  }
  if (!output.commit()) {
    emit logRequested(
        tr("Failed to commit PDF file (%1): %2").arg(p_outputFile, output.errorString()));
    return false;
  }
  return true;
}

static QString marginToStrMM(qreal p_margin) { return QStringLiteral("%1mm").arg(p_margin); }

QStringList HtmlPdfExporter::prepareWkhtmltopdfArguments(const ExportPdfOption &p_pdfOption) const {
  QStringList args;

  // Page layout.
  {
    const auto &layout = p_pdfOption.m_layout;
    args << "--page-size" << layout->pageSize().key();
    args << "--orientation"
         << (layout->orientation() == QPageLayout::Portrait ? "Portrait" : "Landscape");

    const auto marginsMM = layout->margins(QPageLayout::Millimeter);
    args << "--margin-bottom" << marginToStrMM(marginsMM.bottom());
    args << "--margin-left" << marginToStrMM(marginsMM.left());
    args << "--margin-right" << marginToStrMM(marginsMM.right());
    args << "--margin-top" << marginToStrMM(marginsMM.top());

    // Header/footer. Emit each non-empty field; values are unicode-encoded so
    // non-ASCII (e.g. CJK) content survives, matching the TOC header handling.
    const bool hasHeader = !p_pdfOption.m_headerLeft.isEmpty() ||
                           !p_pdfOption.m_headerCenter.isEmpty() ||
                           !p_pdfOption.m_headerRight.isEmpty();
    const bool hasFooter = !p_pdfOption.m_footerLeft.isEmpty() ||
                           !p_pdfOption.m_footerCenter.isEmpty() ||
                           !p_pdfOption.m_footerRight.isEmpty();

    if (!p_pdfOption.m_headerLeft.isEmpty()) {
      args << "--header-left" << HtmlUtils::unicodeEncode(p_pdfOption.m_headerLeft);
    }
    if (!p_pdfOption.m_headerCenter.isEmpty()) {
      args << "--header-center" << HtmlUtils::unicodeEncode(p_pdfOption.m_headerCenter);
    }
    if (!p_pdfOption.m_headerRight.isEmpty()) {
      args << "--header-right" << HtmlUtils::unicodeEncode(p_pdfOption.m_headerRight);
    }
    if (hasHeader) {
      args << "--header-spacing" << QString::number(marginsMM.top() / 3, 'f', 2);
    }

    if (!p_pdfOption.m_footerLeft.isEmpty()) {
      args << "--footer-left" << HtmlUtils::unicodeEncode(p_pdfOption.m_footerLeft);
    }
    if (!p_pdfOption.m_footerCenter.isEmpty()) {
      args << "--footer-center" << HtmlUtils::unicodeEncode(p_pdfOption.m_footerCenter);
    }
    if (!p_pdfOption.m_footerRight.isEmpty()) {
      args << "--footer-right" << HtmlUtils::unicodeEncode(p_pdfOption.m_footerRight);
    }
    if (hasFooter) {
      args << "--footer-spacing" << QString::number(marginsMM.bottom() / 3, 'f', 2);
    }
  }

  args << "--encoding" << "utf-8";

  // Retain the existing delay for MathJax.
  args << "--javascript-delay" << "5000";

  args << "--enable-local-file-access";

  // Append additional global option.
  if (!p_pdfOption.m_wkhtmltopdfArgs.isEmpty()) {
    args.append(ProcessUtils::parseCombinedArgString(p_pdfOption.m_wkhtmltopdfArgs));
  }

  // The Mermaid sizing workaround (see wkhtmltopdfhtmlpatch.h) is an in-page script, so disabling
  // JavaScript would silently drop every diagram from the PDF. Appended AFTER the user arguments
  // because wkhtmltopdf applies these switches in order and the last one wins: that neutralizes
  // `--disable-javascript` and its short form, including inside a bundle such as `-qn`, without
  // parsing (and possibly corrupting) the user's argument list.
  args << "--enable-javascript";

  // Must be put after the global object options.
  if (p_pdfOption.m_addTableOfContents) {
    args << "toc";
    args << "--toc-text-size-shrink" << "1.0";
    args << "--toc-header-text" << HtmlUtils::unicodeEncode(tr("Table of Contents"));
  }
  return args;
}

// Inject the Mermaid workarounds (see wkhtmltopdfhtmlpatch.h) into p_htmlFile in place.
// Byte-level: the file is never decoded, so its encoding survives untouched. Every caller passes a
// file living in a QTemporaryDir, so the user's own output is never mutated. Returns false when the
// file cannot be patched; the caller must then abort instead of feeding wkhtmltopdf an input that
// can hang it forever.
static bool patchHtmlForWkhtmltopdf(const QString &p_htmlFile, int p_maxWidthPx, int p_maxHeightPx,
                                    const QByteArray &p_textFamily,
                                    const QByteArray &p_monoFamily) {
  QFile file(p_htmlFile);
  if (!file.open(QIODevice::ReadOnly)) {
    qWarning() << "failed to read intermediate HTML file for patching" << p_htmlFile
               << file.errorString();
    return false;
  }
  auto content = file.readAll();
  if (file.error() != QFileDevice::NoError) {
    qWarning() << "failed to read intermediate HTML file for patching" << p_htmlFile
               << file.errorString();
    return false;
  }
  file.close();

  if (!insertWkhtmltopdfHtmlPatches(content, p_maxWidthPx, p_maxHeightPx, p_textFamily,
                                    p_monoFamily)) {
    qWarning() << "no </head> found in intermediate HTML file" << p_htmlFile;
    return false;
  }

  QSaveFile out(p_htmlFile);
  if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
    qWarning() << "failed to open intermediate HTML file for writing" << p_htmlFile
               << out.errorString();
    return false;
  }

  if (out.write(content) != content.size() || !out.commit()) {
    qWarning() << "failed to write patched intermediate HTML file" << p_htmlFile
               << out.errorString();
    return false;
  }

  return true;
}

bool HtmlPdfExporter::htmlToPdfViaWkhtmltopdf(const ExportPdfOption &p_pdfOption,
                                              const QStringList &p_htmlFiles,
                                              const QString &p_outputFile,
                                              const std::shared_ptr<Operation> &p_operation) {
  if (p_pdfOption.m_wkhtmltopdfExePath.isEmpty()) {
    emit logRequested(tr("Configure a wkhtmltopdf executable to export PDF with this backend."));
    return false;
  }
  QStringList args = prepareWkhtmltopdfArguments(p_pdfOption);

  // Fit Mermaid diagrams to the text column and to one printable page (see wkhtmltopdfhtmlpatch.h).
  // Both limits are derived from the page layout VNote itself passes to wkhtmltopdf, so they are
  // only trustworthy when nothing else can change the page geometry or the print scaling. The
  // user's extra arguments are appended after VNote's and win, and far more of them than just the
  // margin/page-size options affect the result (`--zoom`, `--default-header`,
  // `--header-html`/`--footer-html` reservation, `--disable-smart-shrinking`, `--user-style-sheet`,
  // ...). Rather than chase that list, ANY custom argument turns both clamps off: that is exactly
  // the pre-existing behavior, and it is reported.
  int maxWidthPx = 0;
  int maxHeightPx = 0;
  if (p_pdfOption.m_layout) {
    maxWidthPx = wkhtmltopdfContentWidthPx(*p_pdfOption.m_layout);
    maxHeightPx = wkhtmltopdfPageContentHeightPx(*p_pdfOption.m_layout);
  }
  if ((maxWidthPx > 0 || maxHeightPx > 0) && !p_pdfOption.m_wkhtmltopdfArgs.isEmpty()) {
    emit logRequested(tr("Custom wkhtmltopdf arguments may change the page geometry; not fitting "
                         "Mermaid diagrams to the page. A large diagram may overflow it."));
    maxWidthPx = 0;
    maxHeightPx = 0;
  }

  // Force an installed, CJK-capable family at the head of the stack (see wkhtmltopdfhtmlpatch.h
  // workaround 5). Unlike the size clamps this stays on even with custom arguments: no wkhtmltopdf
  // option can make a Latin-only font grow CJK glyphs.
  const SystemFontProbe fontProbe;
  const auto fonts = resolveWkhtmltopdfFonts(fontProbe);
  if (fonts.m_text.isEmpty()) {
    emit logRequested(tr("No CJK-capable font that wkhtmltopdf can load was found; non-Latin text "
                         "may be rendered as blank boxes. Install e.g. Noto Sans SC."));
  }

  // Prepare the args.
  for (auto const &file : p_htmlFiles) {
    if (p_operation->cancelled) {
      return false;
    }
    if (!patchHtmlForWkhtmltopdf(file, maxWidthPx, maxHeightPx, fonts.m_text, fonts.m_mono)) {
      emit logRequested(
          tr("Failed to prepare the intermediate HTML file (%1) for wkhtmltopdf.").arg(file));
      return false;
    }

    // Note: system's locale settings (Language for non-Unicode programs) is important to
    // wkhtmltopdf. Input file could be encoded via
    // QUrl::fromLocalFile(p_htmlFile).toString(QUrl::EncodeUnicode) to handle non-ASCII path. But
    // for the output file, it is useless.
    args << QUrl::fromLocalFile(QDir::toNativeSeparators(file)).toString(QUrl::EncodeUnicode);
  }

  // To handle non-ASCII path, export it to a temp file and then copy it.
  QTemporaryDir tmpDir;
  if (!tmpDir.isValid()) {
    emit logRequested(tr("Failed to create the temporary directory for PDF conversion."));
    return false;
  }

  const auto tmpFile = tmpDir.filePath("vx_tmp_output.pdf");
  args << QDir::toNativeSeparators(tmpFile);

  bool ret =
      startProcess(QDir::toNativeSeparators(p_pdfOption.m_wkhtmltopdfExePath), args, p_operation);
  if (p_operation->cancelled) {
    return false;
  }
  if (!ret) {
    emit logRequested(tr("Failed to run wkhtmltopdf (%1). If the path points to a launcher or "
                         "shim (e.g. a scoop shim), try the real executable instead.")
                          .arg(p_pdfOption.m_wkhtmltopdfExePath));
    return false;
  }

  // wkhtmltopdf may exit normally without producing anything (e.g. it rejects an option). Do not
  // report success in that case.
  if (!QFileInfo::exists(tmpFile) || QFileInfo(tmpFile).size() == 0) {
    emit logRequested(tr("wkhtmltopdf did not produce an output file (%1).").arg(p_outputFile));
    return false;
  }

  emit logRequested(tr("Copy output file (%1) to (%2).").arg(tmpFile, p_outputFile));
  QFile input(tmpFile);
  QSaveFile output(p_outputFile);
  if (!input.open(QIODevice::ReadOnly) || !output.open(QIODevice::WriteOnly)) {
    emit logRequested(
        tr("Failed to copy exported PDF to (%1): %2")
            .arg(p_outputFile, input.isOpen() ? output.errorString() : input.errorString()));
    return false;
  }
  char buffer[65536];
  qint64 size = 0;
  while ((size = input.read(buffer, sizeof(buffer))) > 0) {
    if (p_operation->cancelled || output.write(buffer, size) != size) {
      if (!p_operation->cancelled) {
        emit logRequested(
            tr("Failed to write exported PDF (%1): %2").arg(p_outputFile, output.errorString()));
      }
      return false;
    }
  }
  if (size < 0) {
    emit logRequested(tr("Failed to read the temporary PDF file: %1").arg(input.errorString()));
    return false;
  }
  if (p_operation->cancelled) {
    return false;
  }
  if (!output.commit()) {
    emit logRequested(
        tr("Failed to commit exported PDF (%1): %2").arg(p_outputFile, output.errorString()));
    return false;
  }

  return true;
}

bool HtmlPdfExporter::startProcess(const QString &p_program, const QStringList &p_args,
                                   const std::shared_ptr<Operation> &p_operation) {
  emit logRequested(p_program + " " + ProcessUtils::combineArgString(p_args));
  if (p_operation->cancelled) {
    return false;
  }

  QProcess process;
  QEventLoop loop;
  QTimer startDeadline;
  startDeadline.setSingleShot(true);
  startDeadline.setTimerType(Qt::PreciseTimer);
  p_operation->process = &process;
  p_operation->waitLoop = &loop;

  const QPointer<HtmlPdfExporter> exporter(this);
  const auto readOutput = [exporter, p_operation]() {
    if (!exporter || !p_operation->active || !p_operation->process) {
      return;
    }
    QString log = QString::fromLocal8Bit(p_operation->process->readAllStandardOutput());
    log += QString::fromLocal8Bit(p_operation->process->readAllStandardError());
    if (!log.isEmpty()) {
      emit exporter->logRequested(log);
    }
  };
  connect(&process, &QProcess::readyReadStandardOutput, &process, readOutput);
  connect(&process, &QProcess::readyReadStandardError, &process, readOutput);
  connect(&process, &QProcess::started, &startDeadline, &QTimer::stop);
  connect(&process, &QProcess::started, &process,
          [p_operation]() { p_operation->processStarted = true; });
  connect(&process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), &process,
          [p_operation](int, QProcess::ExitStatus) {
            p_operation->processFinished = true;
            p_operation->wake();
          });
  connect(&process, &QProcess::errorOccurred, &process,
          [p_operation](QProcess::ProcessError p_error) {
            if (p_error == QProcess::FailedToStart) {
              p_operation->processFinished = true;
              p_operation->wake();
            }
          });
  connect(&startDeadline, &QTimer::timeout, &process, [p_operation]() {
    p_operation->processStartTimedOut = true;
    if (p_operation->process) {
      p_operation->process->kill();
    }
    p_operation->wake();
  });

  startDeadline.start(c_phaseTimeoutMs);
  process.start(p_program, p_args);
  if (!p_operation->processFinished && !p_operation->cancelled) {
    loop.exec();
  }
  startDeadline.stop();
  p_operation->waitLoop.clear();
  // Include output arriving with the finish signal, especially the explanation of a failed exit.
  readOutput();
  p_operation->process.clear();
  if (p_operation->cancelled) {
    return false;
  }
  if (p_operation->processStartTimedOut) {
    emit logRequested(tr("Timed out starting process (%1).").arg(p_program));
    return false;
  }
  if (!p_operation->processStarted) {
    emit logRequested(tr("Failed to start process (%1): %2").arg(p_program, process.errorString()));
    return false;
  }
  if (!p_operation->processFinished || process.exitStatus() != QProcess::NormalExit) {
    emit logRequested(
        tr("Process (%1) did not finish normally: %2").arg(p_program, process.errorString()));
    return false;
  }
  if (process.exitCode() != 0) {
    emit logRequested(
        tr("Process (%1) finished with exit code %2").arg(p_program).arg(process.exitCode()));
    return false;
  }
  return true;
}
