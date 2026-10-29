#include <QApplication>
#include <QDirIterator>
#include <QFile>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QPageLayout>
#include <QPageSize>
#include <QPdfDocument>
#include <QPdfSelection>
#include <QScopeGuard>
#include <QSemaphore>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>
#include <QWebEnginePage>
#include <QWebEngineView>
#include <QtTest>
#include <algorithm>
#include <memory>
#include <optional>

#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif

#include <core/configmgr2.h>
#include <core/servicelocator.h>
#include <core/services/configcoreservice.h>
#include <core/services/htmltemplateservice.h>
#include <export/exporter.h>
#include <export/exportstylesheet.h>
#include <export/typstexporter.h>
#include <gui/services/themeservice.h>
#include <gui/services/vxpdfschemehandler.h>
#include <gui/services/webengineprofileservice.h>
#include <vtextedit/typstruntime.h>
#include <vxcore/vxcore.h>

using namespace vnotex;

namespace {
bool writeBytes(const QString &path, const QByteArray &bytes) {
  if (!QDir().mkpath(QFileInfo(path).absolutePath()))
    return false;
  QFile file(path);
  return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}
QByteArray bytes(const QString &path) {
  QFile file(path);
  return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}
QStringList files(const QString &path) {
  QStringList result;
  QDirIterator it(path, QDir::Files, QDirIterator::Subdirectories);
  while (it.hasNext())
    result.append(it.next());
  return result;
}
QVariant evaluate(QWebEnginePage *page, const QString &script) {
  struct Reply {
    bool done = false;
    QVariant value;
  };
  auto reply = std::make_shared<Reply>();
  page->runJavaScript(script, [reply](const QVariant &value) {
    reply->value = value;
    reply->done = true;
  });
  return QTest::qWaitFor([reply] { return reply->done; }, 10000) ? reply->value : QVariant();
}

bool loadHtml(QWebEngineView &browser, const QString &path) {
  QSignalSpy loaded(&browser, &QWebEngineView::loadFinished);
  browser.resize(1000, 720);
  browser.show();
  browser.load(QUrl::fromLocalFile(path));
  return QTest::qWaitFor([&loaded] { return !loaded.isEmpty(); }, 30000) &&
         loaded.last().at(0).toBool();
}

QVariantMap bodyLayout(QWebEngineView &browser) {
  return evaluate(
             browser.page(),
             QStringLiteral(
                 "(() => { const body = document.body; const rect = body.getBoundingClientRect();"
                 "const style = getComputedStyle(body); return {"
                 "viewport: document.documentElement.clientWidth, width: rect.width, left: "
                 "rect.left,"
                 "right: document.documentElement.clientWidth - rect.right,"
                 "boxSizing: style.boxSizing, maxWidth: style.maxWidth,"
                 "marginLeft: parseFloat(style.marginLeft), marginRight: "
                 "parseFloat(style.marginRight),"
                 "custom: style.getPropertyValue('--export-style-test').trim() }; })()"))
      .toMap();
}

void verifyCenteredBody(QWebEngineView &browser, int percent) {
  const auto layout = bodyLayout(browser);
  QVERIFY(!layout.isEmpty());
  // Scrollbars can consume viewport width, so percentages use the actual layout viewport.
  const auto viewport = layout.value(QStringLiteral("viewport")).toDouble();
  QCOMPARE(layout.value(QStringLiteral("boxSizing")).toString(), QStringLiteral("border-box"));
  QCOMPARE(layout.value(QStringLiteral("maxWidth")).toString(),
           QString::number(percent) + QLatin1Char('%'));
  QVERIFY(qAbs(layout.value(QStringLiteral("width")).toDouble() - viewport * percent / 100.0) <
          1.0);
  const auto margin = viewport * (100 - percent) / 200.0;
  for (const auto *key : {"left", "right", "marginLeft", "marginRight"})
    QVERIFY2(qAbs(layout.value(QLatin1String(key)).toDouble() - margin) < 1.0, key);
}

QByteArray customizedCss(int percent) {
  return "body { box-sizing: border-box; max-width: " + QByteArray::number(percent) +
         "%; margin-left: auto; margin-right: auto; padding: 11px;"
         "border: 7px solid rgb(17, 34, 51); --export-style-test: applied; }\n";
}

QString mindMapSource() {
  const QJsonObject root{
      {QStringLiteral("id"), QStringLiteral("root")},
      {QStringLiteral("topic"), QStringLiteral("MindMap batch heading")},
      {QStringLiteral("image"),
       QJsonObject{{QStringLiteral("url"), QStringLiteral("images/nested/pixel.png")},
                   {QStringLiteral("width"), 48},
                   {QStringLiteral("height"), 32}}}};
  return QString::fromUtf8(
      QJsonDocument(QJsonObject{{QStringLiteral("schemaVersion"), 1},
                                {QStringLiteral("rootId"), QStringLiteral("root")},
                                {QStringLiteral("nodes"), QJsonArray{root}},
                                {QStringLiteral("crossLinks"), QJsonArray{}}})
          .toJson(QJsonDocument::Compact));
}
ExportCustomOption copyingCustomOption() {
  ExportCustomOption custom;
  custom.m_name = QStringLiteral("Copy actual export input");
  custom.m_targetSuffix = QStringLiteral("copied");
  // Only the executable is interpolated here; Exporter owns %1/%5 path quoting.
  custom.m_command = QLatin1Char('"') + QCoreApplication::applicationFilePath() +
                     QStringLiteral("\" --copy-export-input %1 %5");
  return custom;
}

// Hold both real native compile lanes: cancellation must retire queued work,
// rather than accidentally passing because a small document finished first.
class RuntimeLaneGate {
public:
  explicit RuntimeLaneGate(vte::TypstRuntime &runtime, bool scanner = false)
      : state(std::make_shared<State>()), expected(scanner ? 1 : 2) {
    const QStringList names =
        scanner ? QStringList{QStringLiteral("typst-scanner")}
                : QStringList{QStringLiteral("typst-compile-0"), QStringLiteral("typst-compile-1")};
    for (const auto &name : names) {
      auto *thread = runtime.findChild<QThread *>(name);
      auto *worker = thread ? thread->property("worker").value<QObject *>() : nullptr;
      if (worker && QMetaObject::invokeMethod(
                        worker,
                        [held = state] {
                          held->entered.release();
                          held->resume.acquire();
                        },
                        Qt::QueuedConnection))
        ++scheduled;
    }
  }
  ~RuntimeLaneGate() { release(); }
  bool entered() { return scheduled == expected && state->entered.tryAcquire(expected, 10000); }
  void release() {
    if (scheduled) {
      state->resume.release(scheduled);
      scheduled = 0;
    }
  }

private:
  struct State {
    QSemaphore entered;
    QSemaphore resume;
  };
  std::shared_ptr<State> state;
  const int expected;
  int scheduled = 0;
};
} // namespace

class TestTypstExport : public QObject {
  Q_OBJECT
private slots:
  void initTestCase();
  void cleanupTestCase();
  void init();
  void cleanup();
  void capabilities_data();
  void capabilities();
  void sharedCapabilities_data();
  void sharedCapabilities();
  void unsupportedFileBeforeWrites_data();
  void unsupportedFileBeforeWrites();
  void unsupportedBatchBeforeWrites_data();
  void unsupportedBatchBeforeWrites();
  void emptyBatchBeforeWrites();
  void allInOnePdfBeforeWrites_data();
  void allInOnePdfBeforeWrites();
  void mixedPdfBackends_data();
  void mixedPdfBackends();
  void sourceSnapshotMediaAndAttachments();
  void sourceEmptyAndRepeatedNames();
  void dynamicAndMissingImages();
  void sourcePathFiltering();
  void sourceFailures();
  void nativeHtmlAndPdf();
  void htmlOnlyAndMimeRejection();
  void sharedCssDefaultGeometry_data();
  void sharedCssDefaultGeometry();
  void sharedCssPolicy_data();
  void sharedCssPolicy();
  void sharedCssFreshAndEmpty();
  void sharedCssMissingAndDisabled();
  void sharedCssCollision_data();
  void sharedCssCollision();
  void sharedCssUnreadable();
  void sharedCssHeadBoundary();
  void sharedCssInvalidHtml_data();
  void sharedCssInvalidHtml();
  void compiledFailures_data();
  void compiledFailures();
  void cancellationThenExport();
  void customDiskAndLiveHtml();
  void customMixedAllInOne_data();
  void customMixedAllInOne();
  void customAllInOnePreparationFailure_data();
  void customAllInOnePreparationFailure();
  void customOptionSnapshot_data();
  void customOptionSnapshot();

private:
  QString run(ExportFormat format, const QString &text,
              const QString &name = QStringLiteral("main.typst"));
  ExportOption option(ExportFormat format) const;
  ExportInput input(const QString &text) const;
  ExportFileInfo sourceFile(const QString &type) const;
  QString diagnostic() const { return m_logs.join(QLatin1Char('\n')); }
  QTemporaryDir m_root;
  std::unique_ptr<QTemporaryDir> m_case;
  ServiceLocator m_services;
  VxCoreContextHandle m_context = nullptr;
  QStringList m_appSearchPaths;
  std::unique_ptr<ConfigCoreService> m_configCore;
  std::unique_ptr<ConfigMgr2> m_config;
  std::unique_ptr<HtmlTemplateService> m_templates;
  std::unique_ptr<WebEngineProfileService> m_profiles;
  std::unique_ptr<ThemeService> m_theme;
  std::unique_ptr<vte::TypstRuntime> m_runtime;
  std::unique_ptr<Exporter> m_exporter;
  QString m_project, m_main, m_output, m_attachments;
  QString m_stylesheet;
  std::optional<QByteArray> m_savedStylesheet;
  QFileDevice::Permissions m_savedStylesheetPermissions;
  bool m_manageStylesheet = false;
  QStringList m_logs;
};

