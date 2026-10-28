#include <QtTest>

#include <vtextedit/typstruntime.h>

#include <QAbstractScrollArea>
#include <QAction>
#include <QApplication>
#include <QBuffer>
#include <QClipboard>
#include <QContextMenuEvent>
#include <QDesktopServices>
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
#include <QMenu>
#include <QMessageBox>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QPdfWriter>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPushButton>
#include <QScopeGuard>
#include <QScrollBar>
#include <QTabBar>
#include <QTemporaryDir>
#include <QTextBlock>
#include <QTextCursor>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QWebEnginePage>
#include <QWebEngineProfile>
#include <QWheelEvent>
#include <QWindow>
#include <QtConcurrent/QtConcurrentRun>

#include <controllers/commentcontroller.h>
#include <controllers/markdownrendercontroller.h>
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
#include <core/sessionconfig.h>
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
#include <vtextedit/markdownhighlighter.h>
#include <vtextedit/previewdata.h>
#include <vtextedit/typsteditorconfig.h>
#include <vtextedit/vtextedit.h>
#include <vtextedit/vtypsteditor.h>
#include <vxcore/vxcore.h>
#include <widgets/attachmentpopup2.h>
#include <widgets/detachedwindow.h>
#include <widgets/dialogs/imageinsertdialog.h>
#include <widgets/editors/markdowneditor.h>
#include <widgets/editors/markdownviewer.h>
#include <widgets/editors/markdownvieweradapter.h>
#include <widgets/editors/markdownwebjobhost.h>
#include <widgets/editors/mindmapeditor.h>
#include <widgets/editors/pdfviewer.h>
#include <widgets/editors/pdfvieweradapter.h>
#include <widgets/editors/texteditor.h>
#include <widgets/editreaddiscardaction.h>
#include <widgets/imageviewer.h>
#include <widgets/linkpreviewoverlay.h>
#include <widgets/outlineprovider.h>
#include <widgets/viewarea2.h>
#include <widgets/viewwindow2.h>

#include <functional>
#include <memory>

using namespace vnotex;

namespace tests {
class PreviewDesktopSink : public QObject {
  Q_OBJECT
public:
  QList<QUrl> opened;
public slots:
  void open(const QUrl &p_url) { opened.append(p_url); }
};

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
  const QPointer<MarkdownViewer> viewer(p_viewer);
  if (!viewer || !viewer->adapter()->isDocumentReady())
    return false;
  const auto generation = viewer->adapter()->documentGeneration();
  const auto text = evaluate(viewer->page(), QStringLiteral("document.body.innerText")).toString();
  return viewer && viewer->adapter()->isDocumentReady() &&
         viewer->adapter()->documentGeneration() == generation && text.contains(p_text);
}

bool waitForRender(MarkdownViewer *p_viewer, const QString &p_text) {
  const QPointer<MarkdownViewer> viewer(p_viewer);
  return QTest::qWaitFor([&] { return rendered(viewer, p_text); }, 30000);
}

bool waitForCurrentRender(MarkdownViewer *p_viewer, MarkdownRenderController *p_native,
                          const QString &p_text) {
  const QPointer<MarkdownViewer> viewer(p_viewer);
  const QPointer<MarkdownRenderController> native(p_native);
  // rendered() pumps events. QTRY's repeated post-success evaluations can observe
  // a newer in-flight generation and report a false timeout for this predicate.
  return QTest::qWaitFor(
      [&]() {
        return viewer && native && rendered(viewer, p_text) &&
               native->isCurrent(viewer->adapter()->documentGeneration());
      },
      30000);
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
  explicit DialogAnswer(QMessageBox::StandardButton p_button,
                        std::function<void()> p_beforeAnswer = {}) {
    elapsed.start();
    timer.setInterval(20);
    QObject::connect(
        &timer, &QTimer::timeout, &timer,
        [this, p_button, beforeAnswer = std::move(p_beforeAnswer)] {
          for (auto *widget : QApplication::topLevelWidgets()) {
            auto *box = qobject_cast<QMessageBox *>(widget);
            if (box && box->isVisible() && !answered.contains(box)) {
              if (auto *button = box->button(p_button)) {
                answered.append(box);
                ++answerCount;
                if (answerCount == 1 && beforeAnswer)
                  beforeAnswer();
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
    return answerCount == 1 && !timedOut;
  }
  int count() const { return answerCount; }

private:
  QTimer timer;
  QElapsedTimer elapsed;
  QVector<QPointer<QMessageBox>> answered;
  int answerCount = 0;
  bool timedOut = false;
};

// Drive the menu's nested event loop, including the reader's asynchronous hit test.
// This never substitutes a signal emission for the actual Open Link action.
bool openContextLinkAt(QWidget *p_surface, const QPoint &p_point,
                       const std::function<void()> &p_beforeTrigger = {}) {
  bool selected = false;
  bool finished = false;
  QElapsedTimer elapsed;
  elapsed.start();
  QTimer timer;
  timer.setInterval(10);
  QObject::connect(&timer, &QTimer::timeout, &timer, [&] {
    for (auto *widget : QApplication::topLevelWidgets()) {
      auto *menu = qobject_cast<QMenu *>(widget);
      if (!menu || !menu->isVisible())
        continue;
      for (auto *action : menu->actions()) {
        const bool isOpenLink =
            qobject_cast<MarkdownViewer *>(p_surface)
                ? action->objectName() == QLatin1String("openContextLinkAction")
                : action->text().remove(QLatin1Char('&')) == QLatin1String("Open Link");
        if (!isOpenLink || !action->isEnabled())
          continue;
        timer.stop();
        selected = true;
        const QPointer<QMenu> guardedMenu(menu);
        const QPointer<QAction> guardedAction(action);
        if (p_beforeTrigger)
          p_beforeTrigger();
        if (guardedMenu && guardedMenu->isVisible() && guardedAction)
          QTest::mouseClick(guardedMenu, Qt::LeftButton, Qt::NoModifier,
                            guardedMenu->actionGeometry(guardedAction).center());
        finished = true;
        return;
      }
      if (elapsed.elapsed() >= 5000)
        menu->close();
    }
    if (elapsed.elapsed() >= 5000) {
      timer.stop();
      finished = true;
    }
  });
  timer.start();
  if (qobject_cast<MarkdownViewer *>(p_surface)) {
    // Chromium publishes lastContextMenuRequest only after handling its own
    // right-button gesture; a synthetic event on QWebEngineView bypasses it.
    const auto global = p_surface->mapToGlobal(p_point);
    auto *receiver = QApplication::widgetAt(global);
    if (!receiver || (receiver != p_surface && !p_surface->isAncestorOf(receiver)))
      return false;
    QTest::mouseClick(receiver, Qt::RightButton, Qt::NoModifier, receiver->mapFromGlobal(global));
    if (!QTest::qWaitFor([&] { return finished; }, 6000))
      return false;
  } else {
    QContextMenuEvent event(QContextMenuEvent::Mouse, p_point, p_surface->mapToGlobal(p_point));
    QCoreApplication::sendEvent(p_surface, &event);
  }
  return selected;
}

QPoint readerLinkPoint(MarkdownViewer *p_viewer, const QString &p_text) {
  const auto literal =
      QString::fromUtf8(QJsonDocument(QJsonArray{p_text}).toJson(QJsonDocument::Compact));
  const auto point = evaluate(p_viewer->page(), QStringLiteral(R"JS((() => {
    const link = Array.from(document.querySelectorAll('#vx-content a'))
      .find(a => a.textContent === %1[0]);
    if (!link) return null;
    const r = link.getBoundingClientRect();
    return r.width > 0 && r.height > 0 ? [r.x + r.width / 2, r.y + r.height / 2] : null;
  })())JS")
                                                    .arg(literal))
                         .toList();
  return point.size() == 2 ? QPoint(qRound(point[0].toDouble() * p_viewer->zoomFactor()),
                                    qRound(point[1].toDouble() * p_viewer->zoomFactor()))
                           : QPoint(-1, -1);
}

// Send through the top-level QWindow instead of directly delivering a click to a
// preselected child. Both Qt hit testing and the late WebEngine native child are live.
bool clickNativeSurface(QWidget *p_surface, const QPoint &p_point,
                        Qt::KeyboardModifiers p_modifiers = Qt::NoModifier) {
  if (!p_surface || !p_surface->rect().contains(p_point))
    return false;
  auto *window = p_surface->window()->windowHandle();
  if (!window)
    return false;
  const auto global = p_surface->mapToGlobal(p_point);
  auto *hit = QApplication::widgetAt(global);
  if (hit != p_surface && (!hit || !p_surface->isAncestorOf(hit)))
    return false;
  QTest::mouseClick(window, Qt::LeftButton, p_modifiers, window->mapFromGlobal(global));
  return true;
}

QLabel *linkHint(QWidget *p_surface) {
  for (auto *label : p_surface->findChildren<QLabel *>()) {
    const auto text = label->text();
    if (label->isVisible() && text.size() == 2 && text[0] == QLatin1Char('v') &&
        label->testAttribute(Qt::WA_TransparentForMouseEvents))
      return label;
  }
  return nullptr;
}

bool activateLinkHint(ViewArea2 *p_area, QWidget *p_surface, QWidget *p_covered = nullptr) {
  p_area->registerNavigation(QLatin1Char('v'));
  p_area->showNavigation();
  const auto hide = qScopeGuard([p_area] { p_area->hideNavigation(); });
  if (!QTest::qWaitFor([&] { return linkHint(p_surface); }, 10000) ||
      (p_covered && linkHint(p_covered)))
    return false;
  const auto key = linkHint(p_surface)->text().back().toUpper().unicode();
  return p_area->handleKeyNavigation(Qt::Key_V).m_isKeyConsumed &&
         p_area->handleKeyNavigation(key).m_isKeyConsumed;
}

QPoint coloredSurfacePoint(QWidget *p_surface, const QColor &p_color) {
  const auto image = p_surface->grab().toImage();
  QRect bounds;
  for (int y = 0; y < image.height(); ++y) {
    for (int x = 0; x < image.width(); ++x) {
      const auto pixel = image.pixelColor(x, y);
      // Antialiased glyphs need not contain a fully covered source-color pixel.
      if (qAbs(pixel.red() - p_color.red()) < 80 && qAbs(pixel.green() - p_color.green()) < 80 &&
          qAbs(pixel.blue() - p_color.blue()) < 80)
        bounds |= QRect(x, y, 1, 1);
    }
  }
  return bounds.isEmpty() ? QPoint(-1, -1)
                          : (QPointF(bounds.center()) / image.devicePixelRatio()).toPoint();
}

struct Fixture {
  VxCoreContextHandle context = nullptr;
  vte::TypstRuntime runtime;
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
    services.registerService(&runtime);
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
    QApplication::setActiveWindow(area.get());
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
      // A force-closed source still asks its preview for consent. Retire preview
      // tabs directly first so failed assertions cannot hang fixture cleanup.
      for (const auto &value : workspaces->listWorkspaces()) {
        const auto workspace = value.toObject();
        if (!workspace.value(QStringLiteral("isPreview")).toBool())
          continue;
        for (const auto id :
             area->getViewWindowIdsForWorkspace(workspace.value(QStringLiteral("id")).toString()))
          area->getController()->closeViewWindow(id, true);
      }
      QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
      area->getController()->closeAll(area->getVisibleWorkspaceIds().toVector(), true);
      area.reset();
      QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    }
    runtime.shutdown();
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
  Buffer2 preview(ViewWindow2 *p_source, const NodeIdentifier &p_node) {
    auto settings = p_source->getLinkOpenSettings();
    settings.m_mode = ViewWindowMode::Edit;
    return buffers->openBuffer(p_node, settings);
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
  void previewHostRoutingAndGeometry();
  void previewEditingCommandTargetAndVisibility();
  void previewCloseConsent();
  void previewOpenInNewTabs_data();
  void previewOpenInNewTabs();
  void previewOpenInNewTabsDuplicateConsent_data();
  void previewOpenInNewTabsDuplicateConsent();
  void previewOpenInNewTabsProtectedDuplicate();
  void previewOpenInNewTabsReentrancy();
  void previewOutsideClickDismissal();
  void previewSourceCancellation();
  void previewSharedOwnership_data();
  void previewSharedOwnership();
  void previewCloseReentrancyAndPartialCancellation();
  void previewProtectedAcceptedCloseThenSourceReplacement();
  void previewConversionRecollectsRetiringViews();
  void previewQuitCancellationBeforeSession();
  void previewConversionAndLockRefusal_data();
  void previewConversionAndLockRefusal();
  void previewHiddenCrossNotebookCloseCancellation();
  void previewHomeOnlyMainRetainsHome();
  void previewUnavailableDestination();
  void previewCreationReentrancy();
  void previewMarkdownActivationOrigins();
  void markdownReaderContextMenus();
  void previewReaderNavigationAndNativeHost();
  void previewProtectedLinksAndDispatchFailures();
  void previewMindMapAndTypstActivation();
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
  void explicitDetachedWorkspace();
  void explicitMainWorkspace();
  void unavailableTargetWorkspace_data();
  void unavailableTargetWorkspace();
  void targetWorkspaceWithReentrantOpen();
  void protectedResourcesAndLateWork();
  void pdfCommentDurability_data();
  void pdfCommentDurability();

private:
  std::unique_ptr<QTemporaryDir> directory;
  std::unique_ptr<Fixture> f;
  QString appDataPath;
  QString userConfigPath;
};

void TestViewAreaTabReuse::init() {
  vxcore_set_test_mode(1);
  if (appDataPath.isEmpty()) {
    vxcore_clear_test_directory();
  } else {
    // Only bundled assets survive rows. Contexts, config, sessions and profiles do not.
    // This suite must not edit installed assets (including user.css and templates).
    QVERIFY(!userConfigPath.isEmpty());
    QVERIFY(QDir(userConfigPath).removeRecursively());
    static const QStringList bundledFolders{
        QStringLiteral("themes"), QStringLiteral("tasks"), QStringLiteral("syntax-highlighting"),
        QStringLiteral("web"),    QStringLiteral("dicts"), QStringLiteral("templates"),
        QStringLiteral("docs")};
    const QDir appDirectory(appDataPath);
    for (const auto &entry : appDirectory.entryInfoList(QDir::AllEntries | QDir::Hidden |
                                                        QDir::System | QDir::NoDotAndDotDot)) {
      if (!entry.isSymLink() && entry.isDir() && bundledFolders.contains(entry.fileName()))
        continue;
      const bool removed = !entry.isSymLink() && entry.isDir()
                               ? QDir(entry.filePath()).removeRecursively()
                               : QFile::remove(entry.filePath());
      QVERIFY2(removed, qPrintable(entry.filePath()));
    }
  }
  directory = std::make_unique<QTemporaryDir>(QDir::tempPath() + QStringLiteral("/n-XXXXXX"));
  QVERIFY(directory->isValid());
  f = std::make_unique<Fixture>(directory->path());
  QVERIFY2(f->initialize(), qPrintable(f->error));
  appDataPath = f->config->getAppDataPath();
  userConfigPath = f->config->getUserConfigPath();
  QVERIFY(!appDataPath.isEmpty() && !userConfigPath.isEmpty());
}

void TestViewAreaTabReuse::cleanup() {
  f.reset();
  directory.reset();
}

void TestViewAreaTabReuse::previewMarkdownActivationOrigins() {
  const QByteArray sourceBytes("# Source A\n\nSelect this text, not the link.\n\n[B](b.md#beta)\n");
  QByteArray targetBytes("# Before the destination\n\n");
  for (int line = 0; line < 35; ++line)
    targetBytes += "Before beta paragraph.\n\n";
  targetBytes += "# Beta\n\nDestination reached.\n\n";
  for (int line = 0; line < 35; ++line)
    targetBytes += "After beta paragraph.\n\n";
  QVERIFY(f->create(QStringLiteral("a.md"), sourceBytes));
  QVERIFY(f->create(QStringLiteral("b.md"), targetBytes));
  const auto a = f->open(QStringLiteral("a.md"));
  QVERIFY(a.isValid());
  auto *source = f->current();
  const auto sourceId = source->getViewWindowId();
  const auto main = f->workspace();
  auto *editor = source->findChild<MarkdownEditor *>();
  auto *reader = source->findChild<MarkdownViewer *>();
  QVERIFY(editor && reader);
  auto *edit = editor->getTextEdit();
  QVERIFY(waitForRender(reader, QStringLiteral("Source A")));
  QTRY_COMPARE(editor->getNavigationTargets().size(), 1);
  f->reuse();
  const auto viewportSize = edit->viewport()->size();
  const auto readerSize = reader->size();

  // A real selection drag ends on the link, but must remain ordinary text editing.
  const auto linkPoint = editor->getNavigationTargets().first().m_rect.center();
  QTextCursor start(edit->document());
  start.setPosition(QString::fromUtf8(sourceBytes).indexOf(QStringLiteral("Select")));
  const auto dragStart = edit->cursorRect(start).center();
  QTest::mousePress(edit->viewport(), Qt::LeftButton, Qt::ControlModifier, dragStart);
  QMouseEvent move(QEvent::MouseMove, linkPoint, edit->viewport()->mapToGlobal(linkPoint),
                   Qt::NoButton, Qt::LeftButton, Qt::ControlModifier);
  QApplication::sendEvent(edit->viewport(), &move);
  QTest::mouseRelease(edit->viewport(), Qt::LeftButton, Qt::ControlModifier, linkPoint);
  QVERIFY(edit->textCursor().hasSelection());
  QVERIFY(f->area->previewWorkspaceForWindow(sourceId).isEmpty());
  QCOMPARE(f->buffers->listBuffers().size(), 1);
  auto cursor = edit->textCursor();
  cursor.clearSelection();
  edit->setTextCursor(cursor);

  for (const auto modifiers : {Qt::NoModifier, Qt::ControlModifier}) {
    QVERIFY(clickNativeSurface(edit->viewport(), linkPoint, modifiers));
    if (modifiers == Qt::NoModifier) {
      QCOMPARE(f->current(), source);
      QVERIFY(f->area->previewWorkspaceForWindow(sourceId).isEmpty());
      QCOMPARE(f->buffers->listBuffers().size(), 1);
      QCOMPARE(edit->textCursor().position(), edit->cursorForPosition(linkPoint).position());
      QVERIFY(!edit->textCursor().hasSelection());
      QCOMPARE(editor->getText(), QString::fromUtf8(sourceBytes));
      continue;
    }
    QTRY_VERIFY(!f->area->previewWorkspaceForWindow(sourceId).isEmpty());
    const auto preview = f->area->previewWorkspaceForWindow(sourceId);
    auto *target = f->current();
    QCOMPARE(target->getNodeId(), f->node(QStringLiteral("b.md")));
    QCOMPARE(f->order(preview), QStringList({target->getBuffer().id()}));
    QCOMPARE(source->getViewWindowId(), sourceId);
    QCOMPARE(source->getMode(), ViewWindowMode::Edit);
    QCOMPARE(editor->getText(), QString::fromUtf8(sourceBytes));
    QVERIFY(!edit->textCursor().hasSelection());
    QCOMPARE(edit->textCursor().position(), edit->cursorForPosition(linkPoint).position());
    QCOMPARE(edit->viewport()->size(), viewportSize);
    QCOMPARE(reader->size(), readerSize);
    QCOMPARE(f->coreCurrent(main), a.id());
    QCOMPARE(f->workspaces->getCurrentWorkspace(), main);
    QCOMPARE(f->order(main), QStringList({a.id()}));
    auto *targetReader = target->findChild<MarkdownViewer *>();
    QVERIFY(targetReader);
    QVERIFY(waitForRender(targetReader, QStringLiteral("Destination reached")));
    QVERIFY(QTest::qWaitFor(
        [&] {
          return evaluate(targetReader->page(), QStringLiteral(R"JS((() => {
        const heading = document.getElementById('beta');
        if (!heading) return false;
        const rect = heading.getBoundingClientRect();
        return rect.top >= -2 && rect.top < innerHeight / 2 && scrollY > 0;
      })())JS"))
              .toBool();
        },
        10000));
    QPointer<LinkPreviewOverlay> overlay(source->findChild<LinkPreviewOverlay *>());
    QVERIFY(f->controller()->closePreviewWorkspace(preview));
    QTRY_VERIFY(overlay.isNull());
    QCOMPARE(f->current(), source);
  }

  QVERIFY(activateLinkHint(f->area.get(), edit->viewport()));
  QTRY_VERIFY(!f->area->previewWorkspaceForWindow(sourceId).isEmpty());
  QCOMPARE(f->current()->getNodeId(), f->node(QStringLiteral("b.md")));
  QVERIFY(f->controller()->closeAllPreviewWorkspaces());
  QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);

  // Menu origin is independent of preference and of source/reader presentation.
  f->config->getCoreConfig().setAlwaysOpenNotesInNewTabs(true);
  for (bool enabled : {true, false}) {
    f->config->getEditorConfig().setLinkPreviewEnabled(enabled);
    for (bool reading : {false, true}) {
      source->setMode(reading ? ViewWindowMode::Read : ViewWindowMode::Edit);
      QWidget *surface = edit->viewport();
      QPoint point;
      if (reading) {
        QVERIFY(waitForRender(reader, QStringLiteral("Source A")));
        point = readerLinkPoint(reader, QStringLiteral("B"));
        surface = reader;
      } else {
        QTRY_COMPARE(editor->getNavigationTargets().size(), 1);
        point = editor->getNavigationTargets().first().m_rect.center();
      }
      QVERIFY(surface->rect().contains(point));
      QVERIFY(openContextLinkAt(surface, point));
      QTRY_VERIFY(f->current() != source);
      QCOMPARE(f->current()->getNodeId(), f->node(QStringLiteral("b.md")));
      QCOMPARE(f->workspace(), main);
      QCOMPARE(f->order(main).size(), 2);
      QVERIFY(f->area->previewWorkspaceForWindow(sourceId).isEmpty());
      QVERIFY(f->controller()->closeViewWindow(f->current()->getViewWindowId(), false));
      QCOMPARE(f->current(), source);
      if (!enabled) {
        if (!reading) {
          QVERIFY(clickNativeSurface(surface, point));
          QCOMPARE(f->current(), source);
          QCOMPARE(f->buffers->listBuffers().size(), 1);
          QVERIFY(f->area->previewWorkspaceForWindow(sourceId).isEmpty());
        }
        QVERIFY(clickNativeSurface(surface, point,
                                   reading ? Qt::NoModifier : Qt::ControlModifier));
        QTRY_VERIFY(f->current() != source);
        QCOMPARE(f->current()->getNodeId(), f->node(QStringLiteral("b.md")));
        QCOMPARE(f->workspace(), main);
        QVERIFY(f->area->previewWorkspaceForWindow(sourceId).isEmpty());
        QVERIFY(f->controller()->closeViewWindow(f->current()->getViewWindowId(), false));
      }
    }
  }
  QCOMPARE(source->getLatestContent(), QString::fromUtf8(sourceBytes));
  QCOMPARE(readBytes(f->absolute(QStringLiteral("a.md"))), sourceBytes);
}

void TestViewAreaTabReuse::markdownReaderContextMenus() {
  QVERIFY(f->create(QStringLiteral("menu.png"), imageBytes(Qt::red)));
  QVERIFY(f->create(QStringLiteral("menus.md"),
                    "# Menu source\n\nPlain text\n\n[Local](target.md)\n\n"
                    "[External](https://example.com/menu)\n\n"
                    "![Image](menu.png)\n\n[![Linked image](menu.png)](target.md)\n"));
  QVERIFY(f->open(QStringLiteral("menus.md")).isValid());
  f->current()->setMode(ViewWindowMode::Read);
  auto *reader = f->current()->findChild<MarkdownViewer *>();
  QVERIFY(reader && waitForRender(reader, QStringLiteral("Menu source")));
  QVERIFY(QTest::qWaitFor(
      [&] {
        return evaluate(reader->page(),
                        QStringLiteral("Array.from(document.querySelectorAll('#vx-content img'))"
                                       ".every(img => img.complete && img.naturalWidth > 0)"))
            .toBool();
      },
      10000));

  struct Context {
    const char *selector;
    bool link;
    bool image;
    bool selection;
  };
  const Context contexts[] = {{"#vx-content p", false, false, false},
                              {"#vx-content p a", true, false, false},
                              {"#vx-content a[href^='https:']", true, false, false},
                              {"#vx-content p a", true, false, true},
                              {"#vx-content img", false, true, false},
                              {"#vx-content a img", true, true, false},
                              {"#vx-content p", false, false, false}};
  for (const auto &context : contexts) {
    const auto selector =
        QString::fromUtf8(QJsonDocument(QJsonArray{QString::fromUtf8(context.selector)})
                              .toJson(QJsonDocument::Compact));
    const auto point =
        evaluate(reader->page(), QStringLiteral(R"JS((() => {
      const element = document.querySelector(%1[0]);
      window.getSelection().removeAllRanges();
      if (%2) {
        const range = document.createRange();
        range.selectNodeContents(element);
        window.getSelection().addRange(range);
      }
      element.scrollIntoView({block: 'center'});
      const rect = element.getBoundingClientRect();
      return [rect.x + Math.min(10, rect.width / 2), rect.y + rect.height / 2];
    })())JS")
                                     .arg(selector, context.selection ? QStringLiteral("true")
                                                                      : QStringLiteral("false")))
            .toList();
    QCOMPARE(point.size(), 2);
    if (context.selection)
      QTRY_VERIFY(reader->hasSelection());
    const QPoint local(qRound(point[0].toDouble() * reader->zoomFactor()),
                       qRound(point[1].toDouble() * reader->zoomFactor()));
    const auto global = reader->mapToGlobal(local);
    auto *receiver = QApplication::widgetAt(global);
    QVERIFY(receiver && (receiver == reader || reader->isAncestorOf(receiver)));
    QTest::mouseClick(receiver, Qt::RightButton, Qt::NoModifier, receiver->mapFromGlobal(global));
    QPointer<QMenu> menu;
    QVERIFY(QTest::qWaitFor(
        [&] {
          for (auto *widget : QApplication::topLevelWidgets()) {
            auto *candidate = qobject_cast<QMenu *>(widget);
            if (candidate && candidate->isVisible()) {
              menu = candidate;
              return true;
            }
          }
          return false;
        },
        5000));
    const auto closeMenu = qScopeGuard([&] {
      if (menu)
        menu->close();
    });
    QList<QAction *> visible;
    bool edit = false;
    bool exportAction = false;
    QAction *openLink = nullptr;
    for (auto *action : menu->actions()) {
      if (!action->isVisible() || action->isSeparator() || action->text().isEmpty())
        continue;
      visible.append(action);
      const auto text = action->text().remove(QLatin1Char('&')).section(QLatin1Char('\t'), 0, 0);
      edit |= text == QLatin1String("Edit");
      exportAction |= text == QLatin1String("Export");
      if (action->objectName() == QLatin1String("openContextLinkAction"))
        openLink = action;
    }
    auto *copyLink = reader->pageAction(QWebEnginePage::CopyLinkToClipboard);
    QCOMPARE(bool(openLink), context.link);
    QCOMPARE(visible.contains(copyLink), context.link);
    QCOMPARE(edit, !context.link && !context.image);
    QCOMPARE(exportAction, !context.link && !context.image);
    if (context.link) {
      QCOMPARE(visible, (QList<QAction *>{openLink, copyLink}));
      QTRY_VERIFY(openLink->isEnabled());
    } else {
      QVERIFY(visible.contains(reader->pageAction(QWebEnginePage::SelectAll)));
    }
  }
}

