#include <QtTest>

#include <QAction>
#include <QApplication>
#include <QBuffer>
#include <QClipboard>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMimeData>
#include <QLabel>
#include <QHBoxLayout>
#include <QPdfDocument>
#include <QPdfSelection>
#include <QPointer>
#include <QPrinter>
#include <QScopeGuard>
#include <QSplitter>
#include <QTemporaryDir>
#include <QTabWidget>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextLayout>
#include <QTimer>
#include <QUuid>
#include <QVBoxLayout>
#include <QWebChannel>
#include <QWebEngineView>
#include <QWebEngineProfile>
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
  using MarkdownViewWindow2::handleFindNext;
  using MarkdownViewWindow2::handleFindAndReplaceWidgetClosed;
  using ViewWindow2::fetchWordCountInfo;
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

bool expireHiddenReader(Window &p_window) {
  auto *timer = p_window.findChild<QTimer *>(QStringLiteral("inactiveReaderTimer"));
  if (!timer || p_window.isVisible() || !timer->isActive()) {
    return false;
  }
  QSignalSpy expired(timer, &QTimer::timeout);
  timer->start(20);
  return QTest::qWaitFor([&]() { return expired.count() == 1; }, 5000);
}

bool renderedHeading(MarkdownViewer *p_viewer, const QString &p_heading) {
  return p_viewer && p_viewer->jobSession()->isReady() &&
         evaluate(p_viewer->page(), QStringLiteral("document.body.innerText"))
             .toString().contains(p_heading);
}