void TestTypstExport::initTestCase() {
  QStandardPaths::setTestModeEnabled(true);
  vxcore_set_test_mode(1);
  QVERIFY(m_root.isValid());
  const QString themes = QStringLiteral(VNOTE_SRC_DIR "/data/extra");
  QVERIFY(QDir(themes + QStringLiteral("/themes/pure")).exists());
  const QString rcc = QStringLiteral(VNOTE_TEST_EXTRA_RCC);
  QVERIFY(QFileInfo::exists(rcc));
  m_appSearchPaths = QDir::searchPaths(QStringLiteral("app"));
  QDir::setSearchPaths(QStringLiteral("app"), {QFileInfo(rcc).absolutePath()});
  QCOMPARE(vxcore_context_create(nullptr, &m_context), VXCORE_OK);
  m_configCore = std::make_unique<ConfigCoreService>(m_context);
  m_config = std::make_unique<ConfigMgr2>(m_configCore.get());
  m_config->init();
  // Match the production Markdown fixture: ConfigMgr first sets the executable prefix.
  QDir::setSearchPaths(QStringLiteral("app"), {QFileInfo(rcc).absolutePath()});
  m_config->initAfterQtAppStarted();
  QVERIFY(m_config->extraDataCopyFailures().isEmpty());
  m_templates = std::make_unique<HtmlTemplateService>(m_config.get());
  m_profiles = std::make_unique<WebEngineProfileService>(m_root.filePath(QStringLiteral("profile")),
                                                         m_config.get(), m_templates.get());
  m_services.registerService(m_configCore.get());
  m_services.registerService(m_config.get());
  m_services.registerService(m_templates.get());
  m_services.registerService(m_profiles.get());
  ThemeServiceConfig config;
  config.themeName = QStringLiteral("pure");
  config.locale = QStringLiteral("en_US");
  config.appDataPath = themes;
  m_theme = std::make_unique<ThemeService>(config);
  QVERIFY(!m_theme->fetchTextEditorStyle().isEmpty());
  m_runtime = std::make_unique<vte::TypstRuntime>();
  QVERIFY(m_runtime->isAvailable());
  m_services.registerService(m_theme.get());
  m_services.registerService(m_runtime.get());
}
void TestTypstExport::cleanupTestCase() {
  m_exporter.reset();
  m_profiles.reset();
  m_templates.reset();
  m_runtime.reset();
  m_theme.reset();
  m_config.reset();
  m_configCore.reset();
  if (m_context) {
    vxcore_context_destroy(m_context);
    m_context = nullptr;
  }
  QDir::setSearchPaths(QStringLiteral("app"), m_appSearchPaths);
}
void TestTypstExport::init() {
  m_case = std::make_unique<QTemporaryDir>(m_root.filePath(QStringLiteral("case-XXXXXX")));
  QVERIFY(m_case->isValid());
  m_stylesheet = QDir(m_config->getConfigDataFolder(ConfigMgr2::Web))
                     .filePath(QStringLiteral("css/export.css"));
  m_savedStylesheet.reset();
  if (QFileInfo::exists(m_stylesheet)) {
    QFile saved(m_stylesheet);
    QVERIFY(saved.open(QIODevice::ReadOnly));
    m_savedStylesheet = saved.readAll();
    m_savedStylesheetPermissions = saved.permissions();
    saved.close();
  }
  m_manageStylesheet = true;
  if (m_savedStylesheet)
    QVERIFY(QFile::remove(m_stylesheet));
  m_project = m_case->filePath(QStringLiteral("project"));
  m_main = m_project + QStringLiteral("/main.typst");
  m_output = m_case->filePath(QStringLiteral("output"));
  m_attachments = m_project + QStringLiteral("/attachments");
  QVERIFY(writeBytes(m_main, "= Saved disk heading\n"));
  QVERIFY(writeBytes(sourceFile(QStringLiteral("Markdown")).filePath,
                     "# Markdown batch heading\n\n![pixel](images/nested/pixel.png)\n"));
  QVERIFY(writeBytes(sourceFile(QStringLiteral("MindMap")).filePath, mindMapSource().toUtf8()));
  for (const auto &type :
       {QStringLiteral("Text"), QStringLiteral("Others"), QStringLiteral("Unregistered")})
    QVERIFY(writeBytes(sourceFile(type).filePath, "Plain source heading\n"));
  QVERIFY(writeBytes(m_project + QStringLiteral("/part.typst"), "#let answer = 42\n"));
  QVERIFY(writeBytes(m_attachments + QStringLiteral("/payload.bin"),
                     QByteArray("attachment\0bytes", 16)));
  QVERIFY(QDir().mkpath(m_project + QStringLiteral("/images/nested")));
  QImage image(12, 8, QImage::Format_RGB32);
  image.fill(Qt::red);
  QVERIFY(image.save(m_project + QStringLiteral("/images/nested/pixel.png")));
  QVERIFY(image.save(m_project + QStringLiteral("/literal%20.png")));
  m_logs.clear();
  ExportRendererFactory factory(m_services);
  registerBuiltinExportRenderers(factory);
  m_exporter = std::make_unique<Exporter>(m_services, factory, nullptr);
  connect(m_exporter.get(), &Exporter::logRequested, this,
          [this](const QString &log) { m_logs.append(log); });
}
void TestTypstExport::cleanup() {
  m_exporter.reset();
  if (m_manageStylesheet) {
    m_manageStylesheet = false;
    if (QFileInfo(m_stylesheet).isDir())
      QVERIFY(QDir(m_stylesheet).removeRecursively());
    else if (QFileInfo::exists(m_stylesheet))
      QVERIFY(QFile::remove(m_stylesheet));
    if (m_savedStylesheet) {
      QVERIFY(writeBytes(m_stylesheet, *m_savedStylesheet));
      QVERIFY(QFile::setPermissions(m_stylesheet, m_savedStylesheetPermissions));
    }
  }
  m_case.reset();
}
ExportOption TestTypstExport::option(ExportFormat format) const {
  ExportOption result;
  result.m_targetFormat = format;
  result.m_outputDir = m_output;
  result.m_exportAttachments = false;
  return result;
}
QString TestTypstExport::run(ExportFormat format, const QString &text, const QString &name) {
  return m_exporter->doExportFile(option(format), text, m_main, name, m_project, m_attachments,
                                  QStringLiteral("Typst"));
}
ExportInput TestTypstExport::input(const QString &text) const {
  ExportInput result;
  result.content = text;
  result.file = {m_main, QStringLiteral("main.typst"), m_project, m_attachments,
                 QStringLiteral("Typst")};
  return result;
}
ExportFileInfo TestTypstExport::sourceFile(const QString &type) const {
  const auto name = type.compare(QStringLiteral("Markdown"), Qt::CaseInsensitive) == 0
                        ? QStringLiteral("markdown peer.md")
                    : type == QStringLiteral("MindMap") ? QStringLiteral("mindmap peer.mmm")
                    : type == QStringLiteral("Typst")   ? QStringLiteral("main.typst")
                    : type == QStringLiteral("Text")
                        ? QStringLiteral("text peer.txt")
                        : type.toLower() + QStringLiteral(" peer.other");
  return {m_project + QLatin1Char('/') + name, name, m_project, m_attachments, type};
}
void TestTypstExport::capabilities_data() {
  QTest::addColumn<QString>("sourceType");
  QTest::addColumn<QVector<int>>("formats");
  QTest::addColumn<QStringList>("suffixes");
  QTest::addColumn<QVector<int>>("groups");
  QTest::addColumn<bool>("nativePdf");
  const int md = int(ExportFormat::Markdown), html = int(ExportFormat::HTML);
  const int pdf = int(ExportFormat::PDF), custom = int(ExportFormat::Custom);
  const auto web = ExportOptionGroup::WebStyles | ExportOptionGroup::TransparentBackground;
  const auto print = web | ExportOptionGroup::PdfLayout | ExportOptionGroup::Wkhtmltopdf;
  const QStringList webSuffixes{QString(), QStringLiteral("html"), QStringLiteral("pdf"),
                                QString()};
  for (const auto &type :
       {QStringLiteral("Markdown"), QStringLiteral("mArKdOwN"), QStringLiteral("Text"),
        QStringLiteral("Others"), QStringLiteral("Unregistered")}) {
    const bool markdown = type.compare(QStringLiteral("Markdown"), Qt::CaseInsensitive) == 0;
    QTest::newRow(qPrintable(type))
        << type << QVector<int>{md, html, pdf, custom} << webSuffixes
        << QVector<int>{0, (web | ExportOptionGroup::Html).toInt(),
                        (markdown ? print | ExportOptionGroup::PdfToc : print).toInt(),
                        (web | ExportOptionGroup::CustomScrollable).toInt()}
        << false;
  }
  QTest::newRow("MindMap")
      << QStringLiteral("MindMap")
      << QVector<int>{int(ExportFormat::MindMap), md, html, pdf, custom}
      << QStringList{QStringLiteral("mmm"), QStringLiteral("md"), QStringLiteral("html"),
                     QStringLiteral("pdf"), QString()}
      << QVector<int>{0, 0, 0,
                      (ExportOptionGroup::PdfLayout | ExportOptionGroup::Wkhtmltopdf).toInt(), 0}
      << false;
  QTest::newRow("Typst") << QStringLiteral("Typst")
                         << QVector<int>{int(ExportFormat::Typst), html, pdf, custom}
                         << QStringList{QStringLiteral("typst"), QStringLiteral("html"),
                                        QStringLiteral("pdf"), QString()}
                         << QVector<int>{0, 0, 0, 0} << true;
}
void TestTypstExport::capabilities() {
  QFETCH(QString, sourceType);
  QFETCH(QVector<int>, formats);
  QFETCH(QStringList, suffixes);
  QFETCH(QVector<int>, groups);
  QFETCH(bool, nativePdf);
  // Metadata must be available without constructing a rendering service or compiler.
  ServiceLocator metadataServices;
  ExportRendererFactory factory(metadataServices);
  registerBuiltinExportRenderers(factory);
  const auto targets = factory.capabilities(sourceType).targets;
  QCOMPARE(targets.size(), formats.size());
  for (int index = 0; index < targets.size(); ++index) {
    QCOMPARE(int(targets[index].format), formats[index]);
    QCOMPARE(targets[index].suffix, suffixes[index]);
    QCOMPARE(targets[index].options.toInt(), groups[index]);
    QCOMPARE(targets[index].pdfBackend, nativePdf && targets[index].format == ExportFormat::PDF
                                            ? ExportPdfBackend::Native
                                            : ExportPdfBackend::Html);
  }
  for (const auto format : {ExportFormat::Markdown, ExportFormat::HTML, ExportFormat::PDF,
                            ExportFormat::Custom, ExportFormat::Typst, ExportFormat::MindMap}) {
    QCOMPARE(std::any_of(targets.cbegin(), targets.cend(),
                         [format](const auto &target) { return target.format == format; }),
             formats.contains(int(format)));
  }
}
void TestTypstExport::sharedCapabilities_data() {
  QTest::addColumn<QStringList>("sourceTypes");
  QTest::addColumn<QVector<int>>("expected");
  const QVector<int> rendered{int(ExportFormat::HTML), int(ExportFormat::PDF),
                              int(ExportFormat::Custom)};
  const QVector<int> projected{int(ExportFormat::Markdown), int(ExportFormat::HTML),
                               int(ExportFormat::PDF), int(ExportFormat::Custom)};
  QTest::newRow("Markdown-Typst") << QStringList{QStringLiteral("Markdown"),
                                                 QStringLiteral("Typst")}
                                  << rendered;
  QTest::newRow("Typst-Markdown") << QStringList{QStringLiteral("Typst"),
                                                 QStringLiteral("Markdown")}
                                  << rendered;
  QTest::newRow("Markdown-MindMap")
      << QStringList{QStringLiteral("Markdown"), QStringLiteral("MindMap")} << projected;
  QTest::newRow("MindMap-Markdown")
      << QStringList{QStringLiteral("MindMap"), QStringLiteral("Markdown")} << projected;
}
void TestTypstExport::sharedCapabilities() {
  QFETCH(QStringList, sourceTypes);
  QFETCH(QVector<int>, expected);
  ExportRendererFactory factory(m_services);
  registerBuiltinExportRenderers(factory);
  QVector<int> shared;
  for (const auto &candidate : factory.capabilities(sourceTypes.first()).targets) {
    bool supported = true;
    for (const auto &type : sourceTypes) {
      const auto targets = factory.capabilities(type).targets;
      if (std::none_of(targets.cbegin(), targets.cend(), [&candidate](const auto &target) {
            return target.format == candidate.format;
          })) {
        supported = false;
        break;
      }
    }
    if (supported)
      shared.append(int(candidate.format));
  }
  QCOMPARE(shared, expected);
}
void TestTypstExport::unsupportedFileBeforeWrites_data() {
  QTest::addColumn<QString>("sourceType");
  QTest::addColumn<int>("target");
  QTest::newRow("Markdown-to-Typst") << QStringLiteral("Markdown") << int(ExportFormat::Typst);
  QTest::newRow("MindMap-to-Typst") << QStringLiteral("MindMap") << int(ExportFormat::Typst);
  QTest::newRow("Typst-to-Markdown") << QStringLiteral("Typst") << int(ExportFormat::Markdown);
  QTest::newRow("Typst-to-MindMap") << QStringLiteral("Typst") << int(ExportFormat::MindMap);
  QTest::newRow("unknown-target") << QStringLiteral("Markdown") << 999;
}
void TestTypstExport::unsupportedFileBeforeWrites() {
  QFETCH(QString, sourceType);
  QFETCH(int, target);
  const auto file = sourceFile(sourceType);
  QVERIFY(!QFileInfo::exists(m_output));
  const auto result = m_exporter->doExportFile(
      option(ExportFormat(target)), QString::fromUtf8(bytes(file.filePath)), file.filePath,
      file.fileName, file.resourcePath, file.attachmentFolderPath, file.fileTypeName);
  QVERIFY(result.isEmpty());
  QVERIFY(!QFileInfo::exists(m_output));
  if (target == 999) {
    QVERIFY(!diagnostic().isEmpty());
  } else {
    QVERIFY2(m_logs.contains(QStringLiteral("Target %1 is not supported for source type %2.")
                                 .arg(exportFormatString(ExportFormat(target)), sourceType)),
             qPrintable(diagnostic()));
  }
}
void TestTypstExport::unsupportedBatchBeforeWrites_data() {
  QTest::addColumn<QStringList>("sourceTypes");
  QTest::addColumn<int>("target");
  QTest::addColumn<QString>("unsupportedType");
  QTest::newRow("Typst-then-Markdown-native")
      << QStringList{QStringLiteral("Typst"), QStringLiteral("Markdown")}
      << int(ExportFormat::Typst) << QStringLiteral("Markdown");
  QTest::newRow("Typst-then-MindMap-native")
      << QStringList{QStringLiteral("Typst"), QStringLiteral("MindMap")} << int(ExportFormat::Typst)
      << QStringLiteral("MindMap");
  QTest::newRow("MindMap-then-Typst-native")
      << QStringList{QStringLiteral("MindMap"), QStringLiteral("Typst")}
      << int(ExportFormat::MindMap) << QStringLiteral("Typst");
  QTest::newRow("MindMap-then-Markdown-native")
      << QStringList{QStringLiteral("MindMap"), QStringLiteral("Markdown")}
      << int(ExportFormat::MindMap) << QStringLiteral("Markdown");
  QTest::newRow("Markdown-then-Typst-projection")
      << QStringList{QStringLiteral("Markdown"), QStringLiteral("Typst")}
      << int(ExportFormat::Markdown) << QStringLiteral("Typst");
  QTest::newRow("unknown-target") << QStringList{QStringLiteral("Markdown"),
                                                 QStringLiteral("Typst")}
                                  << 999 << QString();
}
void TestTypstExport::unsupportedBatchBeforeWrites() {
  QFETCH(QStringList, sourceTypes);
  QFETCH(int, target);
  QFETCH(QString, unsupportedType);
  QVector<ExportFileInfo> inputs;
  for (const auto &type : sourceTypes)
    inputs.append(sourceFile(type));
  // An unreadable earlier note must not mask a later incompatible source during preflight.
  QVERIFY(QFile::remove(inputs.first().filePath));
  QSignalSpy progress(m_exporter.get(), &Exporter::progressUpdated);
  QVERIFY(!QFileInfo::exists(m_output));
  QVERIFY(m_exporter
              ->doExportBatch(option(ExportFormat(target)), inputs,
                              QStringLiteral("incompatible batch"))
              .isEmpty());
  QVERIFY(!QFileInfo::exists(m_output));
  QCOMPARE(progress.size(), 0);
  QVERIFY(!diagnostic().contains(QStringLiteral("Failed to read file")));
  if (target == 999) {
    QVERIFY(!diagnostic().isEmpty());
  } else {
    QVERIFY2(m_logs.contains(QStringLiteral("Target %1 is not supported for source type %2.")
                                 .arg(exportFormatString(ExportFormat(target)), unsupportedType)),
             qPrintable(diagnostic()));
  }
}
void TestTypstExport::emptyBatchBeforeWrites() {
  auto custom = copyingCustomOption();
  for (const auto format : {ExportFormat::Markdown, ExportFormat::HTML, ExportFormat::PDF,
                            ExportFormat::Custom, ExportFormat::Typst, ExportFormat::MindMap}) {
    for (const bool allInOne : {false, true}) {
      auto settings = option(format);
      settings.m_outputDir += QStringLiteral("/%1-%2").arg(int(format)).arg(allInOne);
      settings.m_customOption = &custom;
      custom.m_allInOne = allInOne;
      settings.m_pdfOption.m_allInOne = allInOne;
      settings.m_pdfOption.m_useWkhtmltopdf = true;
      settings.m_pdfOption.m_wkhtmltopdfExePath = QStringLiteral("nonexistent-must-not-run");
      QVERIFY(m_exporter->doExportBatch(settings, {}, QStringLiteral("empty")).isEmpty());
      QVERIFY(!QFileInfo::exists(m_output));
    }
  }
}
void TestTypstExport::allInOnePdfBeforeWrites_data() {
  QTest::addColumn<QStringList>("sourceTypes");
  QTest::addColumn<bool>("wkhtmltopdf");
  QTest::addColumn<bool>("singleFile");
  for (const bool wkhtmltopdf : {false, true}) {
    const auto suffix = wkhtmltopdf ? QByteArray("-wkhtmltopdf") : QByteArray("-stale-qt");
    QTest::newRow(("single-Typst" + suffix).constData())
        << QStringList{QStringLiteral("Typst")} << wkhtmltopdf << true;
    QTest::newRow(("batch-Typst" + suffix).constData())
        << QStringList{QStringLiteral("Typst")} << wkhtmltopdf << false;
    QTest::newRow(("batch-Markdown-Typst" + suffix).constData())
        << QStringList{QStringLiteral("Markdown"), QStringLiteral("Typst")} << wkhtmltopdf << false;
    QTest::newRow(("batch-MindMap-Typst" + suffix).constData())
        << QStringList{QStringLiteral("MindMap"), QStringLiteral("Typst")} << wkhtmltopdf << false;
  }
  QTest::newRow("html-backend-without-wkhtmltopdf")
      << QStringList{QStringLiteral("Markdown"), QStringLiteral("MindMap")} << false << false;
}
void TestTypstExport::allInOnePdfBeforeWrites() {
  QFETCH(QStringList, sourceTypes);
  QFETCH(bool, wkhtmltopdf);
  QFETCH(bool, singleFile);
  auto settings = option(ExportFormat::PDF);
  settings.m_pdfOption.m_allInOne = true;
  settings.m_pdfOption.m_useWkhtmltopdf = wkhtmltopdf;
  settings.m_pdfOption.m_wkhtmltopdfExePath = QStringLiteral("nonexistent-must-not-run");
  QVector<ExportFileInfo> inputs;
  for (const auto &type : sourceTypes)
    inputs.append(sourceFile(type));
  QVERIFY(!QFileInfo::exists(m_output));
  if (singleFile) {
    const auto &file = inputs.first();
    QVERIFY(m_exporter
                ->doExportFile(settings, QStringLiteral("= Live native PDF"), file.filePath,
                               file.fileName, file.resourcePath, {}, file.fileTypeName)
                .isEmpty());
  } else {
    QVERIFY(
        m_exporter->doExportBatch(settings, inputs, QStringLiteral("native all in one")).isEmpty());
  }
  QVERIFY(!QFileInfo::exists(m_output));
  QVERIFY2(m_logs.contains(QStringLiteral("All-in-one PDF is not supported for this selection.")),
           qPrintable(diagnostic()));
}
void TestTypstExport::mixedPdfBackends_data() {
  QTest::addColumn<QString>("peerType");
  QTest::addColumn<bool>("nativeFirst");
  for (const auto &type : {QStringLiteral("Markdown"), QStringLiteral("MindMap")}) {
    QTest::newRow(qPrintable(type + QStringLiteral("-then-Typst"))) << type << false;
    QTest::newRow(qPrintable(QStringLiteral("Typst-then-") + type)) << type << true;
  }
}
void TestTypstExport::mixedPdfBackends() {
  QFETCH(QString, peerType);
  QFETCH(bool, nativeFirst);
  QVERIFY(writeBytes(m_main,
                     "#set page(width: 120pt, height: 80pt, margin: 8pt)\n#set text(size: 8pt)\n"
                     "= Native batch\n#image(\"images/nested/pixel.png\", width: 12pt)\n"));
  auto settings = option(ExportFormat::PDF);
  settings.m_pdfOption.m_useWkhtmltopdf = false;
  settings.m_pdfOption.m_allInOne = false;
  settings.m_pdfOption.m_wkhtmltopdfExePath = QStringLiteral("nonexistent-must-not-run");
  settings.m_pdfOption.m_layout = QSharedPointer<QPageLayout>::create(
      QPageSize(QSizeF(420, 600), QPageSize::Point, QStringLiteral("Peer print layout"),
                QPageSize::ExactMatch),
      QPageLayout::Portrait, QMarginsF(12, 12, 12, 12), QPageLayout::Point);
  QVector<ExportFileInfo> inputs{sourceFile(peerType), sourceFile(QStringLiteral("Typst"))};
  if (nativeFirst)
    std::swap(inputs[0], inputs[1]);
  const auto outputs =
      m_exporter->doExportBatch(settings, inputs, QStringLiteral("mixed PDF batch"));
  QVERIFY2(outputs.size() == 2, qPrintable(diagnostic()));
  QCOMPARE(files(m_output).size(), 2);
  QVERIFY(outputs[0] != outputs[1]);
  QCOMPARE(QFileInfo(outputs[0]).absolutePath(), QFileInfo(outputs[1]).absolutePath());
  const auto peerHeading = peerType + QStringLiteral(" batch heading");
  for (int index = 0; index < outputs.size(); ++index) {
    const bool native = inputs[index].fileTypeName == QStringLiteral("Typst");
    QCOMPARE(QFileInfo(outputs[index]).suffix(), QStringLiteral("pdf"));
    QPdfDocument document;
    QCOMPARE(document.load(outputs[index]), QPdfDocument::Error::None);
    QTRY_COMPARE(document.status(), QPdfDocument::Status::Ready);
    QVERIFY(document.pageCount() > 0);
    if (native)
      QCOMPARE(document.pageCount(), 1);
    QString text;
    int redPixels = 0;
    for (int page = 0; page < document.pageCount(); ++page) {
      const auto size = document.pagePointSize(page);
      const auto expected = native ? QSizeF(120, 80) : QSizeF(420, 600);
      QVERIFY2(qAbs(size.width() - expected.width()) < 1.0 &&
                   qAbs(size.height() - expected.height()) < 1.0,
               qPrintable(outputs[index]));
      text += document.getAllText(page).text() + QLatin1Char('\n');
      const auto rendered = document.render(page, (size * 2).toSize());
      QVERIFY(!rendered.isNull());
      for (int y = 0; y < rendered.height(); ++y)
        for (int x = 0; x < rendered.width(); ++x) {
          const auto pixel = rendered.pixelColor(x, y);
          redPixels += pixel.red() > 180 && pixel.green() < 80 && pixel.blue() < 80;
        }
    }
    const auto heading = native ? QStringLiteral("Native batch") : peerHeading;
    QVERIFY2(text.simplified().contains(heading), qPrintable(text));
    QVERIFY(!text.contains(native ? peerHeading : QStringLiteral("Native batch")));
    QVERIFY2(redPixels > 50, "Both PDF backends must retain the real relative image");
  }
}
void TestTypstExport::sourceSnapshotMediaAndAttachments() {
  const QString source = QStringLiteral(
      "= Live snapshot\r\n#image(\"images/nested/pixel.png\")\r#image(\"literal%20.png\")\n");
  auto settings = option(ExportFormat::Typst);
  settings.m_exportAttachments = true;
  const auto path = m_exporter->doExportFile(settings, source, m_main, QStringLiteral("main.typst"),
                                             m_project, m_attachments, QStringLiteral("Typst"));
  QVERIFY2(!path.isEmpty(), qPrintable(diagnostic()));
  QCOMPARE(bytes(path), source.toUtf8());
  const QDir bundle(QFileInfo(path).absolutePath());
  for (const auto &relative :
       {QStringLiteral("images/nested/pixel.png"), QStringLiteral("literal%20.png"),
        QStringLiteral("attachments/payload.bin")}) {
    QCOMPARE(bytes(bundle.filePath(relative)), bytes(m_project + QLatin1Char('/') + relative));
    QVERIFY(QFileInfo::exists(bundle.filePath(relative)));
  }
  QVERIFY(!QFileInfo::exists(bundle.filePath(QStringLiteral("literal .png"))));
  QCOMPARE(bytes(m_main), QByteArray("= Saved disk heading\n"));
}
void TestTypstExport::sourceEmptyAndRepeatedNames() {
  const auto empty = run(ExportFormat::Typst, QString());
  QVERIFY2(!empty.isEmpty(), qPrintable(diagnostic()));
  QVERIFY(QFileInfo::exists(empty));
  QCOMPARE(QFileInfo(empty).size(), 0);
  const auto first = run(ExportFormat::Typst, QStringLiteral("First"));
  QVERIFY2(!first.isEmpty(), qPrintable(diagnostic()));
  const auto secondProject = m_case->filePath(QStringLiteral("other/main.typst"));
  QVERIFY(writeBytes(secondProject, "Other disk"));
  const auto second = m_exporter->doExportFile(
      option(ExportFormat::Typst), QStringLiteral("Second"), secondProject,
      QStringLiteral("main.typst"), QFileInfo(secondProject).absolutePath(), {},
      QStringLiteral("Typst"));
  QVERIFY2(!second.isEmpty(), qPrintable(diagnostic()));
  QVERIFY(first != second && first != empty && second != empty);
  QVERIFY(QFileInfo(first).absolutePath() != QFileInfo(second).absolutePath());
  QCOMPARE(bytes(first), QByteArray("First"));
  QCOMPARE(bytes(second), QByteArray("Second"));
  QCOMPARE(QFileInfo(empty).size(), 0);
}
void TestTypstExport::dynamicAndMissingImages() {
  const QString source = QStringLiteral("#let path = "
                                        "\"dynamic.png\"\n#image(path)\n#image(\"images/nested/"
                                        "pixel.png\")\n#image(\"missing.png\")");
  const auto path = run(ExportFormat::Typst, source);
  QVERIFY2(!path.isEmpty(), qPrintable(diagnostic()));
  QCOMPARE(bytes(path), source.toUtf8());
  QVERIFY(diagnostic().contains(QStringLiteral("Typst image references may be incomplete")));
  QVERIFY(diagnostic().contains(QStringLiteral("missing.png")));
  const QDir bundle(QFileInfo(path).absolutePath());
  QCOMPARE(bytes(bundle.filePath(QStringLiteral("images/nested/pixel.png"))),
           bytes(m_project + QStringLiteral("/images/nested/pixel.png")));
  QVERIFY(!QFileInfo::exists(bundle.filePath(QStringLiteral("attachments"))));
  QVERIFY(!QFileInfo::exists(bundle.filePath(QStringLiteral("dynamic.png"))));
}
void TestTypstExport::sourcePathFiltering() {
  const auto outside = m_case->filePath(QStringLiteral("outside.png"));
  QVERIFY(QFile::copy(m_project + QStringLiteral("/literal%20.png"), outside));
  QString absolute = outside;
  absolute.replace(QLatin1Char('\\'), QStringLiteral("\\\\"));
  absolute.replace(QLatin1Char('"'), QStringLiteral("\\\""));
  const QString source =
      QStringLiteral("#image(\"../outside.png\")\n#image(\"https://example.invalid/x.png\")\n"
                     "#image(\"data:image/png;base64,AAAA\")\n#image(\"%1\")\n"
                     "#image(\"images/../literal%20.png\")")
          .arg(absolute);
  const auto path = run(ExportFormat::Typst, source);
  QVERIFY2(!path.isEmpty(), qPrintable(diagnostic()));
  QCOMPARE(files(QFileInfo(path).absolutePath()).size(), 2);
  QCOMPARE(bytes(QFileInfo(path).absolutePath() + QStringLiteral("/literal%20.png")),
           bytes(outside));
  QCOMPARE(bytes(outside), bytes(m_project + QStringLiteral("/literal%20.png")));
}
void TestTypstExport::sourceFailures() {
  // Missing application runtime fails the actual scanner path, not a renderer double.
  ServiceLocator unavailable;
  ExportRendererFactory factory(unavailable);
  registerBuiltinExportRenderers(factory);
  Exporter exporter(unavailable, factory, nullptr);
  const auto failed = exporter.doExportFile(
      option(ExportFormat::Typst), QStringLiteral("#image(\"literal%20.png\")"), m_main,
      QStringLiteral("main.typst"), m_project, {}, QStringLiteral("Typst"));
  QVERIFY(failed.isEmpty());
  QVERIFY(QDir(m_output).entryList(QDir::NoDotAndDotDot | QDir::AllEntries).isEmpty());

  TypstExporter renderer(m_services, nullptr, nullptr);
  const auto destination = m_case->filePath(QStringLiteral("collision/main.typst"));
  const auto existing = m_case->filePath(QStringLiteral("collision/literal%20.png"));
  QVERIFY(writeBytes(existing, "do not overwrite"));
  QVERIFY(!renderer.renderSource(input(QStringLiteral("#image(\"literal%20.png\")")),
                                 ExportFormat::Typst, destination));
  QVERIFY(!QFileInfo::exists(destination));
  QCOMPARE(bytes(existing), QByteArray("do not overwrite"));
  const auto blocker = m_case->filePath(QStringLiteral("blocked"));
  QVERIFY(writeBytes(blocker, "not a directory"));
  QVERIFY(!renderer.renderSource(input(QStringLiteral("#image(\"literal%20.png\")")),
                                 ExportFormat::Typst, blocker + QStringLiteral("/main.typst")));
  QCOMPARE(bytes(blocker), QByteArray("not a directory"));
  auto settings = option(ExportFormat::Typst);
  settings.m_outputDir = blocker;
  QVERIFY(m_exporter
              ->doExportFile(settings, QStringLiteral("source"), m_main,
                             QStringLiteral("main.typst"), m_project, {}, QStringLiteral("Typst"))
              .isEmpty());
  ExportHtmlRequest request;
  request.options = option(ExportFormat::HTML);
  QVERIFY(!renderer.renderHtml(input(QStringLiteral("= Cannot commit HTML")), request,
                               blocker + QStringLiteral("/main.html")));
  QVERIFY(!renderer.renderPdf(input(QStringLiteral("= Cannot commit PDF")),
                              option(ExportFormat::PDF), blocker + QStringLiteral("/main.pdf")));

  // The literal image creates a directory exactly where the source must commit.
  // The failed QSaveFile must remove the whole freshly allocated export bundle.
  QVERIFY(QDir().mkpath(m_project + QStringLiteral("/clash.typst")));
  QVERIFY(QFile::copy(m_project + QStringLiteral("/literal%20.png"),
                      m_project + QStringLiteral("/clash.typst/pixel.png")));
  QVERIFY(run(ExportFormat::Typst, QStringLiteral("#image(\"clash.typst/pixel.png\")"),
              QStringLiteral("clash.typst"))
              .isEmpty());
  QVERIFY(QDir(m_output).entryList(QDir::NoDotAndDotDot | QDir::AllEntries).isEmpty());

  // Exercise the production scanner's bounded wait and failure diagnostic.
  RuntimeLaneGate scanner(*m_runtime, true);
  QVERIFY(scanner.entered());
  m_logs.clear();
  QVERIFY(run(ExportFormat::Typst, QStringLiteral("#image(\"literal%20.png\")")).isEmpty());
  QVERIFY(!diagnostic().isEmpty());
  QVERIFY(QDir(m_output).entryList(QDir::NoDotAndDotDot | QDir::AllEntries).isEmpty());
  scanner.release();
  QVERIFY2(!run(ExportFormat::Typst, QStringLiteral("Recovered source")).isEmpty(),
           qPrintable(diagnostic()));
}
void TestTypstExport::nativeHtmlAndPdf() {
  const QString source = QStringLiteral(
      "#import \"part.typst\": answer\n"
      "#set page(width: 120pt, height: 80pt, margin: 8pt)\n#set text(size: 8pt)\n"
      "= Live native heading\n#image(\"images/nested/pixel.png\", width: 12pt)\n$answer$\n");
  const auto html = run(ExportFormat::HTML, source);
  QVERIFY2(!html.isEmpty(), qPrintable(diagnostic()));
  // Native PDF must bypass even an unusable, explicitly enabled HTML stylesheet.
  QVERIFY(QFile::remove(m_stylesheet));
  QVERIFY(QDir().mkpath(m_stylesheet));
  auto settings = option(ExportFormat::PDF);
  settings.m_htmlOption.m_applyCustomizedStyles = true;
  settings.m_pdfOption.m_useWkhtmltopdf = true;
  settings.m_pdfOption.m_wkhtmltopdfExePath = QStringLiteral("nonexistent-must-not-run");
  const auto pdf = m_exporter->doExportFile(settings, source, m_main, QStringLiteral("main.typst"),
                                            m_project, {}, QStringLiteral("Typst"));
  QVERIFY2(!pdf.isEmpty(), qPrintable(diagnostic()));
  QVERIFY(QFileInfo(m_stylesheet).isDir());
  QPdfDocument document;
  QCOMPARE(document.load(pdf), QPdfDocument::Error::None);
  QTRY_COMPARE(document.status(), QPdfDocument::Status::Ready);
  QCOMPARE(document.pageCount(), 1);
  const auto size = document.pagePointSize(0);
  QVERIFY(qAbs(size.width() - 120.0) < 0.1);
  QVERIFY(qAbs(size.height() - 80.0) < 0.1);
  const auto text = document.getAllText(0).text();
  QVERIFY2(text.simplified().contains(QStringLiteral("Live native heading")), qPrintable(text));
  QVERIFY(!text.contains(QStringLiteral("Saved disk")));
  const auto image = document.render(0, QSize(480, 320));
  QVERIFY(!image.isNull());
  int red = 0;
  for (int y = 0; y < image.height(); ++y)
    for (int x = 0; x < image.width(); ++x) {
      const auto pixel = image.pixelColor(x, y);
      red += pixel.red() > 180 && pixel.green() < 80 && pixel.blue() < 80;
    }
  QVERIFY2(red > 300, "The authored red image must be visible in the rendered native PDF");
  QVERIFY(QDir().rename(m_project, m_project + QStringLiteral("-retired")));
  QWebEngineView browser;
  QSignalSpy loaded(&browser, &QWebEngineView::loadFinished);
  browser.resize(640, 480);
  browser.show();
  browser.load(QUrl::fromLocalFile(html));
  QTRY_VERIFY_WITH_TIMEOUT(!loaded.isEmpty(), 30000);
  QVERIFY(loaded.last().at(0).toBool());
  QVERIFY(evaluate(browser.page(),
                   QStringLiteral("Array.from(document.querySelectorAll('h1,h2,h3')).some(e => "
                                  "e.textContent.includes('Live native heading'))"))
              .toBool());
  QVERIFY(
      !evaluate(browser.page(), QStringLiteral("document.body.textContent.includes('Saved disk')"))
           .toBool());
  QTRY_VERIFY_WITH_TIMEOUT(
      evaluate(browser.page(),
               QStringLiteral("document.images.length > 0 && Array.from(document.images).every(i "
                              "=> i.complete && i.naturalWidth > 0)"))
          .toBool(),
      15000);
  QVERIFY(evaluate(browser.page(),
                   QStringLiteral("document.querySelector('math, .typst-math, [role=math]') !== "
                                  "null || Array.from(document.querySelectorAll('svg')).some(e => "
                                  "(e.getAttribute('aria-label') || '').length > 0)"))
              .toBool());
}
void TestTypstExport::htmlOnlyAndMimeRejection() {
  const auto html =
      run(ExportFormat::HTML, QStringLiteral("#html.elem(\"article\")[HTML-only live content]"));
  QVERIFY2(!html.isEmpty(), qPrintable(diagnostic()));
  QVERIFY(bytes(html).contains("<article"));
  QVERIFY(bytes(html).contains("HTML-only live content"));
  auto settings = option(ExportFormat::HTML);
  settings.m_htmlOption.m_useMimeHtmlFormat = true;
  QVERIFY(m_exporter
              ->doExportFile(settings, QStringLiteral("text"), m_main, QStringLiteral("main.typst"),
                             m_project, {}, QStringLiteral("Typst"))
              .isEmpty());
  for (const auto &path : files(m_output))
    QVERIFY(!path.endsWith(QStringLiteral(".mht")));
}
void TestTypstExport::sharedCssDefaultGeometry_data() {
  QTest::addColumn<QString>("sourceType");
  for (const auto &type :
       {QStringLiteral("Typst"), QStringLiteral("MindMap"), QStringLiteral("Text"),
        QStringLiteral("Others"), QStringLiteral("Unregistered"), QStringLiteral("Markdown")})
    QTest::newRow(qPrintable(type)) << type;
}
void TestTypstExport::sharedCssDefaultGeometry() {
  QFETCH(QString, sourceType);
  auto settings = option(ExportFormat::HTML);
  // Markdown opts in here; its absent-choice default is covered separately.
  if (sourceType == QStringLiteral("Markdown"))
    settings.m_htmlOption.m_applyCustomizedStyles = true;
  const auto file = sourceFile(sourceType);
  QVERIFY(!QFileInfo::exists(m_stylesheet));
  const auto html =
      m_exporter->doExportFile(settings, QString::fromUtf8(bytes(file.filePath)), file.filePath,
                               file.fileName, file.resourcePath, {}, sourceType);
  QVERIFY2(!html.isEmpty(), qPrintable(diagnostic()));
  QVERIFY(QFileInfo(m_stylesheet).isFile());
  const auto bundled = bytes(QStringLiteral(":/vnotex/export/css/export.css"));
  QVERIFY(!bundled.isEmpty());
  QCOMPARE(bytes(m_stylesheet), bundled);
  QWebEngineView browser;
  QVERIFY(loadHtml(browser, html));
  verifyCenteredBody(browser, 80);
}

