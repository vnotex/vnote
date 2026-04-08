#include <QtTest>

#include <QFile>
#include <QFileInfo>
#include <QMetaObject>
#include <QSignalSpy>
#include <QWidget>

#include <cstring>

#include <controllers/exportcontroller.h>
#include <core/buffer/filetypehelper.h>
#include <core/servicelocator.h>
#include <core/services/filetypecoreservice.h>
#include <core/services/notebookcoreservice.h>
#include <export/exportdata.h>
#include <export/exporter.h>
#include <export/webviewexporter.h>
#include <temp_dir_fixture.h>
#include <utils/contentmediautils.h>
#include <utils/processutils.h>

#include <vxcore/vxcore.h>

namespace tests {

namespace vnotex_test_stubs {} // namespace vnotex_test_stubs

} // namespace tests

namespace vnotex {

bool FileType::isMarkdown() const { return m_type == Type::Markdown; }

WebViewExporter::WebViewExporter(ServiceLocator &p_services, QWidget *p_parent)
    : QObject(p_parent), m_services(p_services) {}

WebViewExporter::~WebViewExporter() = default;

const QMetaObject WebViewExporter::staticMetaObject = QObject::staticMetaObject;

const QMetaObject *WebViewExporter::metaObject() const { return &QObject::staticMetaObject; }

void *WebViewExporter::qt_metacast(const char *p_clname) {
  if (!p_clname) {
    return nullptr;
  }
  if (!std::strcmp(p_clname, "vnotex::WebViewExporter")) {
    return static_cast<void *>(this);
  }
  return QObject::qt_metacast(p_clname);
}

int WebViewExporter::qt_metacall(QMetaObject::Call p_call, int p_id, void **p_args) {
  return QObject::qt_metacall(p_call, p_id, p_args);
}

bool WebViewExporter::doExport(const ExportOption &, const QString &, const QString &,
                               const QString &, const QString &, const QString &) {
  return false;
}

void WebViewExporter::prepare(const ExportOption &) {}

void WebViewExporter::clear() {}

void WebViewExporter::stop() {}

bool WebViewExporter::htmlToPdfViaWkhtmltopdf(const ExportPdfOption &, const QStringList &,
                                              const QString &) {
  return false;
}

void ContentMediaUtils::copyMediaFiles(const QString &, INotebookBackend *, const QString &) {}

ProcessUtils::State ProcessUtils::start(const QString &, const QStringList &,
                                        const std::function<void(const QString &)> &,
                                        const bool &) {
  return State::Succeeded;
}

ProcessUtils::State
ProcessUtils::start(const QString &, const std::function<void(const QString &)> &, const bool &) {
  return State::Succeeded;
}

Exporter::Exporter(ServiceLocator &p_services, QWidget *p_parent)
    : QObject(p_parent), m_services(p_services) {}

const QMetaObject Exporter::staticMetaObject = QObject::staticMetaObject;

const QMetaObject *Exporter::metaObject() const { return &QObject::staticMetaObject; }

void *Exporter::qt_metacast(const char *p_clname) {
  if (!p_clname) {
    return nullptr;
  }
  if (!std::strcmp(p_clname, "vnotex::Exporter")) {
    return static_cast<void *>(this);
  }
  return QObject::qt_metacast(p_clname);
}

int Exporter::qt_metacall(QMetaObject::Call p_call, int p_id, void **p_args) {
  return QObject::qt_metacall(p_call, p_id, p_args);
}

void Exporter::progressUpdated(int, int) {}

void Exporter::logRequested(const QString &) {}

QString Exporter::doExportFile(const ExportOption &p_option, const QString &p_content,
                               const QString &p_filePath, const QString &p_fileName,
                               const QString &, const QString &, bool) {
  if (!QDir().mkpath(p_option.m_outputDir)) {
    return QString();
  }

  QString outputName = p_fileName;
  if (outputName.isEmpty()) {
    outputName = QFileInfo(p_filePath).fileName();
  }
  if (outputName.isEmpty()) {
    outputName = QStringLiteral("export.md");
  }

  const QString outputPath = QDir(p_option.m_outputDir).filePath(outputName);
  QFile outFile(outputPath);
  if (!outFile.open(QIODevice::WriteOnly)) {
    return QString();
  }

  if (!p_content.isEmpty()) {
    outFile.write(p_content.toUtf8());
  } else {
    QFile srcFile(p_filePath);
    if (srcFile.open(QIODevice::ReadOnly)) {
      outFile.write(srcFile.readAll());
    }
  }
  outFile.close();
  return outputPath;
}

QStringList Exporter::doExportBatch(const ExportOption &, const QVector<ExportFileInfo> &,
                                    const QString &) {
  return QStringList();
}

void Exporter::stop() {}

} // namespace vnotex

