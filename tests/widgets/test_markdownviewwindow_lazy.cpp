#include <QtTest>

#include <QAbstractTextDocumentLayout>
#include <QAction>
#include <QApplication>
#include <QBuffer>
#include <QChildEvent>
#include <QClipboard>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QMimeData>
#include <QPdfDocument>
#include <QPdfSelection>
#include <QPointer>
#include <QPrinter>
#include <QScopeGuard>
#include <QSemaphore>
#include <QSplitter>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextLayout>
#include <QThreadPool>
#include <QTimer>
#include <QUuid>
#include <QVBoxLayout>
#include <QWebChannel>
#include <QWebEngineProfile>
#include <QWebEngineView>
#include <QWindow>
#include <QtConcurrent/QtConcurrentRun>

#include <memory>

#include <controllers/markdownrendercontroller.h>
#include <controllers/markdownwebjobcontroller.h>
#include <core/configmgr2.h>
#include <core/editorconfig.h>
#include <core/hooknames.h>
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
#include <core/theme.h>
#include <export/webviewexporter.h>
#include <gui/services/themeservice.h>
#include <gui/services/vxpdfschemehandler.h>
#include <gui/services/webengineprofileservice.h>
#include <gui/services/webviewservice.h>
#include <vtextedit/markdownhighlighter.h>
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
class NativeShell : public QWidget {
public:
  bool m_recreatePending = false;
  bool m_recreated = false;

protected:
  bool eventFilter(QObject *, QEvent *p_event) override {
    if (m_recreatePending && p_event->type() == QEvent::ChildAdded &&
        !static_cast<QChildEvent *>(p_event)->child()->isWidgetType()) {
      // Recreate before the new adapter/viewer has connected page-lifecycle signals.
      m_recreatePending = false;
      const auto state = windowState();
      const bool visible = isVisible();
      QList<QPointer<QWidget>> visibleChildren;
      for (auto *child : findChildren<QWidget *>()) {
        if (child->isVisible()) {
          visibleChildren.append(child);
        }
      }
      destroy();
      create();
      if (auto *handle = windowHandle()) {
        handle->setWindowStates(state);
        setVisible(visible);
        // Windows has native overlay descendants; preserve their prior visibility too.
        for (const auto &child : visibleChildren) {
          if (child && !child->isVisible()) {
            child->show();
          }
        }
        m_recreated = true;
      }
    }
    return false;
  }
};

class Window : public MarkdownViewWindow2 {
public:
  using MarkdownViewWindow2::aboutToClose;
  using MarkdownViewWindow2::handleFindAndReplaceWidgetClosed;
  using MarkdownViewWindow2::handleFindNext;
  using MarkdownViewWindow2::MarkdownViewWindow2;
  using MarkdownViewWindow2::printDocument;
  using ViewWindow2::fetchWordCountInfo;
  using ViewWindow2::isViewFullScreen;
  using ViewWindow2::onFocusGained;
  ~Window() override {
    getServices().get<BufferService>()->unregisterActiveWriter(getBuffer().id(),
                                                               reinterpret_cast<quintptr>(this));
  }
};