void TestTypstExport::sharedCssPolicy_data() {
  QTest::addColumn<QStringList>("sourceTypes");
  QTest::addColumn<int>("choice");
  QTest::addColumn<int>("entry");
  QTest::addColumn<bool>("enabled");
  const auto addChoices = [](const QStringList &types, int entry, bool defaultEnabled) {
    for (const int choice : {-1, 0, 1}) {
      const auto name =
          types.join(QLatin1Char('-')) + QStringLiteral("-entry%1-choice%2").arg(entry).arg(choice);
      QTest::newRow(qPrintable(name))
          << types << choice << entry << (choice < 0 ? defaultEnabled : choice != 0);
    }
  };
  // Every factory family, including its case-insensitive lookup and Others fallback.
  for (const auto &type :
       {QStringLiteral("Typst"), QStringLiteral("MindMap"), QStringLiteral("Text"),
        QStringLiteral("Others"), QStringLiteral("Unregistered")})
    addChoices({type}, 0, true);
  addChoices({QStringLiteral("mArKdOwN")}, 0, false);
  const QStringList native{QStringLiteral("Typst"), QStringLiteral("MindMap"),
                           QStringLiteral("Text"), QStringLiteral("Others")};
  const QStringList markdownLast{QStringLiteral("Typst"), QStringLiteral("MindMap"),
                                 QStringLiteral("Markdown")};
  const QStringList markdownFirst{QStringLiteral("Markdown"), QStringLiteral("Typst")};
  addChoices(native, 1, true);
  addChoices(markdownLast, 1, false);
  addChoices(markdownFirst, 1, false);
  // Copy the actual Custom HTML intermediate, including both all-in-one source orders.
  addChoices({QStringLiteral("Typst")}, 2, true);
  addChoices({QStringLiteral("Markdown")}, 2, false);
  addChoices(native, 3, true);
  addChoices(markdownLast, 3, false);
  addChoices(markdownFirst, 3, false);
}
void TestTypstExport::sharedCssPolicy() {
  QFETCH(QStringList, sourceTypes);
  QFETCH(int, choice);
  QFETCH(int, entry);
  QFETCH(bool, enabled);
  const auto css = customizedCss(61);
  QVERIFY(writeBytes(m_stylesheet, css));
  auto custom = copyingCustomOption();
  custom.m_targetSuffix = QStringLiteral("html");
  custom.m_useHtmlInput = true;
  custom.m_allInOne = entry == 3;
  auto settings = option(entry >= 2 ? ExportFormat::Custom : ExportFormat::HTML);
  settings.m_customOption = &custom;
  if (choice >= 0)
    settings.m_htmlOption.m_applyCustomizedStyles = choice != 0;
  const auto originalChoice = settings.m_htmlOption.m_applyCustomizedStyles;
  QVector<ExportFileInfo> inputs;
  for (const auto &type : sourceTypes)
    inputs.append(sourceFile(type));
  QStringList outputs;
  if (entry == 0 || entry == 2) {
    const auto &file = inputs.first();
    const auto html =
        m_exporter->doExportFile(settings, QString::fromUtf8(bytes(file.filePath)), file.filePath,
                                 file.fileName, file.resourcePath, {}, file.fileTypeName);
    QVERIFY2(!html.isEmpty(), qPrintable(diagnostic()));
    outputs.append(html);
  } else {
    outputs = m_exporter->doExportBatch(settings, inputs, QStringLiteral("styled batch"));
    QCOMPARE(outputs.size(), entry == 3 ? 1 : inputs.size());
  }
  QVERIFY(settings.m_htmlOption.m_applyCustomizedStyles == originalChoice);
  QCOMPARE(bytes(m_stylesheet), css);
  QWebEngineView browser;
  for (const auto &html : outputs) {
    QVERIFY(loadHtml(browser, html));
    const auto layout = bodyLayout(browser);
    QVERIFY(!layout.isEmpty());
    QCOMPARE(layout.value(QStringLiteral("custom")).toString(),
             enabled ? QStringLiteral("applied") : QString());
    if (enabled)
      verifyCenteredBody(browser, 61);
  }
}