QByteArray longNote() {
  QByteArray source;
  for (int section = 0; section < 36; ++section) {
    source += "# Section " + QByteArray::number(section) + "\n\n";
    source += "- [ ] interactive task\n\n";
    for (int line = 0; line < 12; ++line) {
      source += "Paragraph " + QByteArray::number(section) + ":" + QByteArray::number(line) +
                " with enough text to occupy the reading viewport.\n\n";
    }
  }
  return source;
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
  void printUsesCurrentMode_data();
  void printUsesCurrentMode();
  void presentationCreatesReader();
  void protectedLockWithoutReader();
  void inactiveTabPreservesDraft_data();
  void inactiveTabPreservesDraft();
  void inactiveTimeoutLivePolicy();
  void dormantModeChanges_data();
  void dormantModeChanges();
  void inactiveReadNavigation_data();
  void inactiveReadNavigation();
  void inactiveReadUnavailableActions();
  void inactiveProtectedResources();
  void printAfterReaderRestoration();

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

void TestMarkdownViewWindowLazy::printUsesCurrentMode_data() {
  QTest::addColumn<bool>("read");
  QTest::addColumn<bool>("preview");
  QTest::addColumn<bool>("selection");
  QTest::newRow("Read") << true << false << false;
  QTest::newRow("EditOnly") << false << false << false;
  QTest::newRow("EditPreview") << false << true << false;
  QTest::newRow("EditOnlySelection") << false << false << true;
  QTest::newRow("EditPreviewSelection") << false << true << true;
}

void TestMarkdownViewWindowLazy::printUsesCurrentMode() {
  QFETCH(bool, read);
  QFETCH(bool, preview);
  QFETCH(bool, selection);
  markdownConfig().setEditViewMode(preview ? MarkdownEditorConfig::EditPreview
                                          : MarkdownEditorConfig::EditOnly);
  const auto buffer = openNote(QStringLiteral("print.md"), QByteArrayLiteral("# Saved heading\n"));
  QVERIFY(buffer.isValid());
  Window window(m_services, buffer, nullptr, ViewWindowMode::Edit);
  showWindow(window);
  auto *editor = window.findChild<MarkdownEditor *>();
  QVERIFY(editor);
  appendDraft(editor, QStringLiteral("\n**Unsaved draft**\n"));
  if (read) {
    window.setMode(ViewWindowMode::Read);
  }
  if (selection) {
    auto cursor = editor->getTextEdit()->document()->find(QStringLiteral("**Unsaved draft**"));
    QVERIFY(!cursor.isNull());
    editor->getTextEdit()->setTextCursor(cursor);
  }
  const auto source = editor->getText();
  const auto cursor = editor->getTextEdit()->textCursor();
  const auto undo = editor->getTextEdit()->document()->availableUndoSteps();
  const auto output = m_directory.filePath(QStringLiteral("print-%1.pdf").arg(
      QString::fromLatin1(QTest::currentDataTag())));
  window.printDocument({});
  QVERIFY(!QFileInfo::exists(output));
  auto printer = QSharedPointer<QPrinter>::create();
  printer->setOutputFormat(QPrinter::PdfFormat);
  printer->setOutputFileName(output);
  if (selection) {
    printer->setPrintRange(QPrinter::Selection);
  }
  if (read) {
    auto *viewer = window.findChild<MarkdownViewer *>();
    QVERIFY(viewer);
    QSignalSpy printed(viewer, &MarkdownViewer::printFinished);
    window.printDocument(printer);
    QTRY_COMPARE_WITH_TIMEOUT(printed.count(), 1, 30000);
    QVERIFY(printed.first().first().toBool());
  } else {
    window.printDocument(printer);
    if (!preview) {
      QVERIFY(!window.findChild<MarkdownViewer *>());
    }
  }
  QPdfDocument pdf;
  QCOMPARE(pdf.load(output), QPdfDocument::Error::None);
  QString text;
  if (read) {
    // QWebEngineView::print rasterizes the page, so inspect its rendered text.
    text = evaluate(window.findChild<MarkdownViewer *>()->page(),
                    QStringLiteral("document.body.innerText")).toString();
  } else {
    for (int page = 0; page < pdf.pageCount(); ++page) {
      text += pdf.getAllText(page).text();
    }
  }
  QVERIFY2(text.contains(QStringLiteral("Unsaved draft")), qPrintable(text));
  QCOMPARE(text.contains(QStringLiteral("Saved heading")), !selection);
  QCOMPARE(text.contains(QStringLiteral("**Unsaved draft**")), !read);
  QCOMPARE(text.contains(QStringLiteral("# Saved heading")), !read && !selection);
  QCOMPARE(window.getMode(), read ? ViewWindowMode::Read : ViewWindowMode::Edit);
  QCOMPARE(editor->getText(), source);
  QCOMPARE(editor->getTextEdit()->document()->availableUndoSteps(), undo);
  QCOMPARE(editor->getTextEdit()->textCursor().position(), cursor.position());
  QCOMPARE(editor->getTextEdit()->textCursor().anchor(), cursor.anchor());
  QVERIFY(window.isModified());
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

void TestMarkdownViewWindowLazy::inactiveTabPreservesDraft_data() {
  QTest::addColumn<bool>("preview");
  QTest::newRow("Read") << false;
  QTest::newRow("EditPreview") << true;
}

void TestMarkdownViewWindowLazy::inactiveTabPreservesDraft() {
  QFETCH(bool, preview);
  markdownConfig().setEditViewMode(preview ? MarkdownEditorConfig::EditPreview
                                         : MarkdownEditorConfig::EditOnly);
  const auto buffer = openNote(QStringLiteral("inactive-draft.md"),
                               QByteArrayLiteral("# Saved heading\n"));
  QVERIFY(buffer.isValid());
  QTabWidget tabs;
  Window window(m_services, buffer, &tabs, ViewWindowMode::Edit);
  QWidget other;
  tabs.addTab(&window, QStringLiteral("note"));
  tabs.addTab(&other, QStringLiteral("other"));
  tabs.resize(1100, 760);
  tabs.show();
  QVERIFY(QTest::qWaitForWindowExposed(&tabs));
  auto *editor = window.findChild<MarkdownEditor *>();
  QVERIFY(editor);
  tabs.activateWindow();
  editor->getTextEdit()->setFocus();
  QTRY_VERIFY(editor->getTextEdit()->hasFocus());
  appendDraft(editor, QStringLiteral("\n# Unsaved inactive heading\n"));
  if (!preview) {
    window.setMode(ViewWindowMode::Read);
  }
  auto *textEdit = editor->getTextEdit();
  auto selection = textEdit->document()->find(QStringLiteral("Unsaved inactive heading"));
  QVERIFY(!selection.isNull());
  textEdit->setTextCursor(selection);
  const auto source = editor->getText();
  const auto undo = textEdit->document()->availableUndoSteps();
  QPointer<MarkdownViewer> retired = window.findChild<MarkdownViewer *>();
  QVERIFY(retired);
  QTRY_VERIFY_WITH_TIMEOUT(renderedHeading(retired, QStringLiteral("Unsaved inactive heading")),
                           30000);
  retired->setZoomFactor(1.25);
  auto *splitter = qobject_cast<QSplitter *>(retired->parentWidget());
  QVERIFY(splitter);
  if (preview) {
    splitter->setSizes({320, 740});
  }
  const auto sizes = splitter->sizes();
  QPointer<QWebEnginePage> page = retired->page();
  QPointer<MarkdownViewerAdapter> adapter = retired->adapter();
  QPointer<WebViewJobSession> session = retired->jobSession();
  auto *timer = window.findChild<QTimer *>(QStringLiteral("inactiveReaderTimer"));
  QVERIFY(timer);
  tabs.setCurrentWidget(&other);
  QVERIFY(timer->isActive());
  tabs.setCurrentWidget(&window);
  QVERIFY(!timer->isActive());
  QCOMPARE(window.findChild<MarkdownViewer *>(), retired.data());
  tabs.setCurrentWidget(&other);
  QVERIFY(expireHiddenReader(window));
  QVERIFY(!retired && !page && !adapter && !session);
  QCOMPARE(window.findChild<MarkdownEditor *>(), editor);
  QCOMPARE(editor->getText(), source);
  QCOMPARE(textEdit->document()->availableUndoSteps(), undo);
  QCOMPARE(textEdit->textCursor().position(), selection.position());
  QCOMPARE(textEdit->textCursor().anchor(), selection.anchor());
  QVERIFY(window.isModified());
  tabs.setCurrentWidget(&window);
  auto *restored = window.findChild<MarkdownViewer *>();
  QVERIFY(restored);
  QTRY_VERIFY_WITH_TIMEOUT(renderedHeading(restored, QStringLiteral("Unsaved inactive heading")),
                           30000);
  QCOMPARE(restored->zoomFactor(), 1.25);
  if (preview) {
    QTRY_VERIFY(qAbs(splitter->sizes().at(0) - sizes.at(0)) <= 2);
  }
  QCOMPARE(editor->getText(), source);
  QCOMPARE(textEdit->document()->availableUndoSteps(), undo);
  QCOMPARE(textEdit->textCursor().position(), selection.position());
  QCOMPARE(textEdit->textCursor().anchor(), selection.anchor());
  if (!preview) {
    window.setMode(ViewWindowMode::Edit);
  }
  textEdit->document()->undo();
  QCOMPARE(editor->getText(), QStringLiteral("# Saved heading\n"));
}

void TestMarkdownViewWindowLazy::inactiveTimeoutLivePolicy() {
  markdownConfig().setInactiveReaderTimeoutMinutes(0);
  const auto buffer = openNote(QStringLiteral("inactive-policy.md"), QByteArrayLiteral("# Policy\n"));
  QVERIFY(buffer.isValid());
  QWidget shell;
  QHBoxLayout layout(&shell);
  Window first(m_services, buffer, &shell, ViewWindowMode::Read);
  Window second(m_services, buffer, &shell, ViewWindowMode::Read);
  layout.addWidget(&first);
  layout.addWidget(&second);
  shell.resize(1200, 720);
  shell.show();
  QVERIFY(QTest::qWaitForWindowExposed(&shell));
  QPointer<MarkdownViewer> reader = first.findChild<MarkdownViewer *>();
  QPointer<MarkdownViewer> sibling = second.findChild<MarkdownViewer *>();
  QTRY_VERIFY_WITH_TIMEOUT(renderedHeading(reader, QStringLiteral("Policy")), 30000);
  QTRY_VERIFY_WITH_TIMEOUT(renderedHeading(sibling, QStringLiteral("Policy")), 30000);
  auto *timer = first.findChild<QTimer *>(QStringLiteral("inactiveReaderTimer"));
  auto *siblingTimer = second.findChild<QTimer *>(QStringLiteral("inactiveReaderTimer"));
  QVERIFY(timer && siblingTimer);
  first.hide();
  QVERIFY(!timer->isActive());
  QTest::qWait(60);
  QVERIFY(reader);
  markdownConfig().setInactiveReaderTimeoutMinutes(12);
  first.handleEditorConfigChange();
  second.handleEditorConfigChange();
  QVERIFY(timer->isActive());
  QCOMPARE(timer->interval(), 12 * 60 * 1000);
  QVERIFY(!siblingTimer->isActive());
  markdownConfig().setInactiveReaderTimeoutMinutes(1);
  first.handleEditorConfigChange();
  QCOMPARE(timer->interval(), 60 * 1000);
  timer->start(5000);
  first.handleEditorConfigChange();
  QCOMPARE(timer->interval(), 5000);
  QVERIFY(expireHiddenReader(first));
  QVERIFY(!reader);
  QVERIFY(sibling && second.isVisible());
  markdownConfig().setInactiveReaderTimeoutMinutes(0);
  first.handleEditorConfigChange();
  QVERIFY(!timer->isActive());
  QVERIFY(!first.findChild<MarkdownViewer *>());
  // Showing while disabled cannot allocate; visible re-enable must resume.
  first.setEnabled(false);
  first.show();
  QVERIFY(!first.findChild<MarkdownViewer *>());
  first.setEnabled(true);
  auto *restored = first.findChild<MarkdownViewer *>();
  QVERIFY(restored);
  QTRY_VERIFY_WITH_TIMEOUT(renderedHeading(restored, QStringLiteral("Policy")), 30000);
  QVERIFY(sibling);
  // Constructor-completion eligibility includes tabs that have never been shown.
  markdownConfig().setInactiveReaderTimeoutMinutes(1);
  Window neverShown(m_services, buffer, nullptr, ViewWindowMode::Read);
  QPointer<MarkdownViewer> unshownReader = neverShown.findChild<MarkdownViewer *>();
  QVERIFY(unshownReader);
  auto *unshownTimer = neverShown.findChild<QTimer *>(QStringLiteral("inactiveReaderTimer"));
  QVERIFY(unshownTimer);
  QTRY_VERIFY(unshownTimer->isActive());
  QVERIFY(expireHiddenReader(neverShown));
  QVERIFY(!unshownReader);
}

void TestMarkdownViewWindowLazy::dormantModeChanges_data() {
  QTest::addColumn<bool>("initialPreview");
  QTest::addColumn<bool>("finalPreview");
  QTest::newRow("EditOnly-to-EditPreview") << false << true;
  QTest::newRow("EditPreview-to-EditOnly") << true << false;
  QTest::newRow("EditOnly-assistance") << false << false;
}

void TestMarkdownViewWindowLazy::dormantModeChanges() {
  QFETCH(bool, initialPreview);
  QFETCH(bool, finalPreview);
  markdownConfig().setEditViewMode(initialPreview ? MarkdownEditorConfig::EditPreview
                                                : MarkdownEditorConfig::EditOnly);
  const auto buffer = openNote(QStringLiteral("dormant-mode.md"), QByteArrayLiteral("# Modes\n"));
  QVERIFY(buffer.isValid());
  Window window(m_services, buffer, nullptr, ViewWindowMode::Edit);
  showWindow(window);
  auto *editor = window.findChild<MarkdownEditor *>();
  auto *jobs = window.findChild<MarkdownWebJobController *>();
  QVERIFY(editor && jobs);
  if (initialPreview) {
    QTRY_VERIFY_WITH_TIMEOUT(renderedHeading(window.findChild<MarkdownViewer *>(),
                                             QStringLiteral("Modes")), 30000);
  }
  window.hide();
  QVERIFY(expireHiddenReader(window));
  QVERIFY(!window.findChild<MarkdownViewer *>());
  markdownConfig().setEditViewMode(finalPreview ? MarkdownEditorConfig::EditPreview
                                              : MarkdownEditorConfig::EditOnly);
  markdownConfig().setLinkifyEnabled(true);
  window.handleEditorConfigChange();
  m_theme->switchTheme(QStringLiteral("vscode-dark"));
  appendDraft(editor, QStringLiteral("\n# Changed while dormant\n\nhttps://example.org/dormant\n"));
  QCoreApplication::processEvents();
  QVERIFY(!window.findChild<MarkdownViewer *>());
  QCOMPARE(window.findChild<MarkdownEditor *>(), editor);
  showWindow(window);
  if (finalPreview) {
    auto *viewer = window.findChild<MarkdownViewer *>();
    QVERIFY(viewer);
    QTRY_VERIFY_WITH_TIMEOUT(renderedHeading(viewer, QStringLiteral("Changed while dormant")),
                             30000);
    QTRY_VERIFY_WITH_TIMEOUT(evaluate(viewer->page(), QStringLiteral(
        "!!document.querySelector('a[href=\"https://example.org/dormant\"]')")).toBool(), 10000);
    appendDraft(editor, QStringLiteral("\n# Continuous live update\n"));
    QTRY_VERIFY_WITH_TIMEOUT(renderedHeading(viewer, QStringLiteral("Continuous live update")),
                             30000);
  } else {
    QVERIFY(!window.findChild<MarkdownViewer *>());
    QSignalSpy highlighted(jobs, &MarkdownWebJobController::highlightCodeBlockReady);
    editor->externalCodeBlockHighlightRequested(
        7, 19, QStringLiteral("```javascript\nconst resumed = 42;\n```"));
    QTRY_VERIFY_WITH_TIMEOUT(!highlighted.isEmpty(), 30000);
    QVERIFY(highlighted.last().at(2).toString().contains(QStringLiteral("token keyword")));
    QImage math;
    connect(jobs, &MarkdownWebJobController::mathPreviewDataReady, &window,
            [&](const MarkdownWebJobController::PreviewData &p_data) {
              if (p_data.m_id == 97) {
                math = QImage::fromData(p_data.m_data);
              }
            });
    jobs->previewMath(97, 20, QStringLiteral("$x^2 + y^2$"), 1.0);
    QTRY_VERIFY_WITH_TIMEOUT(!math.isNull(), 30000);
    QVERIFY(!window.findChild<MarkdownViewer *>());
  }
  QVERIFY(window.isModified());
}

void TestMarkdownViewWindowLazy::inactiveReadNavigation_data() {
  QTest::addColumn<QString>("target");
  QTest::addColumn<bool>("afterShow");
  QTest::newRow("saved-position") << QStringLiteral("saved") << false;
  QTest::newRow("anchor-before-show") << QStringLiteral("anchor") << false;
  QTest::newRow("anchor-after-show") << QStringLiteral("anchor") << true;
  QTest::newRow("line-before-show") << QStringLiteral("line") << false;
  QTest::newRow("line-after-show") << QStringLiteral("line") << true;
  QTest::newRow("search-before-show") << QStringLiteral("search") << false;
  QTest::newRow("search-after-show") << QStringLiteral("search") << true;
}

void TestMarkdownViewWindowLazy::inactiveReadNavigation() {
  QFETCH(QString, target);
  QFETCH(bool, afterShow);
  const auto buffer = openNote(QStringLiteral("inactive-navigation.md"), longNote());
  QVERIFY(buffer.isValid());
  Window window(m_services, buffer, nullptr, ViewWindowMode::Read);
  showWindow(window);
  QPointer<MarkdownViewer> old = window.findChild<MarkdownViewer *>();
  QTRY_VERIFY_WITH_TIMEOUT(renderedHeading(old, QStringLiteral("Section 35")), 30000);
  old->setZoomFactor(1.25);
  old->adapter()->scrollToPosition(MarkdownViewerAdapter::Position(10 * 28, QString()));
  QTest::qWait(700);
  const auto position = window.capturePositionState();
  QVERIFY(position.m_topLineNumber > 0);
  window.hide();
  QVERIFY(expireHiddenReader(window));
  QVERIFY(!old);
  QCOMPARE(window.capturePositionState().m_topLineNumber, position.m_topLineNumber);
  window.restorePositionState(position);
  FileOpenSettings settings;
  if (target == QStringLiteral("anchor")) {
    settings.m_anchor = QStringLiteral("section-20");
  } else if (target != QStringLiteral("saved")) {
    settings.m_lineNumber = 20 * 28;
    if (target == QStringLiteral("search")) {
      settings.m_searchHighlight.m_isValid = true;
      settings.m_searchHighlight.m_patterns = {QStringLiteral("Section 20")};
      settings.m_searchHighlight.m_currentMatchLine = 20 * 28;
    }
  }
  // Seed an older deferred anchor: the latest line/search request must supersede it.
  if (target == QStringLiteral("line") || target == QStringLiteral("search")) {
    FileOpenSettings older;
    older.m_anchor = QStringLiteral("section-5");
    window.applyFileOpenSettings(older);
  }
  if (!afterShow) {
    window.applyFileOpenSettings(settings);
  }
  window.show();
  if (afterShow) {
    // ViewAreaController activates (shows) before applying explicit navigation.
    window.applyFileOpenSettings(settings);
  }
  auto *viewer = window.findChild<MarkdownViewer *>();
  QVERIFY(viewer);
  QTRY_VERIFY_WITH_TIMEOUT(renderedHeading(viewer, QStringLiteral("Section 35")), 30000);
  QCOMPARE(viewer->zoomFactor(), 1.25);
  const auto expected = target == QStringLiteral("saved") ? 10 : 20;
  const auto headingTop = QStringLiteral(
      "Array.from(document.querySelectorAll('h1')).find(h => "
      "h.textContent.includes('Section %1')).getBoundingClientRect().top").arg(expected);
  QTRY_VERIFY_WITH_TIMEOUT(qAbs(evaluate(viewer->page(), headingTop).toDouble()) < 200, 10000);
  // The JS source-line path is delayed; an older passive scroll must not win later.
  QTest::qWait(700);
  QVERIFY(qAbs(evaluate(viewer->page(), headingTop).toDouble()) < 200);
  // Exercise a live reader action, not just DOM restoration.
  evaluate(viewer->page(), QStringLiteral("document.querySelector('input[type=checkbox]').click()"));
  QTRY_VERIFY_WITH_TIMEOUT(window.getLatestContent().contains(QStringLiteral("- [x] interactive task")),
                           10000);
}

void TestMarkdownViewWindowLazy::inactiveReadUnavailableActions() {
  const auto buffer = openNote(QStringLiteral("unavailable.md"), QByteArrayLiteral("# Unavailable\n"));
  QVERIFY(buffer.isValid());
  Window window(m_services, buffer, nullptr, ViewWindowMode::Read);
  showWindow(window);
  QTRY_VERIFY_WITH_TIMEOUT(renderedHeading(window.findChild<MarkdownViewer *>(),
                                           QStringLiteral("Unavailable")), 30000);
  window.hide();
  QVERIFY(expireHiddenReader(window));
  window.clearHighlights();
  window.handleFindAndReplaceWidgetClosed();
  QVERIFY(!window.findChild<MarkdownViewer *>());
  window.handleFindNext({QStringLiteral("Unavailable")}, FindNone);
  bool counted = false;
  window.fetchWordCountInfo([&](const WordCountInfo &) { counted = true; });
  QVERIFY(!counted);
  QVERIFY(!window.findChild<MarkdownViewer *>());
  bool unavailable = false;
  for (auto *label : window.findChildren<QLabel *>()) {
    unavailable |= label->text().contains(QStringLiteral("Unable to load preview"));
  }
  QVERIFY(unavailable);
  showWindow(window);
  auto *viewer = window.findChild<MarkdownViewer *>();
  QVERIFY(viewer);
  QTRY_VERIFY_WITH_TIMEOUT(renderedHeading(viewer, QStringLiteral("Unavailable")), 30000);
  window.fetchWordCountInfo([&](const WordCountInfo &p_info) {
    counted = p_info.m_wordCount > 0;
  });
  QTRY_VERIFY_WITH_TIMEOUT(counted, 10000);
}

void TestMarkdownViewWindowLazy::inactiveProtectedResources() {
  const auto buffer = openProtectedNote(QByteArrayLiteral("# Protected saved\n"));
  QVERIFY(buffer.isValid() && buffer.isEncrypted());
  Window window(m_services, buffer, nullptr, ViewWindowMode::Edit);
  showWindow(window);
  auto *editor = window.findChild<MarkdownEditor *>();
  auto *jobs = window.findChild<MarkdownWebJobController *>();
  auto *host = window.findChild<MarkdownWebJobHost *>();
  QVERIFY(editor && jobs && host);
  QPointer<WebViewJobSession> fallback;
  QPointer<QWebEngineProfile> fallbackProfile;
  int fallbackCreations = 0;
  connect(host, &MarkdownWebJobHost::protectedSessionReady, &window,
          [&](WebViewJobSession *p_session, const QByteArray &) {
            ++fallbackCreations;
            fallback = p_session;
            if (p_session) {
              auto *page = qobject_cast<QWebEnginePage *>(p_session->parent());
              if (page) {
                fallbackProfile = page->profile();
              }
            }
          });
  QSignalSpy highlighted(jobs, &MarkdownWebJobController::highlightCodeBlockReady);
  editor->externalCodeBlockHighlightRequested(
      8, 81, QStringLiteral("```javascript\nconst privateValue = 42;\n```"));
  QTRY_VERIFY_WITH_TIMEOUT(!highlighted.isEmpty(), 30000);
  QVERIFY(fallback && fallbackProfile);
  appendDraft(editor, QStringLiteral("\n# Protected unsaved draft\n"));
  const auto draft = editor->getText();
  const auto undo = editor->getTextEdit()->document()->availableUndoSteps();
  // First retire the EditOnly protected assistance runtime with no reader at all.
  window.hide();
  QVERIFY(expireHiddenReader(window));
  QVERIFY(!fallback && !fallbackProfile);
  QVERIFY(!window.findChild<MarkdownViewer *>());
  const auto creations = fallbackCreations;
  int cancelled = 0;
  jobs->fetchHeadingAnchor(QStringLiteral("# Late protected work\n"), 0,
      [&](const MarkdownWebJobController::HeadingAnchorResult &p_result) {
        if (p_result.m_status == WebViewJobResult::Status::Cancelled) {
          ++cancelled;
        }
      });
  QCoreApplication::processEvents();
  QCOMPARE(cancelled, 1);
  QCOMPARE(fallbackCreations, creations);
  markdownConfig().setEditViewMode(MarkdownEditorConfig::EditPreview);
  window.handleEditorConfigChange();
  QVERIFY(!window.findChild<MarkdownViewer *>());
  showWindow(window);
  QPointer<MarkdownViewer> reader = window.findChild<MarkdownViewer *>();
  QVERIFY(reader);
  QTRY_VERIFY_WITH_TIMEOUT(renderedHeading(reader, QStringLiteral("Protected unsaved draft")),
                           30000);
  QPointer<QWebEngineProfile> profile = reader->page()->profile();
  QVERIFY(profile && profile->isOffTheRecord());
  window.hide();
  QVERIFY(expireHiddenReader(window));
  QVERIFY(!reader && !profile && !fallback && !fallbackProfile);
  QCOMPARE(window.findChild<MarkdownEditor *>(), editor);
  QCOMPARE(editor->getText(), draft);
  QCOMPARE(editor->getTextEdit()->document()->availableUndoSteps(), undo);
  showWindow(window);
  reader = window.findChild<MarkdownViewer *>();
  QVERIFY(reader);
  QTRY_VERIFY_WITH_TIMEOUT(renderedHeading(reader, QStringLiteral("Protected unsaved draft")),
                           30000);
  profile = reader->page()->profile();
  QVERIFY(profile && profile->isOffTheRecord());
  window.hide();
  QVERIFY(expireHiddenReader(window));
  QVERIFY(!reader && !profile);
  QVERIFY(m_buffers->beginProtectedLocking());
  showWindow(window);
  QVERIFY(!window.findChild<MarkdownViewer *>());
  // Lock retains the native writer through the normal save/close barrier.
  QCOMPARE(window.findChild<MarkdownEditor *>(), editor);
  QCOMPARE(editor->getText(), draft);
  QVERIFY(window.aboutToClose(true));
  QVERIFY(!window.findChild<MarkdownEditor *>());
  QVERIFY(!window.findChild<PreviewHelper *>());
  QVERIFY(!window.findChild<MarkdownViewer *>());
  QVERIFY(!window.isEnabled());
}

void TestMarkdownViewWindowLazy::printAfterReaderRestoration() {
  const auto buffer = openNote(QStringLiteral("restored-print.md"),
                               QByteArrayLiteral("# Restored printable heading\n\nFresh body\n"));
  QVERIFY(buffer.isValid());
  Window window(m_services, buffer, nullptr, ViewWindowMode::Read);
  showWindow(window);
  QTRY_VERIFY_WITH_TIMEOUT(renderedHeading(window.findChild<MarkdownViewer *>(),
                                           QStringLiteral("Restored printable heading")), 30000);
  window.hide();
  QVERIFY(expireHiddenReader(window));
  const auto output = m_directory.filePath(QStringLiteral("restored-print.pdf"));
  auto printer = QSharedPointer<QPrinter>::create();
  printer->setOutputFormat(QPrinter::PdfFormat);
  printer->setOutputFileName(output);
  window.printDocument(printer);
  QVERIFY(!window.findChild<MarkdownViewer *>());
  QVERIFY(!QFileInfo::exists(output));
  window.show();
  auto *viewer = window.findChild<MarkdownViewer *>();
  QVERIFY(viewer);
  QSignalSpy printed(viewer, &MarkdownViewer::printFinished);
  // Printing immediately after Show must await fresh document workFinished.
  window.printDocument(printer);
  QTRY_COMPARE_WITH_TIMEOUT(printed.count(), 1, 30000);
  QVERIFY(printed.first().first().toBool());
  QPdfDocument pdf;
  QCOMPARE(pdf.load(output), QPdfDocument::Error::None);
  const auto text = evaluate(viewer->page(), QStringLiteral("document.body.innerText")).toString();
  QVERIFY(text.contains(QStringLiteral("Restored printable heading")));
  QVERIFY(text.contains(QStringLiteral("Fresh body")));
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
