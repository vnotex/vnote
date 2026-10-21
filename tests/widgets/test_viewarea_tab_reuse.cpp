#include <QtTest>

#include <QAction>
#include <QApplication>
#include <QBuffer>
#include <QClipboard>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QGraphicsPixmapItem>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QMessageBox>
#include <QMimeData>
#include <QPainter>
#include <QPdfWriter>
#include <QPlainTextEdit>
#include <QPointer>
#include <QProcess>
#include <QPushButton>
#include <QScopeGuard>
#include <QTabBar>
#include <QTemporaryDir>
#include <QTextBlock>
#include <QTextCursor>
#include <QTimer>
#include <QWebEnginePage>
#include <QWebEngineProfile>
#include <QtConcurrent/QtConcurrentRun>

#include <controllers/commentcontroller.h>
#include <controllers/markdownwebjobcontroller.h>
#include <controllers/notebooknodecontroller.h>
#include <controllers/viewareacontroller.h>
#include <core/configmgr2.h>
#include <core/coreconfig.h>
#include <core/editorconfig.h>
#include <core/hookcontext.h>
#include <core/hookevents.h>
#include <core/hooknames.h>
#include <core/mainconfig.h>
#include <core/markdowneditorconfig.h>
#include <core/servicelocator.h>
#include <core/services/bufferservice.h>
#include <core/services/commentservice.h>
#include <core/services/configcoreservice.h>
#include <core/services/filetypecoreservice.h>
#include <core/services/hookmanager.h>
#include <core/services/htmltemplateservice.h>
#include <core/services/notebookcoreservice.h>
#include <core/services/notebookiogate.h>
#include <core/services/syncworkqueuemanager.h>
#include <core/services/templateservice.h>
#include <core/services/workspacecoreservice.h>
#include <core/widgetconfig.h>
#include <gui/services/themeservice.h>
#include <gui/services/viewwindowfactory.h>
#include <gui/services/vxpdfschemehandler.h>
#include <gui/services/webengineprofileservice.h>
#include <gui/services/webviewjobs.h>
#include <gui/services/webviewservice.h>
#include <models/notebooknodemodel.h>
#include <views/filelistview.h>
#include <views/notebooknodeview.h>
#include <vtextedit/previewdata.h>
#include <vtextedit/vtextedit.h>
#include <vtextedit/vtypsteditor.h>
#include <vxcore/vxcore.h>
#include <widgets/attachmentpopup2.h>
#include <widgets/dialogs/imageinsertdialog.h>
#include <widgets/editors/markdowneditor.h>
#include <widgets/editors/markdownviewer.h>
#include <widgets/editors/markdownvieweradapter.h>
#include <widgets/editors/markdownwebjobhost.h>
#include <widgets/editors/mindmapeditor.h>
#include <widgets/editors/pdfviewer.h>
#include <widgets/editors/pdfvieweradapter.h>
#include <widgets/editors/texteditor.h>
#include <widgets/imageviewer.h>
#include <widgets/outlineprovider.h>
#include <widgets/viewarea2.h>
#include <widgets/viewwindow2.h>

#include <memory>

using namespace vnotex;