void TestTypstExport::sharedCssFreshAndEmpty() {
  const auto source = QStringLiteral("= Shared CSS snapshot");
  const auto firstCss = customizedCss(55);
  QVERIFY(writeBytes(m_stylesheet, firstCss));
  const auto first = run(ExportFormat::HTML, source);
  QVERIFY2(!first.isEmpty(), qPrintable(diagnostic()));
  QWebEngineView browser;
  QVERIFY(loadHtml(browser, first));
  verifyCenteredBody(browser, 55);
  const auto narrowerCss = customizedCss(37);
  QVERIFY(writeBytes(m_stylesheet, narrowerCss));
  const auto narrower = run(ExportFormat::HTML, source);
  QVERIFY2(!narrower.isEmpty(), qPrintable(diagnostic()));
  QVERIFY(loadHtml(browser, narrower));
  verifyCenteredBody(browser, 37);
  QCOMPARE(bytes(m_stylesheet), narrowerCss);
  // Existing exports are self-contained snapshots, not live links to the config directory.
  QVERIFY(loadHtml(browser, first));
  verifyCenteredBody(browser, 55);

  auto disabled = option(ExportFormat::HTML);
  disabled.m_htmlOption.m_applyCustomizedStyles = false;
  const auto baseline =
      m_exporter->doExportFile(disabled, source, m_main, QStringLiteral("main.typst"), m_project,
                               {}, QStringLiteral("Typst"));
  QVERIFY2(!baseline.isEmpty(), qPrintable(diagnostic()));
  QVERIFY(loadHtml(browser, baseline));
  const auto unstyledLayout = bodyLayout(browser);
  QVERIFY(!unstyledLayout.isEmpty());
  QVERIFY(writeBytes(m_stylesheet, {}));
  const auto empty = run(ExportFormat::HTML, source);
  QVERIFY2(!empty.isEmpty(), qPrintable(diagnostic()));
  QVERIFY(loadHtml(browser, empty));
  QCOMPARE(bodyLayout(browser), unstyledLayout);
  QVERIFY(QFileInfo(m_stylesheet).isFile());
  QCOMPARE(QFileInfo(m_stylesheet).size(), 0);
  // The Edit/ensure path must preserve the deliberately empty file too.
  QString error;
  QCOMPARE(ExportStyleSheet::ensureUserFile(*m_config, &error), m_stylesheet);
  QVERIFY2(error.isEmpty(), qPrintable(error));
  QCOMPARE(QFileInfo(m_stylesheet).size(), 0);
}