namespace tests {

class TestExportController : public QObject {
  Q_OBJECT

private slots:
  void initTestCase();
  void cleanupTestCase();

  void testConstruction();
  void testMarkdownExportContentBased();
  void testMarkdownExportDiskBased();
  void testEmptyContextHandling();

private:
  struct ControllerFixture {
    vnotex::ServiceLocator services;
    vnotex::NotebookCoreService *notebookService = nullptr;
    vnotex::FileTypeCoreService *fileTypeService = nullptr;
    vnotex::ExportController *controller = nullptr;

    explicit ControllerFixture(VxCoreContextHandle p_ctx) {
      notebookService = new vnotex::NotebookCoreService(p_ctx);
      fileTypeService = new vnotex::FileTypeCoreService(p_ctx, QStringLiteral("en_US"));
      services.registerService<vnotex::NotebookCoreService>(notebookService);
      services.registerService<vnotex::FileTypeCoreService>(fileTypeService);
      controller = new vnotex::ExportController(services);
    }

    ~ControllerFixture() {
      delete controller;
      delete fileTypeService;
      delete notebookService;
    }
  };

  bool createNotebookFile(const QString &p_relativePath);
  bool writeContentViaBuffer(const QString &p_relativePath, const QString &p_content);

  VxCoreContextHandle m_ctx = nullptr;
  TempDirFixture m_tempDir;
  QString m_notebookId;
};

void TestExportController::initTestCase() {
  vxcore_set_test_mode(1);
  vxcore_set_app_info("VNote", "VNoteTestExportController");

  VxCoreError err = vxcore_context_create(nullptr, &m_ctx);
  QVERIFY2(err == VXCORE_OK, "Failed to create vxcore context");
  QVERIFY(m_ctx != nullptr);
  QVERIFY(m_tempDir.isValid());

  char *notebookId = nullptr;
  QByteArray notebookPath = m_tempDir.filePath(QStringLiteral("controller_notebook")).toUtf8();
  err = vxcore_notebook_create(m_ctx, notebookPath.constData(), "{\"name\":\"ControllerNotebook\"}",
                               VXCORE_NOTEBOOK_BUNDLED, &notebookId);
  QVERIFY2(err == VXCORE_OK, "Failed to create test notebook");
  QVERIFY(notebookId != nullptr);
  m_notebookId = QString::fromUtf8(notebookId);
  vxcore_string_free(notebookId);
}

void TestExportController::cleanupTestCase() {
  if (m_ctx) {
    vxcore_context_destroy(m_ctx);
    m_ctx = nullptr;
  }
}

bool TestExportController::createNotebookFile(const QString &p_relativePath) {
  char *fileId = nullptr;
  const QByteArray notebookId = m_notebookId.toUtf8();
  const QByteArray relativePath = p_relativePath.toUtf8();
  const VxCoreError err =
      vxcore_file_create(m_ctx, notebookId.constData(), "", relativePath.constData(), &fileId);
  if (fileId) {
    vxcore_string_free(fileId);
  }
  return err == VXCORE_OK;
}

bool TestExportController::writeContentViaBuffer(const QString &p_relativePath,
                                                 const QString &p_content) {
  char *bufferId = nullptr;
  const QByteArray notebookId = m_notebookId.toUtf8();
  const QByteArray relativePath = p_relativePath.toUtf8();

  VxCoreError err =
      vxcore_buffer_open(m_ctx, notebookId.constData(), relativePath.constData(), &bufferId);
  if (err != VXCORE_OK || !bufferId) {
    return false;
  }

  const QByteArray content = p_content.toUtf8();
  err = vxcore_buffer_set_content_raw(m_ctx, bufferId, content.constData(),
                                      static_cast<size_t>(content.size()));
  if (err != VXCORE_OK) {
    vxcore_buffer_close(m_ctx, bufferId);
    vxcore_string_free(bufferId);
    return false;
  }

  err = vxcore_buffer_save(m_ctx, bufferId);
  if (err != VXCORE_OK) {
    vxcore_buffer_close(m_ctx, bufferId);
    vxcore_string_free(bufferId);
    return false;
  }

  err = vxcore_buffer_close(m_ctx, bufferId);
  vxcore_string_free(bufferId);
  return err == VXCORE_OK;
}

void TestExportController::testConstruction() {
  ControllerFixture fixture(m_ctx);
  QVERIFY(fixture.controller != nullptr);
  QVERIFY(!fixture.controller->isExporting());
}

void TestExportController::testMarkdownExportContentBased() {
  ControllerFixture fixture(m_ctx);
  const QString relativePath = QStringLiteral("test_content_based.md");
  QVERIFY(createNotebookFile(relativePath));

  TempDirFixture outputDir;
  QVERIFY(outputDir.isValid());

  vnotex::ExportContext context;
  context.currentNodeId = vnotex::NodeIdentifier{m_notebookId, relativePath};
  context.bufferContent = QStringLiteral("# Hello World\nSome content");
  context.bufferName = QStringLiteral("test_content_based.md");
  context.presetSource = vnotex::ExportSource::CurrentBuffer;

  vnotex::ExportOption option;
  option.m_source = vnotex::ExportSource::CurrentBuffer;
  option.m_targetFormat = vnotex::ExportFormat::Markdown;
  option.m_outputDir = outputDir.path();
  option.m_recursive = false;
  option.m_exportAttachments = false;

  QSignalSpy finishedSpy(fixture.controller, &vnotex::ExportController::exportFinished);
  fixture.controller->doExport(option, context);

  QCOMPARE(finishedSpy.count(), 1);
  const QStringList outputFiles = finishedSpy.takeFirst().at(0).toStringList();
  QVERIFY(!outputFiles.isEmpty());

  QFile exportedFile(outputFiles.first());
  QVERIFY(exportedFile.exists());
  QVERIFY(exportedFile.open(QIODevice::ReadOnly));
  const QString exportedContent = QString::fromUtf8(exportedFile.readAll());
  QVERIFY(exportedContent.contains(QStringLiteral("# Hello World")));
}

void TestExportController::testMarkdownExportDiskBased() {
  ControllerFixture fixture(m_ctx);
  const QString relativePath = QStringLiteral("test_disk_based.md");
  QVERIFY(createNotebookFile(relativePath));
  QVERIFY(
      writeContentViaBuffer(relativePath, QStringLiteral("# Disk Content\nFrom vxcore buffer")));

  TempDirFixture outputDir;
  QVERIFY(outputDir.isValid());

  vnotex::ExportContext context;
  context.currentNodeId = vnotex::NodeIdentifier{m_notebookId, relativePath};
  context.bufferContent = QString();

  vnotex::ExportOption option;
  option.m_source = vnotex::ExportSource::CurrentNote;
  option.m_targetFormat = vnotex::ExportFormat::Markdown;
  option.m_outputDir = outputDir.path();
  option.m_recursive = false;
  option.m_exportAttachments = false;

  QSignalSpy finishedSpy(fixture.controller, &vnotex::ExportController::exportFinished);
  fixture.controller->doExport(option, context);

  QCOMPARE(finishedSpy.count(), 1);
  const QStringList outputFiles = finishedSpy.takeFirst().at(0).toStringList();
  QVERIFY(!outputFiles.isEmpty());

  QFile exportedFile(outputFiles.first());
  QVERIFY(exportedFile.exists());
  QVERIFY(exportedFile.open(QIODevice::ReadOnly));
  const QString exportedContent = QString::fromUtf8(exportedFile.readAll());
  QVERIFY(exportedContent.contains(QStringLiteral("# Disk Content")));
}

void TestExportController::testEmptyContextHandling() {
  ControllerFixture fixture(m_ctx);

  TempDirFixture outputDir;
  QVERIFY(outputDir.isValid());

  vnotex::ExportOption option;
  option.m_source = vnotex::ExportSource::CurrentBuffer;
  option.m_targetFormat = vnotex::ExportFormat::Markdown;
  option.m_outputDir = outputDir.path();
  option.m_recursive = false;
  option.m_exportAttachments = false;

  vnotex::ExportContext context;

  QSignalSpy logSpy(fixture.controller, &vnotex::ExportController::logRequested);
  QSignalSpy finishedSpy(fixture.controller, &vnotex::ExportController::exportFinished);

  fixture.controller->doExport(option, context);

  QVERIFY(logSpy.count() > 0);
  QCOMPARE(finishedSpy.count(), 1);
  const QStringList outputFiles = finishedSpy.takeFirst().at(0).toStringList();
  QVERIFY(outputFiles.isEmpty());
}

} // namespace tests

QTEST_GUILESS_MAIN(tests::TestExportController)
#include "test_exportcontroller.moc"