void TestViewAreaTabReuse::previewReaderNavigationAndNativeHost() {
  const QByteArray aBytes("# Source reader\n\n[B](b.md)\n\n[COVERED](covered.md)\n");
  QByteArray bBytes("# Preview B\n\n[C](c.md)\n\n");
  for (int line = 0; line < 80; ++line)
    bBytes += "Scroll only the editable preview.\n\n";
  QVERIFY(f->create(QStringLiteral("a.md"), aBytes));
  QVERIFY(f->create(QStringLiteral("b.md"), bBytes));
  QVERIFY(f->create(QStringLiteral("c.md"), "# Preview C\n\n[B](b.md)\n"));
  QVERIFY(f->create(QStringLiteral("covered.md"), "# Must not open\n"));
  const auto a = f->open(QStringLiteral("a.md"));
  QVERIFY(a.isValid());
  auto *source = f->current();
  source->setMode(ViewWindowMode::Read);
  const auto main = f->workspace();
  auto *sourceReader = source->findChild<MarkdownViewer *>();
  QVERIFY(sourceReader && waitForRender(sourceReader, QStringLiteral("Source reader")));
  const auto originalReaderSize = sourceReader->size();
  f->reuse();
  QVERIFY(clickNativeSurface(sourceReader, readerLinkPoint(sourceReader, QStringLiteral("B"))));
  QTRY_VERIFY(f->current() != source);
  const auto preview = f->workspace();
  QPointer<LinkPreviewOverlay> overlay(source->findChild<LinkPreviewOverlay *>());
  QVERIFY(overlay && overlay->isVisible());
  auto *split = overlay->getViewSplit();
  QPointer<ViewWindow2> bWindow(f->current());
  const auto b = bWindow->getBuffer();
  auto *bReader = bWindow->findChild<MarkdownViewer *>();
  QVERIFY(bReader && waitForRender(bReader, QStringLiteral("Preview B")));
  QCOMPARE(sourceReader->size(), originalReaderSize);
  QVERIFY(!split->tabBar()->isVisible());

  // Keyboard hints belong to the overlay's reader; covered source links have no hints.
  QVERIFY(activateLinkHint(f->area.get(), bReader, sourceReader));
  QTRY_COMPARE(f->current()->getNodeId(), f->node(QStringLiteral("c.md")));
  const auto c = f->current()->getBuffer();
  QCOMPARE(f->workspace(), preview);
  QCOMPARE(f->order(preview), QStringList({b.id(), c.id()}));
  QVERIFY(split->tabBar()->isVisible());
  auto *cReader = f->current()->findChild<MarkdownViewer *>();
  QVERIFY(cReader && waitForRender(cReader, QStringLiteral("Preview C")));
  QVERIFY(clickNativeSurface(cReader, readerLinkPoint(cReader, QStringLiteral("B"))));
  QTRY_COMPARE(f->current(), bWindow.data());
  QCOMPARE(f->order(preview), QStringList({b.id(), c.id()}));
  QVERIFY(clickNativeSurface(bReader, readerLinkPoint(bReader, QStringLiteral("C"))));
  QTRY_COMPARE(f->current()->getBuffer().id(), c.id());
  QCOMPARE(f->order(preview), QStringList({b.id(), c.id()}));
  QVERIFY(f->controller()->closeViewWindow(f->current()->getViewWindowId(), false));
  QCOMPARE(f->current(), bWindow.data());
  QVERIFY(!split->tabBar()->isVisible());

  // Scroll a native WebEngine child through the actual hit-tested receiver.
  const auto sourceScroll = evaluate(sourceReader->page(), QStringLiteral("scrollY")).toInt();
  const auto previewScroll = evaluate(bReader->page(), QStringLiteral("scrollY")).toInt();
  const auto global = bReader->mapToGlobal(bReader->rect().center());
  auto *receiver = QApplication::widgetAt(global);
  QVERIFY(receiver && (receiver == bReader || bReader->isAncestorOf(receiver)));
  QWheelEvent wheel(receiver->mapFromGlobal(global), global, QPoint(), QPoint(0, -360),
                    Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
  QApplication::sendEvent(receiver, &wheel);
  QVERIFY(QTest::qWaitFor(
      [&] { return evaluate(bReader->page(), QStringLiteral("scrollY")).toInt() > previewScroll; },
      10000));
  QCOMPARE(evaluate(sourceReader->page(), QStringLiteral("scrollY")).toInt(), sourceScroll);

  f->config->getEditorConfig().getMarkdownEditorConfig().setEditViewMode(
      MarkdownEditorConfig::EditOnly);
  bWindow->setMode(ViewWindowMode::Edit);
  auto *editor = bWindow->findChild<MarkdownEditor *>();
  QVERIFY(editor);
  auto *edit = editor->getTextEdit();
  QVERIFY(clickNativeSurface(edit->viewport(), edit->viewport()->rect().center()));
  QTRY_VERIFY(edit->hasFocus());
  QTest::keyClick(edit, Qt::Key_End, Qt::ControlModifier);
  QTest::keyClicks(edit, "Native overlay typing");
  QVERIFY(editor->getText().contains(QStringLiteral("Native overlay typing")));
  QVERIFY(bWindow->save());
  QTRY_VERIFY(readBytes(f->absolute(QStringLiteral("b.md"))).contains("Native overlay typing"));
  QCOMPARE(readBytes(f->absolute(QStringLiteral("a.md"))), aBytes);
  QCOMPARE(sourceReader->size(), originalReaderSize);
  QCOMPARE(f->coreCurrent(main), a.id());
  QCOMPARE(f->order(main), QStringList({a.id()}));

  // Finish syntax layout before capturing geometry: late rehighlighting changes
  // QTextDocument revision/line heights and correctly invalidates stale hints.
  QSignalSpy highlighted(editor->getHighlighter(), &vte::MarkdownHighlighter::highlightCompleted);
  editor->getHighlighter()->updateHighlight();
  QVERIFY(highlighted.count() > 0 || highlighted.wait(10000));
  // Source-mode hints in an overlay must also exclude the loaded reader underneath.
  edit->moveCursor(QTextCursor::Start);
  edit->ensureCursorVisible();
  QTRY_COMPARE(editor->getNavigationTargets().size(), 1);
  QVERIFY(activateLinkHint(f->area.get(), edit->viewport(), sourceReader));
  QTRY_COMPARE(f->current()->getNodeId(), f->node(QStringLiteral("c.md")));
  QVERIFY(f->controller()->closeViewWindow(f->current()->getViewWindowId(), false));
  QCOMPARE(f->current(), bWindow.data());

  const auto document = edit->document();
  const auto draft = editor->getText();
  f->area->resize(900, 650);
  QTRY_VERIFY(source->rect().contains(overlay->geometry()));
  const auto resizedReaderSize = sourceReader->size();
  f->theme->refreshCurrentTheme();
  QTRY_VERIFY(overlay->isVisible());
  QCOMPARE(edit->document(), document);
  QCOMPARE(editor->getText(), draft);
  source->hide();
  QTRY_VERIFY(!overlay->isVisible());
  source->show();
  QTRY_VERIFY(overlay->isVisible());
  QCOMPARE(editor->getText(), draft);
  QAction *presentation = nullptr;
  for (auto *action : source->findChildren<QAction *>()) {
    if (action->text().section(QLatin1Char('\t'), 0, 0).remove(QLatin1Char('&')) !=
        QLatin1String("Presentation Mode")) {
      continue;
    }
    auto *owner = action->parent();
    while (owner && !qobject_cast<ViewWindow2 *>(owner)) {
      owner = owner->parent();
    }
    if (owner == source) {
      presentation = action;
      break;
    }
  }
  QVERIFY(presentation);
  presentation->trigger();
  QTRY_VERIFY_WITH_TIMEOUT(source->window()->isFullScreen() && overlay->isVisible(), 30000);
  QVERIFY(source->rect().contains(overlay->geometry()));
  QVERIFY(clickNativeSurface(edit->viewport(), edit->viewport()->rect().center()));
  presentation->trigger();
  QTRY_VERIFY(!source->window()->isFullScreen());
  QTRY_COMPARE(sourceReader->size(), resizedReaderSize);
  QVERIFY(f->controller()->detachViewWindow(main, source->getViewWindowId(), a.id()));
  auto *detached = qobject_cast<DetachedWindow *>(source->window());
  QVERIFY(detached);
  detached->show();
  detached->activateWindow();
  QTRY_VERIFY(detached->isActiveWindow() && overlay->isVisible());
  // Detachment resizes the source; its overlay re-anchors on the next event-loop turn.
  QTRY_VERIFY(source->contentsRect().contains(overlay->geometry()));
  QCOMPARE(overlay->parentWidget(), source);
  QCOMPARE(overlay->getViewSplit(), split);
  QCOMPARE(f->area->previewWorkspaceForWindow(source->getViewWindowId()), preview);
  QCOMPARE(edit->document(), document);
  QCOMPARE(editor->getText(), draft);
  QVERIFY(clickNativeSurface(edit->viewport(), edit->viewport()->rect().center()));
  QTRY_VERIFY(edit->hasFocus());
  QCOMPARE(f->workspace(), preview);
  sourceReader->setFocus(Qt::OtherFocusReason);
  QTRY_VERIFY(sourceReader->hasFocus() || sourceReader->isAncestorOf(QApplication::focusWidget()));
  QCOMPARE(f->workspace(), main);
  QVERIFY(f->controller()->getCurrentWindowId() != bWindow->getViewWindowId());
  edit->setFocus(Qt::OtherFocusReason);
  QTRY_COMPARE(f->current(), bWindow.data());
  const auto detachedReaderSize = sourceReader->size();
  QVERIFY(f->controller()->closePreviewWorkspace(preview));
  QTRY_VERIFY(overlay.isNull());
  QCOMPARE(sourceReader->size(), detachedReaderSize);
  QCOMPARE(source->getBuffer().id(), a.id());
}

void TestViewAreaTabReuse::previewProtectedLinksAndDispatchFailures() {
  QVERIFY(
      f->create(QStringLiteral("a.md"),
                "# Dispatch source\n\n[Missing](missing.md)\n\n"
                "[External](external.txt)\n\n[Unsupported](unsupported.png)\n\n[B](b.md#beta)\n"));
  QVERIFY(f->create(QStringLiteral("b.md"), "# Beta\n\nAllowed target\n"));
  QVERIFY(f->create(QStringLiteral("external.txt"), "Open with configured system application\n"));
  QVERIFY(f->create(QStringLiteral("unsupported.png"), imageBytes(Qt::red)));
  QVERIFY(f->open(QStringLiteral("a.md")).isValid());
  QPointer<ViewWindow2> source(f->current());
  auto *editor = source->findChild<MarkdownEditor *>();
  QVERIFY(editor);
  QTRY_COMPARE(editor->getNavigationTargets().size(), 4);
  const auto main = f->workspace();
  const auto sourceId = source->getViewWindowId();
  PreviewDesktopSink desktop;
  QDesktopServices::setUrlHandler(QStringLiteral("file"), &desktop, "open");
  const auto restoreDesktop =
      qScopeGuard([] { QDesktopServices::unsetUrlHandler(QStringLiteral("file")); });
  const auto oldPrograms = f->config->getSessionConfig().getExternalPrograms();
  SessionConfig::ExternalProgram program;
  program.m_name = SessionConfig::ExternalProgram::c_systemProgramName;
  program.m_suffixes = {QStringLiteral("txt")};
  f->config->getSessionConfig().setExternalPrograms({program});
  f->factory->unregisterCreator(QStringLiteral("Image"));
  const auto restoreDispatch = qScopeGuard([&] {
    f->config->getSessionConfig().setExternalPrograms(oldPrograms);
    f->factory->registerBuiltInCreators();
  });
  int beforeOpen = 0;
  int splitCreates = 0;
  bool hideBeforeOpen = false;
  const int openHook = f->hooks->addAction<ViewWindowOpenEvent>(
      HookNames::ViewWindowBeforeOpen, [&](HookContext &, const ViewWindowOpenEvent &) {
        ++beforeOpen;
        if (hideBeforeOpen)
          source->hide();
      });
  const int splitHook = f->hooks->addAction<ViewSplitCreateEvent>(
      HookNames::ViewSplitBeforeCreate,
      [&](HookContext &, const ViewSplitCreateEvent &) { ++splitCreates; });
  const auto removeHooks = qScopeGuard([&] {
    f->hooks->removeAction(openHook);
    f->hooks->removeAction(splitHook);
  });
  const auto click = [&](int p_index) {
    const auto target = editor->getNavigationTargets().at(p_index);
    QTest::mouseClick(target.m_widget, Qt::LeftButton, Qt::ControlModifier, target.m_rect.center());
  };
  click(0);
  QCOMPARE(beforeOpen, 0);
  QVERIFY(desktop.opened.isEmpty());
  click(1);
  QCOMPARE(desktop.opened.size(), 1);
  QCOMPARE(desktop.opened.last(), QUrl::fromLocalFile(f->absolute(QStringLiteral("external.txt"))));
  QCOMPARE(beforeOpen, 0); // Configured external dispatch wins even over the view-open hook.
  click(2);
  QCOMPARE(desktop.opened.size(), 2);
  QCOMPARE(desktop.opened.last(),
           QUrl::fromLocalFile(f->absolute(QStringLiteral("unsupported.png"))));
  QCOMPARE(beforeOpen, 1); // Unsupported-type dispatch is after the hook, before host creation.
  QCOMPARE(splitCreates, 0);
  QCOMPARE(f->buffers->listBuffers().size(), 1);
  QCOMPARE(f->workspaces->listWorkspaces().size(), 1);
  QVERIFY(f->area->previewWorkspaceForWindow(sourceId).isEmpty());
  hideBeforeOpen = true;
  click(3);
  QCOMPARE(beforeOpen, 2);
  QCOMPARE(splitCreates, 0);
  QCOMPARE(f->buffers->listBuffers().size(), 1);
  QVERIFY(f->area->previewWorkspaceForWindow(sourceId).isEmpty());
  QVERIFY(!source->isVisible());
  source->show();
  hideBeforeOpen = false;

  const auto privateId = f->createProtected(QStringLiteral("private.md"),
                                            "# Private source\n\n[Allowed](b.md#beta)\n", true);
  const auto nextId =
      f->createProtected(QStringLiteral("next.md"), "# Next protected generation\n", false);
  QVERIFY(!privateId.isEmpty() && !nextId.isEmpty());
  f->reuse();
  const auto privateBuffer = f->openProtected(privateId);
  QVERIFY(privateBuffer.isValid() && privateBuffer.isEncrypted());
  QCOMPARE(f->current(), source.data());
  source->setMode(ViewWindowMode::Read);
  QPointer<MarkdownViewer> reader(source->findChild<MarkdownViewer *>());
  QVERIFY(reader && waitForRender(reader, QStringLiteral("Private source")));
  QVERIFY(reader->page()->url().scheme() == QLatin1String("vxnote"));
  QVERIFY(clickNativeSurface(reader, readerLinkPoint(reader, QStringLiteral("Allowed"))));
  QTRY_COMPARE(f->current()->getNodeId(), f->node(QStringLiteral("b.md")));
  const auto preview = f->workspace();
  QVERIFY(preview != main);
  QCOMPARE(f->coreCurrent(main), privateBuffer.id());
  QVERIFY(f->controller()->closePreviewWorkspace(preview));
  QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);

  f->config->getCoreConfig().setAlwaysOpenNotesInNewTabs(true);
  QSignalSpy contextLinks(reader, &MarkdownViewer::contextLinkOpenRequested);
  QVERIFY(openContextLinkAt(reader, readerLinkPoint(reader, QStringLiteral("Allowed"))));
  QTRY_COMPARE(f->current()->getNodeId(), f->node(QStringLiteral("b.md")));
  QCOMPARE(contextLinks.size(), 1);
  QCOMPARE(contextLinks.first().first().toString(), QStringLiteral("b.md#beta"));
  QCOMPARE(f->workspace(), main);
  QVERIFY(f->area->previewWorkspaceForWindow(sourceId).isEmpty());
  QCOMPARE(desktop.opened.size(), 2);
  QVERIFY(f->controller()->closeViewWindow(f->current()->getViewWindowId(), false));
  QCOMPARE(f->current(), source.data());
  contextLinks.clear();
  QVERIFY(openContextLinkAt(reader, readerLinkPoint(reader, QStringLiteral("Allowed")),
                            [&] { reader->invalidateNavigationTargets(); }));
  QVERIFY(contextLinks.isEmpty());
  QCOMPARE(f->buffers->listBuffers().size(), 1);
  QVERIFY(f->area->previewWorkspaceForWindow(sourceId).isEmpty());

  // Retain a real DOM target, then rebind its owning protected view. Neither an
  // already-completed activation nor an in-flight collection may target the new note.
  QVector<NavigationTarget> oldTargets;
  bool targetsReady = false;
  source->fetchNavigationTargets([&](QVector<NavigationTarget> p_targets) {
    oldTargets = std::move(p_targets);
    targetsReady = true;
  });
  QTRY_VERIFY(targetsReady);
  QCOMPARE(oldTargets.size(), 1);
  struct PendingTargets {
    bool done = false;
    QVector<NavigationTarget> targets;
  };
  const auto pending = std::make_shared<PendingTargets>();
  source->fetchNavigationTargets([pending](QVector<NavigationTarget> p_targets) {
    pending->targets = std::move(p_targets);
    pending->done = true;
  });
  const auto oldGeneration = source->getLinkOpenSettings().m_previewSourceGeneration;
  f->reuse();
  const auto next = f->openProtected(nextId);
  QVERIFY(next.isValid());
  QCOMPARE(f->current(), source.data());
  QVERIFY(source->getLinkOpenSettings().m_previewSourceGeneration > oldGeneration);
  oldTargets.first().m_activate();
  QTRY_VERIFY(pending->done);
  QVERIFY(pending->targets.isEmpty());
  QTRY_VERIFY(reader.isNull());
  QCOMPARE(source->getBuffer().id(), next.id());
  QCOMPARE(f->buffers->listBuffers().size(), 1);
  QCOMPARE(f->order(main), QStringList({next.id()}));
  QVERIFY(f->area->previewWorkspaceForWindow(sourceId).isEmpty());
  QCOMPARE(desktop.opened.size(), 2);
}