void TestTypstExport::sharedCssMissingAndDisabled() {
  auto settings = option(ExportFormat::HTML);
  settings.m_htmlOption.m_applyCustomizedStyles = false;
  const auto source = QStringLiteral("= Disabled custom CSS");
  const auto disabled =
      m_exporter->doExportFile(settings, source, m_main, QStringLiteral("main.typst"), m_project,
                               {}, QStringLiteral("Typst"));
  QVERIFY2(!disabled.isEmpty(), qPrintable(diagnostic()));
  QVERIFY(!QFileInfo::exists(m_stylesheet));
  QWebEngineView browser;
  QVERIFY(loadHtml(browser, disabled));
  const auto baseline = bodyLayout(browser);
  QVERIFY(!baseline.isEmpty());
  const auto enabled = run(ExportFormat::HTML, source);
  QVERIFY2(!enabled.isEmpty(), qPrintable(diagnostic()));
  QVERIFY(QFileInfo(m_stylesheet).isFile());
  QVERIFY(loadHtml(browser, enabled));
  verifyCenteredBody(browser, 80);
  QVERIFY(bodyLayout(browser) != baseline);
}

void TestTypstExport::sharedCssCollision_data() {
  QTest::addColumn<bool>("enabled");
  QTest::addColumn<bool>("customHtml");
  QTest::newRow("HTML-disabled") << false << false;
  QTest::newRow("HTML-enabled") << true << false;
  QTest::newRow("Custom-disabled") << false << true;
  QTest::newRow("Custom-enabled") << true << true;
}
void TestTypstExport::sharedCssCollision() {
  QFETCH(bool, enabled);
  QFETCH(bool, customHtml);
  QVERIFY(QDir().mkpath(m_stylesheet));
  const auto sentinel = QDir(m_stylesheet).filePath(QStringLiteral("keep.bin"));
  QVERIFY(writeBytes(sentinel, "preserved collision"));
  auto custom = copyingCustomOption();
  custom.m_targetSuffix = QStringLiteral("html");
  custom.m_useHtmlInput = true;
  auto settings = option(customHtml ? ExportFormat::Custom : ExportFormat::HTML);
  settings.m_customOption = &custom;
  settings.m_htmlOption.m_applyCustomizedStyles = enabled;
  const auto html = m_exporter->doExportFile(settings, QStringLiteral("= CSS collision"), m_main,
                                             QStringLiteral("main.typst"), m_project, {},
                                             QStringLiteral("Typst"));
  if (enabled) {
    QVERIFY(html.isEmpty());
    QVERIFY(files(m_output).isEmpty());
    QVERIFY(!diagnostic().isEmpty());
  } else {
    QVERIFY2(!html.isEmpty(), qPrintable(diagnostic()));
    QWebEngineView browser;
    QVERIFY(loadHtml(browser, html));
    QVERIFY(evaluate(browser.page(),
                     QStringLiteral("document.body.textContent.includes('CSS collision')"))
                .toBool());
  }
  QVERIFY(QFileInfo(m_stylesheet).isDir());
  QCOMPARE(bytes(sentinel), QByteArray("preserved collision"));
}