// Block the real shared executor so replacement races do not depend on parse duration.
class PoolBlocker {
public:
  PoolBlocker() : m_previousMaximum(QThreadPool::globalInstance()->maxThreadCount()) {
    QThreadPool::globalInstance()->setMaxThreadCount(1);
    m_future = QtConcurrent::run([this]() {
      m_started.release();
      m_release.acquire();
    });
    m_didStart = m_started.tryAcquire(1, 10000);
  }
  ~PoolBlocker() {
    unblock();
    m_future.waitForFinished();
    QThreadPool::globalInstance()->setMaxThreadCount(m_previousMaximum);
  }
  bool started() const { return m_didStart; }
  void unblock() {
    if (!m_unblocked) {
      m_unblocked = true;
      m_release.release();
    }
  }

private:
  int m_previousMaximum;
  QSemaphore m_started;
  QSemaphore m_release;
  QFuture<void> m_future;
  bool m_didStart = false;
  bool m_unblocked = false;
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

QColor sourceTokenColor(MarkdownEditor *p_editor, int p_line, int p_column = 0) {
  const auto block = p_editor->getTextEdit()->document()->findBlockByNumber(p_line);
  if (block.isValid() && block.layout()) {
    for (const auto &format : block.layout()->formats()) {
      if (format.start <= p_column && p_column < format.start + format.length &&
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

bool waitForRenderedHeading(MarkdownViewer *p_viewer, const QString &p_heading) {
  const QPointer<MarkdownViewer> viewer(p_viewer);
  // QTRY re-evaluates its predicate after success. This JavaScript query pumps events,
  // so a subsequent preview generation could invalidate that successful observation.
  return QTest::qWaitFor(
      [&]() {
        if (!viewer || !viewer->adapter()->isDocumentReady()) {
          return false;
        }
        const auto generation = viewer->adapter()->documentGeneration();
        const auto text =
            evaluate(viewer->page(), QStringLiteral("document.body.innerText")).toString();
        return viewer && viewer->adapter()->isDocumentReady() &&
               viewer->adapter()->documentGeneration() == generation && text.contains(p_heading);
      },
      30000);
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
  void concealmentMenuPreservesSourceAndIndependentPreferences();
  void imagePopupCreatesOnlyRequestedSurface();
  void printUsesCurrentMode_data();
  void printUsesCurrentMode();
  void presentationCreatesReader_data();
  void presentationCreatesReader();
  void presentationHiddenDuringPreparationDoesNotResume();
  void protectedLockWithoutReader();
  void nativeGrammarPolicy_data();
  void nativeGrammarPolicy();
  void staleNativeTaskClickCannotEditNewDraft();
  void nativeRequestsAcrossRetainedReplacement();
  void retainedTaskAndExtractionUseIncomingGeneration();
  void editOnlyReplacementResumesHeadings();
  void unavailableReplacementCannotSaveOutgoingSource();
  void nativeHtmlExportIncludesEnhancements();
  void legacyResourcesKeepCustomization();
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
  markdownConfig().setConcealMarkdownMarkersEnabled(false);
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
  QVERIFY(QTest::qWaitForWindowExposed(&p_window));
  p_window.activateWindow();
  QApplication::setActiveWindow(&p_window);
  const QPointer<MarkdownEditor> editor = p_window.findChild<MarkdownEditor *>();
  if (editor && editor->isVisible() && editor->isEnabled() &&
      !(p_window.getBuffer().isEncrypted() && m_buffers->isProtectedLocking())) {
    editor->getTextEdit()->setFocus();
    QTRY_VERIFY(editor && editor->getTextEdit()->hasFocus());
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
  auto *helper = firstWindow.findChild<PreviewHelper *>();
  auto *secondHelper = secondWindow.findChild<PreviewHelper *>();
  QVERIFY(editor && helper && secondHelper);
  bool graphPixels = false;
  bool mathPixels = false;
  connect(secondHelper, &PreviewHelper::inplacePreviewMathBlockUpdated, &secondWindow,
          [&](const QVector<QSharedPointer<vte::PreviewItem>> &p_items) {
            for (const auto &item : p_items) {
              mathPixels |= item && !item->m_image.isNull();
            }
          });
  connect(helper, &PreviewHelper::inplacePreviewCodeBlockUpdated, &firstWindow,
          [&](const QVector<QSharedPointer<vte::PreviewItem>> &p_items) {
            for (const auto &item : p_items) {
              graphPixels |= item && !item->m_image.isNull();
            }
          });
  appendDraft(editor, QStringLiteral("\n```javascript\nconst answer = 42;\n```\n\n"
                                     "```mermaid\ngraph TD; A-->B;\n```\n"));
  appendDraft(secondWindow.findChild<MarkdownEditor *>(),
              QStringLiteral("\n$$\n\\alpha^2 + 17\n$$\n"));
  QTRY_VERIFY_WITH_TIMEOUT(graphPixels && mathPixels, 30000);
  // Native fenced-code highlighting and both shared-runtime preview consumers
  // work without constructing a document reader for either editor.
  QTRY_VERIFY_WITH_TIMEOUT(sourceTokenColor(editor, 3).isValid() &&
                               sourceTokenColor(editor, 3, 15).isValid() &&
                               sourceTokenColor(editor, 3) != sourceTokenColor(editor, 3, 15),
                           30000);
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

void TestMarkdownViewWindowLazy::concealmentMenuPreservesSourceAndIndependentPreferences() {
  markdownConfig().setInplacePreviewSources(MarkdownEditorConfig::NoInplacePreview);
  const QString destination =
      QStringLiteral("abcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyz");
  const QString source =
      QStringLiteral("# Heading ###\n\n*emphasis*\n\n**strong**\n\n~~strike~~\n\n==mark==\n\n"
                     "`code`\n\n[label](https://example.invalid/path)\n\n$math$\n\n$$display$$\n\n"
                     "![image](%1)\n\noutside")
          .arg(destination);
  const auto firstBuffer = openNote(QStringLiteral("conceal-first.md"), source.toUtf8());
  const auto secondBuffer = openNote(QStringLiteral("conceal-second.md"), source.toUtf8());
  QVERIFY(firstBuffer.isValid() && secondBuffer.isValid());
  Window first(m_services, firstBuffer, nullptr, ViewWindowMode::Edit);
  Window second(m_services, secondBuffer, nullptr, ViewWindowMode::Edit);
  showWindow(first);
  showWindow(second);
  auto *firstEditor = first.findChild<MarkdownEditor *>();
  auto *secondEditor = second.findChild<MarkdownEditor *>();
  QVERIFY(firstEditor && secondEditor);
  appendDraft(firstEditor, QStringLiteral(" draft"));
  auto secondCursor = secondEditor->getTextEdit()->textCursor();
  secondCursor.movePosition(QTextCursor::End);
  secondEditor->getTextEdit()->setTextCursor(secondCursor);
  const auto firstCursor = firstEditor->getTextEdit()->textCursor();
  const auto firstUndo = firstEditor->document()->availableUndoSteps();
  const auto secondUndo = secondEditor->document()->availableUndoSteps();

  // Host the same editor-config notification boundary as ViewAreaController.
  const int hook =
      m_hooks->addAction(HookNames::ConfigEditorChanged, [&](HookContext &, const QVariantMap &) {
        first.handleEditorConfigChange();
        second.handleEditorConfigChange();
      });
  const auto removeHook = qScopeGuard([&] { m_hooks->removeAction(hook); });
  auto *destinations = first.findChild<QAction *>(QStringLiteral("concealDestinationsAction"));
  auto *markers = first.findChild<QAction *>(QStringLiteral("concealMarkdownMarkersAction"));
  auto *otherDestinations =
      second.findChild<QAction *>(QStringLiteral("concealDestinationsAction"));
  auto *otherMarkers = second.findChild<QAction *>(QStringLiteral("concealMarkdownMarkersAction"));
  QVERIFY(destinations && markers && otherDestinations && otherMarkers);
  auto *otherMenu = qobject_cast<QMenu *>(otherDestinations->parent());
  QVERIFY(otherMenu);

  const auto width = [](MarkdownEditor *p_editor, const QString &p_token, int p_length) {
    const int position = p_editor->getText().indexOf(p_token);
    const auto block = p_editor->document()->findBlock(position);
    if (position < 0 || !block.isValid()) {
      return qreal(-1);
    }
    p_editor->document()->documentLayout()->blockBoundingRect(block);
    const auto line = block.layout()->lineForTextPosition(position - block.position());
    return line.isValid() ? qAbs(line.cursorToX(position + p_length - block.position()) -
                                 line.cursorToX(position - block.position()))
                          : qreal(-1);
  };
  const QList<QPair<QString, int>> syntax{
      {QStringLiteral("# Heading"), 1},  {QStringLiteral("###"), 3},
      {QStringLiteral("*emphasis*"), 1}, {QStringLiteral("**strong**"), 2},
      {QStringLiteral("~~strike~~"), 2}, {QStringLiteral("==mark=="), 2},
      {QStringLiteral("`code`"), 1},     {QStringLiteral("[label]"), 1},
      {QStringLiteral("$math$"), 1},     {QStringLiteral("$$display$$"), 2}};
  QTRY_VERIFY(width(firstEditor, destination, destination.size()) > 1.0);
  QTRY_VERIFY(width(secondEditor, destination, destination.size()) > 1.0);
  const qreal firstFullDestination = width(firstEditor, destination, destination.size());
  const qreal secondFullDestination = width(secondEditor, destination, destination.size());
  const auto hasDisplay = [&](bool p_destinations, bool p_markers) {
    for (auto *editor : {firstEditor, secondEditor}) {
      for (const auto &span : syntax) {
        const qreal actual = width(editor, span.first, span.second);
        if (actual < 0 || (p_markers ? actual > 0.1 : actual <= 0.1)) {
          return false;
        }
      }
      const qreal full = editor == firstEditor ? firstFullDestination : secondFullDestination;
      const qreal actual = width(editor, destination, destination.size());
      if (actual < 0 || (p_destinations ? actual + 1.0 >= full : qAbs(actual - full) >= 1.0)) {
        return false;
      }
    }
    return true;
  };
  const auto verifySourceState = [&] {
    QCOMPARE(firstEditor->getText(), source + QStringLiteral(" draft"));
    QCOMPARE(secondEditor->getText(), source);
    QCOMPARE(firstEditor->getTextEdit()->textCursor().position(), firstCursor.position());
    QCOMPARE(firstEditor->getTextEdit()->textCursor().anchor(), firstCursor.anchor());
    QCOMPARE(secondEditor->getTextEdit()->textCursor().position(), secondCursor.position());
    QCOMPARE(firstEditor->document()->availableUndoSteps(), firstUndo);
    QCOMPARE(secondEditor->document()->availableUndoSteps(), secondUndo);
    QVERIFY(first.isModified());
    QVERIFY(!second.isModified());
  };
  QTRY_VERIFY(hasDisplay(false, false));
  markers->trigger();
  QTRY_VERIFY(hasDisplay(false, true));
  verifySourceState();
  destinations->trigger();
  QTRY_VERIFY(hasDisplay(true, true));
  verifySourceState();
  markers->trigger();
  QTRY_VERIFY(hasDisplay(true, false));
  verifySourceState();

  // A sibling menu opened after the preference changed must show the current state.
  otherMenu->popup(second.mapToGlobal(QPoint(50, 50)));
  QTRY_VERIFY(otherMenu->isVisible());
  QVERIFY(otherDestinations->isChecked());
  QVERIFY(!otherMarkers->isChecked());
  otherMenu->hide();
  otherDestinations->trigger();
  QTRY_VERIFY(hasDisplay(false, false));
  verifySourceState();
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
  appendDraft(editor, QStringLiteral("\n```javascript\nconst color = 42;\n```\n\n"
                                     "$$\n\\alpha^2 + 42\n$$\n"));
  const auto source = editor->getText();
  auto *textEdit = editor->getTextEdit();
  const auto cursor = textEdit->textCursor();
  const auto undo = textEdit->document()->availableUndoSteps();
  QSignalSpy highlighted(jobs, &MarkdownWebJobController::highlightMathReady);
  QTRY_VERIFY_WITH_TIMEOUT(!highlighted.isEmpty(), 30000);
  QTRY_VERIFY_WITH_TIMEOUT(sourceTokenColor(editor, 3).isValid(), 30000);
  const auto oldColor = sourceTokenColor(editor, 3);
  m_theme->switchTheme(QStringLiteral("vscode-dark"));
  QVERIFY(!cleanWindow.isModified());
  QVERIFY(!cleanWindow.findChild<MarkdownViewer *>());
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
  QSignalSpy rendered(viewer->adapter(), &MarkdownViewerAdapter::htmlUpdated);
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
  const auto output = m_directory.filePath(
      QStringLiteral("print-%1.pdf").arg(QString::fromLatin1(QTest::currentDataTag())));
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
                    QStringLiteral("document.body.innerText"))
               .toString();
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

void TestMarkdownViewWindowLazy::presentationCreatesReader_data() {
  QTest::addColumn<bool>("delayNativeRender");
  QTest::newRow("normal-ordering") << false;
  QTest::newRow("bridge-before-document") << true;
}

void TestMarkdownViewWindowLazy::presentationCreatesReader() {
  QFETCH(bool, delayNativeRender);
  const auto buffer =
      openNote(QStringLiteral("slides.md"), QByteArrayLiteral("# Slides\n\n## First\n"));
  QVERIFY(buffer.isValid());
  NativeShell shell;
  QVBoxLayout layout(&shell);
  Window window(m_services, buffer, &shell, ViewWindowMode::Edit);
  layout.addWidget(&window);
  shell.resize(1100, 800);
  shell.show();
  QVERIFY(QTest::qWaitForWindowExposed(&shell));
  auto *editor = window.findChild<MarkdownEditor *>();
  QVERIFY(editor);
  // Presentation flushes the draft through the focus-registered active writer.
  shell.activateWindow();
  QApplication::setActiveWindow(&shell);
  editor->getTextEdit()->setFocus();
  QTRY_VERIFY(editor->getTextEdit()->hasFocus());
  appendDraft(editor, QStringLiteral("\nUnsaved slide text\n"));
  QAction *presentation = nullptr;
  for (auto *action : window.findChildren<QAction *>()) {
    if (action->property("iconName").toString() == QStringLiteral("presentation_editor.svg")) {
      presentation = action;
      break;
    }
  }
  QVERIFY(presentation);
  QSignalSpy fullscreen(&window, &ViewWindow2::viewFullScreenChanged);
  std::unique_ptr<PoolBlocker> pool;
  if (delayNativeRender) {
    pool = std::make_unique<PoolBlocker>();
    QVERIFY(pool->started());
  }
  window.installEventFilter(&shell);
  shell.m_recreatePending = true;
  presentation->trigger();
  QVERIFY(shell.m_recreated);
  auto *viewer = window.findChild<MarkdownViewer *>();
  QVERIFY(viewer);
  QSignalSpy requests(viewer->adapter(), &MarkdownViewerAdapter::presentationModeRequested);
  QSignalSpy states(viewer->adapter(), &MarkdownViewerAdapter::presentationStateChanged);
  // Cocoa can deliver the reader's focus gain after the mode-switch call has returned,
  // without a preceding dirty focus loss to acknowledge our own buffer sync.
  window.onFocusGained();
  if (pool) {
    // Exercise real bridge readiness while the initial native HTML is still queued.
    QTRY_VERIFY_WITH_TIMEOUT(viewer->adapter()->isReady(), 30000);
    QVERIFY(!viewer->adapter()->isDocumentReady());
    QVERIFY(!window.isViewFullScreen());
    QVERIFY(!presentation->isChecked());
    QCOMPARE(requests.size(), 0);
    pool->unblock();
  }
  const bool entered = QTest::qWaitFor([&] { return window.isViewFullScreen(); }, 30000);
  QString failureDetails;
  if (!entered) {
    QDebug(&failureDetails)
        << "fullscreen transitions:" << fullscreen << "post-trigger requests/states:" << requests
        << states << "adapter ready/document ready:" << viewer->adapter()->isReady()
        << viewer->adapter()->isDocumentReady() << "native fullscreen:" << window.isFullScreen()
        << "viewer visible:" << viewer->isVisible() << "mode:" << static_cast<int>(window.getMode())
        << "web state:"
        << evaluate(viewer->page(),
                    QStringLiteral("JSON.stringify({requested: vxPresentation.requested,"
                                   "active: vxPresentation.isActive(),"
                                   "ready: vxPresentation.session && vxPresentation.session.ready,"
                                   "workers: vxcore.numOfOngoingWorkers,pending: "
                                   "vxcore.pendingData.document !== null})"));
  }
  QVERIFY2(entered, qPrintable(failureDetails));
  QVERIFY(presentation->isChecked());
  if (delayNativeRender) {
    QCOMPARE(requests.size(), 1);
    QVERIFY(requests.first().at(0).toBool());
  }
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

void TestMarkdownViewWindowLazy::presentationHiddenDuringPreparationDoesNotResume() {
  const auto buffer =
      openNote(QStringLiteral("hidden-slides.md"), QByteArrayLiteral("# Hidden presentation\n"));
  QVERIFY(buffer.isValid());
  QWidget shell;
  QVBoxLayout layout(&shell);
  Window window(m_services, buffer, &shell, ViewWindowMode::Edit);
  layout.addWidget(&window);
  shell.resize(1100, 800);
  shell.show();
  QVERIFY(QTest::qWaitForWindowExposed(&shell));
  shell.activateWindow();
  QApplication::setActiveWindow(&shell);
  auto *editor = window.findChild<MarkdownEditor *>();
  QVERIFY(editor);
  editor->getTextEdit()->setFocus();
  QTRY_VERIFY(editor->getTextEdit()->hasFocus());
  appendDraft(editor, QStringLiteral("\nHidden draft remains\n"));
  QAction *presentation = nullptr;
  for (auto *action : window.findChildren<QAction *>()) {
    if (action->property("iconName").toString() == QStringLiteral("presentation_editor.svg")) {
      presentation = action;
      break;
    }
  }
  QVERIFY(presentation);
  PoolBlocker pool;
  QVERIFY(pool.started());
  const auto hide = connect(&window, &ViewWindow2::modeChanged, &shell, [&]() {
    if (window.getMode() == ViewWindowMode::Read) {
      shell.hide();
    }
  });
  presentation->trigger();
  disconnect(hide);
  QVERIFY(!shell.isVisible());
  auto *viewer = window.findChild<MarkdownViewer *>();
  QVERIFY(viewer);
  QSignalSpy requests(viewer->adapter(), &MarkdownViewerAdapter::presentationModeRequested);
  shell.show();
  pool.unblock();
  QVERIFY(waitForRenderedHeading(viewer, QStringLiteral("Hidden draft remains")));
  QCOMPARE(requests.size(), 0);
  QVERIFY(!window.isViewFullScreen());
  QVERIFY(!presentation->isChecked());
  QVERIFY(editor->getText().contains(QStringLiteral("Hidden draft remains")));
}

void TestMarkdownViewWindowLazy::nativeGrammarPolicy_data() {
  QTest::addColumn<bool>("protectedNote");
  QTest::addColumn<bool>("userOptionsEnabled");
  QTest::newRow("ordinary-defaults") << false << true;
  QTest::newRow("ordinary-disabled") << false << false;
  QTest::newRow("protected-defaults") << true << true;
  QTest::newRow("protected-disabled") << true << false;
}

void TestMarkdownViewWindowLazy::nativeGrammarPolicy() {
  QFETCH(bool, protectedNote);
  QFETCH(bool, userOptionsEnabled);
  markdownConfig().setHtmlTagEnabled(userOptionsEnabled);
  markdownConfig().setAutoBreakEnabled(userOptionsEnabled);
  markdownConfig().setLinkifyEnabled(userOptionsEnabled);
  markdownConfig().setEditViewMode(MarkdownEditorConfig::EditPreview);
  const auto body = QByteArrayLiteral("First soft line\nSecond soft line\n\n"
                                      "First hard line  \nSecond hard line\n\n"
                                      "<strong>Raw HTML</strong>\n\nhttps://example.com/grammar\n");
  const auto source = QByteArrayLiteral("# Saved grammar\n\n") + body;
  const auto buffer =
      protectedNote ? openProtectedNote(source) : openNote(QStringLiteral("grammar.md"), source);
  QVERIFY(buffer.isValid());
  QCOMPARE(buffer.isEncrypted(), protectedNote);
  Window window(m_services, buffer, nullptr, ViewWindowMode::Read);
  showWindow(window);
  auto *reader = window.findChild<MarkdownViewer *>();
  QVERIFY(reader);
  QVERIFY(!window.findChild<MarkdownEditor *>());
  QVERIFY(waitForRenderedHeading(reader, QStringLiteral("Saved grammar")));

  // The protected template historically used fixed HTML/linkify on and soft
  // newlines, not the ordinary editor preferences. Inspect the installed DOM.
  const auto grammar = QStringLiteral(R"JS((() => {
    const content = document.querySelector('#vx-content');
    const paragraphs = Array.from(content.querySelectorAll('p'));
    const soft = paragraphs.find(p => p.textContent.startsWith('First soft line'));
    const hard = paragraphs.find(p => p.textContent.startsWith('First hard line'));
    return [soft ? soft.querySelectorAll('br').length : -1,
            hard ? hard.querySelectorAll('br').length : -1,
            content.querySelectorAll('strong').length,
            content.querySelectorAll('a[href="https://example.com/grammar"]').length];
  })())JS");
  const auto renderedGrammar = evaluate(reader->page(), grammar).toList();
  QCOMPARE(renderedGrammar.size(), 4);
  QCOMPARE(renderedGrammar.at(0).toInt(), !protectedNote && userOptionsEnabled ? 1 : 0);
  QCOMPARE(renderedGrammar.at(1).toInt(), 1);
  QCOMPARE(renderedGrammar.at(2).toInt(), protectedNote || userOptionsEnabled ? 1 : 0);
  QCOMPARE(renderedGrammar.at(3).toInt(), protectedNote || userOptionsEnabled ? 1 : 0);

  window.setMode(ViewWindowMode::Edit);
  auto *editor = window.findChild<MarkdownEditor *>();
  auto *native = window.findChild<MarkdownRenderController *>();
  QVERIFY(editor && native);
  QSignalSpy rendered(native, &MarkdownRenderController::rendered);
  const auto draft = QString::fromUtf8(QByteArrayLiteral("# Draft grammar\n\n") + body);
  QTextCursor cursor(editor->getTextEdit()->document());
  cursor.select(QTextCursor::Document);
  cursor.insertText(draft);
  QVERIFY(waitForRenderedHeading(reader, QStringLiteral("Draft grammar")));
  QCOMPARE(evaluate(reader->page(), grammar).toList(), renderedGrammar);
  QTRY_VERIFY_WITH_TIMEOUT(editor->getHighlighter()->currentDocument(), 30000);
  const auto document = editor->getHighlighter()->currentDocument();
  QCOMPARE(document->source(), draft.toUtf8());
  QVERIFY(!rendered.isEmpty());
  const auto result = qvariant_cast<MarkdownHtmlResult>(rendered.last().at(1));
  QVERIFY(result.error.isEmpty());
  QCOMPARE(result.document, document);
  QCOMPARE(editor->getText(), draft);
  QVERIFY(window.isModified());
  editor->getTextEdit()->document()->undo();
  QCOMPARE(editor->getText(), QString::fromUtf8(source));
}

void TestMarkdownViewWindowLazy::staleNativeTaskClickCannotEditNewDraft() {
  markdownConfig().setEditViewMode(MarkdownEditorConfig::EditPreview);
  const auto buffer =
      openNote(QStringLiteral("native-task.md"), QByteArrayLiteral("# Task\n\n- [ ] item\n"));
  Window window(m_services, buffer, nullptr, ViewWindowMode::Edit);
  showWindow(window);
  auto *editor = window.findChild<MarkdownEditor *>();
  auto *viewer = window.findChild<MarkdownViewer *>();
  QVERIFY(editor && viewer);
  QVERIFY(waitForRenderedHeading(viewer, QStringLiteral("Task")));
  const auto previous = viewer->adapter()->documentGeneration();
  QVERIFY(!previous.isEmpty());

  QTextCursor cursor(editor->getTextEdit()->document());
  cursor.setPosition(0);
  cursor.insertText(QStringLiteral("Prefix\n"));
  const auto draft = editor->getText();
  QSignalSpy rejected(viewer->adapter(), &MarkdownViewerAdapter::taskListToggleRejected);
  viewer->adapter()->toggleTaskListItem(previous, 2, true);
  QCOMPARE(rejected.size(), 1);
  QCOMPARE(rejected.first().at(0).toString(), previous);
  QCOMPARE(editor->getText(), draft);

  QTRY_VERIFY_WITH_TIMEOUT(viewer->adapter()->isDocumentReady() &&
                               !viewer->adapter()->documentGeneration().isEmpty() &&
                               viewer->adapter()->documentGeneration() != previous,
                           30000);
  const auto current = viewer->adapter()->documentGeneration();
  viewer->adapter()->toggleTaskListItem(current, 3, true);
  QCOMPARE(editor->getText(),
           QString(draft).replace(QStringLiteral("[ ] item"), QStringLiteral("[x] item")));
  QCOMPARE(rejected.size(), 1);
}

void TestMarkdownViewWindowLazy::nativeRequestsAcrossRetainedReplacement() {
  const auto alpha = openNote(QStringLiteral("native-alpha.md"), QByteArrayLiteral("# Alpha\n"));
  const auto beta = openNote(QStringLiteral("native-beta.md"), QByteArrayLiteral("# Beta\n"));
  QVERIFY(alpha.isValid() && beta.isValid());
  Window window(m_services, alpha, nullptr, ViewWindowMode::Read);
  showWindow(window);
  auto *reader = window.findChild<MarkdownViewer *>();
  auto *native = window.findChild<MarkdownRenderController *>();
  QVERIFY(reader && native);
  QVERIFY(waitForRenderedHeading(reader, QStringLiteral("Alpha")));
  const auto alphaToken = reader->adapter()->documentGeneration();
  QVERIFY(native->isCurrent(alphaToken));
  auto *page = reader->page();
  QSignalSpy nativeHtml(native, &MarkdownRenderController::rendered);
  QSignalSpy printed(reader, &MarkdownViewer::printFinished);

  // Queue real native printing work on the shared executor, then keep reusing the
  // view while it is active. The original controller must retain its bounded slots.
  PoolBlocker pool;
  QVERIFY(pool.started());
  const auto oldOutput = m_directory.filePath(QStringLiteral("retired-native-print.pdf"));
  auto oldPrinter = QSharedPointer<QPrinter>::create();
  oldPrinter->setOutputFormat(QPrinter::PdfFormat);
  oldPrinter->setOutputFileName(oldOutput);
  window.printDocument(oldPrinter);
  QVERIFY(native->isBusy());
  FileOpenSettings settings;
  settings.m_mode = ViewWindowMode::Read;
  for (int replacement = 0; replacement < 9; ++replacement) {
    const auto &incoming = replacement % 2 == 0 ? beta : alpha;
    QVERIFY(window.replaceBuffer(incoming, settings));
    QCOMPARE(window.findChild<MarkdownRenderController *>(), native);
    QCOMPARE(window.findChildren<MarkdownRenderController *>().size(), 1);
    QCOMPARE(window.findChild<MarkdownViewer *>(), reader);
    QCOMPARE(reader->page(), page);
    QVERIFY(native->isBusy());
    QVERIFY(!native->isCurrent(alphaToken));
  }
  QCOMPARE(nativeHtml.size(), 0);
  pool.unblock();
  QVERIFY(waitForRenderedHeading(reader, QStringLiteral("Beta")));
  QTRY_VERIFY_WITH_TIMEOUT(!native->isBusy(), 30000);
  QCOMPARE(nativeHtml.size(), 1);
  const auto token = reader->adapter()->documentGeneration();
  QVERIFY(!token.isEmpty() && token != alphaToken && native->isCurrent(token));
  QCOMPARE(nativeHtml.first().at(0).toString(), token);
  const auto result = qvariant_cast<MarkdownHtmlResult>(nativeHtml.first().at(1));
  QVERIFY(result.error.isEmpty() && result.document && !result.html.isEmpty());
  QCOMPARE(result.document->source(), QByteArrayLiteral("# Beta\n"));
  QVERIFY(result.html.contains(QStringLiteral("Beta")));
  QVERIFY(!result.html.contains(QStringLiteral("Alpha")));
  QCOMPARE(reader->jobSession()->property("markdownJobBufferId").toString(), beta.id());
  QCOMPARE(printed.size(), 0);
  QVERIFY(!QFileInfo::exists(oldOutput));

  // Cancellation of the old print must not disable printing the incoming document.
  const auto output = m_directory.filePath(QStringLiteral("incoming-native-print.pdf"));
  auto printer = QSharedPointer<QPrinter>::create();
  printer->setOutputFormat(QPrinter::PdfFormat);
  printer->setOutputFileName(output);
  window.printDocument(printer);
  QTRY_COMPARE_WITH_TIMEOUT(printed.size(), 1, 30000);
  QVERIFY(printed.first().first().toBool());
  QPdfDocument pdf;
  QCOMPARE(pdf.load(output), QPdfDocument::Error::None);
  QVERIFY(waitForRenderedHeading(reader, QStringLiteral("Beta")));
  QVERIFY(!QFileInfo::exists(oldOutput));
}

void TestMarkdownViewWindowLazy::retainedTaskAndExtractionUseIncomingGeneration() {
  const auto alpha =
      openNote(QStringLiteral("task-alpha.md"), QByteArrayLiteral("# Alpha\n\n- [ ] alpha task\n"));
  const auto beta =
      openNote(QStringLiteral("task-beta.md"), QByteArrayLiteral("# Beta\n\n- [ ] beta task\n"));
  QVERIFY(alpha.isValid() && beta.isValid());
  Window window(m_services, alpha, nullptr, ViewWindowMode::Read);
  showWindow(window);
  auto *reader = window.findChild<MarkdownViewer *>();
  auto *native = window.findChild<MarkdownRenderController *>();
  QVERIFY(reader && native);
  QVERIFY(waitForRenderedHeading(reader, QStringLiteral("Alpha")));
  const auto alphaToken = reader->adapter()->documentGeneration();
  int staleWordCounts = 0;
  window.fetchWordCountInfo([&](const WordCountInfo &) { ++staleWordCounts; });
  FileOpenSettings settings;
  settings.m_mode = ViewWindowMode::Read;
  // No event processing between the real JavaScript extraction and buffer replacement.
  QVERIFY(window.replaceBuffer(beta, settings));
  QCOMPARE(window.findChild<MarkdownViewer *>(), reader);
  QVERIFY(waitForRenderedHeading(reader, QStringLiteral("Beta")));
  const auto betaToken = reader->adapter()->documentGeneration();
  QVERIFY(betaToken != alphaToken && native->isCurrent(betaToken));
  QCOMPARE(staleWordCounts, 0);
  QSignalSpy rejected(reader->adapter(), &MarkdownViewerAdapter::taskListToggleRejected);
  reader->adapter()->toggleTaskListItem(alphaToken, 2, true);
  QCOMPARE(rejected.size(), 1);
  QCOMPARE(window.getLatestContent(), QStringLiteral("# Beta\n\n- [ ] beta task\n"));
  QCOMPARE(alpha.getContentRaw(), QByteArrayLiteral("# Alpha\n\n- [ ] alpha task\n"));

  // Old saved-content replies cannot satisfy an incoming extraction either.
  QSignalSpy content(reader->adapter(), &MarkdownViewerAdapter::contentReady);
  reader->adapter()->saveContent();
  reader->adapter()->setSavedContent(alphaToken, {}, {}, QStringLiteral("retired alpha"), {});
  QCOMPARE(content.size(), 0);
  QTRY_COMPARE_WITH_TIMEOUT(content.size(), 1, 10000);
  QVERIFY(content.first().at(2).toString().contains(QStringLiteral("Beta")));
  QVERIFY(!content.first().at(2).toString().contains(QStringLiteral("retired alpha")));

  evaluate(reader->page(),
           QStringLiteral("document.querySelector('input[type=checkbox]').click()"));
  QTRY_COMPARE_WITH_TIMEOUT(window.getLatestContent(),
                            QStringLiteral("# Beta\n\n- [x] beta task\n"), 10000);
  QCOMPARE(rejected.size(), 1);
  bool currentCounted = false;
  window.fetchWordCountInfo(
      [&](const WordCountInfo &p_info) { currentCounted = p_info.m_wordCount > 0; });
  QTRY_VERIFY_WITH_TIMEOUT(currentCounted, 10000);
  QCOMPARE(staleWordCounts, 0);
}

void TestMarkdownViewWindowLazy::editOnlyReplacementResumesHeadings() {
  markdownConfig().setInplacePreviewSources(MarkdownEditorConfig::NoInplacePreview);
  const auto alpha = openNote(QStringLiteral("heading-alpha.md"), QByteArrayLiteral("# Alpha\n"));
  const auto beta = openNote(QStringLiteral("heading-beta.md"), QByteArrayLiteral("# Beta\n"));
  QVERIFY(alpha.isValid() && beta.isValid());
  Window window(m_services, alpha, nullptr, ViewWindowMode::Edit);
  showWindow(window);
  auto *editor = window.findChild<MarkdownEditor *>();
  auto *native = window.findChild<MarkdownRenderController *>();
  QVERIFY(editor && native);
  QSignalSpy nativeHtml(native, &MarkdownRenderController::rendered);
  FileOpenSettings settings;
  settings.m_mode = ViewWindowMode::Edit;
  QVERIFY(window.replaceBuffer(beta, settings));
  QCOMPARE(window.findChild<MarkdownEditor *>(), editor);
  QCOMPARE(window.findChild<MarkdownRenderController *>(), native);
  QVERIFY(!window.findChild<MarkdownViewer *>());
  int callbacks = 0;
  MarkdownRenderController::HeadingAnchorResult heading;
  native->fetchHeadingAnchor(editor->getText().toUtf8(), 0, {}, {},
                             [&](const MarkdownRenderController::HeadingAnchorResult &p_result) {
                               heading = p_result;
                               ++callbacks;
                             });
  QTRY_COMPARE_WITH_TIMEOUT(callbacks, 1, 10000);
  QVERIFY(heading.found && !heading.cancelled && heading.error.isEmpty());
  QCOMPARE(heading.anchor, QStringLiteral("beta"));
  QCOMPARE(nativeHtml.size(), 0);
  QVERIFY(!editor->getHighlighter()->currentDocument());
  QVERIFY(!window.findChild<MarkdownViewer *>());

  markdownConfig().setEditViewMode(MarkdownEditorConfig::EditPreview);
  window.handleEditorConfigChange();
  auto *reader = window.findChild<MarkdownViewer *>();
  QVERIFY(reader);
  QVERIFY(waitForRenderedHeading(reader, QStringLiteral("Beta")));
  QVERIFY(native->isCurrent(reader->adapter()->documentGeneration()));
  QVERIFY(!nativeHtml.isEmpty());
  const auto result = qvariant_cast<MarkdownHtmlResult>(nativeHtml.last().at(1));
  QVERIFY(result.error.isEmpty() && !result.html.isEmpty() && result.document);
  QCOMPARE(result.document->source(), QByteArrayLiteral("# Beta\n"));
}

void TestMarkdownViewWindowLazy::unavailableReplacementCannotSaveOutgoingSource() {
  markdownConfig().setEditViewMode(MarkdownEditorConfig::EditPreview);
  const auto alpha = openNote(QStringLiteral("available-alpha.md"), QByteArrayLiteral("# Alpha\n"));
  const auto missing = openNote(QStringLiteral("missing-beta.md"), QByteArrayLiteral("# Beta\n"));
  const auto gamma = openNote(QStringLiteral("available-gamma.md"), QByteArrayLiteral("# Gamma\n"));
  QVERIFY(alpha.isValid() && missing.isValid() && gamma.isValid());
  Window window(m_services, alpha, nullptr, ViewWindowMode::Edit);
  showWindow(window);
  auto *editor = window.findChild<MarkdownEditor *>();
  auto *reader = window.findChild<MarkdownViewer *>();
  auto *native = window.findChild<MarkdownRenderController *>();
  QVERIFY(editor && reader && native);
  appendDraft(editor, QStringLiteral("\nOutgoing draft\n"));
  const auto draft = editor->getText();
  const auto undo = editor->getTextEdit()->document()->availableUndoSteps();
  FileOpenSettings settings;
  settings.m_mode = ViewWindowMode::Edit;
  QVERIFY(!window.replaceBuffer(Buffer2(), settings));
  QCOMPARE(window.getBuffer().id(), alpha.id());
  QCOMPARE(editor->getText(), draft);
  QCOMPARE(editor->getTextEdit()->document()->availableUndoSteps(), undo);
  QVERIFY(waitForRenderedHeading(reader, QStringLiteral("Outgoing draft")));
  QTRY_VERIFY(native->isCurrent(reader->adapter()->documentGeneration()));
  QVERIFY(window.save());
  QTRY_VERIFY_WITH_TIMEOUT(!m_buffers->isSaveQueueBusy(alpha.id()) && !window.isModified(), 10000);
  QFile saved(alpha.resolvedPath());
  QVERIFY(saved.open(QIODevice::ReadOnly));
  QCOMPARE(saved.readAll(), draft.toUtf8());
  saved.close();

  // The real file disappears after obtaining a valid, still-unloaded buffer handle.
  QVERIFY(QFile::remove(missing.resolvedPath()));
  QVERIFY(window.replaceBuffer(missing, settings));
  QCOMPARE(window.getBuffer().id(), missing.id());
  QCOMPARE(window.findChild<MarkdownEditor *>(), editor);
  QString snapshot, error;
  QVERIFY(!window.tryGetLatestContent(&snapshot, &error));
  QVERIFY(!error.isEmpty());
  QVERIFY(editor->isReadOnly());
  QVERIFY(editor->getText().isEmpty());
  QVERIFY(!window.save());
  QVERIFY(!QFileInfo::exists(missing.resolvedPath()));
  QVERIFY(native->render(QByteArrayLiteral("# Must stay unavailable\n"), {}).isEmpty());
  QVERIFY(!reader->adapter()->isDocumentReady());
  QVERIFY(saved.open(QIODevice::ReadOnly));
  QCOMPARE(saved.readAll(), draft.toUtf8());
  saved.close();

  QVERIFY(window.replaceBuffer(gamma, settings));
  QCOMPARE(window.findChild<MarkdownRenderController *>(), native);
  QVERIFY(waitForRenderedHeading(reader, QStringLiteral("Gamma")));
  QTRY_VERIFY(native->isCurrent(reader->adapter()->documentGeneration()));
  QCOMPARE(editor->getText(), QStringLiteral("# Gamma\n"));
  QVERIFY(!editor->isReadOnly());
}

void TestMarkdownViewWindowLazy::nativeHtmlExportIncludesEnhancements() {
  const auto source =
      QByteArrayLiteral("# Native export\n\n[[target.md|Native link]]\n\n- [x] done\n\n"
                        "Inline $x^2$ and :smile:.\n\n```mermaid\ngraph LR; A-->B;\n```\n");
  const auto buffer = openNote(QStringLiteral("native-export.md"), source);
  QVERIFY(buffer.isValid());
  ExportInput input;
  input.file.filePath = buffer.resolvedPath();
  input.file.fileName = QStringLiteral("native-export.md");
  input.file.fileTypeName = QStringLiteral("Markdown");
  input.content = QString::fromUtf8(source);
  ExportHtmlRequest request;
  request.purpose = ExportHtmlPurpose::Standalone;
  request.options.m_htmlOption.m_completePage = true;
  request.options.m_htmlOption.m_embedStyles = true;
  request.options.m_htmlOption.m_embedImages = false;
  request.options.m_htmlOption.m_addOutlinePanel = false;
  request.options.m_rasterizeMathEnabled = false;
  request.options.m_rasterizeDiagramsEnabled = false;
  const QString output = m_directory.filePath(QStringLiteral("native-export.html"));
  QStringList errors;
  WebViewExporter exporter(m_services, nullptr, nullptr);
  connect(&exporter, &ExportRenderer::logRequested, this,
          [&](const QString &p_message) { errors.append(p_message); });
  QVERIFY2(exporter.renderHtml(input, request, output), qPrintable(errors.join(QLatin1Char('\n'))));
  QFile file(output);
  QVERIFY(file.open(QIODevice::ReadOnly));
  const auto html = QString::fromUtf8(file.readAll());
  QVERIFY(html.contains(QStringLiteral("id=\"native-export\"")));
  QVERIFY(html.contains(QStringLiteral("class=\"wikilink\"")));
  QVERIFY(!html.contains(QStringLiteral("[[target.md|Native link]]")));
  QVERIFY(html.contains(QStringLiteral("task-list-item-checkbox")));
  QVERIFY(html.contains(QStringLiteral("katex")));
  QVERIFY(html.contains(QStringLiteral("<svg")));
  QVERIFY(html.contains(QStringLiteral(":smile:")));
}

void TestMarkdownViewWindowLazy::legacyResourcesKeepCustomization() {
  const auto scriptName = QStringLiteral("web/js/native-custom-smoke.js");
  const auto styleName = QStringLiteral("web/css/native-custom-smoke.css");
  QFile script(m_config->getFileFromConfigFolder(scriptName));
  QVERIFY(script.open(QIODevice::WriteOnly));
  const QByteArray scriptBytes =
      QByteArrayLiteral("window.vxcore.on('basicMarkdownRendered', function() {"
                        " const heading = document.querySelector('#vx-content h1');"
                        " if (heading) heading.classList.add('custom-preserved');"
                        "});");
  QCOMPARE(script.write(scriptBytes), qint64(scriptBytes.size()));
  script.close();
  QFile style(m_config->getFileFromConfigFolder(styleName));
  QVERIFY(style.open(QIODevice::WriteOnly));
  const QByteArray styleBytes = QByteArrayLiteral("h1.custom-preserved { font-style: italic; }");
  QCOMPARE(style.write(styleBytes), qint64(styleBytes.size()));
  style.close();

  auto config = markdownConfig().toJson();
  auto viewer = config.value(QStringLiteral("viewerResource")).toObject();
  auto resources = viewer.value(QStringLiteral("resources")).toArray();
  bool replaced = false;
  for (int i = 0; i < resources.size(); ++i) {
    auto resource = resources[i].toObject();
    if (resource.value(QStringLiteral("name")).toString() != QStringLiteral("document"))
      continue;
    resource.insert(QStringLiteral("name"), QStringLiteral("markdown_it"));
    resource.insert(QStringLiteral("scripts"),
                    QJsonArray{QStringLiteral("web/js/markdown-it/markdown-it.min.js"),
                               QStringLiteral("web/js/markdownit.js"), scriptName});
    resource.insert(QStringLiteral("styles"),
                    QJsonArray{QStringLiteral("web/css/markdownit.css"), styleName});
    resources[i] = resource;
    replaced = true;
  }
  QVERIFY(replaced);
  viewer.insert(QStringLiteral("resources"), resources);
  config.insert(QStringLiteral("viewerResource"), viewer);
  markdownConfig().fromJson(config);
  m_templates->updateMarkdownViewerTemplate(markdownConfig(), m_theme->fetchWebStyleSheet(),
                                            m_theme->getFile(Theme::File::HighlightStyleSheet),
                                            true);

  const auto buffer = openNote(QStringLiteral("legacy-resources.md"),
                               QByteArrayLiteral("# Native customization\n\n- [x] retained\n"));
  Window window(m_services, buffer, nullptr, ViewWindowMode::Read);
  showWindow(window);
  auto *reader = window.findChild<MarkdownViewer *>();
  QVERIFY(reader);
  QVERIFY(waitForRenderedHeading(reader, QStringLiteral("Native customization")));
  QVERIFY(!window.findChild<MarkdownEditor *>());
  QCOMPARE(evaluate(reader->page(),
                    QStringLiteral(
                        "getComputedStyle(document.querySelector('#vx-content h1')).fontStyle"))
               .toString(),
           QStringLiteral("italic"));
  QVERIFY(
      evaluate(reader->page(),
               QStringLiteral(
                   "document.querySelector('#vx-content input.task-list-item-checkbox').checked"))
          .toBool());
}

void TestMarkdownViewWindowLazy::protectedLockWithoutReader() {
  const auto buffer = openProtectedNote(QByteArrayLiteral("# Protected draft\n"));
  QVERIFY(buffer.isValid() && buffer.isEncrypted());
  const int previousViews = webViewCount();
  Window window(m_services, buffer, nullptr, ViewWindowMode::Edit);
  showWindow(window);
  QPointer<MarkdownEditor> editor = window.findChild<MarkdownEditor *>();
  auto *jobs = window.findChild<MarkdownWebJobController *>();
  auto *host = window.findChild<MarkdownWebJobHost *>();
  QVERIFY(editor && jobs && host);
  QPointer<WebViewJobSession> fallback;
  connect(host, &MarkdownWebJobHost::protectedSessionReady, &window,
          [&](WebViewJobSession *p_session, const QByteArray &) { fallback = p_session; });
  QSignalSpy highlighted(jobs, &MarkdownWebJobController::highlightMathReady);
  appendDraft(editor, QStringLiteral("\n$$\n\\alpha^2 + 42\n$$\n"));
  QTRY_VERIFY_WITH_TIMEOUT(!highlighted.isEmpty(), 30000);
  QVERIFY(fallback);
  QVERIFY(fallback->property("markdownJobProtected").toBool());
  QCOMPARE(webViewCount(), previousViews + 1);
  QVERIFY(!window.findChild<MarkdownViewer *>());
  const auto draft = editor->getText();
  const auto cursor = editor->getTextEdit()->textCursor();
  const auto undo = editor->getTextEdit()->document()->availableUndoSteps();
  QVERIFY(window.isModified());

  // Cancelling lock restores authorization, not the buffer's older saved text.
  QVERIFY(m_buffers->beginProtectedLocking());
  QVERIFY(!fallback);
  QCOMPARE(editor->getText(), draft);
  m_buffers->cancelProtectedLocking();
  QCOMPARE(editor->getText(), draft);
  QCOMPARE(editor->getTextEdit()->textCursor().position(), cursor.position());
  QCOMPARE(editor->getTextEdit()->document()->availableUndoSteps(), undo);
  QVERIFY(window.isModified());

  auto *native = window.findChild<MarkdownRenderController *>();
  QVERIFY(native);
  int callbacks = 0;
  bool cancelled = false;
  native->fetchHeadingAnchor(QByteArrayLiteral("# Pending secret\n"), 0, {}, {},
                             [&](const MarkdownRenderController::HeadingAnchorResult &p_result) {
                               ++callbacks;
                               cancelled = p_result.cancelled;
                             });
  QVERIFY(m_buffers->beginProtectedLocking());
  QVERIFY(!fallback);
  QCOMPARE(callbacks, 1);
  QVERIFY(cancelled);
  QTRY_VERIFY(m_buffers->protectedOperationsIdle());
  // Lock must save the frozen writer before accepted close retires the editor.
  QVERIFY(!window.aboutToClose(true));
  QVERIFY(editor);
  QCOMPARE(editor->getText(), draft);
  QCOMPARE(editor->getTextEdit()->document()->availableUndoSteps(), undo);
  QString error;
  QVERIFY2(m_buffers->saveForSnapshot(buffer.id(), 1000, &error), qPrintable(error));

  // Application settings can already be queued when the lock accepts close.
  bool settingsApplied = false;
  QTimer::singleShot(0, &window, [&]() {
    markdownConfig().setLinkifyEnabled(!markdownConfig().getLinkifyEnabled());
    window.handleEditorConfigChange();
    m_theme->switchTheme(QStringLiteral("vscode-dark"));
    window.setMode(ViewWindowMode::Read);
    window.setMode(ViewWindowMode::Edit);
    settingsApplied = true;
  });
  QVERIFY(window.aboutToClose(true));
  QVERIFY(!editor);
  QVERIFY(!window.findChild<PreviewHelper *>());
  QVERIFY(!window.findChild<MarkdownViewer *>());
  QVERIFY(!window.isEnabled());
  QCOMPARE(webViewCount(), previousViews);
  QTRY_VERIFY(settingsApplied);
  // Releasing the global lock cannot reauthorize this already closed window.
  m_buffers->cancelProtectedLocking();
  QCoreApplication::processEvents();
  QCOMPARE(buffer.getContentRaw(), draft.toUtf8());
  QCOMPARE(callbacks, 1);
  QVERIFY(!window.findChild<MarkdownEditor *>());
  QVERIFY(!window.findChild<PreviewHelper *>());
  QVERIFY(!window.findChild<MarkdownViewer *>());
  QCOMPARE(webViewCount(), previousViews);
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
  const auto buffer =
      openNote(QStringLiteral("inactive-draft.md"), QByteArrayLiteral("# Saved heading\n"));
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
  QApplication::setActiveWindow(&tabs);
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
  QVERIFY(waitForRenderedHeading(retired, QStringLiteral("Unsaved inactive heading")));
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
  QVERIFY(waitForRenderedHeading(restored, QStringLiteral("Unsaved inactive heading")));
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
  const auto buffer =
      openNote(QStringLiteral("inactive-policy.md"), QByteArrayLiteral("# Policy\n"));
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
  QVERIFY(waitForRenderedHeading(reader, QStringLiteral("Policy")));
  QVERIFY(waitForRenderedHeading(sibling, QStringLiteral("Policy")));
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
  QVERIFY(waitForRenderedHeading(restored, QStringLiteral("Policy")));
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
    QVERIFY(waitForRenderedHeading(window.findChild<MarkdownViewer *>(), QStringLiteral("Modes")));
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
    QVERIFY(waitForRenderedHeading(viewer, QStringLiteral("Changed while dormant")));
    QTRY_VERIFY_WITH_TIMEOUT(
        evaluate(
            viewer->page(),
            QStringLiteral("!!document.querySelector('a[href=\"https://example.org/dormant\"]')"))
            .toBool(),
        10000);
    appendDraft(editor, QStringLiteral("\n# Continuous live update\n"));
    QVERIFY(waitForRenderedHeading(viewer, QStringLiteral("Continuous live update")));
  } else {
    QVERIFY(!window.findChild<MarkdownViewer *>());
    appendDraft(editor, QStringLiteral("\n```javascript\nconst resumed = 42;\n```\n"));
    const auto code = editor->getTextEdit()->document()->find(QStringLiteral("const resumed"));
    QVERIFY(!code.isNull());
    QTRY_VERIFY_WITH_TIMEOUT(sourceTokenColor(editor, code.blockNumber()).isValid() &&
                                 sourceTokenColor(editor, code.blockNumber(), 16).isValid() &&
                                 sourceTokenColor(editor, code.blockNumber()) !=
                                     sourceTokenColor(editor, code.blockNumber(), 16),
                             30000);
    QSignalSpy highlighted(jobs, &MarkdownWebJobController::highlightMathReady);
    auto *helper = window.findChild<PreviewHelper *>();
    QVERIFY(helper);
    bool mathPixels = false;
    connect(helper, &PreviewHelper::inplacePreviewMathBlockUpdated, &window,
            [&](const QVector<QSharedPointer<vte::PreviewItem>> &p_items) {
              for (const auto &item : p_items) {
                mathPixels |= item && !item->m_image.isNull();
              }
            });
    appendDraft(editor, QStringLiteral("\n$$\n\\alpha^2 + 42\n$$\n"));
    QTRY_VERIFY_WITH_TIMEOUT(!highlighted.isEmpty() && mathPixels, 30000);
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
  QVERIFY(waitForRenderedHeading(old, QStringLiteral("Section 35")));
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
  QVERIFY(waitForRenderedHeading(viewer, QStringLiteral("Section 35")));
  QCOMPARE(viewer->zoomFactor(), 1.25);
  const auto expected = target == QStringLiteral("saved") ? 10 : 20;
  const auto headingTop =
      QStringLiteral("Array.from(document.querySelectorAll('h1')).find(h => "
                     "h.textContent.includes('Section %1')).getBoundingClientRect().top")
          .arg(expected);
  QTRY_VERIFY_WITH_TIMEOUT(qAbs(evaluate(viewer->page(), headingTop).toDouble()) < 200, 10000);
  // The JS source-line path is delayed; an older passive scroll must not win later.
  QTest::qWait(700);
  QVERIFY(qAbs(evaluate(viewer->page(), headingTop).toDouble()) < 200);
  // Exercise a live reader action, not just DOM restoration.
  evaluate(viewer->page(),
           QStringLiteral("document.querySelector('input[type=checkbox]').click()"));
  QTRY_VERIFY_WITH_TIMEOUT(
      window.getLatestContent().contains(QStringLiteral("- [x] interactive task")), 10000);
}

void TestMarkdownViewWindowLazy::inactiveReadUnavailableActions() {
  const auto buffer =
      openNote(QStringLiteral("unavailable.md"), QByteArrayLiteral("# Unavailable\n"));
  QVERIFY(buffer.isValid());
  Window window(m_services, buffer, nullptr, ViewWindowMode::Read);
  showWindow(window);
  QVERIFY(
      waitForRenderedHeading(window.findChild<MarkdownViewer *>(), QStringLiteral("Unavailable")));
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
  QVERIFY(waitForRenderedHeading(viewer, QStringLiteral("Unavailable")));
  window.fetchWordCountInfo([&](const WordCountInfo &p_info) { counted = p_info.m_wordCount > 0; });
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
  QSignalSpy highlighted(jobs, &MarkdownWebJobController::highlightMathReady);
  appendDraft(editor, QStringLiteral("\n$$\n\\alpha^2 + 42\n$$\n"));
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
  auto *native = window.findChild<MarkdownRenderController *>();
  QVERIFY(native);
  native->fetchHeadingAnchor(QByteArrayLiteral("# Late protected work\n"), 0, {}, {},
                             [&](const MarkdownRenderController::HeadingAnchorResult &p_result) {
                               if (p_result.cancelled) {
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
  QVERIFY(waitForRenderedHeading(reader, QStringLiteral("Protected unsaved draft")));
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
  QVERIFY(waitForRenderedHeading(reader, QStringLiteral("Protected unsaved draft")));
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
  QTRY_VERIFY(m_buffers->protectedOperationsIdle());
  QString error;
  QVERIFY2(m_buffers->saveForSnapshot(buffer.id(), 1000, &error), qPrintable(error));
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
  QVERIFY(waitForRenderedHeading(window.findChild<MarkdownViewer *>(),
                                 QStringLiteral("Restored printable heading")));
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