void TestViewAreaTabReuse::previewMindMapAndTypstActivation() {
  QVERIFY(f->create(QStringLiteral("b.md"), "# Beta\n\nMarkdown from native links\n"));
  auto mapJson = QJsonDocument::fromJson(mindMapBytes(QStringLiteral("Linked map"))).object();
  auto root = mapJson.value(QStringLiteral("nodes")).toArray().first().toObject();
  root.insert(QStringLiteral("hyperLink"), QStringLiteral("b.md#beta"));
  mapJson.insert(QStringLiteral("nodes"), QJsonArray{root});
  const auto mapBytes = QJsonDocument(mapJson).toJson();
  QVERIFY(f->create(QStringLiteral("linked.emind"), mapBytes));
  const auto mapBuffer = f->open(QStringLiteral("linked.emind"));
  QVERIFY(mapBuffer.isValid());
  QPointer<ViewWindow2> mapWindow(f->current());
  const auto main = f->workspace();
  auto *map = mapWindow->findChild<MindMapEditor *>();
  QVERIFY(map);
  auto *canvas = map->findChild<QGraphicsView *>();
  QVERIFY(canvas);
  const auto indicatorPoint = [&] {
    // Layout/fit can rebuild scene items while events are pumped. Retain only
    // the current viewport coordinate, never a QGraphicsItem across that pump.
    for (auto *item : canvas->scene()->items()) {
      if (item->isVisible() && item->cursor().shape() == Qt::PointingHandCursor &&
          item->toolTip().contains(QStringLiteral("b.md#beta"))) {
        const auto point = canvas->mapFromScene(item->sceneBoundingRect().center());
        if (canvas->viewport()->rect().contains(point))
          return point;
      }
    }
    return QPoint(-1, -1);
  };
  QTRY_VERIFY(indicatorPoint().x() >= 0);
  QSignalSpy mapLinks(map, &MindMapEditor::nodeLinkActivated);
  QVERIFY(clickNativeSurface(canvas->viewport(), indicatorPoint()));
  QCOMPARE(mapLinks.size(), 1);
  QTRY_COMPARE(f->current()->getNodeId(), f->node(QStringLiteral("b.md")));
  const auto mapPreview = f->workspace();
  QVERIFY(mapPreview != main);
  QCOMPARE(f->coreCurrent(main), mapBuffer.id());
  QCOMPARE(f->order(main), QStringList({mapBuffer.id()}));
  QCOMPARE(readBytes(f->absolute(QStringLiteral("linked.emind"))), mapBytes);
  QVERIFY(f->controller()->closePreviewWorkspace(mapPreview));
  QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
  QVERIFY(f->controller()->closeViewWindow(mapWindow->getViewWindowId(), false));
  QTRY_VERIFY(mapWindow.isNull());

  QVERIFY(f->create(QStringLiteral("blue.png"), imageBytes(Qt::blue)));
  QVERIFY(f->create(QStringLiteral("red.png"), imageBytes(Qt::red)));
  auto destination = QUrl::fromLocalFile(f->absolute(QStringLiteral("b.md")));
  destination.setFragment(QStringLiteral("beta"));
  const auto typstSource =
      QStringLiteral("#set page(width: 320pt, height: 640pt, margin: 20pt)\n"
                     "#link(\"%1\")[#image(\"blue.png\", width: 72pt)]\n\n"
                     "#link(<destination>)[#image(\"red.png\", width: 36pt) Internal page]\n\n"
                     "#text(fill: rgb(\"#00ff00\"))[Source position]\n"
                     "#pagebreak()\n= Destination <destination>\nSource navigation target.\n")
          .arg(destination.toString(QUrl::FullyEncoded));
  QVERIFY(f->create(QStringLiteral("links.typst"), typstSource.toUtf8()));
  const auto typstBuffer = f->open(QStringLiteral("links.typst"));
  QVERIFY(typstBuffer.isValid());
  auto *window = f->current();
  auto *typst = window->findChild<vte::VTypstEditor *>();
  QVERIFY(typst);
  auto *edit = typst->getTextEditor()->getTextEdit();
  auto *pane = typst->findChild<QAbstractScrollArea *>(QStringLiteral("typstPdf"));
  QVERIFY(pane);
  typst->setInplacePreviewEnabled(false);
  typst->setLivePreviewEnabled(true);
  QTRY_COMPARE_WITH_TIMEOUT(typst->compileStatus(), vte::VTypstEditor::CompileStatus::Ready, 60000);
  QTRY_COMPARE_WITH_TIMEOUT(typst->pdfPageCount(), 2, 60000);
  QSignalSpy links(typst, &vte::VTypstEditor::externalLinkRequested);
  f->reuse();
  for (bool inplace : {false, true}) {
    QWidget *surface = pane->viewport();
    QPoint point(-1, -1);
    if (inplace) {
      typst->setInplacePreviewEnabled(true);
      typst->setLivePreviewEnabled(false);
      edit->moveCursor(QTextCursor::Start);
      edit->ensureCursorVisible();
      QVERIFY(QTest::qWaitFor(
          [&] {
            for (auto *card : typst->findChildren<QWidget *>(QStringLiteral("typstPreviewCard"))) {
              if (card->isVisible() && (point = coloredSurfacePoint(card, Qt::blue)).x() >= 0) {
                surface = card;
                return true;
              }
            }
            return false;
          },
          10000));
    } else {
      QVERIFY(QTest::qWaitFor(
          [&] {
            point = coloredSurfacePoint(surface, Qt::blue);
            return point.x() >= 0;
          },
          10000));
    }
    links.clear();
    QVERIFY(clickNativeSurface(surface, point));
    QTRY_COMPARE(f->current()->getNodeId(), f->node(QStringLiteral("b.md")));
    QCOMPARE(links.size(), 1);
    QCOMPARE(links.first()[0].toUrl(), destination);
    QCOMPARE(links.first()[1].toBool(), false);
    const auto preview = f->workspace();
    QVERIFY(preview != main);
    QCOMPARE(f->order(main), QStringList({typstBuffer.id()}));
    QCOMPARE(f->coreCurrent(main), typstBuffer.id());
    QCOMPARE(edit->toPlainText(), typstSource);
    QVERIFY(f->controller()->closePreviewWorkspace(preview));
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QCOMPARE(f->current(), window);

    f->config->getCoreConfig().setAlwaysOpenNotesInNewTabs(true);
    links.clear();
    QVERIFY(openContextLinkAt(surface, point));
    QTRY_COMPARE(f->current()->getNodeId(), f->node(QStringLiteral("b.md")));
    QCOMPARE(links.size(), 1);
    QCOMPARE(links.first()[0].toUrl(), destination);
    QCOMPARE(links.first()[1].toBool(), true);
    QCOMPARE(f->workspace(), main);
    QVERIFY(f->area->previewWorkspaceForWindow(window->getViewWindowId()).isEmpty());
    QVERIFY(f->controller()->closeViewWindow(f->current()->getViewWindowId(), false));
    QCOMPARE(f->current(), window);
    f->reuse();
  }

  // A compiler-resolved page link is not a file open, even when previews are enabled.
  typst->setInplacePreviewEnabled(false);
  typst->setLivePreviewEnabled(true);
  typst->goToPdfPage(0);
  QTRY_COMPARE(typst->currentPdfPage(), 0);
  QPoint internalPoint(-1, -1);
  QVERIFY(QTest::qWaitFor(
      [&] {
        internalPoint = coloredSurfacePoint(pane->viewport(), Qt::red);
        return internalPoint.x() >= 0;
      },
      10000));
  links.clear();
  QVERIFY(clickNativeSurface(pane->viewport(), internalPoint));
  QTRY_COMPARE(typst->currentPdfPage(), 1);
  QVERIFY(links.isEmpty());
  QCOMPARE(f->current(), window);
  QCOMPARE(f->order(main), QStringList({typstBuffer.id()}));
  QVERIFY(f->area->previewWorkspaceForWindow(window->getViewWindowId()).isEmpty());

  // The actual source resource menu preserves the same internal-page behavior.
  typst->goToPdfPage(0);
  QTextCursor internal(edit->document());
  internal.setPosition(typstSource.indexOf(QStringLiteral("Internal page")) + 2);
  edit->setTextCursor(internal);
  edit->ensureCursorVisible();
  QVERIFY(openContextLinkAt(edit->viewport(), edit->cursorRect(internal).center()));
  QTRY_COMPARE(typst->currentPdfPage(), 1);
  QVERIFY(links.isEmpty());

  // Let source-caret scroll sync finish before locating the PDF hit rectangle.
  edit->moveCursor(QTextCursor::End);
  QTRY_COMPARE(typst->currentPdfPage(), 1);
  typst->goToPdfPage(0);
  QTRY_COMPARE(typst->currentPdfPage(), 0);
  QPoint sourcePoint(-1, -1);
  QVERIFY(QTest::qWaitFor(
      [&] {
        sourcePoint = coloredSurfacePoint(pane->viewport(), Qt::green);
        return sourcePoint.x() >= 0;
      },
      10000));
  QSignalSpy sourceLocations(typst, &vte::VTypstEditor::sourceLocationRequested);
  QTest::mouseClick(pane->viewport(), Qt::LeftButton, Qt::ControlModifier, sourcePoint);
  QTRY_COMPARE(edit->textCursor().blockNumber(),
               edit->document()
                   ->findBlock(typstSource.indexOf(QStringLiteral("Source position")))
                   .blockNumber());
  QVERIFY(sourceLocations.isEmpty()); // Same-file mapping stays inside the native editor.
  QVERIFY(links.isEmpty());
  QCOMPARE(f->current(), window);
  QCOMPARE(edit->toPlainText(), typstSource);
  QCOMPARE(f->order(main), QStringList({typstBuffer.id()}));
  QVERIFY(f->area->previewWorkspaceForWindow(window->getViewWindowId()).isEmpty());
}

void TestViewAreaTabReuse::previewHostRoutingAndGeometry() {
  QVERIFY(f->create(QStringLiteral("a.txt"), "source A\n"));
  QVERIFY(f->create(QStringLiteral("b.txt"), "preview B\n"));
  QVERIFY(f->create(QStringLiteral("c.txt"), "preview C\n"));
  const auto a = f->open(QStringLiteral("a.txt"));
  QVERIFY(a.isValid());
  f->reuse();
  QPointer<ViewWindow2> source(f->current());
  const auto main = f->workspace();
  const auto sourceSize = source->size();
  auto settings = source->getLinkOpenSettings();
  settings.m_mode = ViewWindowMode::Edit;
  const auto b = f->buffers->openBuffer(f->node(QStringLiteral("b.txt")), settings);
  QVERIFY(b.isValid());
  const auto preview = f->area->previewWorkspaceForWindow(source->getViewWindowId());
  QVERIFY(!preview.isEmpty());
  QVERIFY(f->workspaces->getWorkspace(preview).value(QStringLiteral("isPreview")).toBool());
  QCOMPARE(f->workspaces->getCurrentWorkspace(), main);
  QCOMPARE(f->coreCurrent(main), a.id());
  QCOMPARE(f->order(main), QStringList({a.id()}));
  QCOMPARE(source->getBuffer().id(), a.id());
  QCOMPARE(source->getLatestContent(), QStringLiteral("source A\n"));
  QCOMPARE(source->size(), sourceSize);
  QCOMPARE(f->area->getViewSplitCount(), 1);
  auto *overlay = source->findChild<LinkPreviewOverlay *>();
  QVERIFY(overlay && overlay->parentWidget() == source);
  QVERIFY(overlay->isVisible());
  auto *split = overlay->getViewSplit();
  QVERIFY(split->isPreview());
  QCOMPARE(f->area->getCurrentViewSplit(), split);
  QCOMPARE(f->current()->getBuffer().id(), b.id());
  QVERIFY(!split->tabBar()->isVisible());
  QVERIFY(source->rect().contains(overlay->geometry()));
  auto nested = f->current()->getLinkOpenSettings();
  nested.m_mode = ViewWindowMode::Edit;
  const auto c = f->buffers->openBuffer(f->node(QStringLiteral("c.txt")), nested);
  QVERIFY(c.isValid());
  QCOMPARE(f->order(preview), QStringList({b.id(), c.id()}));
  QCOMPARE(source->findChildren<LinkPreviewOverlay *>().size(), 1);
  QVERIFY(split->tabBar()->isVisible());
  f->buffers->openBuffer(b.nodeId(), f->current()->getLinkOpenSettings());
  QCOMPARE(f->order(preview), QStringList({b.id(), c.id()}));
  QCOMPARE(f->current()->getBuffer().id(), b.id());
  FileOpenSettings background;
  background.m_targetWorkspaceId = preview;
  background.m_focus = false;
  f->buffers->openBuffer(c.nodeId(), background);
  QCOMPARE(f->current()->getBuffer().id(), b.id());
  QCOMPARE(f->coreCurrent(main), a.id());
  const auto cWindow = f->area->findWindowIdByBufferId(preview, c.id());
  QVERIFY(f->controller()->closeViewWindow(cWindow, false));
  QCOMPARE(f->order(preview), QStringList({b.id()}));
  QVERIFY(!split->tabBar()->isVisible());
  const struct {
    QRect anchor;
    QRect expected;
  } geometryCases[] = {
      {QRect(20, 30, 400, 300), QRect(54, 59, 331, 241)},
      {QRect(20, 30, 20, 10), QRect(30, 35, 0, 0)},
      {QRect(20, 30, 401, 301), QRect(54, 59, 332, 242)},
  };
  for (const auto &geometryCase : geometryCases) {
    QVERIFY(source->contentsRect().contains(geometryCase.anchor));
    overlay->setAnchorRect(geometryCase.anchor);
    // Observe immediately, before the view area's queued live-layout re-anchor.
    QCOMPARE(overlay->geometry(), geometryCase.expected);
    QCOMPARE(source->size(), sourceSize);
  }
  f->area->resize(760, 550);
  QTRY_VERIFY(source->rect().contains(overlay->geometry()));
  QPointer<LinkPreviewOverlay> closingOverlay(overlay);
  QVERIFY(
      f->controller()->closeViewWindow(f->area->findWindowIdByBufferId(preview, b.id()), false));
  QVERIFY(f->workspaces->getWorkspace(preview).isEmpty());
  QVERIFY(f->area->previewWorkspaceForWindow(source->getViewWindowId()).isEmpty());
  QTRY_VERIFY(closingOverlay.isNull());
  QCOMPARE(f->current(), source.data());
  QCOMPARE(f->order(main), QStringList({a.id()}));
}

void TestViewAreaTabReuse::previewEditingCommandTargetAndVisibility() {
  const QByteArray sourceBytes("# Source A\n\nUntouched source text\n");
  QVERIFY(f->create(QStringLiteral("a.md"), sourceBytes));
  QVERIFY(f->create(QStringLiteral("b.md"), "# Preview B\n\npreview needle\n"));
  QVERIFY(f->create(QStringLiteral("c.txt"), "Ordinary explorer destination\n"));
  const auto a = f->open(QStringLiteral("a.md"));
  QVERIFY(a.isValid());
  QPointer<ViewWindow2> source(f->current());
  const auto main = f->workspace();
  auto *sourceEditor = source->findChild<MarkdownEditor *>();
  QVERIFY(sourceEditor);
  auto sourceCursor = sourceEditor->getTextEdit()->textCursor();
  sourceCursor.setPosition(3);
  sourceEditor->getTextEdit()->setTextCursor(sourceCursor);
  const auto b = f->preview(source, f->node(QStringLiteral("b.md")));
  QVERIFY(b.isValid());
  QPointer<ViewWindow2> previewWindow(f->current());
  auto *editor = previewWindow->findChild<MarkdownEditor *>();
  QVERIFY(editor);
  const auto preview = f->workspace();
  QVERIFY(preview != main);
  auto *overlay = source->findChild<LinkPreviewOverlay *>();
  QVERIFY(overlay);
  editor->getTextEdit()->setFocus();
  QTRY_VERIFY(editor->getTextEdit()->hasFocus());
  QCOMPARE(f->controller()->getCurrentWindowId(), previewWindow->getViewWindowId());
  QCOMPARE(f->area->getCurrentViewSplit(), overlay->getViewSplit());
  QCOMPARE(f->workspaces->getCurrentWorkspace(), main);
  QCOMPARE(f->coreCurrent(main), a.id());
  QVERIFY(f->current()->getOutlineProvider());
  QVERIFY(f->current()->getOutlineProvider() != source->getOutlineProvider());
  f->current()->findNext(QStringLiteral("needle"), {});
  QTRY_COMPARE(editor->getTextEdit()->textCursor().position(),
               editor->getText().indexOf(QStringLiteral("needle")));
  auto selected = editor->getTextEdit()->textCursor();
  selected.movePosition(QTextCursor::NextCharacter, QTextCursor::KeepAnchor, 6);
  editor->getTextEdit()->setTextCursor(selected);
  QCOMPARE(f->current()->getSelectedText(), QStringLiteral("needle"));
  QCOMPARE(sourceEditor->getTextEdit()->textCursor().position(), 3);
  appendDraft(editor->getTextEdit(), QStringLiteral("\nDurable preview edit\n"));
  const auto saved = editor->getText().toUtf8();
  QTest::keyClick(editor->getTextEdit(), Qt::Key_S, Qt::ControlModifier);
  QTRY_COMPARE(readBytes(f->absolute(QStringLiteral("b.md"))), saved);
  QCOMPARE(readBytes(f->absolute(QStringLiteral("a.md"))), sourceBytes);
  QCOMPARE(source->getLatestContent().toUtf8(), sourceBytes);

  appendDraft(editor->getTextEdit(), QStringLiteral("Unsaved overlay draft\n"));
  const auto draft = editor->getText();
  const int undoSteps = editor->getTextEdit()->document()->availableUndoSteps();
  QLineEdit dock(f->area.get());
  dock.show();
  dock.setFocus();
  QTRY_VERIFY(dock.hasFocus());
  QCOMPARE(f->current(), previewWindow.data());
  f->config->getEditorConfig().setLinkPreviewEnabled(false);
  QVERIFY(overlay->isVisible());
  QCOMPARE(editor->getText(), draft);
  f->config->getEditorConfig().setLinkPreviewEnabled(true);
  dock.hide();
  source->hide();
  QTRY_VERIFY(!overlay->isVisible());
  QCOMPARE(f->workspace(), main);
  QCOMPARE(f->current(), source.data());
  QCOMPARE(editor->getText(), draft);
  source->show();
  QTRY_VERIFY(overlay->isVisible());
  editor->getTextEdit()->setFocus();
  QTRY_COMPARE(f->current(), previewWindow.data());
  QCOMPARE(editor->getTextEdit()->document()->availableUndoSteps(), undoSteps);
  f->controller()->focus();
  QTRY_VERIFY(previewWindow->isAncestorOf(QApplication::focusWidget()));

  // Explorer/ordinary opens use the stored main destination, not command focus.
  const auto c = f->open(QStringLiteral("c.txt"), true);
  QVERIFY(c.isValid());
  QCOMPARE(f->workspace(), main);
  QCOMPARE(f->current()->getBuffer().id(), c.id());
  QCOMPARE(f->order(main), QStringList({a.id(), c.id()}));
  QCOMPARE(f->order(preview), QStringList({b.id()}));
  QVERIFY(!overlay->isVisible());
  QCOMPARE(previewWindow->getLatestContent(), draft);
  f->controller()->focusWindow(main, a.id());
  QTRY_VERIFY(overlay->isVisible());
  editor->getTextEdit()->setFocus();
  QTRY_COMPARE(f->controller()->getCurrentWindowId(), previewWindow->getViewWindowId());
  DialogAnswer discard(QMessageBox::Discard);
  QVERIFY(f->controller()->closeViewWindow(f->controller()->getCurrentWindowId(), false));
  QVERIFY(discard.accepted());
  QCOMPARE(f->current(), source.data());
  QCOMPARE(f->workspace(), main);
  QCOMPARE(readBytes(f->absolute(QStringLiteral("b.md"))), saved);
  QCOMPARE(sourceEditor->getTextEdit()->textCursor().position(), 3);
  QCOMPARE(source->getLatestContent().toUtf8(), sourceBytes);

  // Comments are supported by PDF views, not Markdown. The same effective editor
  // selection must expose that preview's real provider and target identity.
  QVERIFY(f->create(QStringLiteral("comments.pdf"), pdfBytes(QStringLiteral("Preview comments"))));
  const auto pdf = f->preview(source, f->node(QStringLiteral("comments.pdf")));
  QVERIFY(pdf.isValid());
  auto *pdfWindow = f->current();
  auto *comments = pdfWindow->findChild<CommentController *>();
  auto *viewer = pdfWindow->findChild<PdfViewer *>();
  QVERIFY(comments && viewer && pdfWindow->getCommentProvider());
  QTRY_VERIFY_WITH_TIMEOUT(viewer->adapter()->getViewerState().m_valid, 30000);
  QCOMPARE(comments->getActiveFile(), pdf.nodeId());
  const QPointer<ViewWindow2> closedPdf(pdfWindow);
  QVERIFY(f->controller()->closePreviewWorkspace(f->workspace()));
  QTRY_VERIFY(closedPdf.isNull());
  QCOMPARE(f->current(), source.data());
}