void TestTypstExport::sharedCssUnreadable() {
  QVERIFY(writeBytes(m_stylesheet, customizedCss(61)));
#ifdef Q_OS_WIN
  // Qt permission flags do not remove Windows read access; deny sharing on the actual file.
  const HANDLE locked =
      CreateFileW(reinterpret_cast<LPCWSTR>(m_stylesheet.utf16()), GENERIC_READ | GENERIC_WRITE, 0,
                  nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  QVERIFY(locked != INVALID_HANDLE_VALUE);
  const auto unlock = qScopeGuard([locked] { CloseHandle(locked); });
#else
  const auto permissions = QFileInfo(m_stylesheet).permissions();
  QVERIFY(QFile::setPermissions(m_stylesheet, QFileDevice::WriteOwner));
  const auto unlock =
      qScopeGuard([this, permissions] { QFile::setPermissions(m_stylesheet, permissions); });
#endif
  QFile probe(m_stylesheet);
#ifndef Q_OS_WIN
  if (probe.open(QIODevice::ReadOnly))
    QSKIP("This account bypasses file read permissions; directory collisions remain covered");
#else
  QVERIFY(!probe.open(QIODevice::ReadOnly));
#endif
  for (const bool enabled : {false, true}) {
    auto settings = option(ExportFormat::HTML);
    settings.m_outputDir += enabled ? QStringLiteral("/enabled") : QStringLiteral("/disabled");
    settings.m_htmlOption.m_applyCustomizedStyles = enabled;
    const auto html = m_exporter->doExportFile(settings, QStringLiteral("= Unreadable CSS"), m_main,
                                               QStringLiteral("main.typst"), m_project, {},
                                               QStringLiteral("Typst"));
    if (enabled) {
      QVERIFY(html.isEmpty());
      QVERIFY(files(settings.m_outputDir).isEmpty());
      QVERIFY(!diagnostic().isEmpty());
    } else {
      QVERIFY2(!html.isEmpty(), qPrintable(diagnostic()));
      QWebEngineView browser;
      QVERIFY(loadHtml(browser, html));
      QVERIFY(evaluate(browser.page(),
                       QStringLiteral("document.body.textContent.includes('Unreadable CSS')"))
                  .toBool());
    }
  }
}

void TestTypstExport::sharedCssHeadBoundary() {
  const auto html = m_case->filePath(QStringLiteral("head-literals.html"));
  const QByteArray source =
      "<!doctype html><html><HeAd data-literal=\"</head>\">"
      "<!-- </head><body id='comment-breakout'> -->"
      "<script data-literal='</head>'>window.headLiteral = '</head>';"
      "window.notAnEnd='</script\xc2\xa0></head>';window.lessThan=1<2;</script>"
      "<style>/* </head> */ body { max-width:95%; margin:0; box-sizing:content-box; }</style>"
      "<title>Literal </head> title</title></HeAd>"
      "<body><p id='content'>Preserved body</p></body></html>";
  const auto css =
      customizedCss(61) +
      QStringLiteral("/* </style></head><script>window.cssBreakout = true;</script> */\n"
                     "body { --closing-head: \"</style></head>\"; }\n"
                     "body::after { content: \"Saved Unicode λ\"; }\n")
          .toUtf8();
  QVERIFY(writeBytes(html, source));
  QVERIFY(writeBytes(m_stylesheet, css));
  QString error;
  QVERIFY2(ExportStyleSheet::apply(html, m_config.get(), &error), qPrintable(error));
  QCOMPARE(bytes(m_stylesheet), css);
  QWebEngineView browser;
  QVERIFY(loadHtml(browser, html));
  verifyCenteredBody(browser, 61);
  QCOMPARE(evaluate(browser.page(), QStringLiteral("document.title")).toString(),
           QStringLiteral("Literal </head> title"));
  QCOMPARE(evaluate(browser.page(), QStringLiteral("window.headLiteral")).toString(),
           QStringLiteral("</head>"));
  QCOMPARE(evaluate(browser.page(), QStringLiteral("window.notAnEnd")).toString(),
           QStringLiteral("</script\u00a0></head>"));
  QVERIFY(evaluate(browser.page(),
                   QStringLiteral(
                       "window.lessThan === true && typeof window.cssBreakout === 'undefined' && "
                       "!document.getElementById('comment-breakout') && "
                       "document.querySelectorAll('body').length === 1 && "
                       "document.getElementById('content').textContent === 'Preserved body'"))
              .toBool());
  QCOMPARE(
      evaluate(browser.page(),
               QStringLiteral(
                   "getComputedStyle(document.body).getPropertyValue('--closing-head').trim()"))
          .toString(),
      QStringLiteral("\"</style></head>\""));
  QVERIFY(evaluate(
              browser.page(),
              QStringLiteral(
                  "getComputedStyle(document.body, '::after').content.includes('Saved Unicode λ')"))
              .toBool());

  // The no-ConfigMgr path shares the resource default and does not touch a bad user path.
  QVERIFY(QFile::remove(m_stylesheet));
  QVERIFY(QDir().mkpath(m_stylesheet));
  const auto bundledHtml = m_case->filePath(QStringLiteral("bundled.html"));
  QVERIFY(writeBytes(bundledHtml, source));
  error.clear();
  QVERIFY2(ExportStyleSheet::apply(bundledHtml, nullptr, &error), qPrintable(error));
  QVERIFY(loadHtml(browser, bundledHtml));
  verifyCenteredBody(browser, 80);
  QVERIFY(QFileInfo(m_stylesheet).isDir());
}

void TestTypstExport::sharedCssInvalidHtml_data() {
  QTest::addColumn<QByteArray>("source");
  QTest::newRow("missing-head") << QByteArray("<html><body>Missing head</body></html>");
  QTest::newRow("unclosed-comment") << QByteArray("<html><head><!-- </head><body>Still a comment");
  QTest::newRow("unclosed-script") << QByteArray("<html><head><script>const literal = '</head>'; ");
}
void TestTypstExport::sharedCssInvalidHtml() {
  QFETCH(QByteArray, source);
  const auto html = m_case->filePath(QStringLiteral("invalid.html"));
  QVERIFY(writeBytes(html, source));
  const auto css = customizedCss(61);
  QVERIFY(writeBytes(m_stylesheet, css));
  QString error;
  QVERIFY(!ExportStyleSheet::apply(html, m_config.get(), &error));
  QVERIFY(!error.isEmpty());
  QCOMPARE(bytes(html), source);
  QCOMPARE(bytes(m_stylesheet), css);
}

void TestTypstExport::compiledFailures_data() {
  QTest::addColumn<int>("format");
  QTest::addColumn<QString>("source");
  for (const auto format : {ExportFormat::HTML, ExportFormat::PDF}) {
    const auto prefix = exportFormatString(format).toLatin1();
    QTest::newRow((prefix + "-syntax").constData()) << int(format) << QStringLiteral("#let = (");
    QTest::newRow((prefix + "-import").constData())
        << int(format) << QStringLiteral("#import \"absent.typst\": answer\n#answer");
  }
}
void TestTypstExport::compiledFailures() {
  QFETCH(int, format);
  QFETCH(QString, source);
  QVERIFY(run(ExportFormat(format), source).isEmpty());
  QVERIFY(files(m_output).isEmpty());
  QVERIFY(!diagnostic().isEmpty());
  const auto good = run(ExportFormat(format), QStringLiteral("= Recovered live heading"));
  QVERIFY2(!good.isEmpty(), qPrintable(diagnostic()));
  QCOMPARE(files(m_output).size(), 1);
}
void TestTypstExport::cancellationThenExport() {
  RuntimeLaneGate gate(*m_runtime);
  QVERIFY(gate.entered());
  bool stopped = false;
  QTimer::singleShot(0, m_exporter.get(), [&] {
    stopped = true;
    m_exporter->stop();
  });
  const auto canceled = run(ExportFormat::HTML, QStringLiteral("= Retired heading"));
  QVERIFY(stopped);
  QVERIFY(canceled.isEmpty());
  QVERIFY(files(m_output).isEmpty());
  gate.release();
  const auto later = run(ExportFormat::HTML, QStringLiteral("= Later heading"));
  QVERIFY2(!later.isEmpty(), qPrintable(diagnostic()));
  QVERIFY(bytes(later).contains("Later heading"));
  QVERIFY(!bytes(later).contains("Retired heading"));
  QCOMPARE(files(m_output).size(), 1);
}
void TestTypstExport::customDiskAndLiveHtml() {
  auto custom = copyingCustomOption();
  custom.m_targetSuffix = QStringLiteral("html");
  auto settings = option(ExportFormat::Custom);
  settings.m_customOption = &custom;
  settings.m_htmlOption.m_applyCustomizedStyles = true;
  QVERIFY(QDir().mkpath(m_stylesheet));
  custom.m_useHtmlInput = false;
  const auto raw = m_exporter->doExportFile(settings, QStringLiteral("= Live custom heading"),
                                            m_main, QStringLiteral("main.typst"), m_project, {},
                                            QStringLiteral("Typst"));
  QVERIFY2(!raw.isEmpty(), qPrintable(diagnostic()));
  QCOMPARE(bytes(raw), bytes(m_main));
  QVERIFY(QFileInfo(m_stylesheet).isDir());
  QVERIFY(QDir().rmdir(m_stylesheet));
  custom.m_useHtmlInput = true;
  const auto html = m_exporter->doExportFile(settings, QStringLiteral("= Live custom heading"),
                                             m_main, QStringLiteral("main.typst"), m_project, {},
                                             QStringLiteral("Typst"));
  QVERIFY2(!html.isEmpty(), qPrintable(diagnostic()));
  QVERIFY(bytes(html).contains("Live custom heading"));
  QVERIFY(bytes(html).contains("<h"));
  QVERIFY(!bytes(html).contains("Saved disk heading"));
  QCOMPARE(bytes(raw), bytes(m_main));
  QWebEngineView browser;
  QVERIFY(loadHtml(browser, html));
  verifyCenteredBody(browser, 80);
}

void TestTypstExport::customMixedAllInOne_data() {
  QTest::addColumn<bool>("htmlInput");
  QTest::addColumn<QString>("firstType");
  for (const bool htmlInput : {false, true}) {
    for (const auto &type :
         {QStringLiteral("Typst"), QStringLiteral("Markdown"), QStringLiteral("MindMap")}) {
      const auto name = (htmlInput ? QStringLiteral("HTML-") : QStringLiteral("raw-")) + type;
      QTest::newRow(qPrintable(name)) << htmlInput << type;
    }
  }
}
void TestTypstExport::customMixedAllInOne() {
  QFETCH(bool, htmlInput);
  QFETCH(QString, firstType);
  auto custom = copyingCustomOption();
  custom.m_useHtmlInput = htmlInput;
  custom.m_allInOne = true;
  auto settings = option(ExportFormat::Custom);
  settings.m_customOption = &custom;
  QVector<ExportFileInfo> inputs{sourceFile(firstType)};
  for (const auto &type :
       {QStringLiteral("Typst"), QStringLiteral("Markdown"), QStringLiteral("MindMap")}) {
    if (type != firstType)
      inputs.append(sourceFile(type));
  }
  // Rotate the first input: the real child command must expose each source's own renderer,
  // and still receive readable paths for the other two members of the heterogeneous batch.
  const auto outputs =
      m_exporter->doExportBatch(settings, inputs, QStringLiteral("custom mixed batch"));
  QVERIFY2(outputs.size() == 1, qPrintable(diagnostic()));
  QCOMPARE(files(m_output), outputs);
  QCOMPARE(QFileInfo(outputs.first()).suffix(), QStringLiteral("copied"));
  const auto copied = bytes(outputs.first());
  if (!htmlInput) {
    QCOMPARE(copied, bytes(inputs.first().filePath));
  } else if (firstType == QStringLiteral("Typst")) {
    QVERIFY(copied.contains("<h"));
    QVERIFY(copied.contains("Saved disk heading"));
    QVERIFY(copied != bytes(m_main));
  } else if (firstType == QStringLiteral("Markdown")) {
    QVERIFY(copied.contains("<h1"));
    QVERIFY(copied.contains("Markdown batch heading"));
    QVERIFY(copied != bytes(inputs.first().filePath));
  } else {
    QVERIFY(copied.contains("<section id=\"visual-map\">"));
    QVERIFY(copied.contains("MindMap batch heading"));
    QVERIFY(copied != bytes(inputs.first().filePath));
  }
  QCOMPARE(bytes(m_main), QByteArray("= Saved disk heading\n"));
}
void TestTypstExport::customAllInOnePreparationFailure_data() {
  QTest::addColumn<bool>("invalidSource");
  QTest::addColumn<bool>("brokenFirst");
  QTest::newRow("missing-first") << false << true;
  QTest::newRow("missing-last") << false << false;
  QTest::newRow("invalid-first") << true << true;
  QTest::newRow("invalid-last") << true << false;
}
void TestTypstExport::customAllInOnePreparationFailure() {
  QFETCH(bool, invalidSource);
  QFETCH(bool, brokenFirst);
  auto custom = copyingCustomOption();
  custom.m_useHtmlInput = true;
  custom.m_allInOne = true;
  auto settings = option(ExportFormat::Custom);
  settings.m_customOption = &custom;
  QVector<ExportFileInfo> inputs{sourceFile(QStringLiteral("Typst")),
                                 sourceFile(QStringLiteral("Markdown"))};
  if (invalidSource) {
    QVERIFY(writeBytes(m_main, "#let = ("));
  } else {
    QVERIFY(QFile::remove(inputs[1].filePath));
    std::swap(inputs[0], inputs[1]);
  }
  if (!brokenFirst)
    std::swap(inputs[0], inputs[1]);
  QVERIFY(m_exporter->doExportBatch(settings, inputs, QStringLiteral("incomplete custom batch"))
              .isEmpty());
  QVERIFY(files(m_output).isEmpty());
  QVERIFY(!diagnostic().isEmpty());
  QVERIFY(!diagnostic().contains(QStringLiteral("Custom command:")));
}
void TestTypstExport::customOptionSnapshot_data() {
  QTest::addColumn<int>("entry");
  QTest::newRow("public-file") << 0;
  QTest::newRow("public-batch-per-file") << 1;
  QTest::newRow("public-batch-all-in-one") << 2;
}
void TestTypstExport::customOptionSnapshot() {
  QFETCH(int, entry);
  auto custom = copyingCustomOption();
  custom.m_useHtmlInput = true;
  custom.m_allInOne = entry == 2;
  auto settings = option(ExportFormat::Custom);
  settings.m_customOption = &custom;
  auto replacement = copyingCustomOption();
  replacement.m_command = QStringLiteral("nonexistent-must-not-run");
  replacement.m_targetSuffix = QStringLiteral("changed");
  replacement.m_useHtmlInput = false;
  RuntimeLaneGate gate(*m_runtime);
  QVERIFY(gate.entered());
  bool mutated = false;
  QObject timerContext;
  QTimer::singleShot(0, &timerContext, [&] {
    // The accepted native compile cannot finish before this pumped event. Change both
    // the pointed scheme and its owner's selected pointer, just as a UI scheme edit can.
    custom = replacement;
    settings.m_customOption = &replacement;
    mutated = true;
    gate.release();
  });
  QStringList outputs;
  if (entry == 0) {
    const auto path = m_exporter->doExportFile(settings, QStringLiteral("= Live captured scheme"),
                                               m_main, QStringLiteral("main.typst"), m_project, {},
                                               QStringLiteral("Typst"));
    if (!path.isEmpty())
      outputs.append(path);
  } else {
    outputs = m_exporter->doExportBatch(
        settings, {sourceFile(QStringLiteral("Typst")), sourceFile(QStringLiteral("Markdown"))},
        QStringLiteral("captured custom scheme"));
  }
  QVERIFY(mutated);
  QVERIFY2(outputs.size() == (entry == 1 ? 2 : 1), qPrintable(diagnostic()));
  QCOMPARE(files(m_output).size(), outputs.size());
  for (const auto &path : outputs)
    QCOMPARE(QFileInfo(path).suffix(), QStringLiteral("copied"));
  const auto native = bytes(outputs.first());
  QVERIFY(native.contains("<h"));
  QVERIFY(native.contains(entry == 0 ? "Live captured scheme" : "Saved disk heading"));
  QVERIFY(native != bytes(m_main));
  if (entry == 1) {
    const auto markdown = bytes(outputs[1]);
    QVERIFY(markdown.contains("<h1"));
    QVERIFY(markdown.contains("Markdown batch heading"));
    QVERIFY(markdown != bytes(sourceFile(QStringLiteral("Markdown")).filePath));
  }
  QVERIFY(!diagnostic().contains(QStringLiteral("nonexistent-must-not-run")));
}

int main(int argc, char **argv) {
  // This mode intentionally precedes QApplication and QtTest initialization.
  if (argc >= 2 && QByteArray(argv[1]) == "--copy-export-input") {
    if (argc < 4)
      return 2;
    // %1 expands to several quoted paths for all-in-one. Preserve the first input verbatim,
    // but require every supplied input to exist and be readable before accepting the command.
    for (int index = 2; index < argc - 1; ++index) {
      QFile input(QString::fromLocal8Bit(argv[index]));
      if (!QFileInfo(input).isFile() || !input.open(QIODevice::ReadOnly))
        return 1;
    }
    return QFile::copy(QString::fromLocal8Bit(argv[2]), QString::fromLocal8Bit(argv[argc - 1])) ? 0
                                                                                                : 1;
  }
  QTemporaryDir storage;
  if (!storage.isValid())
    return 1;
  const auto temporaryRoot = storage.path().toLocal8Bit();
  qputenv("TMP", temporaryRoot);
  qputenv("TEMP", temporaryRoot);
  qputenv("TMPDIR", temporaryRoot);
  QCoreApplication::setAttribute(Qt::AA_ShareOpenGLContexts);
  WebEngineProfileService::registerProtectedScheme();
#if QT_VERSION >= QT_VERSION_CHECK(6, 9, 0)
  VxPdfSchemeHandler::registerScheme();
#endif
  QApplication application(argc, argv);
  TestTypstExport test;
  return QTest::qExec(&test, argc, argv);
}

#include "test_typstexport.moc"
