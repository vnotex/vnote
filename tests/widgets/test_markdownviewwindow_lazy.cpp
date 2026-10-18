#include <QtTest>

#include <QAction>
#include <QApplication>
#include <QBuffer>
#include <QClipboard>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMimeData>
#include <QPointer>
#include <QPrinter>
#include <QScopeGuard>
#include <QSplitter>
#include <QTemporaryDir>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextLayout>
#include <QTimer>
#include <QUuid>
#include <QVBoxLayout>
#include <QWebChannel>
#include <QWebEngineView>
#include <QtConcurrent/QtConcurrentRun>

#include <memory>

#include <controllers/markdownwebjobcontroller.h>
#include <core/configmgr2.h>
#include <core/editorconfig.h>
#include <core/mainconfig.h>
#include <core/markdowneditorconfig.h>
#include <core/servicelocator.h>
#include <core/services/bufferservice.h>
#include <core/services/configcoreservice.h>
#include <core/services/hookmanager.h>
#include <core/services/htmltemplateservice.h>
#include <core/services/notebookcoreservice.h>
#include <core/services/notebookiogate.h>
#include <core/services/syncworkqueuemanager.h>
#include <gui/services/themeservice.h>
#include <gui/services/webengineprofileservice.h>
#include <gui/services/vxpdfschemehandler.h>
#include <gui/services/webviewservice.h>
#include <vtextedit/vtextedit.h>
#include <vxcore/vxcore.h>
#include <widgets/editors/markdowneditor.h>
#include <widgets/editors/markdownviewer.h>
#include <widgets/editors/markdownvieweradapter.h>
#include <widgets/editors/markdownwebjobhost.h>
#include <widgets/editors/previewhelper.h>
#include <widgets/markdownviewwindow2.h>
#include <widgets/outlineprovider.h>

using namespace vnotex;

namespace tests {
namespace {
class Window : public MarkdownViewWindow2 {
public:
  using MarkdownViewWindow2::aboutToClose;
  using MarkdownViewWindow2::printDocument;
  using MarkdownViewWindow2::MarkdownViewWindow2;
  using ViewWindow2::isViewFullScreen;
  ~Window() override {
    getServices().get<BufferService>()->unregisterActiveWriter(getBuffer().id(),
                                                               reinterpret_cast<quintptr>(this));
  }
};

int webViewCount() {
  int count = 0;
  for (auto *widget : QApplication::allWidgets()) {
    count += qobject_cast<QWebEngineView *>(widget) != nullptr;
  }
  return count;
}

QVariant evaluate(QWebEnginePage *p_page, const QString &p_script) {
  struct Result {
    bool finished = false;
    QVariant value;
  };
  const auto result = std::make_shared<Result>();
  p_page->runJavaScript(p_script, [result](const QVariant &p_value) {
    result->value = p_value;
    result->finished = true;
  });
  if (!QTest::qWaitFor([result]() { return result->finished; }, 10000)) {
    return {};
  }
  return result->value;
}

QColor sourceTokenColor(MarkdownEditor *p_editor, int p_line) {
  const auto block = p_editor->getTextEdit()->document()->findBlockByNumber(p_line);
  if (block.isValid() && block.layout()) {
    for (const auto &format : block.layout()->formats()) {
      if (format.start == 0 && format.length >= 5 &&
          format.format.foreground().style() != Qt::NoBrush) {
        return format.format.foreground().color();
      }
    }
  }
  return {};
}

void appendDraft(MarkdownEditor *p_editor, const QString &p_text) {
  auto *edit = p_editor->getTextEdit();
  auto cursor = edit->textCursor();
  cursor.movePosition(QTextCursor::End);
  cursor.beginEditBlock();
  cursor.insertText(p_text);
  cursor.endEditBlock();
  edit->setTextCursor(cursor);
}
} // namespace

class TestMarkdownViewWindowLazy : public QObject {
  Q_OBJECT
private slots:
  void initTestCase();
  void init();
  void cleanup();
  void initialMode_data();
  void initialMode();
  void assistanceBeforeReader();
  void endpointChangeReplaysCachedMath();
  void firstReaderUsesCurrentDraft_data();
  void firstReaderUsesCurrentDraft();
  void themeAndOptionsPreserveEditor();
  void imagePopupCreatesOnlyRequestedSurface();
  void printCreatesReaderForCurrentDraft();
  void presentationCreatesReader();
  void protectedLockWithoutReader();

private:
  Buffer2 openNote(const QString &p_name, const QByteArray &p_source);
  Buffer2 openProtectedNote(const QByteArray &p_source);
  void showWindow(Window &p_window);
  MarkdownEditorConfig &markdownConfig() {
    return m_config->getEditorConfig().getMarkdownEditorConfig();
  }