void TestViewAreaTabReuse::previewOutsideClickDismissal() {
  const QByteArray sourceBytes("[C](c.md)\n\n# Source A\n");
  QVERIFY(f->create(QStringLiteral("a.md"), sourceBytes));
  QVERIFY(f->create(QStringLiteral("b.txt"), "Preview B\n"));
  QVERIFY(f->create(QStringLiteral("c.md"), "# Do not open on dismissal\n"));
  QVERIFY(f->create(QStringLiteral("other.txt"), "Other editor\n"));
  const QByteArray readerBytes(
      "[C](c.md)\n\n[Here](#reader-first-source)\n\n# Reader-first source\n");
  QVERIFY(f->create(QStringLiteral("reader.md"), readerBytes));
  {
    const auto initial = f->buffers->openBuffer(f->node(QStringLiteral("reader.md")));
    QVERIFY(initial.isValid());
    const QPointer<ViewWindow2> readerSource(f->current());
    QVERIFY(!readerSource->findChild<MarkdownEditor *>());
    auto *reader = readerSource->findChild<MarkdownViewer *>();
    QVERIFY(reader && waitForRender(reader, QStringLiteral("Reader-first source")));
    const auto sourceSize = reader->size();
    QVERIFY(clickNativeSurface(reader, readerLinkPoint(reader, QStringLiteral("Here"))));
    const auto point = readerLinkPoint(reader, QStringLiteral("C"));
    QVERIFY(clickNativeSurface(reader, point));
    QTRY_VERIFY(f->current() != readerSource);
    QPointer<LinkPreviewOverlay> preview(readerSource->findChild<LinkPreviewOverlay *>());
    QVERIFY(preview);
    QVERIFY(clickNativeSurface(reader, point));
    QTRY_VERIFY(preview.isNull());
    QCOMPARE(f->current(), readerSource.data());
    QCOMPARE(reader->size(), sourceSize);
    QCOMPARE(readerSource->getBuffer().id(), initial.id());
    QCOMPARE(readerSource->getLatestContent().toUtf8(), readerBytes);
    QVERIFY(clickNativeSurface(reader, point));
    QTRY_VERIFY(!f->area->previewWorkspaceForWindow(readerSource->getViewWindowId()).isEmpty());
    preview = readerSource->findChild<LinkPreviewOverlay *>();
    QVERIFY(f->controller()->closePreviewWorkspace(f->workspace()));
    QTRY_VERIFY(preview.isNull());
    QVERIFY(f->controller()->closeViewWindow(readerSource->getViewWindowId(), false));
    QTRY_VERIFY(readerSource.isNull());
  }
  const auto a = f->open(QStringLiteral("a.md"));
  QVERIFY(a.isValid());
  QPointer<ViewWindow2> source(f->current());
  const auto main = f->workspace();
  const auto sourceId = source->getViewWindowId();
  auto *sourceEditor = source->findChild<MarkdownEditor *>();
  QVERIFY(sourceEditor);
  auto *sourceEdit = sourceEditor->getTextEdit();
  sourceEdit->moveCursor(QTextCursor::End);
  const auto originalCaret = sourceEdit->textCursor().position();
  f->controller()->splitViewSplit(main, Direction::Right, false);
  const auto other = f->open(QStringLiteral("other.txt"));
  QVERIFY(other.isValid());
  auto *otherEditor = f->current()->findChild<TextEditor *>();
  QVERIFY(otherEditor);
  f->controller()->focusWindow(main, a.id());
  QTRY_VERIFY(source->isVisible());
  const auto openPreview = [&]() -> QPointer<LinkPreviewOverlay> {
    f->preview(source, f->node(QStringLiteral("b.txt")));
    return source->findChild<LinkPreviewOverlay *>();
  };
  auto overlay = openPreview();
  QVERIFY(overlay && overlay->isVisible());
  const auto preview = f->area->previewWorkspaceForWindow(sourceId);
  auto *previewEditor = overlay->getViewSplit()->getCurrentViewWindow()->findChild<TextEditor *>();
  QVERIFY(previewEditor);
  QVERIFY(clickNativeSurface(previewEditor->getTextEdit()->viewport(),
                             previewEditor->getTextEdit()->viewport()->rect().center()));
  QVERIFY(overlay && overlay->isVisible());
  QVERIFY(clickNativeSurface(otherEditor->getTextEdit()->viewport(),
                             otherEditor->getTextEdit()->viewport()->rect().center()));
  QCOMPARE(f->area->previewWorkspaceForWindow(sourceId), preview);
  {
    QMenu menu(source);
    bool triggered = false;
    auto *action = menu.addAction(QStringLiteral("Unrelated source menu"));
    connect(action, &QAction::triggered, &menu, [&] { triggered = true; });
    menu.popup(source->mapToGlobal(QPoint(2, 2)));
    QTRY_VERIFY(menu.isVisible());
    QTest::mouseClick(&menu, Qt::LeftButton, Qt::NoModifier, menu.actionGeometry(action).center());
    QVERIFY(triggered);
    QVERIFY(overlay && overlay->isVisible());
  }
  {
    QMessageBox message(QMessageBox::Information, QStringLiteral("Other dialog"),
                        QStringLiteral("Keep the preview"), QMessageBox::Ok, source);
    message.setWindowModality(Qt::WindowModal);
    message.show();
    QTRY_COMPARE(QApplication::activeModalWidget(), &message);
    QTest::mouseClick(message.button(QMessageBox::Ok), Qt::LeftButton);
    QTRY_VERIFY(!message.isVisible());
    QVERIFY(overlay && overlay->isVisible());
  }
  source->hide();
  QTRY_VERIFY(!overlay->isVisible());
  QVERIFY(clickNativeSurface(otherEditor->getTextEdit()->viewport(),
                             otherEditor->getTextEdit()->viewport()->rect().center()));
  source->show();
  QTRY_VERIFY(overlay->isVisible());
  f->controller()->focusWindow(main, a.id());
  QCOMPARE(f->area->previewWorkspaceForWindow(sourceId), preview);
  QTRY_COMPARE(sourceEditor->getNavigationTargets().size(), 1);
  const QPoint linkPoint = sourceEditor->getNavigationTargets().first().m_rect.center();
  const auto sourcePoint = source->mapFromGlobal(sourceEdit->viewport()->mapToGlobal(linkPoint));
  QVERIFY(!overlay->geometry().contains(sourcePoint));
  QVERIFY(clickNativeSurface(sourceEdit->viewport(), linkPoint));
  QTRY_VERIFY(overlay.isNull());
  QVERIFY(f->area->previewWorkspaceForWindow(sourceId).isEmpty());
  QVERIFY(f->area->findWindowIdsByNode(f->node(QStringLiteral("c.md")), false).isEmpty());
  QCOMPARE(sourceEdit->textCursor().position(), originalCaret);
  QCOMPARE(sourceEditor->getText().toUtf8(), sourceBytes);
  QTRY_VERIFY(sourceEdit->hasFocus());

  overlay = openPreview();
  QVERIFY(overlay);
  const auto dragPoint = linkPoint + QPoint(QApplication::startDragDistance() + 5, 0);
  QTest::mousePress(sourceEdit->viewport(), Qt::LeftButton, Qt::NoModifier, linkPoint);
  for (const auto &point : {dragPoint, linkPoint}) {
    QMouseEvent move(QEvent::MouseMove, point, sourceEdit->viewport()->mapToGlobal(point),
                     Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(sourceEdit->viewport(), &move);
  }
  QTest::mouseRelease(sourceEdit->viewport(), Qt::LeftButton, Qt::NoModifier, linkPoint);
  QVERIFY(overlay && overlay->isVisible());
  QCOMPARE(sourceEdit->textCursor().position(), originalCaret);
  QVERIFY(clickNativeSurface(sourceEdit->viewport(), linkPoint));
  QTRY_VERIFY(overlay.isNull());

  source->setMode(ViewWindowMode::Read);
  auto *reader = source->findChild<MarkdownViewer *>();
  QVERIFY(reader && waitForRender(reader, QStringLiteral("Source A")));
  overlay = openPreview();
  QVERIFY(overlay);
  const auto readerPoint = readerLinkPoint(reader, QStringLiteral("C"));
  const auto global = reader->mapToGlobal(readerPoint);
  QVERIFY(!overlay->geometry().contains(source->mapFromGlobal(global)));
  QVERIFY(clickNativeSurface(reader, readerPoint));
  QVERIFY(f->area->previewWorkspaceForWindow(sourceId).isEmpty());
  // Deliver the platform's second press, not another complete single-click gesture.
  auto *receiver = QApplication::widgetAt(global);
  QVERIFY(receiver && (receiver == reader || reader->isAncestorOf(receiver)));
  QMouseEvent doubleClick(QEvent::MouseButtonDblClick, receiver->mapFromGlobal(global), global,
                          Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
  QApplication::sendEvent(receiver, &doubleClick);
  QTest::mouseRelease(receiver, Qt::LeftButton, Qt::NoModifier, receiver->mapFromGlobal(global));
  QVERIFY(evaluate(reader->page(), QStringLiteral("document.body.innerText"))
              .toString()
              .contains(QStringLiteral("Source A")));
  QVERIFY(f->area->previewWorkspaceForWindow(sourceId).isEmpty());
  QVERIFY(f->area->findWindowIdsByNode(f->node(QStringLiteral("c.md")), false).isEmpty());
  QTRY_VERIFY(overlay.isNull());
  QTRY_VERIFY(reader->hasFocus() || reader->isAncestorOf(QApplication::focusWidget()));
  // A subsequent ordinary click must work immediately, not wait for a grace timer.
  QVERIFY(clickNativeSurface(reader, readerPoint));
  QTRY_COMPARE(f->current()->getNodeId(), f->node(QStringLiteral("c.md")));
  const QPointer<LinkPreviewOverlay> next(source->findChild<LinkPreviewOverlay *>());
  QVERIFY(f->controller()->closePreviewWorkspace(f->workspace()));
  QTRY_VERIFY(next.isNull());

  auto *toolbar = source->findChild<QToolBar *>(QString(), Qt::FindDirectChildrenOnly);
  QVERIFY(toolbar);
  auto *modeAction = toolbar->findChild<EditReadDiscardAction *>();
  QVERIFY(modeAction);
  auto *modeButton = toolbar->widgetForAction(modeAction);
  QVERIFY(modeButton && modeButton->isEnabled());
  for (const auto answer : {QMessageBox::Cancel, QMessageBox::Save, QMessageBox::Discard}) {
    QVERIFY(writeBytes(f->absolute(QStringLiteral("b.txt")), "Preview B\n"));
    overlay = openPreview();
    QVERIFY(overlay);
    const auto workspace = f->area->previewWorkspaceForWindow(sourceId);
    auto *window = overlay->getViewSplit()->getCurrentViewWindow();
    auto *editor = window->findChild<TextEditor *>();
    QVERIFY(editor);
    editor->getTextEdit()->setFocus();
    QTRY_VERIFY(editor->getTextEdit()->hasFocus());
    appendDraft(editor->getTextEdit(), QStringLiteral("Outside click draft\n"));
    const auto draft = window->getLatestContent();
    DialogAnswer dialog(answer);
    QVERIFY(answer == QMessageBox::Cancel
                ? clickNativeSurface(reader, readerLinkPoint(reader, QStringLiteral("C")))
                : clickNativeSurface(modeButton, modeButton->rect().center()));
    QVERIFY(dialog.accepted());
    QCOMPARE(dialog.count(), 1);
    QCOMPARE(source->getMode(), ViewWindowMode::Read);
    if (answer == QMessageBox::Cancel) {
      QVERIFY(overlay && overlay->isVisible());
      QCOMPARE(window->getLatestContent(), draft);
      QVERIFY(window->isModified() && window->isEnabled());
      QCOMPARE(f->current(), window);
      QVERIFY(window->hasFocus() || window->isAncestorOf(QApplication::focusWidget()));
      QVERIFY(!f->controller()->isPreviewWorkspaceClosing(workspace));
      QCOMPARE(readBytes(f->absolute(QStringLiteral("b.txt"))), QByteArray("Preview B\n"));
      DialogAnswer discard(QMessageBox::Discard);
      QVERIFY(clickNativeSurface(modeButton, modeButton->rect().center()));
      QVERIFY(discard.accepted());
    }
    QTRY_VERIFY(overlay.isNull());
    QVERIFY(f->workspaces->getWorkspace(workspace).isEmpty());
    QCOMPARE(readBytes(f->absolute(QStringLiteral("b.txt"))),
             answer == QMessageBox::Save ? draft.toUtf8() : QByteArray("Preview B\n"));
    QCOMPARE(source->getMode(), ViewWindowMode::Read);
  }
  overlay = openPreview();
  QVERIFY(overlay);
  QVERIFY(f->controller()->detachViewWindow(main, sourceId, a.id()));
  auto *detached = qobject_cast<DetachedWindow *>(source->window());
  QVERIFY(detached);
  detached->activateWindow();
  QTRY_VERIFY(detached->isActiveWindow() && overlay->isVisible());
  QTRY_VERIFY(source->contentsRect().contains(overlay->geometry()));
  reader->setFocus(Qt::OtherFocusReason);
  // Reparenting may suspend the reader bridge while its cached DOM stays visible.
  // Dismissing from that native surface must not depend on renderer readiness.
  QVERIFY(evaluate(reader->page(), QStringLiteral("document.body.innerText"))
              .toString()
              .contains(QStringLiteral("Source A")));
  QVERIFY(overlay && overlay->isVisible());
  QVERIFY(clickNativeSurface(reader, readerLinkPoint(reader, QStringLiteral("C"))));
  QTRY_VERIFY(overlay.isNull());
  QVERIFY(f->area->previewWorkspaceForWindow(sourceId).isEmpty());
  QVERIFY(f->area->findWindowIdsByNode(f->node(QStringLiteral("c.md")), false).isEmpty());
  QCOMPARE(source->getBuffer().id(), a.id());
  QCOMPARE(readBytes(f->absolute(QStringLiteral("a.md"))), sourceBytes);
}

void TestViewAreaTabReuse::previewOpenInNewTabs_data() {
  QTest::addColumn<bool>("detached");
  QTest::newRow("main-source") << false;
  QTest::newRow("detached-source") << true;
}

void TestViewAreaTabReuse::previewOpenInNewTabs() {
  QFETCH(bool, detached);
  QVERIFY(f->create(QStringLiteral("main.txt"), "Main workspace stays here\n"));
  QVERIFY(f->create(QStringLiteral("a.md"), "# Source A\n\n[B](b.md)\n"));
  QVERIFY(f->create(QStringLiteral("b.md"), "# Preview B\n\n[C](c.md)\n"));
  QVERIFY(f->create(QStringLiteral("c.md"), "# Preview C\n"));
  if (detached)
    QVERIFY(f->open(QStringLiteral("main.txt")).isValid());
  const auto a = f->open(QStringLiteral("a.md"), true);
  QVERIFY(a.isValid());
  QPointer<ViewWindow2> source(f->current());
  const auto sourceId = source->getViewWindowId();
  const auto sourceText = source->getLatestContent();
  const auto sourceMode = source->getMode();
  const auto sourceGeneration = source->getLinkOpenSettings().m_previewSourceGeneration;
  const auto main = f->workspace();
  QString target = main;
  ViewSplit2 *targetSplit = f->area->getCurrentViewSplit();
  if (detached) {
    QVERIFY(f->controller()->detachViewWindow(main, sourceId, a.id()));
    auto *host = qobject_cast<DetachedWindow *>(source->window());
    QVERIFY(host);
    target = host->getWorkspaceId();
    targetSplit = host->getViewSplit();
  }
  const auto mainOrder = f->order(main);
  const auto mainCurrent = f->coreCurrent(main);
  const auto mainWindowId = f->controller()->getCurrentWindowId();
  f->reuse();
  const auto b = f->preview(source, f->node(QStringLiteral("b.md")));
  QVERIFY(b.isValid());
  const auto preview = f->area->previewWorkspaceForWindow(sourceId);
  QPointer<LinkPreviewOverlay> overlay(source->findChild<LinkPreviewOverlay *>());
  QVERIFY(overlay && overlay->isVisible());
  QPointer<ViewWindow2> bWindow(overlay->getViewSplit()->getCurrentViewWindow());
  const auto bId = bWindow->getViewWindowId();
  auto *editor = bWindow->findChild<MarkdownEditor *>();
  QVERIFY(editor);
  auto *edit = editor->getTextEdit();
  auto *document = edit->document();
  edit->setFocus();
  QTRY_VERIFY(edit->hasFocus());
  appendDraft(edit, QStringLiteral("\nUnsaved promoted draft\n"));
  auto cursor = edit->textCursor();
  cursor.setPosition(7);
  edit->setTextCursor(cursor);
  const auto draft = editor->getText();
  const auto undoSteps = document->availableUndoSteps();
  const auto c = f->preview(bWindow, f->node(QStringLiteral("c.md")));
  QVERIFY(c.isValid());
  QPointer<ViewWindow2> cWindow(overlay->getViewSplit()->getCurrentViewWindow());
  const auto cId = cWindow->getViewWindowId();
  f->area->setCurrentBuffer(preview, b.id(), true);
  QCOMPARE(overlay->getViewSplit()->getCurrentViewWindow(), bWindow.data());
  auto *button =
      overlay->findChild<QToolButton *>(QStringLiteral("openLinkPreviewInNewTabsButton"));
  QVERIFY(button);
  QVERIFY(clickNativeSurface(button, button->rect().center()));
  QTRY_VERIFY(overlay.isNull());
  QVERIFY(f->workspaces->getWorkspace(preview).isEmpty());
  QVERIFY(f->area->previewWorkspaceForWindow(sourceId).isEmpty());
  QCOMPARE(targetSplit->getAllViewWindows(), QVector<ViewWindow2 *>({source, bWindow, cWindow}));
  QCOMPARE(f->order(target), QStringList({a.id(), b.id(), c.id()}));
  QCOMPARE(f->area->findWindowIdByBufferId(target, b.id()), bId);
  QCOMPARE(f->area->findWindowIdByBufferId(target, c.id()), cId);
  QCOMPARE(targetSplit->getCurrentViewWindow(), bWindow.data());
  QCOMPARE(f->coreCurrent(target), b.id());
  QCOMPARE(bWindow->findChild<MarkdownEditor *>(), editor);
  QCOMPARE(editor->getTextEdit(), edit);
  QCOMPARE(edit->document(), document);
  QCOMPARE(editor->getText(), draft);
  QCOMPARE(document->availableUndoSteps(), undoSteps);
  QCOMPARE(edit->textCursor().position(), 7);
  QCOMPARE(source->getViewWindowId(), sourceId);
  QCOMPARE(source->getBuffer().id(), a.id());
  QCOMPARE(source->getLinkOpenSettings().m_previewSourceGeneration, sourceGeneration);
  QCOMPARE(source->getLatestContent(), sourceText);
  QCOMPARE(source->getMode(), sourceMode);
  if (detached) {
    QCOMPARE(f->workspaces->getCurrentWorkspace(), main);
    QCOMPARE(f->order(main), mainOrder);
    QCOMPARE(f->coreCurrent(main), mainCurrent);
    QCOMPARE(f->controller()->getCurrentWindowId(), mainWindowId);
    QVERIFY(qobject_cast<DetachedWindow *>(bWindow->window()));
  } else {
    QCOMPARE(f->controller()->getCurrentWindowId(), bId);
  }
  QVERIFY(bWindow->save());
  QTRY_COMPARE(readBytes(f->absolute(QStringLiteral("b.md"))), draft.toUtf8());
}

void TestViewAreaTabReuse::previewOpenInNewTabsDuplicateConsent_data() {
  QTest::addColumn<int>("answer");
  QTest::addColumn<bool>("sourceDuplicate");
  QTest::addColumn<bool>("detached");
  QTest::newRow("other-tab-save") << int(QMessageBox::Save) << false << false;
  QTest::newRow("other-tab-discard") << int(QMessageBox::Discard) << false << false;
  QTest::newRow("other-tab-cancel") << int(QMessageBox::Cancel) << false << false;
  QTest::newRow("source-save") << int(QMessageBox::Save) << true << false;
  QTest::newRow("source-discard") << int(QMessageBox::Discard) << true << false;
  QTest::newRow("source-cancel") << int(QMessageBox::Cancel) << true << false;
  QTest::newRow("detached-source-save") << int(QMessageBox::Save) << true << true;
  QTest::newRow("detached-source-discard") << int(QMessageBox::Discard) << true << true;
  QTest::newRow("detached-source-cancel") << int(QMessageBox::Cancel) << true << true;
}

void TestViewAreaTabReuse::previewOpenInNewTabsDuplicateConsent() {
  QFETCH(int, answer);
  QFETCH(bool, sourceDuplicate);
  QFETCH(bool, detached);
  const bool cancelled = answer == QMessageBox::Cancel;
  const QByteArray sourceBytes("# Source reader\n\n[COVERED](covered.md)\n");
  const QByteArray duplicateBytes("# Destination duplicate\n");
  QVERIFY(f->create(QStringLiteral("a.md"), sourceBytes));
  QVERIFY(f->create(QStringLiteral("b.md"), duplicateBytes));
  QVERIFY(f->create(QStringLiteral("early.txt"), "Earlier committed tab\n"));
  QVERIFY(f->create(QStringLiteral("remaining.txt"), "Remaining preview\n"));
  QVERIFY(f->create(QStringLiteral("covered.md"), "# Covered link must not open\n"));
  if (detached) {
    QVERIFY(f->create(QStringLiteral("main.txt"), "Unchanged main workspace\n"));
    QVERIFY(f->open(QStringLiteral("main.txt")).isValid());
  }
  const auto a = f->open(QStringLiteral("a.md"));
  QVERIFY(a.isValid());
  QPointer<ViewWindow2> source(f->current());
  const auto sourceId = source->getViewWindowId();
  const auto ordinaryMain = f->workspace();
  QString main = ordinaryMain;
  auto *mainSplit = f->area->getCurrentViewSplit();
  if (detached) {
    QVERIFY(f->controller()->detachViewWindow(ordinaryMain, sourceId, a.id()));
    auto *host = qobject_cast<DetachedWindow *>(source->window());
    QVERIFY(host);
    main = host->getWorkspaceId();
    mainSplit = host->getViewSplit();
  }
  const auto ordinaryOrder = f->order(ordinaryMain);
  const auto ordinaryCurrent = f->coreCurrent(ordinaryMain);
  auto *sourceEditor = source->findChild<MarkdownEditor *>();
  QVERIFY(sourceEditor);
  const QString destinationDraft = QStringLiteral("\nDestination draft awaiting consent\n");
  if (sourceDuplicate) {
    sourceEditor->getTextEdit()->setFocus();
    QTRY_VERIFY(sourceEditor->getTextEdit()->hasFocus());
    appendDraft(sourceEditor->getTextEdit(), destinationDraft);
    QVERIFY(source->isModified());
  }
  // Direct mode changes retain the draft; the toolbar's Save-and-Read action would save it.
  source->setMode(ViewWindowMode::Read);
  QPointer<MarkdownViewer> originalReader(source->findChild<MarkdownViewer *>());
  QVERIFY(originalReader && waitForRender(originalReader, QStringLiteral("Source reader")));
  QCOMPARE(source->getMode(), ViewWindowMode::Read);
  QCOMPARE(readBytes(f->absolute(QStringLiteral("a.md"))), sourceBytes);

  Buffer2 duplicate = a;
  QPointer<ViewWindow2> oldDuplicate(source);
  if (!sourceDuplicate) {
    duplicate = f->open(QStringLiteral("b.md"), true);
    QVERIFY(duplicate.isValid());
    oldDuplicate = f->current();
    auto *editor = oldDuplicate->findChild<MarkdownEditor *>();
    QVERIFY(editor);
    editor->getTextEdit()->setFocus();
    QTRY_VERIFY(editor->getTextEdit()->hasFocus());
    appendDraft(editor->getTextEdit(), destinationDraft);
    QVERIFY(oldDuplicate->isModified());
    f->area->setCurrentBuffer(main, a.id(), true);
    QTRY_COMPARE(f->current(), source.data());
  }
  const auto oldId = oldDuplicate->getViewWindowId();
  const auto sourceGeneration = source->getLinkOpenSettings().m_previewSourceGeneration;
  const QString duplicateName = sourceDuplicate ? QStringLiteral("a.md") : QStringLiteral("b.md");
  const QByteArray originalBytes = sourceDuplicate ? sourceBytes : duplicateBytes;
  const auto initialMainOrder = f->order(main);
  f->reuse();

  const auto early = f->preview(source, f->node(QStringLiteral("early.txt")));
  QVERIFY(early.isValid());
  const auto preview = f->workspace();
  QPointer<ViewWindow2> earlyWindow(f->current());
  const auto earlyId = earlyWindow->getViewWindowId();
  QPointer<LinkPreviewOverlay> overlay(source->findChild<LinkPreviewOverlay *>());
  QVERIFY(overlay && overlay->isVisible());
  auto *previewSplit = overlay->getViewSplit();
  QCOMPARE(f->preview(earlyWindow, duplicate.nodeId()).id(), duplicate.id());
  QPointer<ViewWindow2> incoming(f->current());
  QVERIFY(incoming && incoming != oldDuplicate);
  const auto incomingId = incoming->getViewWindowId();
  QPointer<MarkdownEditor> incomingEditor(incoming->findChild<MarkdownEditor *>());
  QVERIFY(incomingEditor);
  auto *incomingEdit = incomingEditor->getTextEdit();
  incomingEdit->setFocus();
  QTRY_VERIFY(incomingEdit->hasFocus());
  appendDraft(incomingEdit, QStringLiteral("\nIncoming preview draft must survive\n"));
  auto cursor = incomingEdit->textCursor();
  cursor.setPosition(3);
  cursor.setPosition(9, QTextCursor::KeepAnchor);
  incomingEdit->setTextCursor(cursor);
  QPointer<MarkdownViewer> incomingReader;
  if (sourceDuplicate) {
    // Both possible owners are real native readers. Promotion must preserve this mode,
    // rather than recreating the incoming document as an ordinary editable tab.
    incoming->setMode(ViewWindowMode::Read);
    incomingReader = incoming->findChild<MarkdownViewer *>();
    QVERIFY(incomingReader &&
            waitForRender(incomingReader, QStringLiteral("Incoming preview draft must survive")));
  }
  const auto remaining = f->preview(incoming, f->node(QStringLiteral("remaining.txt")));
  QVERIFY(remaining.isValid());
  QPointer<ViewWindow2> remainingWindow(f->current());
  const auto remainingId = remainingWindow->getViewWindowId();
  auto *remainingEditor = remainingWindow->findChild<TextEditor *>();
  QVERIFY(remainingEditor);
  auto *remainingEdit = remainingEditor->getTextEdit();
  remainingEdit->setFocus();
  QTRY_VERIFY(remainingEdit->hasFocus());
  QCOMPARE(f->order(preview), QStringList({early.id(), duplicate.id(), remaining.id()}));
  QCOMPARE(previewSplit->indexOf(incoming), 1);
  QCOMPARE(previewSplit->indexOf(remainingWindow), 2);

  const auto document = incomingEdit->document();
  const auto draft = incomingEditor->getText();
  const int undoSteps = document->availableUndoSteps();
  const auto savedCursor = incomingEdit->textCursor();
  const auto incomingMode = incoming->getMode();
  const auto oldDraft = oldDuplicate->getLatestContent();
  QVERIFY(oldDraft.contains(destinationDraft));
  QVERIFY(draft.contains(QStringLiteral("Incoming preview draft must survive")));
  QVERIFY(undoSteps > 0);
  QVERIFY(oldDuplicate->isModified());
  const auto readerSize = originalReader->size();
  const auto originalPage = originalReader->page();
  const auto promotedPage = incomingReader ? incomingReader->page() : nullptr;

  int vetoes = 0;
  int closeChecks = 0;
  QVector<ID> committed;
  const int beforeMove = f->hooks->addAction<ViewWindowMoveEvent>(
      HookNames::ViewWindowBeforeMove, [&](HookContext &context, const ViewWindowMoveEvent &event) {
        if (event.windowId == remainingId) {
          ++vetoes;
          context.cancel();
        }
      });
  const int afterMove = f->hooks->addAction<ViewWindowMoveEvent>(
      HookNames::ViewWindowAfterMove, [&](HookContext &, const ViewWindowMoveEvent &event) {
        if (event.srcWorkspaceId == preview)
          committed.append(event.windowId);
      });
  const int beforeClose = f->hooks->addAction<ViewWindowCloseEvent>(
      HookNames::ViewWindowBeforeClose, [&](HookContext &, const ViewWindowCloseEvent &event) {
        if (event.windowId == oldId)
          ++closeChecks;
      });
  const auto removeHooks = qScopeGuard([&] {
    f->hooks->removeAction(beforeMove);
    f->hooks->removeAction(afterMove);
    f->hooks->removeAction(beforeClose);
  });
  bool sawConsent = false;
  DialogAnswer dialog(static_cast<QMessageBox::StandardButton>(answer), [&] {
    sawConsent = true;
    QVERIFY(oldDuplicate && incoming && overlay);
    QVERIFY(f->area->getViewWindowIdsForWorkspace(main).contains(incomingId));
    QVERIFY(!f->area->getViewWindowIdsForWorkspace(preview).contains(incomingId));
    // During consent both workspace registrations protect the shared buffer.
    QVERIFY(f->order(main).contains(duplicate.id()));
    QVERIFY(f->order(preview).contains(duplicate.id()));
    QCOMPARE(overlay->parentWidget(), sourceDuplicate ? incoming.data() : source.data());
    if (sourceDuplicate) {
      QCOMPARE(oldDuplicate->getMode(), ViewWindowMode::Read);
      QCOMPARE(oldDuplicate->findChild<MarkdownViewer *>(), originalReader.data());
      QCOMPARE(f->area->previewWorkspaceForWindow(incomingId), preview);
      QVERIFY(f->area->previewWorkspaceForWindow(sourceId).isEmpty());
    }
  });
  auto *button =
      overlay->findChild<QToolButton *>(QStringLiteral("openLinkPreviewInNewTabsButton"));
  QVERIFY(button && button->isVisible() && button->isEnabled());
  QVERIFY(clickNativeSurface(button, button->rect().center()));
  QVERIFY(dialog.accepted());
  QVERIFY(sawConsent);
  QCOMPARE(dialog.count(), 1);
  QCOMPARE(closeChecks, 1);
  QCOMPARE(vetoes, cancelled ? 0 : 1);
  QCOMPARE(committed, cancelled ? QVector<ID>({earlyId}) : QVector<ID>({earlyId, incomingId}));
  QVERIFY(earlyWindow && incoming && remainingWindow && overlay);
  QVERIFY(!f->controller()->isPreviewWorkspaceClosing(preview));
  QCOMPARE(f->area->findWindowIdByBufferId(main, early.id()), earlyId);
  QVERIFY(mainSplit->getAllViewWindows().contains(earlyWindow.data()));
  QCOMPARE(incoming->getViewWindowId(), incomingId);
  QCOMPARE(incoming->findChild<MarkdownEditor *>(), incomingEditor.data());
  QCOMPARE(incomingEditor->getTextEdit(), incomingEdit);
  QCOMPARE(incomingEdit->document(), document);
  QCOMPARE(incomingEditor->getText(), draft);
  QCOMPARE(document->availableUndoSteps(), undoSteps);
  QCOMPARE(incomingEdit->textCursor().position(), savedCursor.position());
  QCOMPARE(incomingEdit->textCursor().anchor(), savedCursor.anchor());
  QCOMPARE(incoming->getMode(), incomingMode);

  QStringList expectedMain = initialMainOrder;
  expectedMain.append(early.id());
  if (cancelled) {
    QVERIFY(oldDuplicate);
    QCOMPARE(oldDuplicate->getViewWindowId(), oldId);
    // Normal focus can synchronize same-buffer siblings; neither draft may be lost.
    QVERIFY(oldDuplicate->getLatestContent().contains(destinationDraft));
    QVERIFY(oldDuplicate->isModified());
    QCOMPARE(f->area->findWindowIdByBufferId(main, duplicate.id()), oldId);
    QCOMPARE(f->area->findWindowIdByBufferId(preview, duplicate.id()), incomingId);
    // The earlier tab is already committed: rollback index is now zero, not one.
    QCOMPARE(previewSplit->indexOf(incoming), 0);
    QCOMPARE(previewSplit->indexOf(remainingWindow), 1);
    QCOMPARE(f->order(preview), QStringList({duplicate.id(), remaining.id()}));
    QCOMPARE(overlay->parentWidget(), source.data());
    QCOMPARE(f->area->previewWorkspaceForWindow(sourceId), preview);
    QCOMPARE(f->area->previewWorkspaceForWindow(incomingId), preview);
    QCOMPARE(source->getLinkOpenSettings().m_previewSourceGeneration, sourceGeneration);
  } else {
    QTRY_VERIFY(oldDuplicate.isNull());
    expectedMain.removeAll(duplicate.id());
    expectedMain.append(duplicate.id());
    QCOMPARE(f->area->findWindowIdByBufferId(main, duplicate.id()), incomingId);
    QCOMPARE(previewSplit->indexOf(incoming), -1);
    QCOMPARE(previewSplit->indexOf(remainingWindow), 0);
    QCOMPARE(f->order(preview), QStringList({remaining.id()}));
    if (sourceDuplicate) {
      QVERIFY(source.isNull());
      QCOMPARE(overlay->parentWidget(), incoming.data());
      QCOMPARE(f->area->previewWorkspaceForWindow(incomingId), preview);
      QVERIFY(f->area->previewWorkspaceForWindow(sourceId).isEmpty());
    }
  }
  QCOMPARE(f->order(main), expectedMain);
  QCOMPARE(f->current(), remainingWindow.data());
  QCOMPARE(f->coreCurrent(preview), remaining.id());
  QCOMPARE(f->coreCurrent(main), mainSplit->getCurrentViewWindow()->getBuffer().id());
  if (detached) {
    QCOMPARE(f->workspaces->getCurrentWorkspace(), ordinaryMain);
    QCOMPARE(f->order(ordinaryMain), ordinaryOrder);
    QCOMPARE(f->coreCurrent(ordinaryMain), ordinaryCurrent);
  }
  for (const auto &workspace : {main, preview}) {
    QStringList liveOrder;
    const auto infos = f->area->getViewWindowNavInfos(workspace);
    for (const auto &info : infos) {
      QVERIFY(!liveOrder.contains(info.bufferId));
      liveOrder.append(info.bufferId);
      QVERIFY(f->buffers->getBufferHandle(info.bufferId).isValid());
    }
    QCOMPARE(f->order(workspace), liveOrder);
  }
  if (answer == QMessageBox::Save) {
    QVERIFY(readBytes(f->absolute(duplicateName)).contains(destinationDraft.toUtf8()));
  } else {
    QCOMPARE(readBytes(f->absolute(duplicateName)), originalBytes);
  }

  // The remaining preview must receive input over the native reader after either
  // reparenting to the promoted source or rolling back to the original source.
  auto *host = sourceDuplicate && !cancelled ? incoming.data() : source.data();
  auto *coveredReader =
      sourceDuplicate && !cancelled ? incomingReader.data() : originalReader.data();
  QVERIFY(host && coveredReader);
  QCOMPARE(host->getMode(), ViewWindowMode::Read);
  QCOMPARE(coveredReader->page(), sourceDuplicate && !cancelled ? promotedPage : originalPage);
  QTRY_VERIFY(overlay->isVisible() && remainingWindow->isVisible() && coveredReader->isVisible());
  QCOMPARE(overlay->parentWidget(), host);
  QCOMPARE(overlay->getViewSplit(), previewSplit);
  QTRY_VERIFY(!overlay->geometry().isEmpty() && host->contentsRect().contains(overlay->geometry()));
  QVERIFY(!overlay->visibleRegion().isEmpty());
#ifdef Q_OS_WIN
  QVERIFY(overlay->testAttribute(Qt::WA_NativeWindow));
  QVERIFY(overlay->windowHandle() && host->windowHandle());
  QTRY_COMPARE(overlay->windowHandle()->parent(), host->windowHandle());
  QTRY_VERIFY(overlay->windowHandle()->isExposed());
  QCOMPARE(overlay->windowHandle()->mapToGlobal(QPoint()), overlay->mapToGlobal(QPoint()));
  QCOMPARE(overlay->windowHandle()->size(), overlay->size());
#endif
  if (!sourceDuplicate || cancelled) {
    QCOMPARE(originalReader->size(), readerSize);
    QCOMPARE(source->getMode(), ViewWindowMode::Read);
  }
  const auto coveredContent = host->getLatestContent();
  const auto coveredScroll = evaluate(coveredReader->page(), QStringLiteral("scrollY")).toInt();
  QVERIFY(
      clickNativeSurface(remainingEdit->viewport(), remainingEdit->viewport()->rect().center()));
  QTRY_VERIFY(remainingEdit->hasFocus());
  QTest::keyClick(remainingEdit, Qt::Key_End, Qt::ControlModifier);
  QTest::keyClicks(remainingEdit, " Native input after duplicate consent");
  QVERIFY(
      remainingEditor->getText().contains(QStringLiteral("Native input after duplicate consent")));
  QCOMPARE(host->getLatestContent(), coveredContent);
  QCOMPARE(evaluate(coveredReader->page(), QStringLiteral("scrollY")).toInt(), coveredScroll);
  QCOMPARE(f->current(), remainingWindow.data());
  QCOMPARE(f->coreCurrent(preview), remaining.id());
  QCOMPARE(f->order(main), expectedMain);
  QCOMPARE(incomingEdit->document(), document);
  QCOMPARE(incomingEditor->getText(), draft);
  QCOMPARE(document->availableUndoSteps(), undoSteps);
  if (!sourceDuplicate)
    QCOMPARE(readBytes(f->absolute(QStringLiteral("a.md"))), sourceBytes);
}

void TestViewAreaTabReuse::previewOpenInNewTabsProtectedDuplicate() {
  QVERIFY(f->create(QStringLiteral("a.txt"), "Ordinary source\n"));
  const auto secretId =
      f->createProtected(QStringLiteral("secret.md"), "# Protected duplicate\n", true);
  QVERIFY(!secretId.isEmpty());
  const auto a = f->open(QStringLiteral("a.txt"));
  QVERIFY(a.isValid());
  QPointer<ViewWindow2> source(f->current());
  const auto sourceId = source->getViewWindowId();
  const auto main = f->workspace();
  const auto secret = f->openProtected(secretId, true);
  QVERIFY(secret.isValid() && secret.isEncrypted());
  QPointer<ViewWindow2> outgoing(f->current());
  const auto outgoingId = outgoing->getViewWindowId();
  auto *outgoingEditor = outgoing->findChild<MarkdownEditor *>();
  QVERIFY(outgoingEditor);
  outgoingEditor->getTextEdit()->setFocus();
  QTRY_VERIFY(outgoingEditor->getTextEdit()->hasFocus());
  appendDraft(outgoingEditor->getTextEdit(), QStringLiteral("Shared protected draft\n"));
  QVERIFY(outgoing->isModified());
  QVERIFY(f->buffers->syncNow(secret.id()));
  f->controller()->focusWindow(main, a.id());
  QCOMPARE(f->preview(source, secret.nodeId()).id(), secret.id());
  const auto preview = f->workspace();
  QVERIFY(preview != main);
  QPointer<ViewWindow2> incoming(f->current());
  const auto incomingId = incoming->getViewWindowId();
  QVERIFY(incomingId != outgoingId);
  QPointer<LinkPreviewOverlay> overlay(source->findChild<LinkPreviewOverlay *>());
  QVERIFY(overlay);
  auto *editor = incoming->findChild<MarkdownEditor *>();
  QVERIFY(editor);
  auto *document = editor->getTextEdit()->document();
  const auto draft = incoming->getLatestContent();
  QVERIFY(draft.contains(QStringLiteral("Shared protected draft")));
  QCOMPARE(f->order(main), QStringList({a.id(), secret.id()}));
  QCOMPARE(f->order(preview), QStringList({secret.id()}));

  int closeChecks = 0;
  const int hook = f->hooks->addAction<ViewWindowCloseEvent>(
      HookNames::ViewWindowBeforeClose, [&](HookContext &, const ViewWindowCloseEvent &event) {
        if (event.windowId == outgoingId)
          ++closeChecks;
      });
  const auto removeHook = qScopeGuard([&] { f->hooks->removeAction(hook); });
  DialogAnswer discard(QMessageBox::Discard);
  QVERIFY(f->controller()->openPreviewInNewTabs(preview, main));
  QVERIFY(discard.accepted());
  QCOMPARE(closeChecks, 1);
  // The protected close completes on QObject destruction, after promotion has
  // already removed the preview's ownership of this shared buffer.
  QVERIFY(outgoing);
  QVERIFY(incoming);
  QCOMPARE(f->area->findWindowIdByBufferId(main, secret.id()), incomingId);
  QCOMPARE(f->order(main), QStringList({a.id(), secret.id()}));
  QVERIFY(f->workspaces->getWorkspace(preview).isEmpty());
  QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
  QTRY_VERIFY(outgoing.isNull());
  QTRY_VERIFY(overlay.isNull());
  QVERIFY(incoming && source);
  QCOMPARE(incoming->getViewWindowId(), incomingId);
  QCOMPARE(incoming->findChild<MarkdownEditor *>(), editor);
  QCOMPARE(editor->getTextEdit()->document(), document);
  QCOMPARE(incoming->getLatestContent(), draft);
  QCOMPARE(f->current(), incoming.data());
  QCOMPARE(f->coreCurrent(main), secret.id());
  QCOMPARE(f->order(main), QStringList({a.id(), secret.id()}));
  QCOMPARE(f->area->getViewWindowIdsForWorkspace(main).size(), 2);
  QVERIFY(f->buffers->getBufferHandle(secret.id()).isValid());
  QCOMPARE(f->buffers->listBuffers().size(), 2);
  QCOMPARE(source->getViewWindowId(), sourceId);
  QCOMPARE(source->getLatestContent(), QStringLiteral("Ordinary source\n"));

  editor->getTextEdit()->setFocus();
  QTRY_VERIFY(editor->getTextEdit()->hasFocus());
  appendDraft(editor->getTextEdit(), QStringLiteral("Saved after protected retirement\n"));
  const auto saved = incoming->getLatestContent();
  QVERIFY(incoming->save());
  QTRY_VERIFY_WITH_TIMEOUT(!f->buffers->isSaveQueueBusy(secret.id()) && !incoming->isModified(),
                           10000);
  QCOMPARE(incoming->getBuffer().getContentRaw(), saved.toUtf8());
  QVERIFY(f->controller()->closeViewWindow(incomingId, false));
  QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
  QTRY_VERIFY(incoming.isNull());
  QCOMPARE(f->order(main), QStringList({a.id()}));
  QCOMPARE(f->area->findWindowIdByBufferId(main, secret.id()),
           ViewAreaController::InvalidViewWindowId);
  QVERIFY(!f->buffers->getBufferHandle(secret.id()).isValid());
  QCOMPARE(f->buffers->listBuffers().size(), 1);
  QCOMPARE(closeChecks, 1);
}

void TestViewAreaTabReuse::previewOpenInNewTabsReentrancy() {
  for (const auto &name : {QStringLiteral("a.txt"), QStringLiteral("b.txt"),
                           QStringLiteral("c.txt"), QStringLiteral("other-source.txt"),
                           QStringLiteral("other-preview.txt"), QStringLiteral("busy.txt")})
    QVERIFY(f->create(name, name.toUtf8()));
  const auto a = f->open(QStringLiteral("a.txt"));
  QVERIFY(a.isValid());
  QPointer<ViewWindow2> source(f->current());
  const auto sourceId = source->getViewWindowId();
  const auto main = f->workspace();
  const auto b = f->open(QStringLiteral("b.txt"), true);
  QVERIFY(b.isValid());
  QPointer<ViewWindow2> outgoing(f->current());
  const auto outgoingId = outgoing->getViewWindowId();

  // Use a different preview destination, not merely the equal-ID rejection.
  const auto otherSourceBuffer = f->open(QStringLiteral("other-source.txt"), true);
  QVERIFY(otherSourceBuffer.isValid());
  QPointer<ViewWindow2> otherSource(f->current());
  const auto otherSourceId = otherSource->getViewWindowId();
  const auto otherBuffer = f->preview(otherSource, f->node(QStringLiteral("other-preview.txt")));
  QVERIFY(otherBuffer.isValid());
  const auto otherPreview = f->workspace();
  const auto otherWindowId = f->current()->getViewWindowId();
  f->controller()->focusWindow(main, a.id());
  const auto sourceSettings = source->getLinkOpenSettings();
  QCOMPARE(f->preview(source, b.nodeId()).id(), b.id());
  const auto preview = f->workspace();
  QVERIFY(preview != main && preview != otherPreview);
  QPointer<ViewWindow2> incomingB(f->current());
  const auto bId = incomingB->getViewWindowId();
  QVERIFY(bId != outgoingId);
  const auto c = f->preview(incomingB, f->node(QStringLiteral("c.txt")));
  QVERIFY(c.isValid());
  QPointer<ViewWindow2> incomingC(f->current());
  const auto cId = incomingC->getViewWindowId();
  QPointer<LinkPreviewOverlay> overlay(source->findChild<LinkPreviewOverlay *>());
  QVERIFY(overlay);
  auto *editor = incomingC->findChild<TextEditor *>();
  QVERIFY(editor);
  editor->getTextEdit()->setFocus();
  QTRY_VERIFY(editor->getTextEdit()->hasFocus());
  appendDraft(editor->getTextEdit(), QStringLiteral(" preserved through hooks"));
  const auto draft = incomingC->getLatestContent();
  const auto bText = incomingB->getLatestContent();
  const auto mainOrder = f->order(main);
  const auto previewOrder = f->order(preview);
  const auto otherOrder = f->order(otherPreview);
  const auto mainCurrent = f->coreCurrent(main);
  const auto previewCurrent = f->coreCurrent(preview);
  const auto otherCurrent = f->coreCurrent(otherPreview);
  const auto bufferCount = f->buffers->listBuffers().size();
  const auto workspaceCount = f->workspaces->listWorkspaces().size();
  QVERIFY(!f->controller()->openPreviewInNewTabs(preview, QStringLiteral("missing-workspace")));
  QVERIFY(!f->controller()->openPreviewInNewTabs(preview, otherPreview));
  QVERIFY(!f->controller()->openPreviewInNewTabs(preview, preview));
  QCOMPARE(f->order(main), mainOrder);
  QCOMPARE(f->order(preview), previewOrder);
  QCOMPARE(f->order(otherPreview), otherOrder);
  QCOMPARE(f->coreCurrent(main), mainCurrent);
  QCOMPARE(f->coreCurrent(preview), previewCurrent);
  QCOMPARE(f->coreCurrent(otherPreview), otherCurrent);
  QCOMPARE(f->buffers->listBuffers().size(), bufferCount);
  QCOMPARE(f->workspaces->listWorkspaces().size(), workspaceCount);
  QCOMPARE(f->area->findWindowIdByBufferId(otherPreview, otherBuffer.id()), otherWindowId);
  QCOMPARE(f->area->findWindowIdByBufferId(preview, b.id()), bId);
  QCOMPARE(f->area->findWindowIdByBufferId(preview, c.id()), cId);
  QCOMPARE(incomingC->getLatestContent(), draft);
  QVERIFY(f->controller()->closePreviewWorkspace(otherPreview));
  QVERIFY(f->controller()->closeViewWindow(otherSourceId, false));
  QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
  QVERIFY(otherSource.isNull());
  f->area->setCurrentBuffer(preview, c.id(), true);
  QTRY_COMPARE(f->current(), incomingC.data());
  QCOMPARE(f->order(main), QStringList({a.id(), b.id()}));
  QCOMPARE(f->buffers->listBuffers().size(), 3);

  QVector<quint64> beforeMoves;
  QVector<quint64> afterMoves;
  int duplicateCloseChecks = 0;
  bool veto = true;
  bool exercisedGuards = false;
  bool removedDuplicate = false;
  FileOpenSettings busy;
  busy.m_mode = ViewWindowMode::Edit;
  busy.m_targetWorkspaceId = preview;
  busy.m_focus = false;
  const int before = f->hooks->addAction<ViewWindowMoveEvent>(
      HookNames::ViewWindowBeforeMove, [&](HookContext &ctx, const ViewWindowMoveEvent &event) {
        beforeMoves.append(event.windowId);
        QCOMPARE(event.srcWorkspaceId, preview);
        QCOMPARE(event.dstWorkspaceId, main);
        QCOMPARE(event.bufferId, event.windowId == bId ? b.id() : c.id());
        if (!veto)
          return;
        ctx.cancel();
        exercisedGuards = true;
        QVERIFY(f->controller()->isPreviewWorkspaceClosing(preview));
        QVERIFY(!f->controller()->openPreviewInNewTabs(preview, main));
        QVERIFY(!f->controller()->closePreviewWorkspace(preview));
        QVERIFY(!f->controller()->closeViewWindow(sourceId, true));
        QVERIFY(!f->controller()->closeViewWindow(bId, true));
        QVERIFY(!f->controller()->closeViewWindow(cId, true));
        f->controller()->moveViewWindowOneSplit(preview, bId, Direction::Right, main, b.id());
        f->buffers->openBuffer(f->node(QStringLiteral("busy.txt")), sourceSettings);
        f->buffers->openBuffer(f->node(QStringLiteral("busy.txt")), busy);
        QCOMPARE(f->order(main), QStringList({a.id(), b.id()}));
        QCOMPARE(f->order(preview), QStringList({b.id(), c.id()}));
        QCOMPARE(f->area->findWindowIdByBufferId(main, b.id()), outgoingId);
        QCOMPARE(f->area->findWindowIdByBufferId(preview, b.id()), bId);
        QCOMPARE(f->area->findWindowIdByBufferId(preview, c.id()), cId);
        QCOMPARE(f->buffers->listBuffers().size(), 3);
        QCOMPARE(f->workspaces->listWorkspaces().size(), 2);
        QVERIFY(source && incomingB && incomingC && overlay);
        QCOMPARE(source->getBuffer().id(), a.id());
        QCOMPARE(incomingB->getLatestContent(), bText);
        QCOMPARE(incomingC->getLatestContent(), draft);
      });
  const int after = f->hooks->addAction<ViewWindowMoveEvent>(
      HookNames::ViewWindowAfterMove, [&](HookContext &, const ViewWindowMoveEvent &event) {
        afterMoves.append(event.windowId);
        QCOMPARE(event.srcWorkspaceId, preview);
        QCOMPARE(event.dstWorkspaceId, main);
        QCOMPARE(f->area->findWindowIdByBufferId(main, event.bufferId), event.windowId);
        QVERIFY(f->order(main).contains(event.bufferId));
        QVERIFY(!f->order(preview).contains(event.bufferId));
        QVERIFY(f->buffers->getBufferHandle(event.bufferId).isValid());
      });
  const int close = f->hooks->addAction<ViewWindowCloseEvent>(
      HookNames::ViewWindowBeforeClose, [&](HookContext &, const ViewWindowCloseEvent &event) {
        if (event.windowId != outgoingId)
          return;
        ++duplicateCloseChecks;
        QVERIFY(!veto);
        QVERIFY(!f->controller()->closeViewWindow(outgoingId, false));
        // B is already appended when the close hook runs. Remove the old view
        // directly: the controller's stale close ID will then return false,
        // but the transfer must recognize that no conflict remains.
        QVERIFY(f->area->getViewWindowIdsForWorkspace(main).contains(bId));
        QVERIFY(f->area->closeViewWindow(outgoingId, false));
        removedDuplicate = true;
        QCOMPARE(f->area->findWindowIdByBufferId(main, b.id()), bId);
        QVERIFY(f->order(main).contains(b.id()));
        QVERIFY(f->buffers->getBufferHandle(b.id()).isValid());
      });
  const auto removeHooks = qScopeGuard([&] {
    f->hooks->removeAction(before);
    f->hooks->removeAction(after);
    f->hooks->removeAction(close);
  });

  QVERIFY(!f->controller()->openPreviewInNewTabs(preview, main));
  QVERIFY(exercisedGuards);
  QCOMPARE(beforeMoves, QVector<quint64>({bId}));
  QVERIFY(afterMoves.isEmpty());
  QCOMPARE(duplicateCloseChecks, 0);
  QVERIFY(!removedDuplicate);
  QVERIFY(!f->controller()->isPreviewWorkspaceClosing(preview));
  QVERIFY(f->area->isPreviewWorkspaceAvailable(preview));
  QCOMPARE(f->order(main), QStringList({a.id(), b.id()}));
  QCOMPARE(f->order(preview), QStringList({b.id(), c.id()}));
  QCOMPARE(f->current(), incomingC.data());
  QCOMPARE(f->coreCurrent(preview), c.id());
  QCOMPARE(f->area->previewWorkspaceForWindow(sourceId), preview);
  QCOMPARE(overlay->parentWidget(), source.data());
  QCOMPARE(incomingC->getLatestContent(), draft);

  veto = false;
  QVERIFY(f->controller()->openPreviewInNewTabs(preview, main));
  QVERIFY(removedDuplicate);
  QCOMPARE(duplicateCloseChecks, 1);
  QCOMPARE(beforeMoves, QVector<quint64>({bId, bId, cId}));
  QCOMPARE(afterMoves, QVector<quint64>({bId, cId}));
  QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
  QTRY_VERIFY(outgoing.isNull());
  QTRY_VERIFY(overlay.isNull());
  QVERIFY(source && incomingB && incomingC);
  QCOMPARE(f->order(main), QStringList({a.id(), b.id(), c.id()}));
  QCOMPARE(f->area->getViewWindowIdsForWorkspace(main).size(), 3);
  const auto infos = f->area->getViewWindowNavInfos(main);
  QCOMPARE(infos.size(), 3);
  QCOMPARE(infos.at(0).windowId, sourceId);
  QCOMPARE(infos.at(1).windowId, bId);
  QCOMPARE(infos.at(2).windowId, cId);
  for (const auto &info : infos) {
    QCOMPARE(f->order(main).count(info.bufferId), 1);
    QVERIFY(f->buffers->getBufferHandle(info.bufferId).isValid());
  }
  QCOMPARE(f->buffers->listBuffers().size(), 3);
  QCOMPARE(f->workspaces->listWorkspaces().size(), 1);
  QVERIFY(f->workspaces->getWorkspace(preview).isEmpty());
  QVERIFY(f->area->previewWorkspaceForWindow(sourceId).isEmpty());
  QCOMPARE(f->current(), incomingC.data());
  QCOMPARE(f->coreCurrent(main), c.id());
  QCOMPARE(source->getLatestContent(), QStringLiteral("a.txt"));
  QCOMPARE(incomingB->getLatestContent(), bText);
  QCOMPARE(incomingC->findChild<TextEditor *>(), editor);
  QCOMPARE(incomingC->getLatestContent(), draft);
  QVERIFY(incomingC->isModified());
}

void TestViewAreaTabReuse::previewCloseConsent() {
  QVERIFY(f->create(QStringLiteral("a.txt"), "Source A\n"));
  QVERIFY(f->create(QStringLiteral("b.txt"), "Preview B\n"));
  const auto exercise = [&](QMessageBox::StandardButton answer) {
    const auto a = f->open(QStringLiteral("a.txt"));
    QVERIFY(a.isValid());
    auto *source = f->current();
    const auto main = f->workspace();
    const auto b = f->preview(source, f->node(QStringLiteral("b.txt")));
    QVERIFY(b.isValid());
    const auto preview = f->workspace();
    QPointer<ViewWindow2> window(f->current());
    QPointer<LinkPreviewOverlay> overlay(source->findChild<LinkPreviewOverlay *>());
    QVERIFY(overlay);
    auto *editor = window->findChild<TextEditor *>();
    QVERIFY(editor);
    editor->getTextEdit()->setFocus();
    QTRY_VERIFY(editor->getTextEdit()->hasFocus());
    appendDraft(editor->getTextEdit(), QStringLiteral("Unsaved B\n"));
    const auto draft = window->getLatestContent();
    QVERIFY(window->isModified());
    auto *close = overlay->findChild<QAction *>(QStringLiteral("closeLinkPreviewAction"));
    QVERIFY(close);
    DialogAnswer dialog(static_cast<QMessageBox::StandardButton>(answer));
    close->trigger();
    QVERIFY(dialog.accepted());
    QCOMPARE(dialog.count(), 1);
    if (answer == QMessageBox::Cancel) {
      QVERIFY(window && overlay && overlay->isVisible());
      QVERIFY(!f->controller()->isPreviewWorkspaceClosing(preview));
      QCOMPARE(window->getLatestContent(), draft);
      QVERIFY(window->isEnabled() && window->isModified());
      QCOMPARE(f->order(preview), QStringList({b.id()}));
      QCOMPARE(f->current(), window.data());
      DialogAnswer discard(QMessageBox::Discard);
      QVERIFY(f->controller()->closePreviewWorkspace(preview));
      QVERIFY(discard.accepted());
    }
    QTRY_VERIFY(window.isNull());
    QTRY_VERIFY(overlay.isNull());
    QVERIFY(f->workspaces->getWorkspace(preview).isEmpty());
    QVERIFY(!f->buffers->getBufferHandle(b.id()).isValid());
    QCOMPARE(f->workspace(), main);
    QCOMPARE(f->current(), source);
    QCOMPARE(f->order(main), QStringList({a.id()}));
    QTRY_COMPARE(readBytes(f->absolute(QStringLiteral("b.txt"))),
                 answer == QMessageBox::Save ? draft.toUtf8() : QByteArray("Preview B\n"));
    QCOMPARE(readBytes(f->absolute(QStringLiteral("a.txt"))), QByteArray("Source A\n"));
    f->controller()->openLastClosedFile();
    QCOMPARE(f->current(), source);
    QCOMPARE(f->order(main), QStringList({a.id()}));
    QCOMPARE(f->buffers->listBuffers().size(), 1);
  };
  for (const auto answer : {QMessageBox::Cancel, QMessageBox::Save, QMessageBox::Discard}) {
    QVERIFY(writeBytes(f->absolute(QStringLiteral("b.txt")), "Preview B\n"));
    exercise(answer);
    if (QTest::currentTestFailed())
      return;
  }
}

void TestViewAreaTabReuse::previewSourceCancellation() {
  QVERIFY(f->create(QStringLiteral("a.txt"), "Source A\n"));
  QVERIFY(f->create(QStringLiteral("b.txt"), "Preview B\n"));
  QVERIFY(f->create(QStringLiteral("c.txt"), "Replacement C\n"));
  const auto exercise = [&](const QString &operation) {
    const auto a = f->open(QStringLiteral("a.txt"));
    QVERIFY(a.isValid());
    f->reuse();
    QPointer<ViewWindow2> source(f->current());
    const auto main = f->workspace();
    QPointer<DetachedWindow> detached;
    if (operation == QLatin1String("detach")) {
      QVERIFY(f->controller()->detachViewWindow(main, source->getViewWindowId(), a.id()));
      detached = qobject_cast<DetachedWindow *>(source->window());
      QVERIFY(detached);
      detached->show();
      QTRY_VERIFY(source->isVisible());
      QTRY_VERIFY(f->area->getAllViewSplits().first()->getCurrentViewWindow());
      QCOMPARE(
          f->area->getAllViewSplits().first()->getCurrentViewWindow()->getNodeId().relativePath,
          QStringLiteral("vx://home"));
      detached->activateWindow();
      QTRY_VERIFY(detached->isActiveWindow());
    }
    const auto sourceSettings = source->getLinkOpenSettings();
    const auto b = f->preview(source, f->node(QStringLiteral("b.txt")));
    QVERIFY(b.isValid());
    const auto preview = f->area->previewWorkspaceForWindow(source->getViewWindowId());
    QVERIFY(!preview.isEmpty());
    QPointer<LinkPreviewOverlay> overlay(source->findChild<LinkPreviewOverlay *>());
    QVERIFY(overlay);
    auto *window = overlay->getViewSplit()->getCurrentViewWindow();
    auto *editor = window->findChild<TextEditor *>();
    QVERIFY(editor);
    editor->getTextEdit()->setFocus();
    QTRY_VERIFY(editor->getTextEdit()->hasFocus());
    appendDraft(editor->getTextEdit(), QStringLiteral("Keep this preview draft\n"));
    const auto draft = window->getLatestContent();
    if (operation == QLatin1String("hidden")) {
      f->controller()->newWorkspace(main, QStringLiteral("Visible while source hidden"));
      QVERIFY(!source->isVisible());
      QVERIFY(!overlay->isVisible());
    }
    DialogAnswer dialog(QMessageBox::Cancel);
    if (operation == QLatin1String("replace")) {
      f->open(QStringLiteral("c.txt"));
    } else if (operation == QLatin1String("detach")) {
      detached->close();
    } else if (operation == QLatin1String("quit")) {
      QVERIFY(!f->controller()->closeAllBuffersForQuit());
    } else {
      QVERIFY(!f->controller()->closeViewWindow(source->getViewWindowId(), true));
    }
    QVERIFY(dialog.accepted());
    QCOMPARE(dialog.count(), 1);
    QVERIFY(source && overlay);
    QCOMPARE(source->getBuffer().id(), a.id());
    QCOMPARE(source->getLinkOpenSettings().m_previewSourceGeneration,
             sourceSettings.m_previewSourceGeneration);
    QCOMPARE(source->getLatestContent(), QStringLiteral("Source A\n"));
    QCOMPARE(window->getLatestContent(), draft);
    QVERIFY(window->isEnabled());
    QCOMPARE(f->order(preview), QStringList({b.id()}));
    QVERIFY(!f->controller()->isPreviewWorkspaceClosing(preview));
    QVERIFY(f->controller()->shouldPropagateToCore());
    if (detached)
      QVERIFY(detached->isVisible());
    if (operation == QLatin1String("replace")) {
      QCOMPARE(f->buffers->listBuffers().size(), 2);
      QCOMPARE(f->order(main), QStringList({a.id()}));
    }
    DialogAnswer save(QMessageBox::Save);
    QVERIFY(f->controller()->closePreviewWorkspace(preview));
    QVERIFY(save.accepted());
    QTRY_COMPARE(readBytes(f->absolute(QStringLiteral("b.txt"))), draft.toUtf8());
    if (operation == QLatin1String("replace")) {
      const auto c = f->open(QStringLiteral("c.txt"));
      QVERIFY(c.isValid());
      QCOMPARE(f->current(), source.data());
      QCOMPARE(source->getBuffer().id(), c.id());
      QVERIFY(source->getLinkOpenSettings().m_previewSourceGeneration >
              sourceSettings.m_previewSourceGeneration);
    }
    QVERIFY(f->controller()->closeViewWindow(source->getViewWindowId(), true));
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
  };
  for (const auto &operation :
       {QStringLiteral("force"), QStringLiteral("replace"), QStringLiteral("detach"),
        QStringLiteral("hidden"), QStringLiteral("quit")}) {
    QVERIFY(writeBytes(f->absolute(QStringLiteral("b.txt")), "Preview B\n"));
    exercise(operation);
    if (QTest::currentTestFailed())
      return;
    QVERIFY(f->controller()->closeAll(f->area->getVisibleWorkspaceIds().toVector(), true));
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QCOMPARE(f->buffers->listBuffers().size(), 0);
  }
}

void TestViewAreaTabReuse::previewSharedOwnership_data() {
  QTest::addColumn<QString>("ownerKind");
  QTest::newRow("ordinary-visible-owner") << QStringLiteral("main");
  QTest::newRow("ordinary-hidden-owner") << QStringLiteral("hidden");
  QTest::newRow("detached-owner") << QStringLiteral("detached");
  QTest::newRow("another-preview-owner") << QStringLiteral("preview");
}

void TestViewAreaTabReuse::previewSharedOwnership() {
  QFETCH(QString, ownerKind);
  QVERIFY(f->create(QStringLiteral("a.txt"), "Source A\n"));
  QVERIFY(f->create(QStringLiteral("b.txt"), "Shared B\n"));
  QVERIFY(f->create(QStringLiteral("owner.txt"), "Other preview source\n"));
  Buffer2 b;
  if (ownerKind == QLatin1String("preview")) {
    QVERIFY(f->open(QStringLiteral("owner.txt")).isValid());
    b = f->preview(f->current(), f->node(QStringLiteral("b.txt")));
  } else {
    b = f->open(QStringLiteral("b.txt"));
  }
  QVERIFY(b.isValid());
  QPointer<ViewWindow2> owner(f->current());
  QString ownerWorkspace = f->workspace();
  const auto ownerId = owner->getViewWindowId();
  if (ownerKind == QLatin1String("hidden")) {
    f->controller()->newWorkspace(ownerWorkspace, QStringLiteral("Source workspace"));
  } else if (ownerKind == QLatin1String("detached")) {
    QVERIFY(f->controller()->detachViewWindow(ownerWorkspace, ownerId, b.id()));
    auto *host = qobject_cast<DetachedWindow *>(owner->window());
    QVERIFY(host);
    ownerWorkspace = host->getWorkspaceId();
  }
  const auto a = f->open(QStringLiteral("a.txt"), true);
  QVERIFY(a.isValid());
  auto *source = f->current();
  const auto main = f->workspace();
  QCOMPARE(f->preview(source, b.nodeId()).id(), b.id());
  const auto preview = f->workspace();
  QVERIFY(preview != ownerWorkspace);
  auto *window = f->current();
  QVERIFY(window != owner);
  auto *editor = window->findChild<TextEditor *>();
  QVERIFY(editor);
  editor->getTextEdit()->setFocus();
  QTRY_VERIFY(editor->getTextEdit()->hasFocus());
  appendDraft(editor->getTextEdit(), QStringLiteral("Shared in-memory edit\n"));
  QVERIFY(f->buffers->syncNow(b.id()));
  QVERIFY(b.getContentRaw().contains("Shared in-memory edit"));
  DialogAnswer discard(QMessageBox::Discard);
  QVERIFY(f->controller()->closePreviewWorkspace(preview));
  QVERIFY(discard.accepted());
  QVERIFY(owner);
  QVERIFY(owner->getBuffer().isValid());
  QVERIFY(f->buffers->getBufferHandle(b.id()).isValid());
  QVERIFY(f->order(ownerWorkspace).contains(b.id()));
  QCOMPARE(owner->getViewWindowId(), ownerId);
  if (ownerKind != QLatin1String("hidden"))
    QCOMPARE(f->area->findWindowIdByBufferId(ownerWorkspace, b.id()), ownerId);
  QVERIFY(b.getContentRaw().contains("Shared in-memory edit"));
  QCOMPARE(readBytes(f->absolute(QStringLiteral("b.txt"))), QByteArray("Shared B\n"));
  QCOMPARE(f->workspace(), main);
  QCOMPARE(source->getBuffer().id(), a.id());
  QVERIFY(f->workspaces->getWorkspace(preview).isEmpty());
}

void TestViewAreaTabReuse::previewCloseReentrancyAndPartialCancellation() {
  for (const auto &name :
       {QStringLiteral("a.txt"), QStringLiteral("b.txt"), QStringLiteral("c.txt"),
        QStringLiteral("d.txt"), QStringLiteral("reentrant.txt")})
    QVERIFY(f->create(name, name.toUtf8()));
  const auto a = f->open(QStringLiteral("a.txt"));
  QVERIFY(a.isValid());
  auto *source = f->current();
  const auto main = f->workspace();
  const auto sourceSettings = source->getLinkOpenSettings();
  const auto b = f->preview(source, f->node(QStringLiteral("b.txt")));
  const auto preview = f->workspace();
  const auto c = f->preview(f->current(), f->node(QStringLiteral("c.txt")));
  QVERIFY(b.isValid() && c.isValid());
  auto *dirty = f->current();
  const auto dirtyId = dirty->getViewWindowId();
  auto *editor = dirty->findChild<TextEditor *>();
  QVERIFY(editor);
  editor->getTextEdit()->setFocus();
  QTRY_VERIFY(editor->getTextEdit()->hasFocus());
  appendDraft(editor->getTextEdit(), QStringLiteral(" draft awaiting consent"));
  const auto draft = dirty->getLatestContent();
  FileOpenSettings background;
  background.m_targetWorkspaceId = preview;
  background.m_focus = false;
  const auto d = f->buffers->openBuffer(f->node(QStringLiteral("d.txt")), background);
  QVERIFY(d.isValid());
  const auto dId = f->area->findWindowIdByBufferId(preview, d.id());
  QCOMPARE(f->order(preview), QStringList({b.id(), c.id(), d.id()}));
  bool checkedReentry = false;
  DialogAnswer cancel(QMessageBox::Cancel, [&] {
    checkedReentry = true;
    QVERIFY(f->controller()->isPreviewWorkspaceClosing(preview));
    QVERIFY(!f->controller()->closePreviewWorkspace(preview));
    QVERIFY(!f->controller()->closeViewWindow(source->getViewWindowId(), true));
    f->buffers->openBuffer(f->node(QStringLiteral("reentrant.txt")), sourceSettings);
    f->buffers->openBuffer(f->node(QStringLiteral("reentrant.txt")), background);
    QCOMPARE(f->order(preview), QStringList({c.id(), d.id()}));
    QCOMPARE(f->buffers->listBuffers().size(), 3);
    QCOMPARE(source->getBuffer().id(), a.id());
    QVERIFY(dirty->isEnabled());
  });
  QVERIFY(!f->controller()->closePreviewWorkspace(preview));
  QVERIFY(cancel.accepted());
  QVERIFY(checkedReentry);
  QCOMPARE(cancel.count(), 1);
  QVERIFY(!f->controller()->isPreviewWorkspaceClosing(preview));
  QCOMPARE(f->order(preview), QStringList({c.id(), d.id()}));
  QCOMPARE(f->area->findWindowIdByBufferId(preview, c.id()), dirtyId);
  QCOMPARE(f->area->findWindowIdByBufferId(preview, d.id()), dId);
  QCOMPARE(dirty->getLatestContent(), draft);
  QVERIFY(dirty->isModified() && dirty->isEnabled());
  QVERIFY(!f->buffers->getBufferHandle(b.id()).isValid());
  DialogAnswer save(QMessageBox::Save);
  QVERIFY(f->controller()->closeAllPreviewWorkspaces());
  QVERIFY(save.accepted());
  QTRY_COMPARE(readBytes(f->absolute(QStringLiteral("c.txt"))), draft.toUtf8());
  QVERIFY(f->workspaces->getWorkspace(preview).isEmpty());
  QCOMPARE(f->order(main), QStringList({a.id()}));
  f->controller()->openLastClosedFile();
  QCOMPARE(f->order(main), QStringList({a.id()}));
  QCOMPARE(f->buffers->listBuffers().size(), 1);
}

void TestViewAreaTabReuse::previewProtectedAcceptedCloseThenSourceReplacement() {
  QVERIFY(f->create(QStringLiteral("a.txt"), "Source A\n"));
  QVERIFY(f->create(QStringLiteral("next.txt"), "Replacement generation\n"));
  const auto secretId = f->createProtected(QStringLiteral("secret.md"), "# Secret preview\n", true);
  QVERIFY(!secretId.isEmpty());
  const auto a = f->open(QStringLiteral("a.txt"));
  QVERIFY(a.isValid());
  f->reuse();
  QPointer<ViewWindow2> source(f->current());
  const auto main = f->workspace();
  const auto oldSettings = source->getLinkOpenSettings();
  auto settings = oldSettings;
  settings.m_mode = ViewWindowMode::Edit;
  const auto secret = f->buffers->openBufferByNodeId(secretId, settings);
  QVERIFY(secret.isValid() && secret.isEncrypted());
  const auto preview = f->workspace();
  QPointer<ViewWindow2> retiring(f->current());
  QPointer<LinkPreviewOverlay> overlay(source->findChild<LinkPreviewOverlay *>());
  QVERIFY(overlay);
  const auto retiringId = retiring->getViewWindowId();
  auto *editor = retiring->findChild<MarkdownEditor *>();
  QVERIFY(editor);
  editor->getTextEdit()->setFocus();
  QTRY_VERIFY(editor->getTextEdit()->hasFocus());
  appendDraft(editor->getTextEdit(), QStringLiteral("Unsaved protected consent\n"));
  QVERIFY(retiring->isModified());
  int protectedCloseChecks = 0;
  const int hook = f->hooks->addAction<ViewWindowCloseEvent>(
      HookNames::ViewWindowBeforeClose, [&](HookContext &, const ViewWindowCloseEvent &event) {
        if (event.windowId == retiringId)
          ++protectedCloseChecks;
      });
  const auto removeHook = qScopeGuard([&] { f->hooks->removeAction(hook); });
  DialogAnswer discard(QMessageBox::Discard);
  QVERIFY(f->controller()->closePreviewWorkspace(preview));
  QVERIFY(discard.accepted());
  QCOMPARE(discard.count(), 1);
  // No event pumping here: QObject destruction, resource retirement and core
  // membership completion must still be pending after accepted consent.
  QVERIFY(retiring && overlay);
  QVERIFY(f->controller()->isPreviewWorkspaceClosing(preview));
  QCOMPARE(f->order(preview), QStringList({secret.id()}));
  QVERIFY(f->area->getViewWindowIdsForWorkspace(preview).isEmpty());
  QVERIFY(f->controller()->closePreviewWorkspace(preview));
  QCOMPARE(protectedCloseChecks, 1);
  f->buffers->openBuffer(a.nodeId(), oldSettings);
  QCOMPARE(f->order(preview), QStringList({secret.id()}));
  const auto next = f->open(QStringLiteral("next.txt"));
  QVERIFY(next.isValid());
  QCOMPARE(f->current(), source.data());
  QCOMPARE(source->getBuffer().id(), next.id());
  QCOMPARE(f->order(main), QStringList({next.id()}));
  QVERIFY(source->getLinkOpenSettings().m_previewSourceGeneration >
          oldSettings.m_previewSourceGeneration);
  QCOMPARE(protectedCloseChecks, 1);
  // Lock must not freeze a retired ID removed from the live window map. Its
  // already-accepted destruction still drains before encryption keys retire.
  QString error;
  QVERIFY2(f->controller()->lockAllProtectedNotes(&error), qPrintable(error));
  QVERIFY(!f->buffers->isProtectedLocking());
  QVERIFY(!source->isNoteConversionFrozen() && source->isEnabled());
  QVERIFY(retiring.isNull());
  QTRY_VERIFY(overlay.isNull());
  QVERIFY(f->workspaces->getWorkspace(preview).isEmpty());
  QVERIFY(f->area->previewWorkspaceForWindow(source->getViewWindowId()).isEmpty());
  QCOMPARE(protectedCloseChecks, 1);
  QCOMPARE(source->getLatestContent(), QStringLiteral("Replacement generation\n"));
  QCOMPARE(readBytes(f->absolute(QStringLiteral("a.txt"))), QByteArray("Source A\n"));
}

void TestViewAreaTabReuse::previewConversionAndLockRefusal_data() {
  QTest::addColumn<bool>("locking");
  QTest::newRow("conversion-before-freeze") << false;
  QTest::newRow("lock-before-locking-and-freeze") << true;
}

void TestViewAreaTabReuse::previewConversionAndLockRefusal() {
  QFETCH(bool, locking);
  QVERIFY(f->create(QStringLiteral("b.txt"), "Editable preview\n"));
  Buffer2 sourceBuffer;
  if (locking) {
    const auto id = f->createProtected(QStringLiteral("a.md"), "# Protected source\n", true);
    QVERIFY(!id.isEmpty());
    sourceBuffer = f->openProtected(id);
  } else {
    QVERIFY(f->create(QStringLiteral("a.md"), "# Source for conversion\n"));
    sourceBuffer = f->open(QStringLiteral("a.md"));
  }
  QVERIFY(sourceBuffer.isValid());
  QPointer<ViewWindow2> source(f->current());
  const auto sourceNode = sourceBuffer.nodeId();
  const auto sourceContent = source->getLatestContent();
  const auto b = f->preview(source, f->node(QStringLiteral("b.txt")));
  QVERIFY(b.isValid());
  const auto preview = f->workspace();
  QPointer<ViewWindow2> window(f->current());
  auto *editor = window->findChild<TextEditor *>();
  QVERIFY(editor);
  editor->getTextEdit()->setFocus();
  QTRY_VERIFY(editor->getTextEdit()->hasFocus());
  appendDraft(editor->getTextEdit(), QStringLiteral("Cancel before freezing source\n"));
  const auto draft = window->getLatestContent();
  bool observedConsent = false;
  DialogAnswer cancel(QMessageBox::Cancel, [&] {
    observedConsent = true;
    QVERIFY(source->isEnabled() && !source->isNoteConversionFrozen());
    QVERIFY(window->isEnabled() && !window->isNoteConversionFrozen());
    QVERIFY(!f->buffers->isProtectedLocking());
    QCOMPARE(window->getLatestContent(), draft);
  });
  if (locking) {
    QString error;
    QVERIFY(!f->controller()->lockAllProtectedNotes(&error));
    QVERIFY(!error.isEmpty());
  } else {
    const auto conversion = f->controller()->prepareNoteConversion(sourceNode, true);
    QVERIFY(conversion);
    QVERIFY(conversion->m_error != VXCORE_OK);
    f->controller()->cancelNoteConversion(conversion);
  }
  QVERIFY(cancel.accepted());
  QVERIFY(observedConsent);
  QVERIFY(source && window);
  QVERIFY(source->isEnabled() && !source->isNoteConversionFrozen());
  QVERIFY(window->isEnabled() && !window->isNoteConversionFrozen());
  QVERIFY(!f->buffers->isProtectedLocking());
  QVERIFY(!f->controller()->isNoteConversionBlocked());
  QCOMPARE(source->getLatestContent(), sourceContent);
  QCOMPARE(window->getLatestContent(), draft);
  QCOMPARE(f->order(preview), QStringList({b.id()}));
  // Refusal must release every preflight gate; saving and retrying is viable.
  DialogAnswer save(QMessageBox::Save);
  QVERIFY(f->controller()->closePreviewWorkspace(preview));
  QVERIFY(save.accepted());
  QTRY_COMPARE(readBytes(f->absolute(QStringLiteral("b.txt"))), draft.toUtf8());
  if (locking) {
    QString error;
    QVERIFY2(f->controller()->lockAllProtectedNotes(&error), qPrintable(error));
    QVERIFY(!f->buffers->isProtectedLocking());
    QTRY_VERIFY(source.isNull());
  } else {
    const auto conversion = f->controller()->prepareNoteConversion(sourceNode, true);
    QVERIFY(conversion);
    QCOMPARE(conversion->m_error, VXCORE_OK);
    QVERIFY(source->isNoteConversionFrozen());
    f->controller()->cancelNoteConversion(conversion);
    QVERIFY(!source->isNoteConversionFrozen() && source->isEnabled());
    QCOMPARE(source->getLatestContent(), sourceContent);
  }
}

void TestViewAreaTabReuse::previewHiddenCrossNotebookCloseCancellation() {
  QVERIFY(f->create(QStringLiteral("a.txt"), "Source in first notebook\n"));
  const auto other = f->notebooks->createNotebook(f->root + QStringLiteral("/other"),
                                                  QStringLiteral("{\"name\":\"Other notebook\"}"),
                                                  NotebookType::Bundled);
  QVERIFY(!other.isEmpty());
  QVERIFY(!f->notebooks->createFile(other, QString(), QStringLiteral("b.txt")).isEmpty());
  const auto targetPath = f->notebooks->buildAbsolutePath(other, QStringLiteral("b.txt"));
  QVERIFY(writeBytes(targetPath, "Cross-notebook preview\n"));
  const auto a = f->open(QStringLiteral("a.txt"));
  QVERIFY(a.isValid());
  QPointer<ViewWindow2> source(f->current());
  const auto sourceWorkspace = f->workspace();
  const auto b = f->preview(source, {other, QStringLiteral("b.txt")});
  QVERIFY(b.isValid());
  const auto preview = f->workspace();
  QPointer<ViewWindow2> previewWindow(f->current());
  QPointer<LinkPreviewOverlay> overlay(source->findChild<LinkPreviewOverlay *>());
  QVERIFY(overlay);
  auto *editor = previewWindow->findChild<TextEditor *>();
  QVERIFY(editor);
  editor->getTextEdit()->setFocus();
  QTRY_VERIFY(editor->getTextEdit()->hasFocus());
  appendDraft(editor->getTextEdit(), QStringLiteral("Do not abandon cross-notebook draft\n"));
  const auto draft = previewWindow->getLatestContent();
  f->controller()->newWorkspace(sourceWorkspace, QStringLiteral("Hide source workspace"));
  QVERIFY(!source->isVisible());
  QVERIFY(!overlay->isVisible());
  QVERIFY(f->area->previewWorkspacesForSourceNotebook(f->notebook).contains(preview));
  DialogAnswer cancel(QMessageBox::Cancel);
  QVERIFY(!f->notebooks->closeNotebook(f->notebook));
  QVERIFY(cancel.accepted());
  QVERIFY(!f->notebooks->getNotebookConfig(f->notebook).isEmpty());
  QVERIFY(!f->notebooks->getNotebookConfig(other).isEmpty());
  QVERIFY(source && previewWindow && overlay);
  QCOMPARE(previewWindow->getLatestContent(), draft);
  QVERIFY(previewWindow->isEnabled());
  QCOMPARE(f->order(preview), QStringList({b.id()}));
  QCOMPARE(source->getBuffer().id(), a.id());
  QVERIFY(!source->isVisible());

  // Closing only the target notebook finds preview tabs even while their source
  // lives in hidden ordinary-window storage. It does not close that source.
  QVERIFY(previewWindow->save());
  QTRY_COMPARE(readBytes(targetPath), draft.toUtf8());
  QVERIFY(f->notebooks->closeNotebook(other));
  QTRY_VERIFY(previewWindow.isNull());
  QTRY_VERIFY(overlay.isNull());
  QVERIFY(source && source->getBuffer().isValid());
  QVERIFY(!f->notebooks->getNotebookConfig(f->notebook).isEmpty());
  QVERIFY(f->workspaces->getWorkspace(preview).isEmpty());
  QVERIFY(f->area->previewWorkspacesForSourceNotebook(f->notebook).isEmpty());
  QVERIFY(f->notebooks->closeNotebook(f->notebook));
  QTRY_VERIFY(source.isNull());
}

void TestViewAreaTabReuse::previewHomeOnlyMainRetainsHome() {
  QVERIFY(f->create(QStringLiteral("a.txt"), "Detached preview source\n"));
  QVERIFY(f->create(QStringLiteral("b.txt"), "First preview target\n"));
  QVERIFY(f->create(QStringLiteral("c.txt"), "Explicit preview target\n"));
  const auto a = f->open(QStringLiteral("a.txt"));
  QVERIFY(a.isValid());
  QPointer<ViewWindow2> source(f->current());
  const auto main = f->workspace();
  QVERIFY(f->controller()->detachViewWindow(main, source->getViewWindowId(), a.id()));
  auto *detached = qobject_cast<DetachedWindow *>(source->window());
  QVERIFY(detached);
  f->controller()->openVxUrl(QUrl(QStringLiteral("vx://home")));
  QPointer<ViewWindow2> home(f->current());
  QVERIFY(home && home != source && home->getNodeId().isVirtual());
  const auto homeId = home->getViewWindowId();
  const auto mainOrder = f->order(main);
  QCOMPARE(mainOrder.size(), 1);
  detached->show();
  QTRY_VERIFY(source->isVisible());
  const auto b = f->preview(source, f->node(QStringLiteral("b.txt")));
  QVERIFY(b.isValid());
  const auto preview = f->area->previewWorkspaceForWindow(source->getViewWindowId());
  QVERIFY(!preview.isEmpty());
  FileOpenSettings target;
  target.m_targetWorkspaceId = preview;
  target.m_mode = ViewWindowMode::Edit;
  const auto c = f->buffers->openBuffer(f->node(QStringLiteral("c.txt")), target);
  QVERIFY(c.isValid());
  QCOMPARE(f->order(preview), QStringList({b.id(), c.id()}));
  QVERIFY(f->area->findWindowIdByBufferId(preview, c.id()) !=
          ViewAreaController::InvalidViewWindowId);
  QVERIFY(home);
  QCOMPARE(home->getViewWindowId(), homeId);
  QCOMPARE(f->order(main), mainOrder);
  QCOMPARE(f->area->getViewWindowIdsForWorkspace(main), QVector<ID>({homeId}));
  QCOMPARE(f->area->getViewSplitCount(), 1);
  QCOMPARE(f->workspaces->getCurrentWorkspace(), main);
  QCOMPARE(source->getBuffer().id(), a.id());
  QVERIFY(f->controller()->closePreviewWorkspace(preview));
  QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
  QVERIFY(home);
  QCOMPARE(f->order(main), mainOrder);
}

void TestViewAreaTabReuse::previewUnavailableDestination() {
  for (const auto &name : {QStringLiteral("a.txt"), QStringLiteral("b.txt"),
                           QStringLiteral("c.txt"), QStringLiteral("rejected.txt")})
    QVERIFY(f->create(name, name.toUtf8()));
  const auto scenario = [&](const QString &kind) {
    const auto a = f->open(QStringLiteral("a.txt"));
    QVERIFY(a.isValid());
    f->reuse();
    QPointer<ViewWindow2> source(f->current());
    const auto main = f->workspace();
    auto settings = source->getLinkOpenSettings();
    settings.m_mode = ViewWindowMode::Edit;
    QString preview;
    Buffer2 b;
    if (kind == QLatin1String("hidden") || kind == QLatin1String("closed")) {
      b = f->preview(source, f->node(QStringLiteral("b.txt")));
      QVERIFY(b.isValid());
      preview = f->workspace();
      settings.m_targetWorkspaceId = preview;
      if (kind == QLatin1String("hidden")) {
        QVERIFY(f->open(QStringLiteral("c.txt"), true).isValid());
        QVERIFY(!source->isVisible());
      } else {
        QVERIFY(f->controller()->closePreviewWorkspace(preview));
        QVERIFY(f->workspaces->getWorkspace(preview).isEmpty());
      }
    } else if (kind == QLatin1String("generation")) {
      QVERIFY(f->open(QStringLiteral("c.txt")).isValid());
      QCOMPARE(f->current(), source.data());
      QVERIFY(source->getLinkOpenSettings().m_previewSourceGeneration !=
              settings.m_previewSourceGeneration);
    }
    int hook = -1;
    bool hooked = false;
    const auto restore = qScopeGuard([&] {
      if (hook >= 0)
        f->hooks->removeAction(hook);
      if (kind == QLatin1String("factory"))
        f->factory->registerBuiltInCreators();
    });
    if (kind == QLatin1String("hide") || kind == QLatin1String("rebind") ||
        kind == QLatin1String("veto")) {
      hook = f->hooks->addAction<ViewWindowOpenEvent>(
          HookNames::ViewWindowBeforeOpen, [&](HookContext &ctx, const ViewWindowOpenEvent &event) {
            if (hooked || event.nodeId != QStringLiteral("rejected.txt"))
              return;
            hooked = true;
            if (kind == QLatin1String("hide"))
              source->hide();
            else if (kind == QLatin1String("rebind"))
              f->open(QStringLiteral("c.txt"));
            else
              ctx.cancel();
          });
    } else if (kind == QLatin1String("create-hide")) {
      hook = f->hooks->addAction<ViewSplitCreateEvent>(
          HookNames::ViewSplitAfterCreate, [&](HookContext &, const ViewSplitCreateEvent &) {
            hooked = true;
            source->hide();
          });
    } else if (kind == QLatin1String("factory")) {
      f->factory->registerCreator(QStringLiteral("Text"),
                                  [](ServiceLocator &, const Buffer2 &, QWidget *,
                                     ViewWindowMode) -> ViewWindow2 * { return nullptr; });
    }
    f->buffers->openBuffer(f->node(QStringLiteral("rejected.txt")), settings);
    if (hook >= 0) {
      QVERIFY(hooked);
      f->hooks->removeAction(hook);
      hook = -1;
    }
    if (kind == QLatin1String("factory"))
      f->factory->registerBuiltInCreators();
    QVERIFY(source);
    QCOMPARE(f->area->getViewSplitCount(), 1);
    QCOMPARE(f->workspaces->getCurrentWorkspace(), main);
    QCOMPARE(f->workspaces->listWorkspaces().size(), kind == QLatin1String("hidden") ? 2 : 1);
    for (const auto &value : f->buffers->listBuffers())
      QVERIFY(value.toObject().value(QStringLiteral("filePath")).toString() !=
              QStringLiteral("rejected.txt"));
    if (kind == QLatin1String("hidden")) {
      QCOMPARE(f->order(preview), QStringList({b.id()}));
      QVERIFY(!f->area->isPreviewWorkspaceAvailable(preview));
      f->controller()->focusWindow(main, a.id());
    } else {
      QVERIFY(f->area->previewWorkspaceForWindow(source->getViewWindowId()).isEmpty());
      QCOMPARE(f->buffers->listBuffers().size(), 1);
      QCOMPARE(f->order(main), QStringList({source->getBuffer().id()}));
      source->show();
    }
    QTRY_VERIFY(source->isVisible());
    // Rejection releases the allocation guard; a fresh request can still open.
    const auto accepted = f->preview(source, f->node(QStringLiteral("rejected.txt")));
    QVERIFY(accepted.isValid());
    const auto freshPreview = f->area->previewWorkspaceForWindow(source->getViewWindowId());
    QVERIFY(!freshPreview.isEmpty());
    QVERIFY(f->area->findWindowIdByBufferId(freshPreview, accepted.id()) !=
            ViewAreaController::InvalidViewWindowId);
    if (kind == QLatin1String("closed"))
      QVERIFY(freshPreview != preview);
    QVERIFY(f->controller()->closePreviewWorkspace(freshPreview));
  };
  for (const auto &kind :
       {QStringLiteral("hidden"), QStringLiteral("closed"), QStringLiteral("generation"),
        QStringLiteral("hide"), QStringLiteral("rebind"), QStringLiteral("create-hide"),
        QStringLiteral("veto"), QStringLiteral("factory")}) {
    scenario(kind);
    if (QTest::currentTestFailed()) {
      return;
    }
    QVERIFY(f->controller()->closeAllPreviewWorkspaces());
    QVERIFY(f->controller()->closeAll(f->area->getVisibleWorkspaceIds().toVector(), true));
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
  }
}

void TestViewAreaTabReuse::previewCreationReentrancy() {
  for (const auto &name : {QStringLiteral("a.txt"), QStringLiteral("outer.txt"),
                           QStringLiteral("before.txt"), QStringLiteral("after.txt")})
    QVERIFY(f->create(name, name.toUtf8()));
  const auto a = f->open(QStringLiteral("a.txt"));
  QVERIFY(a.isValid());
  auto *source = f->current();
  const auto main = f->workspace();
  const auto settings = source->getLinkOpenSettings();
  int beforeCount = 0;
  int afterCount = 0;
  const int before = f->hooks->addAction<ViewSplitCreateEvent>(
      HookNames::ViewSplitBeforeCreate, [&](HookContext &, const ViewSplitCreateEvent &) {
        if (++beforeCount == 1)
          f->buffers->openBuffer(f->node(QStringLiteral("before.txt")), settings);
      });
  const int after = f->hooks->addAction<ViewSplitCreateEvent>(
      HookNames::ViewSplitAfterCreate, [&](HookContext &, const ViewSplitCreateEvent &) {
        if (++afterCount == 1)
          f->buffers->openBuffer(f->node(QStringLiteral("after.txt")), settings);
      });
  const auto removeHooks = qScopeGuard([&] {
    f->hooks->removeAction(before);
    f->hooks->removeAction(after);
  });
  const auto outer = f->buffers->openBuffer(f->node(QStringLiteral("outer.txt")), settings);
  QVERIFY(outer.isValid());
  QCOMPARE(beforeCount, 1);
  QCOMPARE(afterCount, 1);
  const auto preview = f->area->previewWorkspaceForWindow(source->getViewWindowId());
  QVERIFY(!preview.isEmpty());
  QCOMPARE(source->findChildren<LinkPreviewOverlay *>().size(), 1);
  QCOMPARE(f->workspaces->listWorkspaces().size(), 2);
  QCOMPARE(f->buffers->listBuffers().size(), 2);
  QCOMPARE(f->order(preview), QStringList({outer.id()}));
  QCOMPARE(f->order(main), QStringList({a.id()}));
  QCOMPARE(f->coreCurrent(main), a.id());
  QCOMPARE(f->workspaces->getCurrentWorkspace(), main);
  QVERIFY(f->controller()->closePreviewWorkspace(preview));
}

void TestViewAreaTabReuse::previewConversionRecollectsRetiringViews() {
  const auto id =
      f->createProtected(QStringLiteral("same.md"), "# Shared protected source\n", true);
  QVERIFY(!id.isEmpty());
  const auto buffer = f->openProtected(id);
  QVERIFY(buffer.isValid());
  QPointer<ViewWindow2> source(f->current());
  const auto sourceId = source->getViewWindowId();
  auto settings = source->getLinkOpenSettings();
  settings.m_mode = ViewWindowMode::Edit;
  QCOMPARE(f->buffers->openBufferByNodeId(id, settings).id(), buffer.id());
  const auto preview = f->workspace();
  QPointer<ViewWindow2> retiring(f->current());
  QVERIFY(retiring != source);
  const auto retiringId = retiring->getViewWindowId();
  auto *editor = retiring->findChild<MarkdownEditor *>();
  QVERIFY(editor);
  editor->getTextEdit()->setFocus();
  QTRY_VERIFY(editor->getTextEdit()->hasFocus());
  appendDraft(editor->getTextEdit(), QStringLiteral("Preview consent before conversion\n"));
  QVERIFY(retiring->isModified());
  QCOMPARE(f->area->findWindowIdsByNode(buffer.nodeId(), false).size(), 2);
  int closeChecks = 0;
  const int hook = f->hooks->addAction<ViewWindowCloseEvent>(
      HookNames::ViewWindowBeforeClose, [&](HookContext &, const ViewWindowCloseEvent &event) {
        if (event.windowId == retiringId)
          ++closeChecks;
      });
  const auto removeHook = qScopeGuard([&] { f->hooks->removeAction(hook); });
  DialogAnswer discard(QMessageBox::Discard, [&] {
    QVERIFY(!source->isNoteConversionFrozen() && source->isEnabled());
    QVERIFY(!retiring->isNoteConversionFrozen() && retiring->isEnabled());
  });
  QVERIFY(f->buffers->beginProtectedOperation());
  const auto protectedOperation = qScopeGuard([&] { f->buffers->endProtectedOperation(); });
  const auto conversion = f->controller()->prepareNoteConversion(buffer.nodeId(), false);
  const auto cancelConversion =
      qScopeGuard([&] { f->controller()->cancelNoteConversion(conversion); });
  QVERIFY(discard.accepted());
  QCOMPARE(discard.count(), 1);
  QCOMPARE(closeChecks, 1);
  QVERIFY(conversion);
  QVERIFY2(conversion->m_error == VXCORE_OK, qPrintable(conversion->m_errorMessage));
  QVERIFY(source && source->isNoteConversionFrozen());
  QCOMPARE(f->area->findWindowIdsByNode(buffer.nodeId(), false), QVector<ID>({sourceId}));
  f->controller()->cancelNoteConversion(conversion);
  QVERIFY(!source->isNoteConversionFrozen() && source->isEnabled());
  QTRY_VERIFY(retiring.isNull());
  QVERIFY(f->workspaces->getWorkspace(preview).isEmpty());
  QVERIFY(source->getBuffer().isValid());
  QVERIFY(f->area->previewWorkspaceForWindow(sourceId).isEmpty());
}

void TestViewAreaTabReuse::previewQuitCancellationBeforeSession() {
  QVERIFY(f->create(QStringLiteral("a.txt"), "Detached source at quit\n"));
  QVERIFY(f->create(QStringLiteral("b.txt"), "Preview at quit\n"));
  const auto a = f->open(QStringLiteral("a.txt"));
  QVERIFY(a.isValid());
  auto *source = f->current();
  const auto main = f->workspace();
  QVERIFY(f->controller()->detachViewWindow(main, source->getViewWindowId(), a.id()));
  QPointer<DetachedWindow> detached(qobject_cast<DetachedWindow *>(source->window()));
  QVERIFY(detached);
  const auto detachedWorkspace = detached->getWorkspaceId();
  detached->show();
  QTRY_VERIFY(source->isVisible());
  QTRY_VERIFY(f->area->getAllViewSplits().first()->getCurrentViewWindow());
  QCOMPARE(f->area->getAllViewSplits().first()->getCurrentViewWindow()->getNodeId().relativePath,
           QStringLiteral("vx://home"));
  detached->activateWindow();
  QTRY_VERIFY(detached->isActiveWindow());
  const auto b = f->preview(source, f->node(QStringLiteral("b.txt")));
  QVERIFY(b.isValid());
  const auto preview = f->workspace();
  auto *window = f->current();
  auto *editor = window->findChild<TextEditor *>();
  QVERIFY(editor);
  editor->getTextEdit()->setFocus();
  QTRY_VERIFY(editor->getTextEdit()->hasFocus());
  appendDraft(editor->getTextEdit(), QStringLiteral("Cancel quit without reattachment\n"));
  const auto draft = window->getLatestContent();
  DialogAnswer cancel(QMessageBox::Cancel);
  QVERIFY(f->hooks->doAction(HookNames::MainWindowBeforeClose));
  QVERIFY(cancel.accepted());
  QVERIFY(f->controller()->shouldPropagateToCore());
  QVERIFY(detached && detached->isVisible());
  QCOMPARE(source->window(), detached.data());
  QCOMPARE(f->order(detachedWorkspace), QStringList({a.id()}));
  QCOMPARE(f->order(preview), QStringList({b.id()}));
  QCOMPARE(window->getLatestContent(), draft);
  QVERIFY(window->isEnabled() && window->isModified());
  QVERIFY(!f->controller()->isPreviewWorkspaceClosing(preview));
  DialogAnswer save(QMessageBox::Save);
  QVERIFY(f->controller()->closeAllPreviewWorkspaces());
  QVERIFY(save.accepted());
  QTRY_COMPARE(readBytes(f->absolute(QStringLiteral("b.txt"))), draft.toUtf8());
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
  auto *native = window->findChild<MarkdownRenderController *>();
  QVERIFY(editor && viewer && native);
  QSignalSpy nativeHtml(native, &MarkdownRenderController::rendered);
  QVERIFY(waitForRender(viewer, QStringLiteral("Alpha")));
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
  QVERIFY(waitForCurrentRender(viewer, native, QStringLiteral("Alpha")));
  const auto alphaToken = viewer->adapter()->documentGeneration();
  const auto alphaEditorGeneration = editor->getHighlighter()->documentGeneration();
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
  QVERIFY(waitForCurrentRender(viewer, native, heading));
  const auto betaToken = viewer->adapter()->documentGeneration();
  QVERIFY(betaToken != alphaToken);
  QVERIFY(editor->getHighlighter()->documentGeneration() != alphaEditorGeneration);
  QCOMPARE(window->findChild<MarkdownRenderController *>(), native);
  QCOMPARE(window->findChildren<MarkdownRenderController *>().size(), 1);
  QCOMPARE(viewer->jobSession()->property("markdownJobBufferId").toString(), b.id());
  QVERIFY(!viewer->jobSession()->property("markdownJobProtected").toBool());
  QVERIFY(!nativeHtml.isEmpty());
  QCOMPARE(nativeHtml.last().at(0).toString(), betaToken);
  const auto betaResult = qvariant_cast<MarkdownHtmlResult>(nativeHtml.last().at(1));
  QVERIFY(betaResult.error.isEmpty() && !betaResult.html.isEmpty() && betaResult.document);
  QCOMPARE(betaResult.document->source(), sourceB);
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
  QTRY_VERIFY_WITH_TIMEOUT(!f->buffers->isSaveQueueBusy(b.id()) && !window->isModified(), 10000);

  // Return to A through the same retained window/editor/page, even for identical source.
  const auto reopenedA = f->open(QStringLiteral("a/a.md"));
  QVERIFY(reopenedA.isValid());
  QCOMPARE(f->current(), window);
  QCOMPARE(window->findChild<MarkdownEditor *>(), editor);
  QCOMPARE(window->findChild<MarkdownViewer *>(), viewer);
  QCOMPARE(window->findChild<MarkdownRenderController *>(), native);
  QCOMPARE(viewer->page(), page);
  QVERIFY(waitForCurrentRender(viewer, native, QStringLiteral("Alpha")));
  const auto returnedToken = viewer->adapter()->documentGeneration();
  QVERIFY(returnedToken != alphaToken && returnedToken != betaToken);
  QCOMPARE(nativeHtml.last().at(0).toString(), returnedToken);
  const auto returnedResult = qvariant_cast<MarkdownHtmlResult>(nativeHtml.last().at(1));
  QVERIFY(returnedResult.error.isEmpty() && !returnedResult.html.isEmpty() &&
          returnedResult.document);
  QCOMPARE(returnedResult.document->source(), sourceA);
  QCOMPARE(viewer->jobSession()->property("markdownJobBufferId").toString(), reopenedA.id());
  QCOMPARE(editor->getText(), QString::fromUtf8(sourceA));
  QVERIFY(!editor->getTextEdit()->document()->isUndoAvailable());
  QTRY_COMPARE_WITH_TIMEOUT(
      evaluate(page, QStringLiteral("document.querySelector('img')?.naturalWidth")).toInt(), 17,
      10000);
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
  QCOMPARE(typst->getConfig().m_runtime, &f->runtime);
  QVERIFY(f->runtime.isAvailable());
  QVERIFY(f->open(QStringLiteral("b.typst")).isValid());
  QCOMPARE(f->current(), window);
  QCOMPARE(window->findChild<vte::VTypstEditor *>(), typst);
  QCOMPARE(typst->getTextEditor(), source);
  QCOMPARE(typst->getConfig().m_runtime, &f->runtime);
  QVERIFY(f->runtime.isAvailable());
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
  const auto outgoing = adapter->getViewerState();
  const QJsonObject lateState{{QStringLiteral("page"), outgoing.m_page},
                              {QStringLiteral("pageCount"), outgoing.m_pageCount},
                              {QStringLiteral("scale"), outgoing.m_scale},
                              {QStringLiteral("scaleValue"), outgoing.m_scaleValue},
                              {QStringLiteral("rotation"), outgoing.m_rotation},
                              {QStringLiteral("scrollMode"), outgoing.m_scrollMode},
                              {QStringLiteral("spreadMode"), outgoing.m_spreadMode},
                              {QStringLiteral("cursorTool"), outgoing.m_cursorTool},
                              {QStringLiteral("sidebarOpen"), outgoing.m_sidebarOpen}};
  QVERIFY(f->open(QStringLiteral("b.pdf")).isValid());
  QVERIFY(pdf->isHidden());
  QVERIFY(!adapter->isReady());
  // An outgoing WebChannel invocation may already be queued during the inert navigation.
  adapter->setViewerState(lateState);
  QCOMPARE(f->current(), window);
  QCOMPARE(window->findChild<PdfViewer *>(), pdf);
  QCOMPARE(pdf->page(), page);
  QCOMPARE(pdf->adapter(), adapter);
  QVERIFY(QTest::qWaitFor(
      [&] {
        return pdf->isVisible() && evaluate(page, QStringLiteral("document.body.innerText"))
                                       .toString()
                                       .contains(QStringLiteral("PDF beta"));
      },
      15000));
  QTRY_VERIFY_WITH_TIMEOUT(adapter->getViewerState().m_valid, 30000);
  QTRY_COMPARE_WITH_TIMEOUT(adapter->getDocumentPageCount(), 3, 30000);
  QTRY_COMPARE_WITH_TIMEOUT(adapter->getViewerState().m_pageCount, 3, 30000);
  QCOMPARE(adapter->getViewerState().m_page, 1);
  QCOMPARE(adapter->getViewerState().m_rotation, 0);
  QVERIFY(!adapter->isViewerReplayPending());
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
  // Real typing follows focus, which registers the outgoing buffer's active writer.
  f->area->activateWindow();
  QApplication::setActiveWindow(f->area.get());
  editor->getTextEdit()->setFocus();
  QTRY_VERIFY(editor->getTextEdit()->hasFocus());
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
    auto *reader = window->findChild<MarkdownViewer *>();
    auto *native = window->findChild<MarkdownRenderController *>();
    QVERIFY(reader && native);
    QVERIFY(waitForCurrentRender(reader, native, QStringLiteral("Unsaved alpha draft")));
  } else {
    QCOMPARE(window->getBuffer().id(), b.id());
    QCOMPARE(editor->getText(), QStringLiteral("# Beta\n"));
    QCOMPARE(f->order(), QStringList({b.id()}));
    QCOMPARE(f->coreCurrent(), b.id());
    QVERIFY(!editor->getTextEdit()->document()->isUndoAvailable());
    auto *reader = window->findChild<MarkdownViewer *>();
    auto *native = window->findChild<MarkdownRenderController *>();
    QVERIFY(reader && native);
    QVERIFY(waitForCurrentRender(reader, native, QStringLiteral("Beta")));
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
  QVERIFY(waitForRender(reader, QStringLiteral("Secret")));
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

void TestViewAreaTabReuse::explicitDetachedWorkspace() {
  f->reuse();
  for (const auto &name : {QStringLiteral("main.txt"), QStringLiteral("one.txt"),
                           QStringLiteral("two.txt"), QStringLiteral("next.txt")})
    QVERIFY(f->create(name, name.toUtf8()));
  const auto mainBuffer = f->open(QStringLiteral("main.txt"));
  const auto main = f->workspace();
  auto *mainWindow = f->current();
  QStringList created;
  const int hook = f->hooks->addAction<ViewSplitCreateEvent>(
      HookNames::ViewSplitAfterCreate, [&](HookContext &, const ViewSplitCreateEvent &event) {
        created.append(event.newWorkspaceId);
      });
  const auto removeHook = qScopeGuard([&] { f->hooks->removeAction(hook); });
  FileOpenSettings settings;
  settings.m_mode = ViewWindowMode::Edit;
  settings.m_detachedView = true;
  const auto one = f->buffers->openBuffer(f->node(QStringLiteral("one.txt")), settings);
  f->controller()->resetCliDetachedBatch();
  const auto two = f->buffers->openBuffer(f->node(QStringLiteral("two.txt")), settings);
  QCOMPARE(created.size(), 2);
  const auto first = created[0];
  const auto second = created[1];
  ViewSplit2 *targetSplit = nullptr;
  for (auto *widget : QApplication::topLevelWidgets()) {
    auto *host = qobject_cast<DetachedWindow *>(widget);
    if (host && host->getWorkspaceId() == first)
      targetSplit = host->getViewSplit();
  }
  QVERIFY(targetSplit);
  f->controller()->resetCliDetachedBatch();

  settings.m_targetWorkspaceId = first;
  settings.m_detachedView = false;
  // Exercise settings serialization and the real FileOpenEvent hook transport.
  settings = FileOpenSettings::fromVariantMap(settings.toVariantMap());
  const auto next = f->buffers->openBuffer(f->node(QStringLiteral("next.txt")), settings);
  QVERIFY(next.isValid());
  QCOMPARE(f->order(first), QStringList({one.id(), next.id()}));
  QCOMPARE(f->order(second), QStringList({two.id()}));
  QCOMPARE(f->coreCurrent(first), next.id());
  const auto nextWindow = f->area->findWindowIdByBufferId(first, next.id());
  QVERIFY(nextWindow != ViewAreaController::InvalidViewWindowId);

  settings.m_focus = false;
  f->buffers->openBuffer(one.nodeId(), settings);
  QCOMPARE(targetSplit->getCurrentViewWindow()->getBuffer().id(), next.id());
  QCOMPARE(f->coreCurrent(first), next.id());
  // An existing buffer in a different workspace still gets a view in the target.
  f->buffers->openBuffer(two.nodeId(), settings);
  QCOMPARE(f->order(first), QStringList({one.id(), next.id(), two.id()}));
  QCOMPARE(targetSplit->getCurrentViewWindow()->getBuffer().id(), next.id());
  QCOMPARE(f->coreCurrent(first), next.id());

  settings.m_focus = true;
  settings.m_detachedView = true;
  f->buffers->openBuffer(one.nodeId(), settings);
  QCOMPARE(targetSplit->getCurrentViewWindow()->getBuffer().id(), one.id());
  QCOMPARE(f->coreCurrent(first), one.id());
  QCOMPARE(f->area->findWindowIdByBufferId(first, next.id()), nextWindow);
  QCOMPARE(f->order(first), QStringList({one.id(), next.id(), two.id()}));
  QCOMPARE(created.size(), 2);
  QCOMPARE(f->workspace(), main);
  QCOMPARE(f->current(), mainWindow);
  QCOMPARE(f->order(main), QStringList({mainBuffer.id()}));
  QCOMPARE(f->coreCurrent(main), mainBuffer.id());
}

void TestViewAreaTabReuse::explicitMainWorkspace() {
  f->reuse();
  for (const auto &name : {QStringLiteral("a.txt"), QStringLiteral("b.txt"),
                           QStringLiteral("c.txt"), QStringLiteral("d.txt")})
    QVERIFY(f->create(name, name.toUtf8()));
  const auto a = f->open(QStringLiteral("a.txt"));
  const auto first = f->workspace();
  f->controller()->splitViewSplit(first, Direction::Right, false);
  const auto second = f->workspace();
  QVERIFY(second != first);
  const auto b = f->open(QStringLiteral("b.txt"));
  f->controller()->focusWindow(first, a.id());
  auto *original = f->current();

  FileOpenSettings settings;
  settings.m_mode = ViewWindowMode::Edit;
  settings.m_targetWorkspaceId = second;
  settings.m_detachedView = true; // Explicit main destination wins.
  settings.m_focus = false;
  const auto c = f->buffers->openBuffer(f->node(QStringLiteral("c.txt")), settings);
  QCOMPARE(f->workspace(), first);
  QCOMPARE(f->current(), original);
  QCOMPARE(f->coreCurrent(first), a.id());
  QCOMPARE(f->order(second), QStringList({b.id(), c.id()}));
  QCOMPARE(f->coreCurrent(second), b.id());
  QCOMPARE(f->area->getCurrentBufferIdForWorkspace(second), b.id());
  f->buffers->openBuffer(c.nodeId(), settings);
  QCOMPARE(f->current(), original);
  QCOMPARE(f->area->getCurrentBufferIdForWorkspace(second), b.id());

  settings.m_focus = true;
  f->buffers->openBuffer(c.nodeId(), settings);
  QCOMPARE(f->workspace(), second);
  QCOMPARE(f->current()->getBuffer().id(), c.id());
  QCOMPARE(f->coreCurrent(second), c.id());
  const auto reusedId = f->current()->getViewWindowId();
  f->controller()->focusWindow(first, a.id());
  const auto d = f->buffers->openBuffer(f->node(QStringLiteral("d.txt")), settings);
  QCOMPARE(f->workspace(), second);
  QCOMPARE(f->current()->getViewWindowId(), reusedId);
  QCOMPARE(f->current()->getBuffer().id(), d.id());
  QCOMPARE(f->order(second), QStringList({b.id(), d.id()}));
  QCOMPARE(f->order(first), QStringList({a.id()}));
  QCOMPARE(f->coreCurrent(first), a.id());
  QCOMPARE(f->workspaces->listWorkspaces().size(), 2);
}

void TestViewAreaTabReuse::unavailableTargetWorkspace_data() {
  QTest::addColumn<QString>("kind");
  QTest::newRow("missing") << QStringLiteral("missing");
  QTest::newRow("hidden") << QStringLiteral("hidden");
  QTest::newRow("closed") << QStringLiteral("closed");
  QTest::newRow("closed-by-open-hook") << QStringLiteral("hook");
}

void TestViewAreaTabReuse::unavailableTargetWorkspace() {
  QFETCH(QString, kind);
  QVERIFY(f->create(QStringLiteral("a.txt"), "Owned main note\n"));
  QVERIFY(f->create(QStringLiteral("b.txt"), "Detached seed\n"));
  QVERIFY(f->create(QStringLiteral("c.txt"), "Rejected candidate\n"));
  const auto a = f->open(QStringLiteral("a.txt"));
  QString target = QStringLiteral("missing-workspace");
  ID detachedWindow = ViewAreaController::InvalidViewWindowId;
  int closeHook = -1;
  const auto removeHook = qScopeGuard([&] {
    if (closeHook >= 0)
      f->hooks->removeAction(closeHook);
  });
  if (kind == QLatin1String("hidden")) {
    target = f->workspace();
    f->controller()->newWorkspace(target, QStringLiteral("Visible"));
    f->open(QStringLiteral("a.txt"));
  } else if (kind != QLatin1String("missing")) {
    const int created = f->hooks->addAction<ViewSplitCreateEvent>(
        HookNames::ViewSplitAfterCreate,
        [&](HookContext &, const ViewSplitCreateEvent &event) { target = event.newWorkspaceId; });
    FileOpenSettings detached;
    detached.m_detachedView = true;
    const auto b = f->buffers->openBuffer(f->node(QStringLiteral("b.txt")), detached);
    f->hooks->removeAction(created);
    detachedWindow = f->area->findWindowIdByBufferId(target, b.id());
    QVERIFY(detachedWindow != ViewAreaController::InvalidViewWindowId);
    if (kind == QLatin1String("closed")) {
      QVERIFY(f->controller()->closeViewWindow(detachedWindow, true));
    } else {
      closeHook = f->hooks->addAction<ViewWindowOpenEvent>(
          HookNames::ViewWindowBeforeOpen, [&](HookContext &, const ViewWindowOpenEvent &) {
            f->controller()->closeViewWindow(detachedWindow, true);
          });
    }
  }
  const auto main = f->workspace();
  const auto current = f->current();
  FileOpenSettings settings;
  settings.m_targetWorkspaceId = target;
  settings.m_detachedView = true; // No detached fallback on invalid explicit ID.
  f->buffers->openBuffer(f->node(QStringLiteral("c.txt")), settings);
  if (closeHook >= 0) {
    f->hooks->removeAction(closeHook);
    closeHook = -1;
  }
  // Rejection must clean an unowned candidate, but keep a buffer owned elsewhere.
  f->buffers->openBuffer(a.nodeId(), settings);
  QCOMPARE(f->buffers->listBuffers().size(), 1);
  QVERIFY(f->buffers->getBufferHandle(a.id()).isValid());
  QCOMPARE(f->workspace(), main);
  QCOMPARE(f->current(), current);
  QCOMPARE(f->order(main), QStringList({a.id()}));
  QCOMPARE(f->coreCurrent(main), a.id());
  QCOMPARE(f->area->getViewWindowIdsForWorkspace(target).size(), 0);
  QCOMPARE(f->workspaces->listWorkspaces().size(), kind == QLatin1String("hidden") ? 2 : 1);
}

void TestViewAreaTabReuse::targetWorkspaceWithReentrantOpen() {
  for (const auto &name :
       {QStringLiteral("a.txt"), QStringLiteral("b.txt"), QStringLiteral("outer.txt")})
    QVERIFY(f->create(name, name.toUtf8()));
  const auto a = f->open(QStringLiteral("a.txt"));
  const auto first = f->workspace();
  f->controller()->splitViewSplit(first, Direction::Right, false);
  const auto second = f->workspace();
  const auto b = f->open(QStringLiteral("b.txt"));
  f->controller()->focusWindow(first, a.id());
  QTRY_VERIFY(f->current()->isAncestorOf(QApplication::focusWidget()));
  bool nested = false;
  QString nestedWorkspace;
  const int hook = f->hooks->addAction<ViewWindowOpenEvent>(
      HookNames::ViewWindowAfterOpen, [&](HookContext &, const ViewWindowOpenEvent &) {
        if (nested)
          return;
        nested = true;
        FileOpenSettings inner;
        inner.m_targetWorkspaceId = second;
        f->controller()->openBuffer(b, inner);
        nestedWorkspace = f->workspace();
      });
  const auto removeHook = qScopeGuard([&] { f->hooks->removeAction(hook); });
  FileOpenSettings outer;
  outer.m_targetWorkspaceId = second;
  outer.m_focus = false;
  const auto buffer = f->buffers->openBuffer(f->node(QStringLiteral("outer.txt")), outer);
  QVERIFY(nested);
  QCOMPARE(nestedWorkspace, second);
  QCOMPARE(f->order(second), QStringList({b.id(), buffer.id()}));
  QCOMPARE(f->order(first), QStringList({a.id()}));
  QCOMPARE(f->workspace(), first);
  QCOMPARE(f->current()->getBuffer().id(), a.id());
  QCOMPARE(f->coreCurrent(first), a.id());
  QCOMPARE(f->coreCurrent(second), b.id());
  QVERIFY(f->area->findWindowIdByBufferId(second, buffer.id()) !=
          ViewAreaController::InvalidViewWindowId);
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
  auto *native = window->findChild<MarkdownRenderController *>();
  QVERIFY(editor && ordinary && native);
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
  QVERIFY(waitForCurrentRender(ordinary, native, QStringLiteral("Ordinary")));
  const auto ordinaryToken = ordinary->adapter()->documentGeneration();
  QVERIFY(native->isCurrent(ordinaryToken));
  const auto tabId = window->getViewWindowId();
  const auto a = f->openProtected(protectedA);
  QVERIFY(a.isValid() && a.isEncrypted());
  QCOMPARE(f->current(), window);
  QCOMPARE(window->findChild<MarkdownEditor *>(), editor);
  QTRY_VERIFY(ordinary.isNull());
  QPointer<MarkdownViewer> privateReader = window->findChild<MarkdownViewer *>();
  QVERIFY(privateReader);
  QVERIFY(waitForCurrentRender(privateReader, native, QStringLiteral("Protected alpha")));
  const auto alphaToken = privateReader->adapter()->documentGeneration();
  QVERIFY(alphaToken != ordinaryToken && native->isCurrent(alphaToken));
  QCOMPARE(window->findChild<MarkdownRenderController *>(), native);
  QCOMPARE(privateReader->jobSession()->property("markdownJobBufferId").toString(), a.id());
  QVERIFY(privateReader->jobSession()->property("markdownJobProtected").toBool());
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
  QVERIFY(waitForRender(siblingReader, QStringLiteral("Protected alpha")));
  f->controller()->focusWindow(first, a.id());
  auto *jobs = window->findChild<MarkdownWebJobController *>();
  QVERIFY(jobs);
  struct AnchorCompletion {
    int count = 0;
    bool cancelled = false;
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
      native->fetchHeadingAnchor(
          alphaSource, 0, {}, editor->getHighlighter()->currentDocument(),
          [anchorCompletion](const MarkdownRenderController::HeadingAnchorResult &result) {
            ++anchorCompletion->count;
            anchorCompletion->cancelled = result.cancelled;
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
  QVERIFY(anchorCompletion->cancelled);
  auto *newReader = window->findChild<MarkdownViewer *>();
  QVERIFY(newReader);
  QVERIFY(newReader->page()->url() != oldUrl);
  QVERIFY(waitForCurrentRender(newReader, native, QStringLiteral("Protected beta")));
  const auto betaToken = newReader->adapter()->documentGeneration();
  QVERIFY(betaToken != alphaToken && native->isCurrent(betaToken));
  QCOMPARE(window->findChild<MarkdownRenderController *>(), native);
  QCOMPARE(window->findChildren<MarkdownRenderController *>().size(), 1);
  QCOMPARE(newReader->jobSession()->property("markdownJobBufferId").toString(), b.id());
  QVERIFY(newReader->jobSession()->property("markdownJobProtected").toBool());
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
  QVERIFY(waitForCurrentRender(publicReader, native, QStringLiteral("Public end")));
  const auto publicToken = publicReader->adapter()->documentGeneration();
  QVERIFY(publicToken != betaToken && native->isCurrent(publicToken));
  QCOMPARE(window->findChild<MarkdownRenderController *>(), native);
  QCOMPARE(publicReader->jobSession()->property("markdownJobBufferId").toString(),
           window->getBuffer().id());
  QVERIFY(!publicReader->jobSession()->property("markdownJobProtected").toBool());
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