namespace tests {
namespace {
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
  return QTest::qWaitFor([result] { return result->finished; }, 10000) ? result->value : QVariant();
}

bool rendered(MarkdownViewer *p_viewer, const QString &p_text) {
  return p_viewer && p_viewer->jobSession()->isReady() &&
         evaluate(p_viewer->page(), QStringLiteral("document.body.innerText"))
             .toString()
             .contains(p_text);
}

QByteArray readBytes(const QString &p_path) {
  QFile file(p_path);
  return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

bool writeBytes(const QString &p_path, const QByteArray &p_bytes) {
  QFile file(p_path);
  return file.open(QIODevice::WriteOnly) && file.write(p_bytes) == p_bytes.size();
}

QByteArray imageBytes(const QColor &p_color, const QSize &p_size = QSize(40, 24)) {
  QImage image(p_size, QImage::Format_RGB32);
  image.fill(p_color);
  QByteArray bytes;
  QBuffer buffer(&bytes);
  buffer.open(QIODevice::WriteOnly);
  image.save(&buffer, "PNG");
  return bytes;
}

QByteArray pdfBytes(const QString &p_title) {
  QByteArray bytes;
  QBuffer buffer(&bytes);
  buffer.open(QIODevice::WriteOnly);
  {
    QPdfWriter writer(&buffer);
    writer.setTitle(p_title);
    QPainter painter(&writer);
    for (int page = 1; page <= 3; ++page) {
      if (page > 1)
        writer.newPage();
      painter.drawText(200, 600, p_title + QStringLiteral(" page %1").arg(page));
    }
  }
  return bytes;
}

QByteArray mindMapBytes(const QString &p_title) {
  const QJsonObject root{{QStringLiteral("id"), QStringLiteral("r")},
                         {QStringLiteral("topic"), p_title}};
  return QJsonDocument(QJsonObject{{QStringLiteral("schemaVersion"), 1},
                                   {QStringLiteral("rootId"), QStringLiteral("r")},
                                   {QStringLiteral("nodes"), QJsonArray{root}}})
      .toJson();
}

void appendDraft(vte::VTextEdit *p_edit, const QString &p_text) {
  auto cursor = p_edit->textCursor();
  cursor.movePosition(QTextCursor::End);
  cursor.insertText(p_text);
  p_edit->setTextCursor(cursor);
}

// Own the timer for the duration of the synchronous operation. A missing/wrong dialog
// is a failure, not a hung ctest: timeout rejects every visible modal before returning.
class DialogAnswer {
public:
  explicit DialogAnswer(QMessageBox::StandardButton p_button) {
    elapsed.start();
    timer.setInterval(20);
    QObject::connect(&timer, &QTimer::timeout, &timer, [this, p_button] {
      for (auto *widget : QApplication::topLevelWidgets()) {
        auto *box = qobject_cast<QMessageBox *>(widget);
        if (!clicked && box && box->isVisible()) {
          if (auto *button = box->button(p_button)) {
            clicked = true;
            button->click();
            return;
          }
        }
      }
      if (elapsed.elapsed() < 5000)
        return;
      timedOut = true;
      QTest::qFail("Timed out completing the requested Save/Discard/Cancel operation", __FILE__,
                   __LINE__);
      for (auto *widget : QApplication::topLevelWidgets()) {
        if (auto *dialog = qobject_cast<QDialog *>(widget); dialog && dialog->isVisible())
          dialog->reject();
      }
      timer.stop();
    });
    timer.start();
  }
  bool accepted() {
    timer.stop();
    return clicked && !timedOut;
  }

private:
  QTimer timer;
  QElapsedTimer elapsed;
  bool clicked = false;
  bool timedOut = false;
};

struct Fixture {
  VxCoreContextHandle context = nullptr;
  ServiceLocator services;
  QString root;
  QString notebook;
  QString error;
  QStringList oldSearchPaths;
  std::unique_ptr<QMimeData> clipboard;
  std::unique_ptr<ConfigCoreService> configCore;
  std::unique_ptr<ConfigMgr2> config;
  std::unique_ptr<HookManager> hooks;
  std::unique_ptr<NotebookIoGate> gate;
  std::unique_ptr<SyncWorkQueueManager> queues;
  std::unique_ptr<NotebookCoreService> notebooks;
  std::unique_ptr<BufferService> buffers;
  std::unique_ptr<WorkspaceCoreService> workspaces;
  std::unique_ptr<FileTypeCoreService> fileTypes;
  std::unique_ptr<CommentService> comments;
  std::unique_ptr<ThemeService> theme;
  std::unique_ptr<TemplateService> noteTemplates;
  std::unique_ptr<HtmlTemplateService> templates;
  std::unique_ptr<WebEngineProfileService> profiles;
  std::unique_ptr<WebViewService> web;
  std::unique_ptr<ViewWindowFactory> factory;
  std::unique_ptr<ViewArea2> area;

  explicit Fixture(const QString &p_root) : root(p_root) {}
  bool initialize(const QString &p_existingNotebook = {}) {
    vxcore_set_test_mode(1);
    oldSearchPaths = QDir::searchPaths(QStringLiteral("app"));
    clipboard = std::make_unique<QMimeData>();
    if (const auto *mime = QApplication::clipboard()->mimeData()) {
      for (const auto &format : mime->formats())
        clipboard->setData(format, mime->data(format));
    }
    QApplication::clipboard()->clear();
    const QString rcc = QStringLiteral(VNOTE_TEST_EXTRA_RCC);
    if (!QFileInfo::exists(rcc)) {
      error = QStringLiteral("Missing real extra RCC: ") + rcc;
      return false;
    }
    if (vxcore_context_create(nullptr, &context) != VXCORE_OK) {
      error = QStringLiteral("Could not create isolated vxcore context");
      return false;
    }
    configCore = std::make_unique<ConfigCoreService>(context);
    config = std::make_unique<ConfigMgr2>(configCore.get());
    config->init();
    QDir::setSearchPaths(QStringLiteral("app"), {QFileInfo(rcc).absolutePath()});
    config->initAfterQtAppStarted();
    if (!config->extraDataCopyFailures().isEmpty()) {
      error = QStringLiteral("Could not install real bundled runtime");
      return false;
    }
    auto &markdown = config->getEditorConfig().getMarkdownEditorConfig();
    markdown.setEditViewMode(MarkdownEditorConfig::EditPreview);
    markdown.setInplacePreviewSources(MarkdownEditorConfig::NoInplacePreview);
    markdown.setAutoSectionNumberInEditModeEnabled(false);
    markdown.setAutoFoldPreviewedBlocksEnabled(false);
    markdown.setConcealmentEnabled(false);
    markdown.setMathRenderer(QStringLiteral("katex"));
    markdown.setFetchImagesInParseAndPaste(false);
    hooks = std::make_unique<HookManager>();
    gate = std::make_unique<NotebookIoGate>();
    queues = std::make_unique<SyncWorkQueueManager>();
    notebooks = std::make_unique<NotebookCoreService>(context);
    notebooks->setHookManager(hooks.get());
    notebooks->setNotebookIoGate(gate.get());
    buffers =
        std::make_unique<BufferService>(context, hooks.get(), gate.get(), AutoSavePolicy::None);
    buffers->setSyncWorkQueueManager(queues.get());
    workspaces = std::make_unique<WorkspaceCoreService>(context);
    workspaces->setHookManager(hooks.get());
    fileTypes = std::make_unique<FileTypeCoreService>(context, QStringLiteral("en_US"));
    comments = std::make_unique<CommentService>(notebooks.get(), gate.get(), hooks.get());
    ThemeServiceConfig themeConfig;
    themeConfig.themeName = QStringLiteral("pure");
    themeConfig.locale = QStringLiteral("en_US");
    themeConfig.appDataPath = QStringLiteral(VNOTE_SRC_DIR "/data/extra");
    theme = std::make_unique<ThemeService>(themeConfig);
    noteTemplates = std::make_unique<TemplateService>(config.get());
    templates = std::make_unique<HtmlTemplateService>(config.get());
    profiles = std::make_unique<WebEngineProfileService>(root + QStringLiteral("/p"), config.get(),
                                                         templates.get());
    services.registerService(configCore.get());
    services.registerService(config.get());
    services.registerService(hooks.get());
    services.registerService(gate.get());
    services.registerService(queues.get());
    services.registerService(notebooks.get());
    services.registerService(buffers.get());
    services.registerService(workspaces.get());
    services.registerService(fileTypes.get());
    services.registerService(comments.get());
    services.registerService(theme.get());
    services.registerService(noteTemplates.get());
    services.registerService(templates.get());
    services.registerService(profiles.get());
    web = std::make_unique<WebViewService>(services, profiles->profile(),
                                           MarkdownWebJobHost::createSharedRuntime(services));
    services.registerService(web.get());
    QObject::connect(theme.get(), &ThemeService::themeAboutToChange, web.get(),
                     &WebViewService::invalidate);
    factory = std::make_unique<ViewWindowFactory>();
    factory->registerBuiltInCreators();
    services.registerService(factory.get());
    notebook = p_existingNotebook.isEmpty()
                   ? notebooks->createNotebook(root + QStringLiteral("/n"),
                                               QStringLiteral("{\"name\":\"Tab reuse\"}"),
                                               NotebookType::Bundled)
                   : p_existingNotebook;
    if (notebook.isEmpty()) {
      error = QStringLiteral("Could not create scratch notebook");
      return false;
    }
    area = std::make_unique<ViewArea2>(services);
    area->resize(1050, 760);
    area->show();
    area->activateWindow();
    if (p_existingNotebook.isEmpty()) {
      controller()->openVxUrl(QUrl(QStringLiteral("vx://home")));
      QCoreApplication::processEvents();
    }
    return true;
  }
  ~Fixture() {
    // No close prompts, user profiles or startup reconciliation in fixture teardown.
    if (area) {
      area->getController()->setShouldPropagateToCore(false);
      area->getController()->closeAll(area->getVisibleWorkspaceIds().toVector(), true);
      area.reset();
      QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    }
    web.reset();
    profiles.reset();
    comments.reset();
    noteTemplates.reset();
    templates.reset();
    theme.reset();
    if (buffers)
      buffers->cancelProtectedLocking();
    buffers.reset();
    notebooks.reset();
    queues.reset();
    gate.reset();
    workspaces.reset();
    fileTypes.reset();
    factory.reset();
    hooks.reset();
    config.reset();
    configCore.reset();
    if (context)
      vxcore_context_destroy(context);
    QDir::setSearchPaths(QStringLiteral("app"), oldSearchPaths);
    if (clipboard)
      QApplication::clipboard()->setMimeData(clipboard.release());
  }
  ViewAreaController *controller() const { return area->getController(); }
  ViewWindow2 *current() const { return area->getCurrentViewWindow(); }
  QString workspace() const { return controller()->getCurrentWorkspaceId(); }
  QString absolute(const QString &p_name) const {
    return notebooks->buildAbsolutePath(notebook, p_name);
  }
  NodeIdentifier node(const QString &p_name) const { return {notebook, p_name}; }
  bool create(const QString &p_name, const QByteArray &p_bytes) {
    const auto parts = p_name.split(QLatin1Char('/'));
    QString parent;
    for (int i = 0; i + 1 < parts.size(); ++i) {
      const QString path = parent.isEmpty() ? parts[i] : parent + QLatin1Char('/') + parts[i];
      if (!QFileInfo::exists(absolute(path)) &&
          notebooks->createFolder(notebook, parent, parts[i]).isEmpty())
        return false;
      parent = path;
    }
    return !notebooks->createFile(notebook, parent, parts.last()).isEmpty() &&
           writeBytes(absolute(p_name), p_bytes);
  }
  Buffer2 open(const QString &p_name, bool p_new = false, bool p_focus = true) {
    FileOpenSettings settings;
    settings.m_mode = ViewWindowMode::Edit;
    settings.m_alwaysNewWindow = p_new;
    settings.m_focus = p_focus;
    return buffers->openBuffer(node(p_name), settings);
  }
  QStringList order(const QString &p_workspace = {}) const {
    QStringList result;
    const auto json = workspaces->getWorkspace(p_workspace.isEmpty() ? workspace() : p_workspace);
    for (const auto &id : json.value(QStringLiteral("bufferIds")).toArray())
      result.append(id.toString());
    return result;
  }
  QString coreCurrent(const QString &p_workspace = {}) const {
    return workspaces->getWorkspace(p_workspace.isEmpty() ? workspace() : p_workspace)
        .value(QStringLiteral("currentBufferId"))
        .toString();
  }
  int tabs() const { return area->getCurrentViewSplit()->getViewWindowCount(); }
  void reuse() { config->getCoreConfig().setAlwaysOpenNotesInNewTabs(false); }
  QString createProtected(const QString &p_name, const QByteArray &p_source, bool p_setup) {
    QString id;
    const auto result =
        QtConcurrent::run([&]() {
          if (p_setup) {
            auto setup = notebooks->prepareNotebookEncryption(
                notebook, QString(), QByteArrayLiteral("reuse-test-password"));
            if (!setup.isValid())
              return setup.m_error;
            auto maintenance = queues->tryAcquireMaintenance({notebook});
            if (!maintenance.isValid())
              return VXCORE_ERR_INVALID_STATE;
            NotebookIoGate::ScopedLock lock(*gate, notebook);
            const auto error = notebooks->commitNotebookEncryption(setup);
            if (error != VXCORE_OK)
              return error;
            return notebooks->createEncryptedNote(notebook, QString(), p_name,
                                                  QStringLiteral("markdown"), p_source, &id);
          }
          auto maintenance = queues->tryAcquireMaintenance({notebook});
          if (!maintenance.isValid())
            return VXCORE_ERR_INVALID_STATE;
          NotebookIoGate::ScopedLock lock(*gate, notebook);
          return notebooks->createEncryptedNote(notebook, QString(), p_name,
                                                QStringLiteral("markdown"), p_source, &id);
        }).result();
    return result == VXCORE_OK ? id : QString();
  }
  Buffer2 openProtected(const QString &p_id, bool p_new = false) {
    FileOpenSettings settings;
    settings.m_mode = ViewWindowMode::Edit;
    settings.m_alwaysNewWindow = p_new;
    return buffers->openBufferByNodeId(p_id, settings);
  }
};
} // namespace

class TestViewAreaTabReuse : public QObject {
  Q_OBJECT
private slots:
  void init();
  void cleanup();
  void missingPreferenceAndBackgroundOpen();
  void markdownResourcesAndSaveIdentity_data();
  void markdownResourcesAndSaveIdentity();
  void nativeTextAndMindMap();
  void typstCompilerAndUnavailableSource();
  void typstSessionPosition_data();
  void typstSessionPosition();
  void imageAndPdfDocuments();
  void dirtyReplacement_data();
  void dirtyReplacement();
  void differentTypeAndCreationFailure();
  void vetoAndOwnedCandidate();
  void siblingAndVirtualTabs();
  void explorerGestures_data();
  void explorerGestures();
  void restoreOrderedWorkspacesAndLockedNote();
  void reopenSplitAndBatches();
  void protectedResourcesAndLateWork();
  void pdfCommentDurability_data();
  void pdfCommentDurability();

private:
  std::unique_ptr<QTemporaryDir> directory;
  std::unique_ptr<Fixture> f;
};

void TestViewAreaTabReuse::init() {
  vxcore_set_test_mode(1);
  vxcore_clear_test_directory();
  directory = std::make_unique<QTemporaryDir>(QDir::tempPath() + QStringLiteral("/n-XXXXXX"));
  QVERIFY(directory->isValid());
  f = std::make_unique<Fixture>(directory->path());
  QVERIFY2(f->initialize(), qPrintable(f->error));
}

void TestViewAreaTabReuse::cleanup() {
  f.reset();
  directory.reset();
}

void TestViewAreaTabReuse::missingPreferenceAndBackgroundOpen() {
  QVERIFY(f->config->getCoreConfig().getAlwaysOpenNotesInNewTabs());
  QVERIFY(f->create(QStringLiteral("a.md"), "# Alpha\n"));
  QVERIFY(f->create(QStringLiteral("b.md"), "# Beta\n"));
  QVERIFY(f->create(QStringLiteral("c.md"), "# Background\n"));
  const auto a = f->open(QStringLiteral("a.md"));
  const auto b = f->open(QStringLiteral("b.md"));
  QVERIFY(a.isValid() && b.isValid());
  QCOMPARE(f->tabs(), 2);
  QCOMPARE(f->order(), QStringList({a.id(), b.id()}));
  f->reuse();
  auto *active = f->current();
  const auto c = f->open(QStringLiteral("c.md"), false, false);
  QVERIFY(c.isValid());
  QCOMPARE(f->tabs(), 3);
  QCOMPARE(f->current(), active);
  QCOMPARE(active->getBuffer().id(), b.id());
  QCOMPARE(f->coreCurrent(), b.id());
  // Explicit force-new still deduplicates a buffer already owned by this workspace.
  const auto order = f->order();
  QVERIFY(f->open(QStringLiteral("a.md"), true).isValid());
  QCOMPARE(f->tabs(), 3);
  QCOMPARE(f->order(), order);
  QCOMPARE(f->current()->getBuffer().id(), a.id());
}

void TestViewAreaTabReuse::markdownResourcesAndSaveIdentity_data() {
  QTest::addColumn<bool>("equalSource");
  QTest::newRow("different-content") << false;
  QTest::newRow("identical-source-different-directories") << true;
}

void TestViewAreaTabReuse::markdownResourcesAndSaveIdentity() {
  QFETCH(bool, equalSource);
  f->reuse();
  const QByteArray sourceA("# Alpha\n\n![relative](asset.png)\n\nneedle\n");
  const QByteArray sourceB =
      equalSource ? sourceA : QByteArray("# Beta\n\n![relative](asset.png)\n\nnew body\n");
  QVERIFY(f->create(QStringLiteral("a/a.md"), sourceA));
  QVERIFY(f->create(QStringLiteral("b/b.md"), sourceB));
  QVERIFY(
      writeBytes(f->absolute(QStringLiteral("a/asset.png")), imageBytes(Qt::red, QSize(17, 13))));
  QVERIFY(
      writeBytes(f->absolute(QStringLiteral("b/asset.png")), imageBytes(Qt::blue, QSize(31, 19))));
  const auto a = f->open(QStringLiteral("a/a.md"));
  QVERIFY(a.isValid());
  auto *window = f->current();
  QVERIFY(window);
  auto *editor = window->findChild<MarkdownEditor *>();
  auto *viewer = window->findChild<MarkdownViewer *>();
  QVERIFY(editor && viewer);
  QTRY_VERIFY_WITH_TIMEOUT(rendered(viewer, QStringLiteral("Alpha")), 30000);
  QTRY_COMPARE_WITH_TIMEOUT(
      evaluate(viewer->page(), QStringLiteral("document.querySelector('img')?.naturalWidth"))
          .toInt(),
      17, 10000);
  auto *page = viewer->page();
  const auto windowId = window->getViewWindowId();
  const int index = f->area->getCurrentViewSplit()->currentIndex();
  // Save leaves actual undo history; reuse must clear it even if the source is equal.
  appendDraft(editor->getTextEdit(), QStringLiteral("x"));
  editor->getTextEdit()->undo();
  QVERIFY(editor->getTextEdit()->document()->isRedoAvailable());
  QVERIFY(window->save());
  auto cursor = editor->getTextEdit()->textCursor();
  cursor.movePosition(QTextCursor::End);
  editor->getTextEdit()->setTextCursor(cursor);
  FileOpenSettings search;
  search.m_searchHighlight.m_isValid = true;
  search.m_searchHighlight.m_patterns = {QStringLiteral("needle")};
  search.m_searchHighlight.m_currentMatchLine = 4;
  window->applyFileOpenSettings(search);
  const auto hasOldSearch = [&] {
    for (const auto &selection : editor->getTextEdit()->extraSelections()) {
      if (selection.cursor.selectedText() == QStringLiteral("needle"))
        return true;
    }
    return false;
  };
  QTRY_VERIFY(hasOldSearch());
  const auto attachmentPath = directory->filePath(QStringLiteral("old-attachment.txt"));
  QVERIFY(writeBytes(attachmentPath, "only A"));
  QVERIFY(!window->getBuffer().insertAttachment(attachmentPath).isEmpty());
  QVERIFY(window->getBuffer().hasAttachments());
  auto *attachments = window->findChild<AttachmentPopup2 *>();
  QVERIFY(attachments);
  attachments->popup(window->mapToGlobal(QPoint(100, 100)));
  auto *attachmentList = attachments->findChild<QListView *>();
  QVERIFY(attachmentList);
  QCOMPARE(attachmentList->model()->rowCount(), 1);
  QVERIFY(attachments->isVisible());
  QSignalSpy published(f->controller(), &ViewAreaController::currentViewWindowChanged);
  QSignalSpy names(window, &ViewWindow2::nameChanged);
  QSignalSpy statuses(window, &ViewWindow2::statusChanged);
  QList<QStringList> publicationSnapshots;
  const auto publication =
      connect(f->controller(), &ViewAreaController::currentViewWindowChanged, this, [&] {
        publicationSnapshots.append(
            QStringList({f->current()->getBuffer().id(), f->coreCurrent()}) + f->order());
      });
  const auto disconnectPublication = qScopeGuard([&] { disconnect(publication); });
  const auto b = f->open(QStringLiteral("b/b.md"));
  QVERIFY(b.isValid());
  QCOMPARE(f->current(), window);
  QCOMPARE(window->getViewWindowId(), windowId);
  QCOMPARE(f->tabs(), 1);
  QCOMPARE(f->area->getCurrentViewSplit()->currentIndex(), index);
  QCOMPARE(window->findChild<MarkdownEditor *>(), editor);
  QCOMPARE(window->findChild<MarkdownViewer *>(), viewer);
  QCOMPARE(viewer->page(), page);
  QCOMPARE(editor->getText(), QString::fromUtf8(sourceB));
  QVERIFY(!editor->getTextEdit()->document()->isUndoAvailable());
  QVERIFY(!editor->getTextEdit()->document()->isRedoAvailable());
  QCOMPARE(editor->getTextEdit()->textCursor().position(), 0);
  QVERIFY(!editor->getTextEdit()->textCursor().hasSelection());
  QVERIFY(!hasOldSearch());
  QVERIFY(!attachments->isVisible());
  QVERIFY(!published.isEmpty());
  for (const auto &snapshot : publicationSnapshots)
    QCOMPARE(snapshot, QStringList({b.id(), b.id(), b.id()}));
  QTRY_VERIFY(!names.isEmpty());
  QTRY_VERIFY(!statuses.isEmpty());
  QCOMPARE(f->order(), QStringList({b.id()}));
  QCOMPARE(f->coreCurrent(), b.id());
  QVERIFY(!window->getBuffer().hasAttachments());
  attachments->popup(window->mapToGlobal(QPoint(100, 100)));
  QCOMPARE(attachmentList->model()->rowCount(), 0);
  attachments->close();
  QCOMPARE(window->getName(), QStringLiteral("b.md"));
  QTRY_VERIFY(f->area->getCurrentViewSplit()->tabText(index).contains(QStringLiteral("b.md")));
  const auto heading = equalSource ? QStringLiteral("Alpha") : QStringLiteral("Beta");
  QTRY_VERIFY_WITH_TIMEOUT(rendered(viewer, heading), 30000);
  QTRY_COMPARE_WITH_TIMEOUT(
      evaluate(page, QStringLiteral("document.querySelector('img')?.naturalWidth")).toInt(), 31,
      10000);
  auto provider = window->getOutlineProvider();
  QVERIFY(provider);
  QTRY_VERIFY(provider->getOutline() && !provider->getOutline()->m_headings.isEmpty());
  QCOMPARE(provider->getOutline()->m_headings.first().m_name, heading);
  appendDraft(editor->getTextEdit(), QStringLiteral("\nSaved B only\n"));
  QVERIFY(window->save());
  QTRY_VERIFY(readBytes(f->absolute(QStringLiteral("b/b.md"))).contains("Saved B only"));
  QCOMPARE(readBytes(f->absolute(QStringLiteral("a/a.md"))), sourceA);
}

void TestViewAreaTabReuse::nativeTextAndMindMap() {
  f->reuse();
  QVERIFY(f->create(QStringLiteral("a.txt"), "Text alpha\n"));
  QVERIFY(f->create(QStringLiteral("b.txt"), "Text beta\n"));
  QVERIFY(f->open(QStringLiteral("a.txt")).isValid());
  auto *window = f->current();
  auto *text = window->findChild<TextEditor *>();
  QVERIFY(text);
  const auto id = window->getViewWindowId();
  QVERIFY(f->open(QStringLiteral("b.txt")).isValid());
  QCOMPARE(f->current(), window);
  QCOMPARE(window->getViewWindowId(), id);
  QCOMPARE(window->findChild<TextEditor *>(), text);
  QCOMPARE(text->getText(), QStringLiteral("Text beta\n"));
  appendDraft(text->getTextEdit(), QStringLiteral("saved beta"));
  QVERIFY(window->save());
  QCOMPARE(readBytes(f->absolute(QStringLiteral("a.txt"))), QByteArray("Text alpha\n"));
  QTRY_VERIFY(readBytes(f->absolute(QStringLiteral("b.txt"))).contains("saved beta"));
  // Text and Others share a C++ class but are different registered creator keys.
  QVERIFY(f->create(QStringLiteral("a.unknown-reuse"), "Other alpha\n"));
  QVERIFY(f->create(QStringLiteral("b.unknown-reuse"), "Other beta\n"));
  QPointer<ViewWindow2> plainTextWindow = window;
  QVERIFY(f->open(QStringLiteral("a.unknown-reuse")).isValid());
  QVERIFY(f->current() != plainTextWindow.data());
  QTRY_VERIFY(plainTextWindow.isNull());
  window = f->current();
  text = window->findChild<TextEditor *>();
  QVERIFY(text);
  QCOMPARE(window->getFileType().toLower(), QStringLiteral("others"));
  QVERIFY(f->open(QStringLiteral("b.unknown-reuse")).isValid());
  QCOMPARE(f->current(), window);
  QCOMPARE(window->findChild<TextEditor *>(), text);
  QCOMPARE(text->getText(), QStringLiteral("Other beta\n"));
  QVERIFY(f->create(QStringLiteral("a.emind"), mindMapBytes(QStringLiteral("Map alpha"))));
  QVERIFY(f->create(QStringLiteral("b.emind"), mindMapBytes(QStringLiteral("Map beta"))));
  QVERIFY(f->create(QStringLiteral("bad.emind"), "{ broken map"));
  QVERIFY(f->open(QStringLiteral("a.emind")).isValid());
  window = f->current();
  auto *map = window->findChild<MindMapEditor *>();
  QVERIFY(map);
  auto *surface = map->findChild<QGraphicsView *>();
  QVERIFY(surface);
  QVERIFY(window->getLatestContent().contains(QStringLiteral("Map alpha")));
  auto *tools = map->nodePropertiesPanel();
  QVERIFY(tools);
  map->zoom(2.0 / map->zoomFactor());
  const auto chosenZoom = map->zoomFactor();
  QVERIFY(f->open(QStringLiteral("b.emind")).isValid());
  QCOMPARE(f->current(), window);
  QCOMPARE(window->findChild<MindMapEditor *>(), map);
  QCOMPARE(map->nodePropertiesPanel(), tools);
  QVERIFY(!map->canUndo());
  QCOMPARE(map->selectedNodeIds(), QStringList({QStringLiteral("r")}));
  QCOMPARE(map->zoomFactor(), chosenZoom);
  QCOMPARE(map->findChild<QGraphicsView *>(), surface);
  QVERIFY(window->getLatestContent().contains(QStringLiteral("Map beta")));
  QVERIFY(!window->getLatestContent().contains(QStringLiteral("Map alpha")));
  map->selectNode(QStringLiteral("r"));
  surface->setFocus();
  QTest::keyClick(surface->viewport(), Qt::Key_F2);
  auto *draft = surface->findChild<QPlainTextEdit *>(QStringLiteral("topicEditor"));
  QVERIFY(draft);
  draft->selectAll();
  QTest::keyClicks(draft, "Edited map beta");
  QVERIFY(window->save());
  QTRY_VERIFY(readBytes(f->absolute(QStringLiteral("b.emind"))).contains("Edited map beta"));
  QVERIFY(readBytes(f->absolute(QStringLiteral("a.emind"))).contains("Map alpha"));
  const auto bad = f->open(QStringLiteral("bad.emind"));
  QVERIFY(bad.isValid());
  QCOMPARE(f->current(), window);
  QCOMPARE(window->findChild<MindMapEditor *>(), map);
  QCOMPARE(window->getBuffer().id(), bad.id());
  QString snapshot, error;
  QVERIFY(!window->tryGetLatestContent(&snapshot, &error));
  QVERIFY(!error.isEmpty());
  QVERIFY(!map->canInsert());
  QVERIFY(!map->isEnabled() || !map->isVisible());
  QCOMPARE(readBytes(f->absolute(QStringLiteral("bad.emind"))), QByteArray("{ broken map"));
}

void TestViewAreaTabReuse::typstSessionPosition_data() {
  QTest::addColumn<bool>("reuse");
  QTest::newRow("new-tabs") << false;
  QTest::newRow("reuse-tabs") << true;
}

void TestViewAreaTabReuse::typstSessionPosition() {
  QFETCH(bool, reuse);
  if (reuse)
    f->reuse();
  QByteArray source("#set page(width: 320pt, height: 440pt, margin: 20pt)\n");
  for (int line = 0; line < 120; ++line)
    source += "Line " + QByteArray::number(line) + ": restore this source. $x^2$\n\n";
  QVERIFY(f->create(QStringLiteral("position.typst"), source));
  QVERIFY(f->open(QStringLiteral("position.typst")).isValid());
  auto *typst = f->current()->findChild<vte::VTypstEditor *>();
  QVERIFY(typst);
  auto *edit = typst->getTextEditor()->getTextEdit();
  QTRY_COMPARE_WITH_TIMEOUT(typst->compileStatus(), vte::VTypstEditor::CompileStatus::Ready, 60000);
  QTRY_VERIFY_WITH_TIMEOUT(typst->pdfPageCount() > 1, 60000);
  QTest::qWait(500);
  QCOMPARE(edit->textCursor().position(), 0);
  QVERIFY(edit->viewport()->rect().intersects(edit->cursorRect()));

  // Exercise the source surface, then persist through the real session shutdown path.
  edit->setFocus();
  for (int line = 0; line < 80; ++line)
    QTest::keyClick(edit, Qt::Key_Down);
  const int savedLine = edit->textCursor().blockNumber();
  QVERIFY(savedLine > 0 && savedLine < edit->document()->blockCount() - 1);
  const auto layout = f->area->saveLayout();
  const auto notebook = f->notebook;
  f->config->updateMainConfig(f->config->getConfig().toJson());
  f->area->saveSession();
  QCOMPARE(vxcore_prepare_shutdown(f->context), VXCORE_OK);
  f.reset();
  f = std::make_unique<Fixture>(directory->path());
  QVERIFY2(f->initialize(notebook), qPrintable(f->error));
  f->area->loadLayoutFromSession(layout);
  f->area->restoreSession();
  QVERIFY(f->current());
  QCOMPARE(f->current()->getNodeId().relativePath, QStringLiteral("position.typst"));
  QCOMPARE(f->current()->getMode(), ViewWindowMode::Edit);
  typst = f->current()->findChild<vte::VTypstEditor *>();
  QVERIFY(typst);
  edit = typst->getTextEditor()->getTextEdit();
  QTRY_COMPARE_WITH_TIMEOUT(typst->compileStatus(), vte::VTypstEditor::CompileStatus::Ready, 60000);
  QTRY_VERIFY_WITH_TIMEOUT(typst->pdfPageCount() > 1, 60000);
  QTest::qWait(500);
  QCOMPARE(edit->textCursor().blockNumber(), savedLine);
  QVERIFY2(edit->viewport()->rect().contains(edit->cursorRect()),
           qPrintable(QStringLiteral("savedLine=%1 cursorY=%2 viewportHeight=%3")
                          .arg(savedLine)
                          .arg(edit->cursorRect().y())
                          .arg(edit->viewport()->height())));
  QCOMPARE(edit->toPlainText(), QString::fromUtf8(source));
}

void TestViewAreaTabReuse::typstCompilerAndUnavailableSource() {
  f->reuse();
  QVERIFY(f->create(QStringLiteral("a.typst"), "= Typst alpha\nAlpha body\n"));
  QVERIFY(f->create(QStringLiteral("b.typst"), "= Typst beta\nBeta body\n"));
  QVERIFY(f->open(QStringLiteral("a.typst")).isValid());
  auto *window = f->current();
  auto *typst = window->findChild<vte::VTypstEditor *>();
  QVERIFY(typst);
  auto *source = typst->getTextEditor();
  QVERIFY(source);
  typst->setLivePreviewEnabled(true);
  QTRY_COMPARE_WITH_TIMEOUT(typst->compileStatus(), vte::VTypstEditor::CompileStatus::Ready, 60000);
  auto *compiler = typst->findChild<QProcess *>();
  QVERIFY(compiler);
  const auto pid = compiler->processId();
  QVERIFY(pid != 0);
  QVERIFY(f->open(QStringLiteral("b.typst")).isValid());
  QCOMPARE(f->current(), window);
  QCOMPARE(window->findChild<vte::VTypstEditor *>(), typst);
  QCOMPARE(typst->getTextEditor(), source);
  QCOMPARE(typst->findChild<QProcess *>(), compiler);
  QCOMPARE(compiler->processId(), pid);
  QVERIFY(typst->isLivePreviewEnabled());
  QCOMPARE(source->getText(), QStringLiteral("= Typst beta\nBeta body\n"));
  QVERIFY(!source->getTextEdit()->document()->isUndoAvailable());
  QTRY_COMPARE_WITH_TIMEOUT(typst->compileStatus(), vte::VTypstEditor::CompileStatus::Ready, 60000);
  QTRY_VERIFY(!typst->getHeadings().isEmpty());
  QCOMPARE(typst->getHeadings().first().m_title, QStringLiteral("Typst beta"));
  appendDraft(source->getTextEdit(), QStringLiteral("\nSaved beta\n"));
  QVERIFY(window->save());
  QTRY_VERIFY(readBytes(f->absolute(QStringLiteral("b.typst"))).contains("Saved beta"));
  QCOMPARE(readBytes(f->absolute(QStringLiteral("a.typst"))),
           QByteArray("= Typst alpha\nAlpha body\n"));
  // A valid open handle whose project disappeared before view creation exercises the
  // production controller's unavailable-project branch, not a mocked decoding result.
  QVERIFY(f->create(QStringLiteral("gone/c.typst"), "= Unavailable candidate\n"));
  bool movedProject = false;
  const int hook = f->hooks->addAction<FileOpenEvent>(
      HookNames::FileAfterOpen,
      [&](HookContext &, const FileOpenEvent &event) {
        if (event.bufferId != window->getBuffer().id()) {
          movedProject = QDir().rename(f->absolute(QStringLiteral("gone")),
                                       f->absolute(QStringLiteral("moved")));
        }
      },
      1);
  const auto unavailable = f->open(QStringLiteral("gone/c.typst"));
  f->hooks->removeAction(hook);
  QVERIFY(movedProject);
  QVERIFY(unavailable.isValid());
  QCOMPARE(f->current(), window);
  QCOMPARE(window->getBuffer().id(), unavailable.id());
  QCOMPARE(window->findChild<vte::VTypstEditor *>(), typst);
  QVERIFY(!typst->isVisible());
  QString snapshot, error;
  QVERIFY(!window->tryGetLatestContent(&snapshot, &error));
  QVERIFY(!error.isEmpty());
}

void TestViewAreaTabReuse::imageAndPdfDocuments() {
  f->reuse();
  QVERIFY(f->create(QStringLiteral("a.png"), imageBytes(Qt::red)));
  QVERIFY(f->create(QStringLiteral("b.png"), imageBytes(Qt::blue)));
  QVERIFY(f->create(QStringLiteral("bad.png"), "not an image"));
  QVERIFY(f->open(QStringLiteral("a.png")).isValid());
  auto *window = f->current();
  auto *image = window->findChild<ImageViewer *>();
  QVERIFY(image);
  auto *canvas = image->findChild<QGraphicsView *>();
  QVERIFY(canvas);
  const auto pixel = [&] {
    for (auto *item : canvas->scene()->items()) {
      if (auto *pixmap = qgraphicsitem_cast<QGraphicsPixmapItem *>(item);
          pixmap && !pixmap->pixmap().isNull())
        return pixmap->pixmap().toImage().pixelColor(0, 0);
    }
    return QColor();
  };
  QCOMPARE(pixel(), QColor(Qt::red));
  image->zoom(true);
  image->zoom(true);
  const auto oldCamera = canvas->transform();
  QVERIFY(f->open(QStringLiteral("b.png")).isValid());
  QCOMPARE(f->current(), window);
  QCOMPARE(window->findChild<ImageViewer *>(), image);
  QCOMPARE(image->findChild<QGraphicsView *>(), canvas);
  QCOMPARE(pixel(), QColor(Qt::blue));
  QTRY_VERIFY(canvas->transform() != oldCamera);
  QVERIFY(f->open(QStringLiteral("bad.png")).isValid());
  QCOMPARE(f->current(), window);
  QVERIFY(!pixel().isValid());
  QVERIFY(f->create(QStringLiteral("a.pdf"), pdfBytes(QStringLiteral("PDF alpha"))));
  QVERIFY(f->create(QStringLiteral("b.pdf"), pdfBytes(QStringLiteral("PDF beta"))));
  QVERIFY(f->open(QStringLiteral("a.pdf")).isValid());
  window = f->current();
  auto *pdf = window->findChild<PdfViewer *>();
  QVERIFY(pdf);
  auto *page = pdf->page();
  auto *adapter = pdf->adapter();
  QTRY_VERIFY_WITH_TIMEOUT(adapter->getViewerState().m_valid, 30000);
  QTRY_COMPARE_WITH_TIMEOUT(adapter->getDocumentPageCount(), 3, 30000);
  QTRY_COMPARE_WITH_TIMEOUT(adapter->getViewerState().m_pageCount, 3, 30000);
  adapter->setZoom(QStringLiteral("1.5"));
  QTRY_COMPARE(adapter->getViewerState().m_scale, 1.5);
  adapter->setRotation(90);
  QTRY_COMPARE(adapter->getViewerState().m_rotation, 90);
  // Zoom and rotation can reposition the document; choose A's replay page last.
  adapter->gotoPage(3);
  QTRY_COMPARE(adapter->getViewerState().m_page, 3);
  QVERIFY(f->open(QStringLiteral("b.pdf")).isValid());
  QCOMPARE(f->current(), window);
  QCOMPARE(window->findChild<PdfViewer *>(), pdf);
  QCOMPARE(pdf->page(), page);
  QCOMPARE(pdf->adapter(), adapter);
  QTRY_VERIFY_WITH_TIMEOUT(adapter->getViewerState().m_valid, 30000);
  QTRY_COMPARE_WITH_TIMEOUT(adapter->getDocumentPageCount(), 3, 30000);
  QTRY_COMPARE_WITH_TIMEOUT(adapter->getViewerState().m_pageCount, 3, 30000);
  QCOMPARE(adapter->getViewerState().m_page, 1);
  QCOMPARE(adapter->getViewerState().m_rotation, 0);
  QVERIFY(!adapter->isViewerReplayPending());
  QTRY_VERIFY_WITH_TIMEOUT(evaluate(page, QStringLiteral("document.body.innerText"))
                               .toString()
                               .contains(QStringLiteral("PDF beta")),
                           15000);
  adapter->gotoPage(2);
  QTRY_COMPARE(adapter->getViewerState().m_page, 2);
  f->theme->switchTheme(QStringLiteral("vscode-dark"));
  QTRY_VERIFY_WITH_TIMEOUT(adapter->getViewerState().m_valid && !adapter->isViewerReplayPending(),
                           30000);
  QTRY_COMPARE_WITH_TIMEOUT(adapter->getViewerState().m_page, 2, 30000);
}

void TestViewAreaTabReuse::dirtyReplacement_data() {
  QTest::addColumn<int>("answer");
  QTest::newRow("Cancel") << int(QMessageBox::Cancel);
  QTest::newRow("Save") << int(QMessageBox::Save);
  QTest::newRow("Discard") << int(QMessageBox::Discard);
}

void TestViewAreaTabReuse::dirtyReplacement() {
  QFETCH(int, answer);
  f->reuse();
  QVERIFY(f->create(QStringLiteral("a.md"), "# Alpha\n"));
  QVERIFY(f->create(QStringLiteral("b.md"), "# Beta\n"));
  const auto a = f->open(QStringLiteral("a.md"));
  QVERIFY(a.isValid());
  auto *window = f->current();
  auto *editor = window->findChild<MarkdownEditor *>();
  QVERIFY(editor);
  appendDraft(editor->getTextEdit(), QStringLiteral("\nUnsaved alpha draft\n"));
  QVERIFY(window->isModified());
  const auto draft = editor->getText();
  const int undo = editor->getTextEdit()->document()->availableUndoSteps();
  const auto oldOrder = f->order();
  DialogAnswer dialog(static_cast<QMessageBox::StandardButton>(answer));
  const auto b = f->open(QStringLiteral("b.md"));
  QVERIFY(dialog.accepted());
  QCOMPARE(f->current(), window);
  QCOMPARE(f->tabs(), 1);
  if (answer == QMessageBox::Cancel) {
    QCOMPARE(window->getBuffer().id(), a.id());
    QCOMPARE(editor->getText(), draft);
    QCOMPARE(editor->getTextEdit()->document()->availableUndoSteps(), undo);
    QCOMPARE(f->order(), oldOrder);
    QCOMPARE(f->coreCurrent(), a.id());
    QCOMPARE(f->buffers->listBuffers().size(), 1);
  } else {
    QCOMPARE(window->getBuffer().id(), b.id());
    QCOMPARE(editor->getText(), QStringLiteral("# Beta\n"));
    QCOMPARE(f->order(), QStringList({b.id()}));
    QCOMPARE(f->coreCurrent(), b.id());
    QVERIFY(!editor->getTextEdit()->document()->isUndoAvailable());
    appendDraft(editor->getTextEdit(), QStringLiteral("Saved beta only\n"));
    QVERIFY(window->save());
  }
  QTRY_COMPARE(readBytes(f->absolute(QStringLiteral("a.md"))),
               answer == QMessageBox::Save ? draft.toUtf8() : QByteArray("# Alpha\n"));
  QTRY_COMPARE(readBytes(f->absolute(QStringLiteral("b.md"))),
               answer == QMessageBox::Cancel ? QByteArray("# Beta\n")
                                             : QByteArray("# Beta\nSaved beta only\n"));
}

void TestViewAreaTabReuse::differentTypeAndCreationFailure() {
  f->reuse();
  QVERIFY(f->create(QStringLiteral("a.md"), "# Alpha\n"));
  QVERIFY(f->create(QStringLiteral("b.txt"), "Text beta\n"));
  QVERIFY(f->open(QStringLiteral("a.md")).isValid());
  QPointer<ViewWindow2> old = f->current();
  QSignalSpy failed(f->area.get(), &ViewArea2::viewWindowCreationFailed);
  f->factory->registerCreator(QStringLiteral("Text"),
                              [](ServiceLocator &, const Buffer2 &, QWidget *,
                                 ViewWindowMode) -> ViewWindow2 * { return nullptr; });
  QVERIFY(f->factory->hasCreator(QStringLiteral("Text")));
  f->open(QStringLiteral("b.txt"));
  QCOMPARE(failed.count(), 1);
  QCOMPARE(f->current(), old.data());
  QCOMPARE(f->tabs(), 1);
  QCOMPARE(f->buffers->listBuffers().size(), 1);
  f->factory->registerBuiltInCreators();
  QVERIFY(f->open(QStringLiteral("b.txt")).isValid());
  QCOMPARE(f->tabs(), 1);
  QVERIFY(f->current()->findChild<TextEditor *>());
  QTRY_VERIFY(old.isNull());
  QVERIFY(f->open(QStringLiteral("a.md")).isValid());
  old = f->current();
  auto *editor = old->findChild<MarkdownEditor *>();
  QVERIFY(editor);
  appendDraft(editor->getTextEdit(), QStringLiteral("draft"));
  DialogAnswer dialog(QMessageBox::Cancel);
  const auto b = f->open(QStringLiteral("b.txt"));
  QVERIFY(dialog.accepted());
  QVERIFY(old);
  QCOMPARE(f->tabs(), 2);
  QVERIFY(f->order().contains(old->getBuffer().id()));
  QVERIFY(f->order().contains(b.id()));
  QVERIFY(old->getLatestContent().contains(QStringLiteral("draft")));
}

void TestViewAreaTabReuse::vetoAndOwnedCandidate() {
  f->reuse();
  QVERIFY(f->create(QStringLiteral("a.md"), "# Alpha\n"));
  QVERIFY(f->create(QStringLiteral("b.md"), "# Beta\n"));
  const auto a = f->open(QStringLiteral("a.md"));
  QVERIFY(a.isValid());
  auto *window = f->current();
  int before = 0, afterClose = 0, afterOpen = 0;
  const int veto = f->hooks->addAction<ViewWindowCloseEvent>(
      HookNames::ViewWindowBeforeClose, [&](HookContext &ctx, const ViewWindowCloseEvent &) {
        ++before;
        ctx.cancel();
      });
  const int closedHook = f->hooks->addAction<ViewWindowCloseEvent>(
      HookNames::ViewWindowAfterClose,
      [&](HookContext &, const ViewWindowCloseEvent &) { ++afterClose; });
  const int openedHook = f->hooks->addAction<ViewWindowOpenEvent>(
      HookNames::ViewWindowAfterOpen,
      [&](HookContext &, const ViewWindowOpenEvent &) { ++afterOpen; });
  const auto removeHooks = qScopeGuard([&] {
    f->hooks->removeAction(veto);
    f->hooks->removeAction(closedHook);
    f->hooks->removeAction(openedHook);
  });
  f->open(QStringLiteral("b.md"));
  QCOMPARE(before, 1);
  QCOMPARE(afterClose, 0);
  QCOMPARE(afterOpen, 0);
  QCOMPARE(f->current(), window);
  QCOMPARE(f->order(), QStringList({a.id()}));
  QCOMPARE(f->buffers->listBuffers().size(), 1);
  // An owner outside the visible splitter tree must protect B from veto cleanup.
  const auto hidden = f->workspaces->createWorkspace(QStringLiteral("Unshown owner"));
  QVERIFY(!hidden.isEmpty());
  QString candidate;
  const int owner = f->hooks->addAction<FileOpenEvent>(
      HookNames::FileAfterOpen,
      [&](HookContext &, const FileOpenEvent &event) {
        candidate = event.bufferId;
        f->workspaces->addBuffer(hidden, candidate);
      },
      1);
  f->open(QStringLiteral("b.md"));
  f->hooks->removeAction(owner);
  QCOMPARE(before, 2);
  QCOMPARE(f->order(hidden), QStringList({candidate}));
  QVERIFY(f->buffers->getBufferHandle(candidate).isValid());
  QCOMPARE(f->current(), window);
  f->hooks->removeAction(veto);
  const auto b = f->open(QStringLiteral("b.md"));
  QCOMPARE(f->current(), window);
  QCOMPARE(window->getBuffer().id(), b.id());
  QCOMPARE(afterClose, 1);
  QCOMPARE(afterOpen, 1);
  QCOMPARE(f->order(), QStringList({b.id()}));
  QCOMPARE(f->coreCurrent(), b.id());
}

void TestViewAreaTabReuse::siblingAndVirtualTabs() {
  f->reuse();
  f->controller()->openVxUrl(QUrl(QStringLiteral("vx://home")));
  QPointer<ViewWindow2> home = f->current();
  QVERIFY(home && home->getNodeId().isVirtual());
  QVERIFY(f->create(QStringLiteral("a.md"), "# Alpha\n"));
  QVERIFY(f->create(QStringLiteral("b.md"), "# Beta\n"));
  const auto a = f->open(QStringLiteral("a.md"));
  QVERIFY(a.isValid());
  QTRY_VERIFY(home.isNull());
  QCOMPARE(f->tabs(), 1);
  const auto first = f->workspace();
  auto *original = f->current();
  f->controller()->splitViewSplit(first, Direction::Right, true);
  QCOMPARE(f->area->getViewSplitCount(), 2);
  auto *sibling = f->current();
  QVERIFY(sibling && sibling != original);
  QCOMPARE(sibling->getBuffer().id(), a.id());
  const auto second = f->workspace();
  f->controller()->focusWindow(first, a.id());
  QCOMPARE(f->current(), original);
  const auto b = f->open(QStringLiteral("b.md"));
  QCOMPARE(f->current(), original);
  QCOMPARE(original->getBuffer().id(), b.id());
  QCOMPARE(sibling->getBuffer().id(), a.id());
  QCOMPARE(sibling->getLatestContent(), QStringLiteral("# Alpha\n"));
  QCOMPARE(f->order(second), QStringList({a.id()}));
  QVERIFY(f->buffers->getBufferHandle(a.id()).isValid());
  f->controller()->openVxUrl(QUrl(QStringLiteral("vx://settings")));
  QPointer<ViewWindow2> settings = f->current();
  QVERIFY(settings && settings->getNodeId().isVirtual());
  const int before = f->tabs();
  QVERIFY(f->open(QStringLiteral("a.md")).isValid());
  QVERIFY(settings);
  QCOMPARE(f->tabs(), before + 1);
  f->controller()->openVxUrl(QUrl(QStringLiteral("vx://home")));
  home = f->current();
  QVERIFY(home);
  QVERIFY(f->create(QStringLiteral("c.md"), "# Gamma\n"));
  const int withHome = f->tabs();
  QVERIFY(f->open(QStringLiteral("c.md")).isValid());
  QVERIFY(home);
  QCOMPARE(f->tabs(), withHome + 1);
}

void TestViewAreaTabReuse::explorerGestures_data() {
  QTest::addColumn<bool>("flat");
  QTest::addColumn<int>("gesture");
  for (bool flat : {false, true}) {
    const QByteArray prefix = flat ? "file-list-" : "tree-";
    QTest::newRow((prefix + "single-click").constData()) << flat << 0;
    QTest::newRow((prefix + "double-click").constData()) << flat << 1;
    QTest::newRow((prefix + "Enter").constData()) << flat << 2;
    QTest::newRow((prefix + "Return").constData()) << flat << 3;
  }
}

void TestViewAreaTabReuse::explorerGestures() {
  QFETCH(bool, flat);
  QFETCH(int, gesture);
  f->reuse();
  f->config->getWidgetConfig().setNodeExplorerSingleClickActivation(gesture == 0);
  QVERIFY(f->create(QStringLiteral("a.md"), "# Alpha\n"));
  QVERIFY(f->create(QStringLiteral("b.md"), "# Beta\n"));
  QVERIFY(f->create(QStringLiteral("c.md"), "# Gamma\n"));
  QVERIFY(!f->notebooks->createFolder(f->notebook, QString(), QStringLiteral("folder")).isEmpty());
  const auto a = f->open(QStringLiteral("a.md"));
  QVERIFY(a.isValid());
  NotebookNodeModel model(f->services);
  model.setNotebookId(f->notebook);
  NotebookNodeController controller(f->services);
  controller.setModel(&model);
  std::unique_ptr<QAbstractItemView> explorer;
  if (flat) {
    auto *view = new FileListView;
    view->setModel(&model);
    view->setController(&controller);
    controller.setSelectedNodesCallback([view] { return view->selectedNodeIds(); });
    explorer.reset(view);
  } else {
    auto *view = new NotebookNodeView;
    view->setModel(&model);
    view->setController(&controller);
    controller.setView(view);
    explorer.reset(view);
  }
  connect(&controller, &NotebookNodeController::nodeActivated, f->buffers->asQObject(),
          [&](const NodeIdentifier &node, const FileOpenSettings &settings) {
            f->buffers->openBuffer(node, settings);
          });
  explorer->resize(420, 520);
  explorer->show();
  explorer->activateWindow();
  if (model.canFetchMore(QModelIndex()))
    model.fetchMore(QModelIndex());
  QCoreApplication::processEvents();
  QSignalSpy activated(&controller, &NotebookNodeController::nodeActivated);
  const auto activate = [&](const QModelIndex &index, Qt::KeyboardModifiers modifiers) {
    explorer->setCurrentIndex(index);
    explorer->scrollTo(index);
    explorer->setFocus();
    const QPoint point = explorer->visualRect(index).center();
    if (gesture == 0)
      QTest::mouseClick(explorer->viewport(), Qt::LeftButton, modifiers, point);
    else if (gesture == 1)
      QTest::mouseDClick(explorer->viewport(), Qt::LeftButton, modifiers, point);
    else
      QTest::keyClick(explorer.get(), gesture == 2 ? Qt::Key_Enter : Qt::Key_Return, modifiers);
    QCoreApplication::processEvents();
  };
  const auto bIndex = model.indexFromNodeId(f->node(QStringLiteral("b.md")));
  QVERIFY(bIndex.isValid());
  if (gesture == 1) {
    QTest::mouseClick(explorer->viewport(), Qt::LeftButton, Qt::ControlModifier,
                      explorer->visualRect(bIndex).center());
    QCOMPARE(activated.count(), 0);
    QCOMPARE(f->tabs(), 1);
    QVERIFY(explorer->selectionModel()->isSelected(bIndex));
  }
  activate(bIndex, Qt::ControlModifier);
  QCOMPARE(activated.count(), 1);
  QCOMPARE(f->tabs(), 2);
  const auto bId = f->current()->getBuffer().id();
  QCOMPARE(f->order(), QStringList({a.id(), bId}));
  // A Ctrl activation of an existing target is deduplication, not replacement or duplication.
  const auto aIndex = model.indexFromNodeId(f->node(QStringLiteral("a.md")));
  QVERIFY(aIndex.isValid());
  activate(aIndex, Qt::ControlModifier);
  QCOMPARE(activated.count(), 2);
  QCOMPARE(f->tabs(), 2);
  QCOMPARE(f->current()->getBuffer().id(), a.id());
  const auto cIndex = model.indexFromNodeId(f->node(QStringLiteral("c.md")));
  QVERIFY(cIndex.isValid());
  auto *retained = f->current();
  activate(cIndex, Qt::NoModifier);
  QCOMPARE(activated.count(), 3);
  QCOMPARE(f->current(), retained);
  QCOMPARE(f->current()->getNodeId(), f->node(QStringLiteral("c.md")));
  QCOMPARE(f->tabs(), 2);
  const auto folder = model.indexFromNodeId(f->node(QStringLiteral("folder")));
  QVERIFY(folder.isValid());
  activate(folder, Qt::ControlModifier);
  QCOMPARE(activated.count(), 3);
  QCOMPARE(f->tabs(), 2);
  // Use Qt's real delegate editor: Ctrl+Enter must commit editing, never activate.
  explorer->setCurrentIndex(cIndex);
  explorer->edit(cIndex);
  QPointer<QLineEdit> rename = explorer->findChild<QLineEdit *>();
  QVERIFY(rename);
  QVERIFY(rename->isVisible());
  rename->selectAll();
  QTest::keyClicks(rename, "renamed.md");
  QTest::keyClick(rename, Qt::Key_Enter, Qt::ControlModifier);
  QCoreApplication::processEvents();
  QCOMPARE(activated.count(), 3);
  QCOMPARE(f->tabs(), 2);
  QTRY_VERIFY(!rename || !rename->isVisible());
  QVERIFY(QFileInfo::exists(f->absolute(QStringLiteral("renamed.md"))));
}

void TestViewAreaTabReuse::restoreOrderedWorkspacesAndLockedNote() {
  f->reuse();
  QVERIFY(f->create(QStringLiteral("a.md"), "# Alpha\n"));
  QVERIFY(f->create(QStringLiteral("b.md"), "# Beta\n"));
  const auto protectedId = f->createProtected(QStringLiteral("secret.md"), "# Secret\n", true);
  QVERIFY(!protectedId.isEmpty());
  const auto a = f->open(QStringLiteral("a.md"), true);
  const auto b = f->open(QStringLiteral("b.md"), true);
  const auto secret = f->openProtected(protectedId, true);
  QVERIFY(a.isValid() && b.isValid() && secret.isValid());
  const auto first = f->workspace();
  f->controller()->setBufferOrder(first, {secret.id(), a.id(), b.id()});
  // setBufferOrder is the core reorder callback; mirror the actual user tab drag.
  auto *split = f->area->getCurrentViewSplit();
  split->tabBar()->moveTab(2, 0);
  f->controller()->focusWindow(first, b.id());
  f->controller()->newWorkspace(first, QStringLiteral("Second"));
  const auto second = f->workspace();
  QVERIFY(second != first);
  QVERIFY(f->open(QStringLiteral("b.md"), true).isValid());
  QVERIFY(f->open(QStringLiteral("a.md"), true).isValid());
  // First is hidden and contains the protected tab. Both hidden and visible restores
  // must ignore the OFF preference; no authentication happens during session loading.
  const auto layout = f->area->saveLayout();
  const auto firstOrder = f->order(first);
  const auto secondOrder = f->order(second);
  QCOMPARE(firstOrder.size(), 3);
  QCOMPARE(secondOrder.size(), 2);
  const auto firstCurrent = f->coreCurrent(first);
  const auto secondCurrent = f->coreCurrent(second);
  const auto notebook = f->notebook;
  f->config->updateMainConfig(f->config->getConfig().toJson());
  f->area->saveSession();
  QCOMPARE(vxcore_prepare_shutdown(f->context), VXCORE_OK);
  f.reset();
  // A genuinely new isolated test-mode context reads vxsession.json, not a copied model.
  f = std::make_unique<Fixture>(directory->path());
  QVERIFY2(f->initialize(notebook), qPrintable(f->error));
  QVERIFY(!f->config->getCoreConfig().getAlwaysOpenNotesInNewTabs());
  QCOMPARE(f->order(first), firstOrder);
  QCOMPARE(f->order(second), secondOrder);
  f->area->loadLayoutFromSession(layout);
  f->area->restoreSession();
  QCOMPARE(f->area->getViewWindowIdsForWorkspace(second).size(), 2);
  QCOMPARE(f->coreCurrent(second), secondCurrent);
  QCOMPARE(f->area->getCurrentBufferIdForWorkspace(second), secondCurrent);
  f->controller()->setShouldPropagateToCore(true);
  f->controller()->switchWorkspace(second, first);
  QCOMPARE(f->tabs(), 3);
  QCOMPARE(f->order(first), firstOrder);
  QCOMPARE(f->coreCurrent(first), firstCurrent);
  QCOMPARE(f->area->getCurrentBufferIdForWorkspace(first), firstCurrent);
  const auto windows = f->area->getCurrentViewSplit()->getAllViewWindows();
  QStringList visibleOrder;
  ViewWindow2 *locked = nullptr;
  for (auto *window : windows) {
    visibleOrder.append(window->getBuffer().id());
    if (window->getBuffer().id() == secret.id())
      locked = window;
  }
  QCOMPARE(visibleOrder, firstOrder);
  QVERIFY(locked);
  QVERIFY(locked->findChild<QPushButton *>(QStringLiteral("unlockProtectedNote")));
  QVERIFY(!locked->findChild<MarkdownEditor *>());
  QVERIFY(!locked->findChild<MarkdownViewer *>());
  // Authenticate explicitly, then the normal open path replaces only the locked placeholder.
  QByteArray password("reuse-test-password");
  QCOMPARE(f->controller()->unlockNoteEncryption(notebook, secret.nodeId().relativePath, password),
           VXCORE_OK);
  QVERIFY(f->openProtected(protectedId).isValid());
  QCOMPARE(f->tabs(), 3);
  QVERIFY(!f->current()->findChild<QPushButton *>(QStringLiteral("unlockProtectedNote")));
  auto *reader = f->current()->findChild<MarkdownViewer *>();
  QVERIFY(reader);
  QTRY_VERIFY_WITH_TIMEOUT(rendered(reader, QStringLiteral("Secret")), 30000);
}

void TestViewAreaTabReuse::reopenSplitAndBatches() {
  f->reuse();
  for (const auto &name : {QStringLiteral("a.md"), QStringLiteral("b.md"), QStringLiteral("c.md"),
                           QStringLiteral("d.md")})
    QVERIFY(f->create(name, ("# " + name + '\n').toUtf8()));
  const auto a = f->open(QStringLiteral("a.md"));
  const auto b = f->open(QStringLiteral("b.md"));
  QVERIFY(a.isValid() && b.isValid());
  QCOMPARE(f->tabs(), 1);
  f->controller()->openLastClosedFile();
  QCOMPARE(f->tabs(), 2);
  QCOMPARE(f->current()->getNodeId(), f->node(QStringLiteral("a.md")));
  const auto first = f->workspace();
  f->controller()->splitViewSplit(first, Direction::Right, true);
  QCOMPARE(f->area->getViewSplitCount(), 2);
  QCOMPARE(f->tabs(), 1);
  QCOMPARE(f->current()->getNodeId(), f->node(QStringLiteral("a.md")));
  QCOMPARE(f->order(first).size(), 2);
  NotebookNodeController controller(f->services);
  connect(&controller, &NotebookNodeController::nodeActivated, f->buffers->asQObject(),
          [&](const NodeIdentifier &node, const FileOpenSettings &settings) {
            f->buffers->openBuffer(node, settings);
          });
  controller.openNodes({f->node(QStringLiteral("c.md")), f->node(QStringLiteral("d.md"))});
  QCOMPARE(f->tabs(), 3);
  const auto main = f->workspace();
  const auto mainOrder = f->order(main);
  const auto mainCurrent = f->coreCurrent(main);
  controller.openNodes({f->node(QStringLiteral("b.md")), f->node(QStringLiteral("c.md"))}, true);
  QCOMPARE(f->order(main), mainOrder);
  QCOMPARE(f->coreCurrent(main), mainCurrent);
  QStringList detachedNotes;
  for (const auto &value : f->workspaces->listWorkspaces()) {
    const auto object = value.toObject();
    const auto id = object.value(QStringLiteral("id")).toString();
    if (id == first || id == main) {
      continue;
    }
    for (const auto &bufferId : f->order(id)) {
      QVERIFY(f->area->findWindowIdByBufferId(id, bufferId) !=
              ViewAreaController::InvalidViewWindowId);
      detachedNotes.append(f->buffers->getBufferHandle(bufferId).nodeId().relativePath);
    }
  }
  detachedNotes.sort();
  QCOMPARE(detachedNotes, QStringList({QStringLiteral("b.md"), QStringLiteral("c.md")}));
}

void TestViewAreaTabReuse::protectedResourcesAndLateWork() {
  f->reuse();
  f->config->getEditorConfig().getMarkdownEditorConfig().setInplacePreviewSources(
      MarkdownEditorConfig::ImageLink);
  const QByteArray alphaImage = imageBytes(Qt::red, QSize(17, 13));
  const QByteArray betaImage = imageBytes(Qt::blue, QSize(31, 19));
  const QByteArray alphaSource =
      "# Protected alpha\n\n![alpha](data:image/png;base64," + alphaImage.toBase64() + ")\n";
  const QByteArray betaSource =
      "# Protected beta\n\n![beta](data:image/png;base64," + betaImage.toBase64() + ")\n";
  QVERIFY(f->create(QStringLiteral("ordinary.md"), "# Ordinary\n"));
  QVERIFY(f->create(QStringLiteral("end.md"), "# Public end\n\n![public](end.png)\n"));
  QVERIFY(
      writeBytes(f->absolute(QStringLiteral("end.png")), imageBytes(Qt::yellow, QSize(41, 23))));
  const auto protectedA = f->createProtected(QStringLiteral("a.md"), alphaSource, true);
  const auto protectedB = f->createProtected(QStringLiteral("b.md"), betaSource, false);
  QVERIFY(!protectedA.isEmpty() && !protectedB.isEmpty());
  QVERIFY(f->open(QStringLiteral("ordinary.md")).isValid());
  auto *window = f->current();
  auto *editor = window->findChild<MarkdownEditor *>();
  QPointer<MarkdownViewer> ordinary = window->findChild<MarkdownViewer *>();
  QVERIFY(editor && ordinary);
  const auto nativeImage = [editor](QString *name = nullptr) -> QImage {
    for (auto block = editor->getTextEdit()->document()->begin(); block.isValid();
         block = block.next()) {
      const auto previews = vte::BlockPreviewData::get(block);
      if (!previews)
        continue;
      for (const auto *preview : previews->getPreviewData()) {
        const auto *data = preview->getImageData();
        if (preview->source() != vte::PreviewData::ImageLink || !data)
          continue;
        if (const auto *pixmap = editor->findImageFromDocumentResourceMgr(data->m_imageName)) {
          if (name)
            *name = data->m_imageName;
          return pixmap->toImage();
        }
      }
    }
    return {};
  };
  QTRY_VERIFY_WITH_TIMEOUT(rendered(ordinary, QStringLiteral("Ordinary")), 30000);
  const auto tabId = window->getViewWindowId();
  const auto a = f->openProtected(protectedA);
  QVERIFY(a.isValid() && a.isEncrypted());
  QCOMPARE(f->current(), window);
  QCOMPARE(window->findChild<MarkdownEditor *>(), editor);
  QTRY_VERIFY(ordinary.isNull());
  QPointer<MarkdownViewer> privateReader = window->findChild<MarkdownViewer *>();
  QVERIFY(privateReader);
  QTRY_VERIFY_WITH_TIMEOUT(rendered(privateReader, QStringLiteral("Protected alpha")), 30000);
  QTRY_VERIFY_WITH_TIMEOUT(!nativeImage().isNull(), 10000);
  QString oldImageName;
  QCOMPARE(nativeImage(&oldImageName).pixelColor(0, 0), QColor(Qt::red));
  QVERIFY(!oldImageName.isEmpty());
  QPointer<QWebEnginePage> oldPage = privateReader->page();
  QPointer<QWebEngineProfile> oldProfile = oldPage->profile();
  QPointer<WebViewJobSession> oldEndpoint = privateReader->jobSession();
  QVERIFY(oldProfile->isOffTheRecord());
  const auto oldUrl = oldPage->url();
  QVERIFY(oldUrl.scheme() == QStringLiteral("vxnote"));
  QStringList destroyed;
  const auto pageDestroyed = connect(oldPage, &QObject::destroyed, this,
                                     [&] { destroyed.append(QStringLiteral("page")); });
  const auto profileDestroyed = connect(oldProfile, &QObject::destroyed, this,
                                        [&] { destroyed.append(QStringLiteral("profile")); });
  const auto disconnectLifetime = qScopeGuard([&] {
    disconnect(pageDestroyed);
    disconnect(profileDestroyed);
  });
  const auto first = f->workspace();
  f->controller()->splitViewSplit(first, Direction::Right, true);
  auto *sibling = f->current();
  QVERIFY(sibling != window);
  auto *siblingReader = sibling->findChild<MarkdownViewer *>();
  QVERIFY(siblingReader);
  QTRY_VERIFY_WITH_TIMEOUT(rendered(siblingReader, QStringLiteral("Protected alpha")), 30000);
  f->controller()->focusWindow(first, a.id());
  auto *jobs = window->findChild<MarkdownWebJobController *>();
  QVERIFY(jobs);
  struct AnchorCompletion {
    int count = 0;
    WebViewJobResult::Status status = WebViewJobResult::Status::Error;
  };
  const auto anchorCompletion = std::make_shared<AnchorCompletion>();
  int staleMath = 0;
  const auto mathConnection = connect(jobs, &MarkdownWebJobController::mathPreviewDataReady, this,
                                      [&](const MarkdownWebJobController::PreviewData &data) {
                                        if (data.m_id == 901)
                                          ++staleMath;
                                      });
  const auto disconnectMath = qScopeGuard([&] { disconnect(mathConnection); });
  Buffer2 b;
  bool completedImageDialog = false;
  bool handlingImageDialog = false;
  QTimer imageTimer;
  QElapsedTimer imageWait;
  imageWait.start();
  imageTimer.setInterval(20);
  connect(&imageTimer, &QTimer::timeout, this, [&] {
    for (auto *widget : QApplication::topLevelWidgets()) {
      auto *dialog = qobject_cast<ImageInsertDialog *>(widget);
      if (handlingImageDialog || !dialog || !dialog->isVisible())
        continue;
      handlingImageDialog = true;
      dialog->setImageSource(ImageInsertDialog::ImageData);
      dialog->setImage(QImage::fromData(alphaImage));
      dialog->setInsertAsBase64(true);
      // Real pending reader work and a real user image insertion cross the rebind.
      jobs->fetchHeadingAnchor(
          QString::fromUtf8(alphaSource), 0,
          [anchorCompletion](const MarkdownWebJobController::HeadingAnchorResult &result) {
            ++anchorCompletion->count;
            anchorCompletion->status = result.m_status;
          });
      jobs->previewMath(901, 902, QStringLiteral("$x^2+y^2$"), 1.0);
      FileOpenSettings navigation;
      navigation.m_anchor = QStringLiteral("protected-alpha");
      window->applyFileOpenSettings(navigation);
      b = f->openProtected(protectedB);
      imageTimer.stop();
      dialog->accept();
      completedImageDialog = true;
      return;
    }
    if (imageWait.elapsed() >= 5000) {
      QTest::qFail("Timed out waiting for the real image insertion dialog", __FILE__, __LINE__);
      imageTimer.stop();
      for (auto *widget : QApplication::topLevelWidgets()) {
        if (auto *dialog = qobject_cast<QDialog *>(widget); dialog && dialog->isVisible())
          dialog->reject();
      }
    }
  });
  imageTimer.start();
  editor->typeImage();
  QVERIFY(completedImageDialog);
  QVERIFY(b.isValid() && b.isEncrypted());
  QCOMPARE(f->current(), window);
  QCOMPARE(window->getViewWindowId(), tabId);
  QCOMPARE(window->findChild<MarkdownEditor *>(), editor);
  QTRY_VERIFY(privateReader.isNull() && oldPage.isNull() && oldProfile.isNull() &&
              oldEndpoint.isNull());
  QCOMPARE(destroyed, QStringList({QStringLiteral("page"), QStringLiteral("profile")}));
  QTRY_COMPARE(anchorCompletion->count, 1);
  QCOMPARE(anchorCompletion->status, WebViewJobResult::Status::Cancelled);
  auto *newReader = window->findChild<MarkdownViewer *>();
  QVERIFY(newReader);
  QVERIFY(newReader->page()->url() != oldUrl);
  QTRY_VERIFY_WITH_TIMEOUT(rendered(newReader, QStringLiteral("Protected beta")), 30000);
  QCOMPARE(staleMath, 0);
  QCOMPARE(editor->getText(), QString::fromUtf8(betaSource));
  QVERIFY(!editor->findImageFromDocumentResourceMgr(oldImageName));
  QTRY_VERIFY_WITH_TIMEOUT(!nativeImage().isNull(), 10000);
  QCOMPARE(nativeImage().pixelColor(0, 0), QColor(Qt::blue));
  QVERIFY(!evaluate(newReader->page(), QStringLiteral("document.body.innerText"))
               .toString()
               .contains(QStringLiteral("Protected alpha")));
  QCOMPARE(editor->getTextEdit()->textCursor().position(), 0);
  QCOMPARE(sibling->getBuffer().id(), a.id());
  QVERIFY(rendered(siblingReader, QStringLiteral("Protected alpha")));
  appendDraft(editor->getTextEdit(), QStringLiteral("\nSaved protected beta\n"));
  QVERIFY(window->save());
  QTRY_VERIFY(window->getBuffer().getContentRaw().contains("Saved protected beta"));
  QTRY_VERIFY_WITH_TIMEOUT(!f->buffers->isSaveQueueBusy(b.id()) && !window->isModified(), 10000);
  QVERIFY(!readBytes(f->absolute(b.nodeId().relativePath)).contains("Saved protected beta"));
  QPointer<MarkdownViewer> betaReader = newReader;
  QPointer<QWebEngineProfile> betaProfile = newReader->page()->profile();
  QVERIFY(f->open(QStringLiteral("end.md")).isValid());
  QCOMPARE(f->current(), window);
  QCOMPARE(window->findChild<MarkdownEditor *>(), editor);
  QTRY_VERIFY(betaReader.isNull() && betaProfile.isNull());
  auto *publicReader = window->findChild<MarkdownViewer *>();
  QVERIFY(publicReader);
  QCOMPARE(publicReader->page()->profile(), f->profiles->profile());
  QTRY_VERIFY_WITH_TIMEOUT(rendered(publicReader, QStringLiteral("Public end")), 30000);
  QTRY_VERIFY_WITH_TIMEOUT(!nativeImage().isNull(), 10000);
  QCOMPARE(nativeImage().pixelColor(0, 0), QColor(Qt::yellow));
  QCOMPARE(sibling->getLatestContent(), QString::fromUtf8(alphaSource));
  // Reopen the actual encrypted bytes to prove the save went to B, not A.
  QVERIFY(f->openProtected(protectedB).isValid());
  QCOMPARE(f->current(), window);
  QVERIFY(editor->getText().contains(QStringLiteral("Saved protected beta")));
  QVERIFY(!editor->getText().contains(QStringLiteral("Protected alpha")));
}

void TestViewAreaTabReuse::pdfCommentDurability_data() {
  QTest::addColumn<bool>("blocked");
  QTest::newRow("immediate-edit-flushed-to-outgoing-note") << false;
  QTest::newRow("unwritable-sidecar-refuses-replacement") << true;
}

void TestViewAreaTabReuse::pdfCommentDurability() {
  QFETCH(bool, blocked);
  f->reuse();
  QVERIFY(f->create(QStringLiteral("a.pdf"), pdfBytes(QStringLiteral("PDF alpha"))));
  QVERIFY(f->create(QStringLiteral("b.pdf"), pdfBytes(QStringLiteral("PDF beta"))));
  const auto a = f->open(QStringLiteral("a.pdf"));
  QVERIFY(a.isValid());
  auto *window = f->current();
  auto *comments = window->findChild<CommentController *>();
  auto *viewer = window->findChild<PdfViewer *>();
  QVERIFY(comments && viewer);
  QTRY_VERIFY_WITH_TIMEOUT(viewer->adapter()->getViewerState().m_valid, 30000);
  QVERIFY(comments->isEditable());
  const auto location = f->comments->resolveLocation(a.nodeId());
  QVERIFY(!location.m_storePath.isEmpty());
  if (blocked) {
    // A directory at the final filename fails QSaveFile::commit on every platform,
    // including Windows/admin where chmod/read-only bits are not deterministic.
    QVERIFY(QDir().mkpath(location.m_storePath));
  }
  const QJsonObject anchor{{QStringLiteral("type"), QStringLiteral("pdf-freetext")},
                           {QStringLiteral("page"), 0},
                           {QStringLiteral("x"), 10.0},
                           {QStringLiteral("y"), 10.0},
                           {QStringLiteral("fontSize"), 12.0}};
  comments->addComment(anchor, QStringLiteral("yellow"));
  QCOMPARE(comments->getComments().m_comments.size(), 1);
  const auto id = comments->getComments().m_comments.first().m_id;
  comments->setCommentText(id, QStringLiteral("Immediate outgoing comment"));
  QVERIFY(comments->hasUnsavedChanges());
  // Deliberately no processEvents/wait/flush between editing and opening B.
  const auto b = f->open(QStringLiteral("b.pdf"));
  QCOMPARE(f->current(), window);
  QCOMPARE(f->tabs(), 1);
  QCOMPARE(window->findChild<PdfViewer *>(), viewer);
  if (blocked) {
    QCOMPARE(window->getBuffer().id(), a.id());
    QCOMPARE(comments->getActiveFile(), a.nodeId());
    QVERIFY(comments->hasUnsavedChanges());
    QCOMPARE(comments->getComments().m_comments.first().m_text,
             QStringLiteral("Immediate outgoing comment"));
    QCOMPARE(f->order(), QStringList({a.id()}));
    QCOMPARE(f->coreCurrent(), a.id());
    QCOMPARE(f->buffers->listBuffers().size(), 1);
    QVERIFY(QDir().rmdir(location.m_storePath));
    comments->flushPendingSave();
    QTRY_VERIFY_WITH_TIMEOUT(!comments->hasUnsavedChanges(), 10000);
  } else {
    QCOMPARE(window->getBuffer().id(), b.id());
    QCOMPARE(comments->getActiveFile(), b.nodeId());
    QVERIFY(comments->getComments().m_comments.isEmpty());
    QCOMPARE(f->order(), QStringList({b.id()}));
    QCOMPARE(f->coreCurrent(), b.id());
    QCOMPARE(f->comments->load(b.nodeId()).m_status, CommentService::LoadResult::Status::Missing);
  }
  const auto saved = f->comments->load(a.nodeId());
  QCOMPARE(saved.m_status, CommentService::LoadResult::Status::Loaded);
  QCOMPARE(saved.m_comments.m_comments.size(), 1);
  QCOMPARE(saved.m_comments.m_comments.first().m_text,
           QStringLiteral("Immediate outgoing comment"));
}
} // namespace tests

int main(int argc, char **argv) {
  // Keep encrypted transaction paths short on Windows and all config/cache/session data
  // beneath one disposable root. This executable never instantiates Application/startup.
  QTemporaryDir storage(QDir::tempPath() + QStringLiteral("/vr-XXXXXX"));
  if (!storage.isValid())
    return 1;
  const auto root = storage.path().toLocal8Bit();
  qputenv("TMP", root);
  qputenv("TEMP", root);
  qputenv("TMPDIR", root);
  vxcore_set_test_mode(1);
  QCoreApplication::setAttribute(Qt::AA_ShareOpenGLContexts);
  WebEngineProfileService::registerProtectedScheme();
  VxPdfSchemeHandler::registerScheme();
  QApplication app(argc, argv);
  app.setQuitOnLastWindowClosed(false);
  tests::TestViewAreaTabReuse test;
  return QTest::qExec(&test, argc, argv);
}

#include "test_viewarea_tab_reuse.moc"