  VxCoreContextHandle m_context = nullptr;
  QTemporaryDir m_directory;
  ServiceLocator m_services;
  std::unique_ptr<ConfigCoreService> m_configCore;
  std::unique_ptr<ConfigMgr2> m_config;
  std::unique_ptr<HookManager> m_hooks;
  std::unique_ptr<NotebookIoGate> m_gate;
  std::unique_ptr<SyncWorkQueueManager> m_queues;
  std::unique_ptr<NotebookCoreService> m_notebooks;
  std::unique_ptr<BufferService> m_buffers;
  std::unique_ptr<ThemeService> m_theme;
  std::unique_ptr<HtmlTemplateService> m_templates;
  std::unique_ptr<WebEngineProfileService> m_profiles;
  std::unique_ptr<WebViewService> m_webService;
  std::unique_ptr<QMimeData> m_clipboard;
  QJsonObject m_originalConfig;
  QStringList m_appSearchPaths;
  QString m_notebookId;
};

void TestMarkdownViewWindowLazy::initTestCase() {
  vxcore_set_test_mode(1);
  QVERIFY(m_directory.isValid());
}

void TestMarkdownViewWindowLazy::init() {
  m_appSearchPaths = QDir::searchPaths(QStringLiteral("app"));
  const QString rcc = QStringLiteral(VNOTE_TEST_EXTRA_RCC);
  QVERIFY2(QFileInfo::exists(rcc), qPrintable(rcc));
  QDir::setSearchPaths(QStringLiteral("app"), {QFileInfo(rcc).absolutePath()});
  m_clipboard = std::make_unique<QMimeData>();
  const auto *mime = QApplication::clipboard()->mimeData();
  if (mime) {
    for (const auto &format : mime->formats()) {
      m_clipboard->setData(format, mime->data(format));
    }
  }
  QCOMPARE(vxcore_context_create(nullptr, &m_context), VXCORE_OK);
  m_configCore = std::make_unique<ConfigCoreService>(m_context);
  m_config = std::make_unique<ConfigMgr2>(m_configCore.get());
  m_config->init();
  // init() establishes the executable prefix; install the real bundled runtime from our build.
  QDir::setSearchPaths(QStringLiteral("app"), {QFileInfo(rcc).absolutePath()});
  m_config->initAfterQtAppStarted();
  QVERIFY(m_config->extraDataCopyFailures().isEmpty());
  m_originalConfig = m_config->getConfig().toJson();
  markdownConfig().setEditViewMode(MarkdownEditorConfig::EditOnly);
  markdownConfig().setAutoSectionNumberInEditModeEnabled(false);
  markdownConfig().setAutoFoldPreviewedBlocksEnabled(false);
  markdownConfig().setConcealmentEnabled(false);
  markdownConfig().setMathRenderer(QStringLiteral("katex"));
  markdownConfig().setFetchImagesInParseAndPaste(false);
  m_hooks = std::make_unique<HookManager>();
  m_gate = std::make_unique<NotebookIoGate>();
  m_queues = std::make_unique<SyncWorkQueueManager>();
  m_notebooks = std::make_unique<NotebookCoreService>(m_context);
  m_notebooks->setHookManager(m_hooks.get());
  m_notebooks->setNotebookIoGate(m_gate.get());
  m_buffers =
      std::make_unique<BufferService>(m_context, m_hooks.get(), m_gate.get(), AutoSavePolicy::None);
  m_buffers->setSyncWorkQueueManager(m_queues.get());
  ThemeServiceConfig themeConfig;
  themeConfig.themeName = QStringLiteral("pure");
  themeConfig.locale = QStringLiteral("en_US");
  const auto pure = QFINDTESTDATA("../../src/data/extra/themes/pure");
  QVERIFY(!pure.isEmpty());
  themeConfig.appDataPath = QFileInfo(QFileInfo(pure).absolutePath()).absolutePath();
  m_theme = std::make_unique<ThemeService>(themeConfig);
  m_templates = std::make_unique<HtmlTemplateService>(m_config.get());
  m_profiles = std::make_unique<WebEngineProfileService>(
      m_directory.filePath(QStringLiteral("profile")), m_config.get(), m_templates.get());
  m_services.registerService(m_configCore.get());
  m_services.registerService(m_config.get());
  m_services.registerService(m_hooks.get());
  m_services.registerService(m_gate.get());
  m_services.registerService(m_queues.get());
  m_services.registerService(m_notebooks.get());
  m_services.registerService(m_buffers.get());
  m_services.registerService(m_theme.get());
  m_services.registerService(m_templates.get());
  m_services.registerService(m_profiles.get());
  m_webService = std::make_unique<WebViewService>(
      m_services, m_profiles->profile(), MarkdownWebJobHost::createSharedRuntime(m_services));
  m_services.registerService(m_webService.get());
  // Same ordering as application integration: one global invalidation before tab refresh.
  connect(m_theme.get(), &ThemeService::themeAboutToChange, m_webService.get(),
          &WebViewService::invalidate);
  const auto notebookPath = m_directory.filePath(
      QStringLiteral("notebook-%1").arg(QUuid::createUuid().toString(QUuid::WithoutBraces)));
  m_notebookId = m_notebooks->createNotebook(
      notebookPath, QStringLiteral("{\"name\":\"Lazy reader regression\"}"), NotebookType::Bundled);
  QVERIFY(!m_notebookId.isEmpty());
}

void TestMarkdownViewWindowLazy::cleanup() {
  m_webService.reset();
  m_profiles.reset();
  m_templates.reset();
  m_theme.reset();
  if (m_buffers) {
    m_buffers->cancelProtectedLocking();
  }
  if (m_notebooks && !m_notebookId.isEmpty()) {
    m_notebooks->closeNotebook(m_notebookId);
    m_notebooks->lockAllEncryption();
  }
  m_buffers.reset();
  m_notebooks.reset();
  m_queues.reset();
  m_gate.reset();
  m_hooks.reset();
  if (m_config) {
    m_config->updateMainConfig(m_originalConfig);
  }
  m_config.reset();
  m_configCore.reset();
  if (m_context) {
    vxcore_context_destroy(m_context);
    m_context = nullptr;
  }
  m_notebookId.clear();
  if (m_clipboard) {
    QApplication::clipboard()->setMimeData(m_clipboard.release());
  }
  QDir::setSearchPaths(QStringLiteral("app"), m_appSearchPaths);
}

Buffer2 TestMarkdownViewWindowLazy::openNote(const QString &p_name, const QByteArray &p_source) {
  if (m_notebooks->createFile(m_notebookId, QString(), p_name).isEmpty()) {
    return {};
  }
  QFile file(m_notebooks->buildAbsolutePath(m_notebookId, p_name));
  if (!file.open(QIODevice::WriteOnly) || file.write(p_source) != p_source.size()) {
    return {};
  }
  file.close();
  return m_buffers->openBuffer({m_notebookId, p_name});
}

Buffer2 TestMarkdownViewWindowLazy::openProtectedNote(const QByteArray &p_source) {
  QString noteId;
  const auto result =
      QtConcurrent::run([&]() {
        auto setup = m_notebooks->prepareNotebookEncryption(
            m_notebookId, QString(), QByteArrayLiteral("lazy-window-protected-password"));
        if (!setup.isValid())
          return setup.m_error;
        auto maintenance = m_queues->tryAcquireMaintenance({m_notebookId});
        if (!maintenance.isValid())
          return VXCORE_ERR_INVALID_STATE;
        NotebookIoGate::ScopedLock lock(*m_gate, m_notebookId);
        const auto error = m_notebooks->commitNotebookEncryption(setup);
        return error == VXCORE_OK
                   ? m_notebooks->createEncryptedNote(m_notebookId, QString(),
                                                      QStringLiteral("private.md"),
                                                      QStringLiteral("markdown"), p_source, &noteId)
                   : error;
      }).result();
  return result == VXCORE_OK ? m_buffers->openBufferByNodeId(noteId) : Buffer2();
}

void TestMarkdownViewWindowLazy::showWindow(Window &p_window) {
  p_window.resize(1000, 720);
  p_window.show();
  p_window.activateWindow();
  if (auto *editor = p_window.findChild<MarkdownEditor *>()) {
    editor->getTextEdit()->setFocus();
  }
  QCoreApplication::processEvents();
}

void TestMarkdownViewWindowLazy::initialMode_data() {
  QTest::addColumn<bool>("read");
  QTest::addColumn<bool>("preview");
  QTest::newRow("EditOnly") << false << false;
  QTest::newRow("EditPreview") << false << true;
  QTest::newRow("Read") << true << false;
}

void TestMarkdownViewWindowLazy::initialMode() {
  QFETCH(bool, read);
  QFETCH(bool, preview);
  markdownConfig().setInplacePreviewSources(MarkdownEditorConfig::NoInplacePreview);
  markdownConfig().setEditViewMode(preview ? MarkdownEditorConfig::EditPreview
                                           : MarkdownEditorConfig::EditOnly);
  const auto buffer = openNote(QStringLiteral("initial.md"), QByteArrayLiteral("A plain note.\n"));
  QVERIFY(buffer.isValid());
  const int previousViews = webViewCount();
  Window window(m_services, buffer, nullptr, read ? ViewWindowMode::Read : ViewWindowMode::Edit);
  showWindow(window);
  auto *viewer = window.findChild<MarkdownViewer *>();
  QCOMPARE(viewer != nullptr, read || preview);
  QCOMPARE(window.findChild<MarkdownEditor *>() != nullptr, !read);
  QVERIFY(!window.isModified());
  if (viewer) {
    QCOMPARE(viewer->page()->webChannel()->registeredObjects().value(QStringLiteral("vxAdapter")),
             static_cast<QObject *>(viewer->adapter()));
    QCOMPARE(viewer->page()->webChannel()->registeredObjects().value(QStringLiteral("vxJobs")),
             static_cast<QObject *>(viewer->jobSession()));
    QTRY_VERIFY_WITH_TIMEOUT(viewer->jobSession()->isReady(), 30000);
  } else {
    QCOMPARE(webViewCount(), previousViews);
  }
}

void TestMarkdownViewWindowLazy::assistanceBeforeReader() {
  const auto first = openNote(QStringLiteral("first.md"), QByteArrayLiteral("# First\n"));
  const auto second = openNote(QStringLiteral("second.md"), QByteArrayLiteral("# Second\n"));
  QVERIFY(first.isValid() && second.isValid());
  const int previousViews = webViewCount();
  Window firstWindow(m_services, first, nullptr, ViewWindowMode::Edit);
  Window secondWindow(m_services, second, nullptr, ViewWindowMode::Edit);
  showWindow(firstWindow);
  showWindow(secondWindow);
  auto *editor = firstWindow.findChild<MarkdownEditor *>();
  auto *jobs = firstWindow.findChild<MarkdownWebJobController *>();
  auto *helper = firstWindow.findChild<PreviewHelper *>();
  QVERIFY(editor && jobs && helper);
  QSignalSpy highlighted(jobs, &MarkdownWebJobController::highlightCodeBlockReady);
  bool graphPixels = false;
  connect(helper, &PreviewHelper::inplacePreviewCodeBlockUpdated, &firstWindow,
          [&](const QVector<QSharedPointer<vte::PreviewItem>> &p_items) {
            for (const auto &item : p_items) {
              graphPixels |= item && !item->m_image.isNull();
            }
          });
  appendDraft(editor, QStringLiteral("\n```javascript\nconst answer = 42;\n```\n\n"
                                     "```mermaid\ngraph TD; A-->B;\n```\n"));
  appendDraft(secondWindow.findChild<MarkdownEditor *>(),
              QStringLiteral("\n```javascript\nlet other = 17;\n```\n"));
  QTRY_VERIFY_WITH_TIMEOUT(graphPixels, 30000);
  QTRY_VERIFY_WITH_TIMEOUT(!highlighted.isEmpty(), 30000);
  bool keyword = false;
  for (const auto &result : highlighted) {
    keyword |= result.at(2).toString().contains(QStringLiteral("token keyword"));
  }
  QVERIFY(keyword);
  QCOMPARE(webViewCount(), previousViews + 1);
  QVERIFY(!firstWindow.findChild<MarkdownViewer *>());
  QVERIFY(!secondWindow.findChild<MarkdownViewer *>());

  auto *clipboard = new QMimeData();
  clipboard->setHtml(QStringLiteral("<p><strong>converted draft</strong></p>"));
  clipboard->setText(QStringLiteral("converted draft"));
  QApplication::clipboard()->setMimeData(clipboard);
  QVERIFY(QMetaObject::invokeMethod(editor, "parseToMarkdownAndPaste", Qt::DirectConnection));
  QTRY_VERIFY_WITH_TIMEOUT(editor->getText().contains(QStringLiteral("**converted draft**")),
                           30000);
  QVERIFY(!firstWindow.findChild<MarkdownViewer *>());
}

void TestMarkdownViewWindowLazy::endpointChangeReplaysCachedMath() {
  const QString mathJaxScript = QStringLiteral(VNOTE_TEST_MATHJAX_SCRIPT);
  QVERIFY2(QFile::exists(mathJaxScript), qPrintable(mathJaxScript));
  markdownConfig().setMathRenderer(QStringLiteral("mathjax"));
  markdownConfig().setMathJaxScript(QUrl::fromLocalFile(mathJaxScript).toString());
  const auto buffer =
      openNote(QStringLiteral("endpoint-math.md"), QByteArrayLiteral("# Math scope\n"));
  QVERIFY(buffer.isValid());
  Window window(m_services, buffer, nullptr, ViewWindowMode::Edit);
  showWindow(window);
  auto *editor = window.findChild<MarkdownEditor *>();
  auto *jobs = window.findChild<MarkdownWebJobController *>();
  QVERIFY(editor && jobs);
  QHash<quint64, QImage> images;
  connect(jobs, &MarkdownWebJobController::mathPreviewDataReady, &window,
          [&](const MarkdownWebJobController::PreviewData &p_data) {
            const auto image = QImage::fromData(p_data.m_data);
            if (!image.isNull())
              images.insert(p_data.m_id, image);
          });
  appendDraft(editor, QStringLiteral("\n$$\n\\def\\vxendpointmacro{ABCD}\\vxendpointmacro\n$$\n\n"
                                     "$$\n\\vxendpointmacro\n$$\n"));
  QTRY_VERIFY_WITH_TIMEOUT(images.contains(0) && images.contains(1), 30000);
  QCOMPARE(images.value(0), images.value(1));
  QVERIFY(!window.findChild<MarkdownViewer *>());
  const auto expectedImage = images.value(1);
  const auto source = editor->getText();
  const auto cursor = editor->getTextEdit()->textCursor();
  const auto undo = editor->getTextEdit()->document()->availableUndoSteps();
  images.clear();

  // A cancelled image request initializes an empty reader without rendering the note.
  // The new reader therefore cannot accidentally inherit macros from document rendering.
  QImage image(2, 2, QImage::Format_ARGB32);
  image.fill(Qt::white);
  QByteArray png;
  QBuffer bytes(&png);
  QVERIFY(bytes.open(QIODevice::WriteOnly));
  QVERIFY(image.save(&bytes, "PNG"));
  editor->imageViewRequested(
      QStringLiteral("data:image/png;base64,%1").arg(QString::fromLatin1(png.toBase64())));
  auto *viewer = window.findChild<MarkdownViewer *>();
  QVERIFY(viewer);
  QStringList replayed;
  connect(viewer->jobSession(), &WebViewJobSession::jobRequested, &window,
          [&](const QString &, const QString &p_operation, const QJsonObject &p_arguments) {
            if (p_operation == QStringLiteral("markdown.previewMath")) {
              replayed.append(p_arguments.value(QStringLiteral("source")).toString());
            }
          });
  QTest::keyClick(editor->getTextEdit(), Qt::Key_Escape);
  QTRY_VERIFY_WITH_TIMEOUT(viewer->jobSession()->isReady(), 30000);
  QTRY_VERIFY_WITH_TIMEOUT(replayed.size() >= 2, 30000);
  QVERIFY(replayed.at(0).contains(QStringLiteral("\\def\\vxendpointmacro")));
  QVERIFY(!replayed.at(1).contains(QStringLiteral("\\def")));
  QVERIFY(replayed.at(1).contains(QStringLiteral("\\vxendpointmacro")));
  QTRY_VERIFY_WITH_TIMEOUT(images.contains(0) && images.contains(1), 30000);
  QCOMPARE(images.value(1), expectedImage);
  QCOMPARE(editor->getText(), source);
  QCOMPARE(editor->getTextEdit()->textCursor().position(), cursor.position());
  QCOMPARE(editor->getTextEdit()->document()->availableUndoSteps(), undo);
  QVERIFY(window.isModified());
  QVERIFY(editor->isVisible() && viewer->isHidden());
}

void TestMarkdownViewWindowLazy::firstReaderUsesCurrentDraft_data() {
  QTest::addColumn<bool>("preview");
  QTest::newRow("Read") << false;
  QTest::newRow("EditPreview") << true;
}

void TestMarkdownViewWindowLazy::firstReaderUsesCurrentDraft() {
  QFETCH(bool, preview);
  const auto buffer = openNote(QStringLiteral("draft.md"), QByteArrayLiteral("# Saved heading\n"));
  QVERIFY(buffer.isValid());
  Window window(m_services, buffer, nullptr, ViewWindowMode::Edit);
  showWindow(window);
  auto *editor = window.findChild<MarkdownEditor *>();
  QVERIFY(editor);
  appendDraft(editor, QStringLiteral("\n# Current unsaved heading\n"));
  const auto source = editor->getText();
  const auto cursor = editor->getTextEdit()->textCursor();
  const auto undo = editor->getTextEdit()->document()->availableUndoSteps();
  QVERIFY(window.isModified());
  if (preview) {
    markdownConfig().setEditViewMode(MarkdownEditorConfig::EditPreview);
    window.handleEditorConfigChange();
  } else {
    window.setMode(ViewWindowMode::Read);
  }
  auto *viewer = window.findChild<MarkdownViewer *>();
  QVERIFY(viewer);
  QTRY_VERIFY_WITH_TIMEOUT(viewer->jobSession()->isReady(), 30000);
  QTRY_VERIFY_WITH_TIMEOUT(evaluate(viewer->page(), QStringLiteral("document.body.innerText"))
                               .toString()
                               .contains(QStringLiteral("Current unsaved heading")),
                           30000);
  QCOMPARE(window.getLatestContent(), source);
  QCOMPARE(editor->getTextEdit()->document()->availableUndoSteps(), undo);
  QCOMPARE(editor->getTextEdit()->textCursor().position(), cursor.position());
  QVERIFY(window.isModified());
  if (!preview)
    window.setMode(ViewWindowMode::Edit);
  QCOMPARE(window.findChild<MarkdownEditor *>(), editor);
  editor->getTextEdit()->document()->undo();
  QCOMPARE(editor->getText(), QStringLiteral("# Saved heading\n"));
}

void TestMarkdownViewWindowLazy::themeAndOptionsPreserveEditor() {
  const auto buffer = openNote(QStringLiteral("theme.md"), QByteArrayLiteral("# Theme\n"));
  const auto cleanBuffer =
      openNote(QStringLiteral("clean-theme.md"), QByteArrayLiteral("Plain note\n"));
  QVERIFY(buffer.isValid() && cleanBuffer.isValid());
  Window cleanWindow(m_services, cleanBuffer, nullptr, ViewWindowMode::Edit);
  QVERIFY(!cleanWindow.isModified());
  Window window(m_services, buffer, nullptr, ViewWindowMode::Edit);
  showWindow(window);
  auto *editor = window.findChild<MarkdownEditor *>();
  auto *jobs = window.findChild<MarkdownWebJobController *>();
  QVERIFY(editor && jobs);
  appendDraft(editor, QStringLiteral("\n```javascript\nconst color = 42;\n```\n"));
  const auto source = editor->getText();
  auto *textEdit = editor->getTextEdit();
  const auto cursor = textEdit->textCursor();
  const auto undo = textEdit->document()->availableUndoSteps();
  QSignalSpy highlighted(jobs, &MarkdownWebJobController::highlightCodeBlockReady);
  QTRY_VERIFY_WITH_TIMEOUT(!highlighted.isEmpty(), 30000);
  QTRY_VERIFY_WITH_TIMEOUT(sourceTokenColor(editor, 3).isValid(), 30000);
  const auto oldColor = sourceTokenColor(editor, 3);
  highlighted.clear();
  m_theme->switchTheme(QStringLiteral("vscode-dark"));
  QVERIFY(!cleanWindow.isModified());
  QVERIFY(!cleanWindow.findChild<MarkdownViewer *>());
  QTRY_VERIFY_WITH_TIMEOUT(!highlighted.isEmpty(), 30000);
  QTRY_VERIFY_WITH_TIMEOUT(
      sourceTokenColor(editor, 3).isValid() && sourceTokenColor(editor, 3) != oldColor, 30000);
  QVERIFY(!window.findChild<MarkdownViewer *>());
  QCOMPARE(editor->getText(), source);
  QCOMPARE(textEdit->textCursor().position(), cursor.position());
  QCOMPARE(textEdit->document()->availableUndoSteps(), undo);
  QVERIFY(window.isModified());
  const auto refreshedColor = sourceTokenColor(editor, 3);
  m_webService->invalidate();
  markdownConfig().setLinkifyEnabled(!markdownConfig().getLinkifyEnabled());
  cleanWindow.handleEditorConfigChange();
  QVERIFY(!cleanWindow.isModified());
  window.handleEditorConfigChange();
  QTRY_COMPARE(sourceTokenColor(editor, 3), refreshedColor);
  QVERIFY(!window.findChild<MarkdownViewer *>());
  QCOMPARE(editor->getText(), source);
  QCOMPARE(textEdit->textCursor().position(), cursor.position());
  QCOMPARE(textEdit->document()->availableUndoSteps(), undo);
  textEdit->document()->undo();
  QCOMPARE(editor->getText(), QStringLiteral("# Theme\n"));
}

void TestMarkdownViewWindowLazy::imagePopupCreatesOnlyRequestedSurface() {
  const auto buffer = openNote(QStringLiteral("image.md"), QByteArrayLiteral("# Image draft\n"));
  QVERIFY(buffer.isValid());
  Window window(m_services, buffer, nullptr, ViewWindowMode::Edit);
  showWindow(window);
  auto *editor = window.findChild<MarkdownEditor *>();
  QVERIFY(editor);
  appendDraft(editor, QStringLiteral("Unsaved image source\n"));
  const auto source = editor->getText();
  const auto undo = editor->getTextEdit()->document()->availableUndoSteps();
  const auto size = editor->size();
  QImage image(48, 24, QImage::Format_ARGB32);
  image.fill(Qt::red);
  QByteArray png;
  QBuffer bytes(&png);
  QVERIFY(bytes.open(QIODevice::WriteOnly));
  QVERIFY(image.save(&bytes, "PNG"));
  const auto url =
      QStringLiteral("data:image/png;base64,%1").arg(QString::fromLatin1(png.toBase64()));
  editor->imageViewRequested(url);
  auto *viewer = window.findChild<MarkdownViewer *>();
  QVERIFY(viewer);
  QSignalSpy rendered(viewer->adapter(), &MarkdownViewerAdapter::textUpdated);
  // Cancel before the initial page is ready; late acknowledgements must not open it.
  QTest::keyClick(editor->getTextEdit(), Qt::Key_Escape);
  QTRY_VERIFY_WITH_TIMEOUT(viewer->jobSession()->isReady(), 30000);
  QVERIFY(editor->isVisible());
  QVERIFY(viewer->isHidden());
  QSignalSpy popup(viewer->adapter(), &MarkdownViewerAdapter::imageViewStateChanged);
  editor->imageViewRequested(url);
  QTRY_VERIFY_WITH_TIMEOUT(viewer->isVisible() && editor->isHidden(), 10000);
  QVERIFY(!popup.isEmpty());
  QCOMPARE(rendered.count(), 0);
  QTest::keyClick(viewer, Qt::Key_Escape);
  QTRY_VERIFY(editor->isVisible() && viewer->isHidden());
  QTRY_VERIFY(editor->getTextEdit()->hasFocus());
  QCOMPARE(editor->size(), size);
  QCOMPARE(editor->getText(), source);
  QCOMPARE(editor->getTextEdit()->document()->availableUndoSteps(), undo);
}

void TestMarkdownViewWindowLazy::printCreatesReaderForCurrentDraft() {
  const auto buffer = openNote(QStringLiteral("print.md"), QByteArrayLiteral("# Saved print\n"));
  QVERIFY(buffer.isValid());
  Window window(m_services, buffer, nullptr, ViewWindowMode::Edit);
  showWindow(window);
  auto *editor = window.findChild<MarkdownEditor *>();
  QVERIFY(editor);
  appendDraft(editor, QStringLiteral("\n# Unsaved print content\n"));
  const auto source = editor->getText();
  const auto undo = editor->getTextEdit()->document()->availableUndoSteps();
  const auto output = m_directory.filePath(QStringLiteral("lazy-print.pdf"));
  window.printDocument({});
  QVERIFY(!window.findChild<MarkdownViewer *>());
  auto printer = QSharedPointer<QPrinter>::create();
  printer->setOutputFormat(QPrinter::PdfFormat);
  printer->setOutputFileName(output);
  window.printDocument(printer);
  auto *viewer = window.findChild<MarkdownViewer *>();
  QVERIFY(viewer);
  QSignalSpy printed(viewer, &MarkdownViewer::printFinished);
  QTRY_COMPARE_WITH_TIMEOUT(printed.count(), 1, 30000);
  QVERIFY(printed.first().first().toBool());
  QVERIFY(QFileInfo(output).size() > 0);
  QFile pdf(output);
  QVERIFY(pdf.open(QIODevice::ReadOnly));
  QCOMPARE(pdf.read(5), QByteArrayLiteral("%PDF-"));
  QVERIFY(evaluate(viewer->page(), QStringLiteral("document.body.innerText"))
              .toString()
              .contains(QStringLiteral("Unsaved print content")));
  QCOMPARE(window.getMode(), ViewWindowMode::Edit);
  QVERIFY(editor->isVisible() && viewer->isHidden());
  QCOMPARE(editor->getText(), source);
  QCOMPARE(editor->getTextEdit()->document()->availableUndoSteps(), undo);
}

void TestMarkdownViewWindowLazy::presentationCreatesReader() {
  const auto buffer =
      openNote(QStringLiteral("slides.md"), QByteArrayLiteral("# Slides\n\n## First\n"));
  QVERIFY(buffer.isValid());
  QWidget shell;
  QVBoxLayout layout(&shell);
  Window window(m_services, buffer, &shell, ViewWindowMode::Edit);
  layout.addWidget(&window);
  shell.resize(1100, 800);
  shell.show();
  QVERIFY(QTest::qWaitForWindowExposed(&shell));
  auto *editor = window.findChild<MarkdownEditor *>();
  QVERIFY(editor);
  appendDraft(editor, QStringLiteral("\nUnsaved slide text\n"));
  QAction *presentation = nullptr;
  for (auto *action : window.findChildren<QAction *>()) {
    if (action->property("iconName").toString() == QStringLiteral("presentation_editor.svg")) {
      presentation = action;
      break;
    }
  }
  QVERIFY(presentation);
  presentation->trigger();
  auto *viewer = window.findChild<MarkdownViewer *>();
  QVERIFY(viewer);
  QTRY_VERIFY_WITH_TIMEOUT(window.isViewFullScreen(), 30000);
  // Inactive slides are intentionally absent from innerText; the deck still owns their draft.
  QVERIFY(evaluate(viewer->page(), QStringLiteral("document.body.textContent"))
              .toString()
              .contains(QStringLiteral("Unsaved slide text")));
  presentation->trigger();
  QTRY_VERIFY(!window.isViewFullScreen());
  window.setMode(ViewWindowMode::Edit);
  QVERIFY(editor->getText().contains(QStringLiteral("Unsaved slide text")));
  QVERIFY(editor->getTextEdit()->document()->isUndoAvailable());
}

void TestMarkdownViewWindowLazy::protectedLockWithoutReader() {
  const auto buffer = openProtectedNote(QByteArrayLiteral("# Protected draft\n"));
  QVERIFY(buffer.isValid() && buffer.isEncrypted());
  const int previousViews = webViewCount();
  Window window(m_services, buffer, nullptr, ViewWindowMode::Edit);
  showWindow(window);
  auto *jobs = window.findChild<MarkdownWebJobController *>();
  auto *host = window.findChild<MarkdownWebJobHost *>();
  QVERIFY(jobs && host);
  QPointer<WebViewJobSession> fallback;
  connect(host, &MarkdownWebJobHost::protectedSessionReady, &window,
          [&](WebViewJobSession *p_session, const QByteArray &) { fallback = p_session; });
  QSignalSpy highlighted(jobs, &MarkdownWebJobController::highlightCodeBlockReady);
  window.findChild<MarkdownEditor *>()->externalCodeBlockHighlightRequested(
      2, 77, QStringLiteral("```javascript\nconst secret = 42;\n```"));
  QTRY_VERIFY_WITH_TIMEOUT(!highlighted.isEmpty(), 30000);
  QVERIFY(fallback);
  QVERIFY(fallback->property("markdownJobProtected").toBool());
  QCOMPARE(webViewCount(), previousViews + 1);
  QVERIFY(!window.findChild<MarkdownViewer *>());
  int callbacks = 0;
  WebViewJobResult::Status status = WebViewJobResult::Status::Success;
  jobs->fetchHeadingAnchor(QStringLiteral("# Pending secret\n"), 0,
                           [&](const MarkdownWebJobController::HeadingAnchorResult &p_result) {
                             ++callbacks;
                             status = p_result.m_status;
                           });
  QVERIFY(m_buffers->beginProtectedLocking());
  QVERIFY(!fallback);
  QCOMPARE(callbacks, 1);
  QCOMPARE(status, WebViewJobResult::Status::Cancelled);
  // Lock saves the frozen writer before closing it: the native draft stays until this gate.
  QVERIFY(window.findChild<MarkdownEditor *>());
  QVERIFY(window.aboutToClose(true));
  QVERIFY(!window.findChild<MarkdownEditor *>());
  QVERIFY(!window.findChild<PreviewHelper *>());
  QVERIFY(!window.findChild<MarkdownViewer *>());
  QVERIFY(!window.isEnabled());
  QCOMPARE(webViewCount(), previousViews);
  QCoreApplication::processEvents();
  QCOMPARE(callbacks, 1);
}

} // namespace tests

int main(int argc, char **argv) {
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
  QApplication app(argc, argv);
  tests::TestMarkdownViewWindowLazy test;
  return QTest::qExec(&test, argc, argv);
}

#include "test_markdownviewwindow_lazy.moc"
