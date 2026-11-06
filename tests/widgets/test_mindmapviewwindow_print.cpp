#include <QtTest>

#include <QBuffer>
#include <QDir>
#include <QDockWidget>
#include <QFile>
#include <QFileInfo>
#include <QGraphicsScene>
#include <QGraphicsTextItem>
#include <QGraphicsView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLineEdit>
#include <QMainWindow>
#include <QPainter>
#include <QPdfDocument>
#include <QPdfSelection>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPrinter>
#include <QScrollArea>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QTemporaryDir>
#include <QToolButton>
#include <QtConcurrent/QtConcurrentRun>

#include <controllers/outlinecontroller.h>
#include <core/configmgr2.h>
#include <core/editorconfig.h>
#include <core/mainconfig.h>
#include <core/mindmapeditorconfig.h>
#include <core/servicelocator.h>
#include <core/services/bufferservice.h>
#include <core/services/configcoreservice.h>
#include <core/services/hookmanager.h>
#include <core/services/notebookcoreservice.h>
#include <core/services/notebookiogate.h>
#include <core/services/syncworkqueuemanager.h>
#include <gui/services/themeservice.h>
#include <vxcore/vxcore.h>
#include <widgets/detachedwindow.h>
#include <widgets/editors/mindmapeditor.h>
#include <widgets/mindmapviewwindow2.h>
#include <widgets/outlineprovider.h>
#include <widgets/toolspanelhost.h>
#include <widgets/viewsplit2.h>

#include <limits>
#include <memory>

using namespace vnotex;

namespace tests {
namespace {
class Window : public MindMapViewWindow2 {
public:
  using MindMapViewWindow2::MindMapViewWindow2;
  using MindMapViewWindow2::printDocument;
  using MindMapViewWindow2::syncEditorFromBuffer;
  ~Window() override {
    getServices().get<BufferService>()->unregisterActiveWriter(getBuffer().id(),
                                                               reinterpret_cast<quintptr>(this));
  }
};

const QColor c_imageColor(25, 164, 120);

QByteArray fixture() {
  QImage image(80, 40, QImage::Format_RGB32);
  image.fill(c_imageColor);
  QByteArray bytes;
  QBuffer buffer(&bytes);
  buffer.open(QIODevice::WriteOnly);
  image.save(&buffer, "PNG");
  auto document = QJsonDocument::fromJson(QByteArrayLiteral(R"({
    "schemaVersion":1,"rootId":"r","orphanIds":["o"],"nodes":[
      {"id":"r","topic":"RootPrintToken","children":["a"]},
      {"id":"a","topic":"ChildPrintToken","children":["b"]},
      {"id":"b","topic":"FoldedPrintToken"},
      {"id":"o","topic":"OrphanPrintToken"}],
    "crossLinks":[{"id":"l","source":"a","target":"o","directed":true,
                   "topic":"LinkPrintToken"}]})"))
                      .object();
  auto nodes = document.value(QStringLiteral("nodes")).toArray();
  auto root = nodes.at(0).toObject();
  root.insert(QStringLiteral("image"),
              QJsonObject{{QStringLiteral("url"), QStringLiteral("data:image/png;base64,") +
                                                      QString::fromLatin1(bytes.toBase64())},
                          {QStringLiteral("width"), 80},
                          {QStringLiteral("height"), 40}});
  nodes[0] = root;
  document.insert(QStringLiteral("nodes"), nodes);
  return QJsonDocument(document).toJson(QJsonDocument::Compact);
}

QByteArray reorderFixture() {
  return QByteArrayLiteral(R"({
    "schemaVersion":1,"rootId":"r","orphanIds":["o","p"],"nodes":[
      {"id":"r","topic":"r","children":["a","c","d"]},
      {"id":"a","topic":"a","children":["b"],"note":"Preserved note"},
      {"id":"b","topic":"b","children":["b1"]},
      {"id":"b1","topic":"b1","children":["b2"]},
      {"id":"b2","topic":"b2","children":["b3"]},
      {"id":"b3","topic":"b3","children":["b4"]},
      {"id":"b4","topic":"b4","children":["b5"]},
      {"id":"b5","topic":"b5"},
      {"id":"c","topic":"c","children":["c1"]},
      {"id":"c1","topic":"c1"},
      {"id":"d","topic":"d"},
      {"id":"o","topic":"o","children":["ox"]},
      {"id":"ox","topic":"ox"},
      {"id":"p","topic":"p"}],
    "crossLinks":[{"id":"l","source":"a","target":"o","directed":true,
                   "topic":"Preserved cross-link"}]})");
}

QStringList outlineNodeIds(const MindMapEditor &p_editor) {
  QStringList ids;
  const auto entries = p_editor.outline();
  ids.reserve(entries.size());
  for (const auto &entry : entries) {
    ids.append(entry.id);
  }
  return ids;
}

bool hasVisibleTopic(const MindMapEditor &p_editor, const QString &p_topic) {
  const auto *view = p_editor.findChild<QGraphicsView *>();
  if (!view || !view->scene()) {
    return false;
  }
  for (auto *item : view->scene()->items()) {
    const auto *label = dynamic_cast<QGraphicsTextItem *>(item);
    if (label && label->isVisible() && label->toPlainText() == p_topic) {
      return true;
    }
  }
  return false;
}

bool containsImageColor(const QImage &p_image) {
  int matches = 0;
  for (int y = 0; y < p_image.height(); ++y) {
    for (int x = 0; x < p_image.width(); ++x) {
      // QPrinter's PDF engine JPEG-encodes images, with small channel-rounding differences.
      const auto pixel = p_image.pixelColor(x, y);
      if (qAbs(pixel.red() - c_imageColor.red()) <= 3 &&
          qAbs(pixel.green() - c_imageColor.green()) <= 3 &&
          qAbs(pixel.blue() - c_imageColor.blue()) <= 3 && ++matches == 32) {
        return true;
      }
    }
  }
  return false;
}

QImage mapImage(MindMapEditor &p_editor) {
  QImage image(800, 800, QImage::Format_RGB32);
  image.fill(Qt::white);
  QPainter painter(&image);
  return p_editor.renderMap(painter, image.rect()) ? image : QImage();
}

QSharedPointer<QPrinter> pdfPrinter(const QString &p_path) {
  auto printer = QSharedPointer<QPrinter>::create();
  printer->setOutputFormat(QPrinter::PdfFormat);
  printer->setOutputFileName(p_path);
  return printer;
}

MindMapEditor *showWindow(Window &p_window) {
  p_window.resize(1000, 700);
  p_window.show();
  p_window.activateWindow();
  QApplication::setActiveWindow(&p_window);
  auto *editor = p_window.findChild<MindMapEditor *>();
  if (editor) {
    editor->setFocus();
  }
  QCoreApplication::processEvents();
  return editor;
}

QPlainTextEdit *beginDraft(MindMapEditor &p_editor) {
  auto *view = p_editor.findChild<QGraphicsView *>();
  if (!view) {
    return nullptr;
  }
  p_editor.selectNode(QStringLiteral("r"));
  view->setFocus();
  QTest::keyClick(view->viewport(), Qt::Key_F2);
  QPlainTextEdit *input = nullptr;
  const bool visible = QTest::qWaitFor([&] {
    input = view->findChild<QPlainTextEdit *>(QStringLiteral("topicEditor"));
    return input && input->isVisible();
  });
  return visible ? input : nullptr;
}
} // namespace

class TestMindMapViewWindowPrint : public QObject {
  Q_OBJECT
private slots:
  void init();
  void cleanup();
  void printsLayoutWithoutCameraOrSelection_data();
  void printsLayoutWithoutCameraOrSelection();
  void printsCurrentDraftAndRejectsReplacement();
  void rejectsUnavailableDocuments();
  void respectsSinglePageRange();
  void renderFailurePreservesState();
  void livePropertiesHostHandoff_data();
  void livePropertiesHostHandoff();
  void detachedPropertiesPanel_data();
  void detachedPropertiesPanel();
  void outlineReorder_data();
  void outlineReorder();
  void outlineRejectsRootAndInvalidMoves();
  void outlineRejectsStaleConfirmation_data();
  void outlineRejectsStaleConfirmation();
  void outlineRejectsUnavailable_data();
  void outlineRejectsUnavailable();
  void outlineRejectsProtectedLocking();

private:
  Buffer2 openNote(const QString &p_name, const QByteArray &p_content, bool p_readOnly = false);
  Buffer2 openProtectedNote();

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
  QString m_notebookId;
  QJsonObject m_originalConfig;
  QStringList m_appSearchPaths;
};

void TestMindMapViewWindowPrint::init() {
  vxcore_set_test_mode(1);
  QVERIFY(m_directory.isValid());
  m_appSearchPaths = QDir::searchPaths(QStringLiteral("app"));
  const QString rcc = QStringLiteral(VNOTE_TEST_EXTRA_RCC);
  QVERIFY(QFileInfo::exists(rcc));
  QCOMPARE(vxcore_context_create(nullptr, &m_context), VXCORE_OK);
  m_configCore = std::make_unique<ConfigCoreService>(m_context);
  m_config = std::make_unique<ConfigMgr2>(m_configCore.get());
  m_config->init();
  QDir::setSearchPaths(QStringLiteral("app"), {QFileInfo(rcc).absolutePath()});
  m_config->initAfterQtAppStarted();
  m_originalConfig = m_config->getConfig().toJson();
  m_config->getEditorConfig().getMindMapEditorConfig().setAutoSectionNumbering(false);
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
  m_services.registerService(m_configCore.get());
  m_services.registerService(m_config.get());
  m_services.registerService(m_hooks.get());
  m_services.registerService(m_gate.get());
  m_services.registerService(m_queues.get());
  m_services.registerService(m_notebooks.get());
  m_services.registerService(m_buffers.get());
  m_services.registerService(m_theme.get());
  m_notebookId = m_notebooks->createNotebook(
      m_directory.filePath(QString::fromLatin1(QTest::currentTestFunction()) +
                           QString::fromLatin1(QTest::currentDataTag())),
      QStringLiteral("{\"name\":\"Mind-map print regression\"}"), NotebookType::Bundled);
  QVERIFY(!m_notebookId.isEmpty());
}

void TestMindMapViewWindowPrint::cleanup() {
  if (m_config) {
    m_config->updateMainConfig(m_originalConfig);
  }
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
  m_config.reset();
  m_configCore.reset();
  if (m_context) {
    vxcore_context_destroy(m_context);
    m_context = nullptr;
  }
  m_notebookId.clear();
  QDir::setSearchPaths(QStringLiteral("app"), m_appSearchPaths);
}

Buffer2 TestMindMapViewWindowPrint::openNote(const QString &p_name, const QByteArray &p_content,
                                             bool p_readOnly) {
  if (m_notebooks->createFile(m_notebookId, QString(), p_name).isEmpty()) {
    return {};
  }
  QFile file(m_notebooks->buildAbsolutePath(m_notebookId, p_name));
  if (!file.open(QIODevice::WriteOnly) || file.write(p_content) != p_content.size()) {
    return {};
  }
  file.close();
  FileOpenSettings settings;
  settings.m_readOnly = p_readOnly;
  return m_buffers->openBuffer({m_notebookId, p_name}, settings);
}

Buffer2 TestMindMapViewWindowPrint::openProtectedNote() {
  QString noteId;
  const auto result = QtConcurrent::run([&] {
                        auto setup = m_notebooks->prepareNotebookEncryption(
                            m_notebookId, QString(), QByteArrayLiteral("mindmap-print-password"));
                        if (!setup.isValid()) {
                          return setup.m_error;
                        }
                        auto maintenance = m_queues->tryAcquireMaintenance({m_notebookId});
                        if (!maintenance.isValid()) {
                          return VXCORE_ERR_INVALID_STATE;
                        }
                        NotebookIoGate::ScopedLock lock(*m_gate, m_notebookId);
                        const auto error = m_notebooks->commitNotebookEncryption(setup);
                        return error == VXCORE_OK
                                   ? m_notebooks->createEncryptedNote(
                                         m_notebookId, QString(), QStringLiteral("private.emind"),
                                         QStringLiteral("mindmap"), fixture(), &noteId)
                                   : error;
                      }).result();
  return result == VXCORE_OK ? m_buffers->openBufferByNodeId(noteId) : Buffer2();
}

void TestMindMapViewWindowPrint::outlineReorder_data() {
  QTest::addColumn<QString>("sourceId");
  QTest::addColumn<QString>("targetId");
  QTest::addColumn<int>("position");
  QTest::addColumn<QString>("parentId");
  QTest::addColumn<int>("targetLevel");
  QTest::addColumn<QString>("expectedOrder");
  using Drop = OutlineDropPosition;
  QTest::newRow("sibling-above") << QStringLiteral("d") << QStringLiteral("a")
                                 << int(Drop::AboveItem) << QStringLiteral("r") << 2
                                 << QStringLiteral("r d a b b1 b2 b3 b4 b5 c c1 o ox p");
  QTest::newRow("sibling-below-after-source-removal")
      << QStringLiteral("a") << QStringLiteral("c") << int(Drop::BelowItem) << QStringLiteral("r")
      << 2 << QStringLiteral("r c c1 a b b1 b2 b3 b4 b5 d o ox p");
  QTest::newRow("last-root-child-before-orphan")
      << QStringLiteral("a") << QStringLiteral("d") << int(Drop::BelowItem) << QStringLiteral("r")
      << 2 << QStringLiteral("r c c1 d a b b1 b2 b3 b4 b5 o ox p");
  QTest::newRow("reparent-before-foreign-sibling")
      << QStringLiteral("d") << QStringLiteral("a") << int(Drop::OnItem) << QStringLiteral("a") << 3
      << QStringLiteral("r a b b1 b2 b3 b4 b5 d c c1 o ox p");
  QTest::newRow("root-drop-target")
      << QStringLiteral("o") << QStringLiteral("r") << int(Drop::OnItem) << QStringLiteral("r") << 2
      << QStringLiteral("r a b b1 b2 b3 b4 b5 c c1 d o ox p");
  QTest::newRow("explicit-last-parent-without-anchor")
      << QStringLiteral("a") << QStringLiteral("p") << int(Drop::OnItem) << QStringLiteral("p") << 2
      << QStringLiteral("r c c1 d o ox p a b b1 b2 b3 b4 b5");
  QTest::newRow("viewport-creates-orphan")
      << QStringLiteral("a") << QStringLiteral("") << int(Drop::OnViewport) << QStringLiteral("")
      << 1 << QStringLiteral("r c c1 d o ox p a b b1 b2 b3 b4 b5");
  QTest::newRow("orphan-order") << QStringLiteral("p") << QStringLiteral("o")
                                << int(Drop::AboveItem) << QStringLiteral("") << 1
                                << QStringLiteral("r a b b1 b2 b3 b4 b5 c c1 d p o ox");
  QTest::newRow("orphan-reparent")
      << QStringLiteral("o") << QStringLiteral("a") << int(Drop::OnItem) << QStringLiteral("a") << 3
      << QStringLiteral("r a b b1 b2 b3 b4 b5 o ox c c1 d p");
  QTest::newRow("level-nine") << QStringLiteral("d") << QStringLiteral("b5") << int(Drop::OnItem)
                              << QStringLiteral("b5") << 9
                              << QStringLiteral("r a b b1 b2 b3 b4 b5 d c c1 o ox p");
  QTest::newRow("deep-descendants")
      << QStringLiteral("c") << QStringLiteral("b4") << int(Drop::OnItem) << QStringLiteral("b4")
      << 8 << QStringLiteral("r a b b1 b2 b3 b4 b5 c c1 d o ox p");
  QTest::newRow("deep-viewport-orphan")
      << QStringLiteral("b4") << QStringLiteral("") << int(Drop::OnViewport) << QStringLiteral("")
      << 1 << QStringLiteral("r a b b1 b2 b3 c c1 d o ox p b4 b5");
}

void TestMindMapViewWindowPrint::outlineReorder() {
  QFETCH(QString, sourceId);
  QFETCH(QString, targetId);
  QFETCH(int, position);
  QFETCH(QString, parentId);
  QFETCH(int, targetLevel);
  QFETCH(QString, expectedOrder);
  const auto buffer = openNote(QStringLiteral("outline.emind"), reorderFixture());
  QVERIFY(buffer.isValid());
  Window window(m_services, buffer);
  auto *editor = showWindow(window);
  QVERIFY(editor);
  QVERIFY(editor->canReorderOutline());
  QVERIFY(editor->setExpanded(QStringLiteral("a"), false));
  QVERIFY(editor->setExpanded(QStringLiteral("c"), false));
  QVERIFY(!hasVisibleTopic(*editor, QStringLiteral("b")));
  QVERIFY(!hasVisibleTopic(*editor, QStringLiteral("c1")));
  const auto original = editor->toJson();
  const auto originalMap = mapImage(*editor);
  const auto ids = outlineNodeIds(*editor);
  const auto provider = window.getOutlineProvider();
  const auto snapshot = provider->getOutline();
  QVERIFY(snapshot->m_reorderSupported);
  QCOMPARE(snapshot->m_maxLevel, std::numeric_limits<int>::max());
  QCOMPARE(snapshot->m_headings.size(), ids.size());
  QVERIFY(!snapshot->m_headings.first().m_reorderable);
  QVERIFY(snapshot->m_headings.first().m_acceptsChildren);
  QVERIFY(snapshot->m_headings.at(ids.indexOf(QStringLiteral("b5"))).m_reorderable);
  const int source = ids.indexOf(sourceId);
  const int target = ids.indexOf(targetId);
  const auto drop = static_cast<OutlineDropPosition>(position);
  const auto move = snapshot->planMove(source, target, drop);
  QVERIFY(move.isValid());
  QCOMPARE(move.m_parentHeadingIndex, ids.indexOf(parentId));
  QCOMPARE(move.m_targetLevel, targetLevel);

  OutlineController controller(m_services);
  controller.setOutlineProvider(provider);
  QSignalSpy confirmation(&controller, &OutlineController::reorderConfirmationRequested);
  QSignalSpy changed(editor, &MindMapEditor::documentChanged);
  QSignalSpy dirty(editor, &MindMapEditor::contentsChanged);
  controller.requestItemMove(source, target, drop);
  QCOMPARE(confirmation.count(), 1);
  QCOMPARE(editor->toJson(), original);
  controller.confirmReorder(true);
  QCOMPARE(changed.count(), 1);
  QCOMPARE(dirty.count(), 1);
  QVERIFY(window.isModified());
  QVERIFY(editor->isModified());
  const auto movedIds = outlineNodeIds(*editor);
  QCOMPARE(movedIds, expectedOrder.split(QLatin1Char(' ')));
  const auto movedEntries = editor->outline();
  const int movedSource = movedIds.indexOf(sourceId);
  QCOMPARE(movedEntries.at(movedSource).level, targetLevel);
  const auto movedDocument = QJsonDocument::fromJson(editor->toJson()).object();
  const auto originalDocument = QJsonDocument::fromJson(original).object();
  QCOMPARE(movedDocument.value(QStringLiteral("crossLinks")),
           originalDocument.value(QStringLiteral("crossLinks")));
  QCOMPARE(QJsonDocument::fromJson(editor->nodeJson(QStringLiteral("a")))
               .object()
               .value(QStringLiteral("note"))
               .toString(),
           QStringLiteral("Preserved note"));
  for (int i = 0; i < movedEntries.size(); ++i) {
    QCOMPARE(provider->getOutline()->m_headings.at(i).m_name, movedEntries.at(i).topic);
    QCOMPARE(provider->getOutline()->m_headings.at(i).m_level, movedEntries.at(i).level);
  }
  QVERIFY(!hasVisibleTopic(*editor, QStringLiteral("b")));
  QVERIFY(!hasVisibleTopic(*editor, QStringLiteral("c1")));
  QVERIFY(editor->canUndo());
  QVERIFY(editor->undo());
  QCOMPARE(editor->toJson(), original);
  QVERIFY(!editor->canUndo());
  QCOMPARE(mapImage(*editor), originalMap);
  QCOMPARE(outlineNodeIds(*editor), ids);
  QVERIFY(editor->redo());
  QCOMPARE(outlineNodeIds(*editor), expectedOrder.split(QLatin1Char(' ')));
}

void TestMindMapViewWindowPrint::outlineRejectsRootAndInvalidMoves() {
  const auto buffer = openNote(QStringLiteral("invalid-moves.emind"), reorderFixture());
  QVERIFY(buffer.isValid());
  Window window(m_services, buffer);
  auto *editor = showWindow(window);
  QVERIFY(editor);
  const auto provider = window.getOutlineProvider();
  const auto snapshot = provider->getOutline();
  const auto ids = outlineNodeIds(*editor);
  const int a = ids.indexOf(QStringLiteral("a"));
  const int b = ids.indexOf(QStringLiteral("b"));
  const int d = ids.indexOf(QStringLiteral("d"));
  const auto original = editor->toJson();
  OutlineController controller(m_services);
  controller.setOutlineProvider(provider);
  QSignalSpy confirmation(&controller, &OutlineController::reorderConfirmationRequested);
  QSignalSpy changed(editor, &MindMapEditor::documentChanged);
  controller.requestItemMove(0, a, OutlineDropPosition::OnItem);
  controller.requestItemMove(0, -1, OutlineDropPosition::OnViewport);
  controller.requestItemMove(a, 0, OutlineDropPosition::AboveItem);
  controller.requestItemMove(a, 0, OutlineDropPosition::BelowItem);
  controller.requestItemMove(a, b, OutlineDropPosition::OnItem);
  QCOMPARE(confirmation.count(), 0);
  controller.confirmReorder(true);
  auto malformed = snapshot->planMove(d, a, OutlineDropPosition::OnItem);
  QVERIFY(malformed.isValid());
  malformed.m_sourceHeadingIndex = 0;
  provider->requestMove(malformed);
  malformed.m_sourceHeadingIndex = d;
  malformed.m_parentHeadingIndex = d;
  provider->requestMove(malformed);
  malformed.m_parentHeadingIndex = a;
  malformed.m_targetLevel = 1;
  provider->requestMove(malformed);
  QCOMPARE(editor->toJson(), original);
  QVERIFY(changed.isEmpty());
  QVERIFY(!editor->canUndo());
}

void TestMindMapViewWindowPrint::outlineRejectsStaleConfirmation_data() {
  QTest::addColumn<QString>("transition");
  QTest::newRow("outline-change") << QStringLiteral("rename");
  QTest::newRow("buffer-generation") << QStringLiteral("reload");
  QTest::newRow("unpublished-native-preorder") << QStringLiteral("silent-move");
}

void TestMindMapViewWindowPrint::outlineRejectsStaleConfirmation() {
  QFETCH(QString, transition);
  const auto buffer = openNote(QStringLiteral("stale.emind"), reorderFixture());
  QVERIFY(buffer.isValid());
  Window window(m_services, buffer);
  auto *editor = showWindow(window);
  QVERIFY(editor);
  const auto provider = window.getOutlineProvider();
  const auto snapshot = provider->getOutline();
  const auto ids = outlineNodeIds(*editor);
  OutlineController controller(m_services);
  controller.setOutlineProvider(provider);
  QSignalSpy confirmation(&controller, &OutlineController::reorderConfirmationRequested);
  controller.requestItemMove(ids.indexOf(QStringLiteral("d")), ids.indexOf(QStringLiteral("a")),
                             OutlineDropPosition::AboveItem);
  QCOMPARE(confirmation.count(), 1);
  if (transition == QLatin1String("rename")) {
    QVERIFY(editor->renameNode(QStringLiteral("a"), QStringLiteral("Changed while confirming")));
    QVERIFY(provider->getOutline() != snapshot);
  } else if (transition == QLatin1String("reload")) {
    window.syncEditorFromBuffer();
    QVERIFY(provider->getOutline() != snapshot);
  } else {
    // Simulate an unreported native change: pointer identity alone must not authorize old IDs.
    const QSignalBlocker blocker(editor);
    QVERIFY(editor->reorderOutlineNode(QStringLiteral("d"), QStringLiteral("a"), -1));
    QCOMPARE(provider->getOutline(), snapshot);
  }
  const auto current = editor->toJson();
  QSignalSpy changed(editor, &MindMapEditor::documentChanged);
  controller.confirmReorder(true);
  QCOMPARE(editor->toJson(), current);
  QVERIFY(changed.isEmpty());
}

void TestMindMapViewWindowPrint::outlineRejectsUnavailable_data() {
  QTest::addColumn<QString>("transition");
  QTest::newRow("read-only-buffer") << QStringLiteral("buffer-read-only");
  QTest::newRow("read-only-editor") << QStringLiteral("editor-read-only");
  QTest::newRow("disabled-window") << QStringLiteral("disabled");
  QTest::newRow("pending-draft") << QStringLiteral("draft");
  QTest::newRow("invalid-content") << QStringLiteral("invalid-content");
}

void TestMindMapViewWindowPrint::outlineRejectsUnavailable() {
  QFETCH(QString, transition);
  const auto buffer = openNote(QStringLiteral("unavailable.emind"), reorderFixture(),
                               transition == QLatin1String("buffer-read-only"));
  QVERIFY(buffer.isValid());
  Window window(m_services, buffer);
  auto *editor = showWindow(window);
  QVERIFY(editor);
  const auto provider = window.getOutlineProvider();
  const auto ids = outlineNodeIds(*editor);
  const int source = ids.indexOf(QStringLiteral("d"));
  const int target = ids.indexOf(QStringLiteral("a"));
  OutlineController controller(m_services);
  controller.setOutlineProvider(provider);
  QSignalSpy confirmation(&controller, &OutlineController::reorderConfirmationRequested);
  controller.requestItemMove(source, target, OutlineDropPosition::AboveItem);
  QCOMPARE(confirmation.count(), transition == QLatin1String("buffer-read-only") ? 0 : 1);
  QPointer<QPlainTextEdit> draft;
  if (transition == QLatin1String("editor-read-only")) {
    editor->setReadOnly(true);
  } else if (transition == QLatin1String("disabled")) {
    window.setEnabled(false);
  } else if (transition == QLatin1String("draft")) {
    draft = beginDraft(*editor);
    QVERIFY(draft);
    draft->setPlainText(QStringLiteral("Uncommitted outline draft"));
    QVERIFY(editor->hasPendingEdit());
  } else if (transition == QLatin1String("invalid-content")) {
    QVERIFY(!editor->loadContent(QByteArrayLiteral("invalid JSON")));
  }
  QVERIFY(!editor->canReorderOutline());
  QVERIFY(!provider->getOutline()->m_reorderSupported);
  const auto current = editor->toJson();
  QSignalSpy changed(editor, &MindMapEditor::documentChanged);
  controller.confirmReorder(true);
  OutlineMove direct;
  direct.m_sourceHeadingIndex = source;
  direct.m_beforeHeadingIndex = target;
  direct.m_targetLevel = 2;
  direct.m_parentHeadingIndex = 0;
  provider->requestMove(direct);
  QVERIFY(!editor->reorderOutlineNode(QStringLiteral("d"), QStringLiteral("r"), 0));
  QCOMPARE(editor->toJson(), current);
  QVERIFY(changed.isEmpty());
  QVERIFY(!editor->canUndo());
  if (draft) {
    QVERIFY(editor->hasPendingEdit());
    QCOMPARE(draft->toPlainText(), QStringLiteral("Uncommitted outline draft"));
    QTest::keyClick(draft, Qt::Key_Escape);
    QVERIFY(!editor->hasPendingEdit());
    QVERIFY(provider->getOutline()->m_reorderSupported);
  } else if (transition == QLatin1String("editor-read-only")) {
    editor->setReadOnly(false);
    QVERIFY(provider->getOutline()->m_reorderSupported);
  } else if (transition == QLatin1String("disabled")) {
    window.setEnabled(true);
    QVERIFY(provider->getOutline()->m_reorderSupported);
  }
}

void TestMindMapViewWindowPrint::outlineRejectsProtectedLocking() {
  const auto buffer = openProtectedNote();
  QVERIFY(buffer.isValid() && buffer.isEncrypted());
  Window window(m_services, buffer);
  auto *editor = showWindow(window);
  QVERIFY(editor);
  const auto provider = window.getOutlineProvider();
  QVERIFY(provider->getOutline()->m_reorderSupported);
  const auto ids = outlineNodeIds(*editor);
  OutlineController controller(m_services);
  controller.setOutlineProvider(provider);
  QSignalSpy confirmation(&controller, &OutlineController::reorderConfirmationRequested);
  controller.requestItemMove(ids.indexOf(QStringLiteral("o")), 0, OutlineDropPosition::OnItem);
  QCOMPARE(confirmation.count(), 1);
  const auto current = editor->toJson();
  QVERIFY(m_buffers->beginProtectedLocking());
  QVERIFY(!editor->canReorderOutline());
  QVERIFY(!provider->getOutline()->m_reorderSupported);
  QSignalSpy changed(editor, &MindMapEditor::documentChanged);
  controller.confirmReorder(true);
  QVERIFY(!editor->reorderOutlineNode(QStringLiteral("o"), QStringLiteral("r"), -1));
  QCOMPARE(editor->toJson(), current);
  QVERIFY(changed.isEmpty());
  m_buffers->cancelProtectedLocking();
  QVERIFY(editor->canReorderOutline());
  QVERIFY(provider->getOutline()->m_reorderSupported);
}

void TestMindMapViewWindowPrint::livePropertiesHostHandoff_data() {
  QTest::addColumn<bool>("initiallySeparated");
  QTest::addColumn<bool>("propertiesFocus");
  QTest::newRow("embedded-properties-focus") << false << true;
  QTest::newRow("separated-properties-focus") << true << true;
  QTest::newRow("embedded-inline-draft") << false << false;
  QTest::newRow("separated-inline-draft") << true << false;
}

void TestMindMapViewWindowPrint::livePropertiesHostHandoff() {
  QFETCH(bool, initiallySeparated);
  QFETCH(bool, propertiesFocus);
  auto &config = m_config->getEditorConfig().getMindMapEditorConfig();
  config.setSeparateNodePropertiesPanel(initiallySeparated);
  const auto buffer = openNote(QStringLiteral("live-properties.emind"), fixture());
  QVERIFY(buffer.isValid());
  QMainWindow shell;
  Window window(m_services, buffer, &shell);
  shell.setCentralWidget(&window);
  auto *dock = new QDockWidget(&shell);
  auto *host = new ToolsPanelHost(m_services, dock);
  dock->setWidget(host);
  shell.addDockWidget(Qt::RightDockWidgetArea, dock);
  auto *editor = window.findChild<MindMapEditor *>();
  QVERIFY(editor);
  QPointer<QWidget> panel = editor->findChild<QWidget *>(QStringLiteral("nodePropertiesPanel"));
  QVERIFY(panel);
  auto *toggle = panel->findChild<QToolButton *>(QStringLiteral("nodePropertiesToggle"));
  auto *scroll = panel->findChild<QScrollArea *>();
  auto *url = panel->findChild<QLineEdit *>(QStringLiteral("nodeUrl"));
  QVERIFY(toggle && scroll && url);
  const auto bindPanel = [&]() {
    host->setPanel(&window, window.IsToolsPanelSupported() ? window.getToolsPanel() : nullptr);
  };
  bindPanel();
  shell.resize(1300, 900);
  shell.show();
  shell.activateWindow();
  QApplication::setActiveWindow(&shell);
  QVERIFY(QTest::qWaitForWindowExposed(&shell));
  auto *view = editor->findChild<QGraphicsView *>();
  QVERIFY(view);
  view->setFocus();
  QTRY_VERIFY(view->hasFocus());
  QVERIFY(editor->selectNode(QStringLiteral("r")));
  QVERIFY(editor->setExpanded(QStringLiteral("a"), false));
  QVERIFY(editor->setLayoutDirection(m3::qt::MindMapEditor::LayoutDirection::Left));
  editor->zoom(1.375 / editor->zoomFactor());
  url->setText(QStringLiteral("https://example.test/live-properties"));
  QVERIFY(editor->renameNode(QStringLiteral("r"), QStringLiteral("History one")));
  QVERIFY(editor->renameNode(QStringLiteral("r"), QStringLiteral("History two")));
  QVERIFY(editor->undo());
  QVERIFY(editor->canUndo() && editor->canRedo());

  QPointer<QPlainTextEdit> draft;
  QTextCursor draftCursor;
  if (!propertiesFocus) {
    draft = beginDraft(*editor);
    QVERIFY(draft);
    draft->setPlainText(QStringLiteral("Uncommitted placement draft"));
    draftCursor = draft->textCursor();
    draftCursor.setPosition(4);
    draftCursor.setPosition(12, QTextCursor::KeepAnchor);
    draft->setTextCursor(draftCursor);
    QVERIFY(editor->hasPendingEdit());
  }
  bool separated = initiallySeparated;
  QStringList handoff;
  QSignalSpy about(&window, &ViewWindow2::toolsPanelAboutToChange);
  QSignalSpy changed(&window, &ViewWindow2::toolsPanelChanged);
  connect(&window, &ViewWindow2::toolsPanelAboutToChange, host, [&]() {
    handoff.append(QStringLiteral("release"));
    QCOMPARE(editor->nodePropertiesPanel(), separated ? panel.data() : nullptr);
    QCOMPARE(host->getPanel(), separated ? panel.data() : nullptr);
    host->setPanel(nullptr, nullptr);
    QVERIFY(!host->getPanel());
  });
  connect(&window, &ViewWindow2::toolsPanelChanged, host, [&]() {
    handoff.append(QStringLiteral("acquire"));
    QCOMPARE(editor->nodePropertiesPanel(), separated ? nullptr : panel.data());
    bindPanel();
  });
  QSignalSpy documentChanged(editor, &MindMapEditor::documentChanged);
  QSignalSpy dirty(editor, &MindMapEditor::contentsChanged);
  for (int transition = 0; transition < 2; ++transition) {
    if (propertiesFocus) {
      if (!separated && !toggle->isChecked()) {
        toggle->click();
      }
      scroll->ensureWidgetVisible(url);
      url->setFocus();
      QTRY_VERIFY(url->hasFocus());
    }
    const auto source = editor->toJson();
    const auto selection = editor->selectedNodeIds();
    const auto fieldText = url->text();
    const auto revision = buffer.getRevision();
    const auto direction = editor->layoutDirection();
    const bool undo = editor->canUndo();
    const bool redo = editor->canRedo();
    const bool modified = editor->isModified();
    documentChanged.clear();
    dirty.clear();
    config.setSeparateNodePropertiesPanel(!separated);
    window.handleEditorConfigChange();
    separated = !separated;
    QCOMPARE(about.count(), transition + 1);
    QCOMPARE(changed.count(), transition + 1);
    QCOMPARE(handoff, QStringList({QStringLiteral("release"), QStringLiteral("acquire")}));
    handoff.clear();
    QVERIFY(panel);
    QCOMPARE(window.findChild<MindMapEditor *>(), editor);
    QCOMPARE(editor->nodePropertiesPanel(), separated ? panel.data() : nullptr);
    QCOMPARE(host->getPanel(), separated ? panel.data() : nullptr);
    QCOMPARE(panel->parentWidget(), separated ? static_cast<QWidget *>(host) : editor);
    if (separated) {
      // Repeated acquisition must not steal an already-hosted panel back into the view.
      QCOMPARE(window.getToolsPanel(), panel.data());
      QCOMPARE(panel->parentWidget(), host);
      QVERIFY(scroll->isVisible() && toggle->isHidden());
    }
    QCOMPARE(url->text(), fieldText);
    QCOMPARE(editor->toJson(), source);
    QCOMPARE(editor->selectedNodeIds(), selection);
    QCOMPARE(editor->layoutDirection(), direction);
    QCOMPARE(editor->zoomFactor(), 1.375);
    QCOMPARE(editor->canUndo(), undo);
    QCOMPARE(editor->canRedo(), redo);
    QCOMPARE(editor->isModified(), modified);
    QVERIFY(documentChanged.isEmpty() && dirty.isEmpty());
    window.handleEditorConfigChange();
    QCOMPARE(about.count(), transition + 1);
    QCOMPARE(changed.count(), transition + 1);
    QVERIFY(handoff.isEmpty());
    if (propertiesFocus) {
      // Reparenting may drop the caret; the existing writer must remain usable without
      // refocusing or reloading the view, and controls must still edit the same node.
      QString writerText;
      QVERIFY(m_buffers->captureActiveWriterContent(buffer.id(), &writerText));
      QCOMPARE(QJsonDocument::fromJson(writerText.toUtf8()), QJsonDocument::fromJson(source));
      if (!separated && !toggle->isChecked()) {
        toggle->click();
      }
      scroll->ensureWidgetVisible(url);
      url->setFocus();
      QTRY_VERIFY(url->hasFocus());
      url->end(false);
      QTest::keyClicks(url, "/moved");
      QCOMPARE(QJsonDocument::fromJson(editor->nodeJson(QStringLiteral("r")))
                   .object()
                   .value(QStringLiteral("hyperLink"))
                   .toString(),
               fieldText + QStringLiteral("/moved"));
    } else {
      QVERIFY(draft && draft->isVisible() && draft->hasFocus());
      QVERIFY(editor->hasPendingEdit());
      QCOMPARE(draft->toPlainText(), QStringLiteral("Uncommitted placement draft"));
      QCOMPARE(draft->textCursor().position(), draftCursor.position());
      QCOMPARE(draft->textCursor().anchor(), draftCursor.anchor());
      QCOMPARE(buffer.getRevision(), revision);
    }
  }
  QString writerText;
  QVERIFY(m_buffers->captureActiveWriterContent(buffer.id(), &writerText));
  QVERIFY(writerText.contains(propertiesFocus ? url->text()
                                              : QStringLiteral("Uncommitted placement draft")));
  QVERIFY(editor->canUndo());
  host->setPanel(nullptr, nullptr);
}

void TestMindMapViewWindowPrint::detachedPropertiesPanel_data() {
  QTest::addColumn<bool>("directDetachedOpen");
  QTest::addColumn<bool>("separate");
  QTest::newRow("detach-separated") << false << true;
  QTest::newRow("detach-embedded") << false << false;
  QTest::newRow("open-detached-separated") << true << true;
  QTest::newRow("open-detached-embedded") << true << false;
}

void TestMindMapViewWindowPrint::detachedPropertiesPanel() {
  QFETCH(bool, directDetachedOpen);
  QFETCH(bool, separate);
  auto &config = m_config->getEditorConfig().getMindMapEditorConfig();
  config.setSeparateNodePropertiesPanel(separate);
  const auto buffer = openNote(QStringLiteral("detached.emind"), fixture());
  QVERIFY(buffer.isValid());
  QMainWindow shell;
  auto *mainSplit = new ViewSplit2(m_services, QStringLiteral("main"), &shell);
  shell.setCentralWidget(mainSplit);
  DetachedWindow detached(m_services, QStringLiteral("detached"));
  auto *detachedSplit = detached.getViewSplit();
  auto *dock = new QDockWidget(&shell);
  auto *host = new ToolsPanelHost(m_services, dock);
  dock->setWidget(host);
  shell.addDockWidget(Qt::RightDockWidgetArea, dock);
  auto *window = new Window(m_services, buffer);
  auto *editor = window->findChild<MindMapEditor *>();
  QVERIFY(editor);
  QPointer<QWidget> panel = editor->findChild<QWidget *>(QStringLiteral("nodePropertiesPanel"));
  QVERIFY(panel);
  auto *toggle = panel->findChild<QToolButton *>(QStringLiteral("nodePropertiesToggle"));
  auto *scroll = panel->findChild<QScrollArea *>();
  auto *url = panel->findChild<QLineEdit *>(QStringLiteral("nodeUrl"));
  QVERIFY(toggle && scroll && url);
  const auto bindPanel = [&]() {
    auto *current = mainSplit->getCurrentViewWindow();
    host->setPanel(current, current ? current->getToolsPanel() : nullptr);
    dock->setVisible(host->getPanel() != nullptr);
  };
  connect(mainSplit, &ViewSplit2::currentViewWindowChanged, host, bindPanel);
  connect(window, &ViewWindow2::toolsPanelAboutToChange, host, [&]() {
    if (host->getPanel() == panel) {
      host->setPanel(nullptr, nullptr);
    }
  });
  connect(window, &ViewWindow2::toolsPanelChanged, host, bindPanel);
  shell.resize(1300, 900);
  shell.show();
  QVERIFY(QTest::qWaitForWindowExposed(&shell));
  if (directDetachedOpen) {
    detachedSplit->addViewWindow(window);
  } else {
    mainSplit->addViewWindow(window);
    bindPanel();
    QCOMPARE(editor->nodePropertiesPanel(), separate ? panel.data() : nullptr);
  }
  QVERIFY(editor->selectNode(QStringLiteral("r")));
  QVERIFY(editor->setExpanded(QStringLiteral("a"), false));
  editor->zoom(1.375 / editor->zoomFactor());
  url->setText(QStringLiteral("https://example.test/detached"));
  const auto foldedChildVisible = [&]() {
    for (auto *item : editor->findChild<QGraphicsView *>()->scene()->items()) {
      auto *text = qgraphicsitem_cast<QGraphicsTextItem *>(item);
      if (text && text->isVisible() &&
          text->toPlainText().contains(QStringLiteral("FoldedPrintToken"))) {
        return true;
      }
    }
    return false;
  };
  const auto source = editor->toJson();
  const auto selection = editor->selectedNodeIds();
  const bool undo = editor->canUndo();
  QVERIFY(undo);
  if (!directDetachedOpen) {
    mainSplit->takeViewWindow(window);
    detachedSplit->addViewWindow(window);
  }
  detached.resize(1000, 850);
  detached.show();
  detached.activateWindow();
  QApplication::setActiveWindow(&detached);
  QVERIFY(QTest::qWaitForWindowExposed(&detached));
  QVERIFY(window->isDetached());
  QVERIFY(!window->IsToolsPanelSupported() && !editor->nodePropertiesPanel());
  QVERIFY(!host->getPanel());
  QCOMPARE(config.getSeparateNodePropertiesPanel(), separate);
  QCOMPARE(panel->parentWidget(), editor);
  QTRY_VERIFY(toggle->isVisible());
  toggle->setChecked(true);
  QVERIFY(scroll->isVisible());
  QCOMPARE(editor->toJson(), source);
  QCOMPARE(editor->selectedNodeIds(), selection);
  QCOMPARE(editor->zoomFactor(), 1.375);
  QVERIFY(!foldedChildVisible());
  QCOMPARE(editor->canUndo(), undo);
  QPointer<QPlainTextEdit> draft = beginDraft(*editor);
  QVERIFY(draft);
  draft->setPlainText(QStringLiteral("Detached settings draft"));
  // Reattachment must use the new preference, not a detach-time snapshot.
  const bool separateOnReturn = !separate;
  for (bool preference : {false, true, separateOnReturn}) {
    config.setSeparateNodePropertiesPanel(preference);
    window->handleEditorConfigChange();
    QVERIFY(window->isDetached() && !window->IsToolsPanelSupported());
    QVERIFY(!editor->nodePropertiesPanel() && !host->getPanel());
    QCOMPARE(panel->parentWidget(), editor);
    QVERIFY(draft && draft->isVisible() && editor->hasPendingEdit());
    QCOMPARE(draft->toPlainText(), QStringLiteral("Detached settings draft"));
    QCOMPARE(editor->toJson(), source);
    QCOMPARE(config.getSeparateNodePropertiesPanel(), preference);
  }
  QTest::keyClick(draft, Qt::Key_Escape);
  QVERIFY(!editor->hasPendingEdit());
  detachedSplit->takeViewWindow(window);
  mainSplit->addViewWindow(window);
  bindPanel();
  QVERIFY(!window->isDetached());
  QCOMPARE(window->findChild<MindMapEditor *>(), editor);
  QCOMPARE(editor->nodePropertiesPanel(), separateOnReturn ? panel.data() : nullptr);
  QCOMPARE(host->getPanel(), separateOnReturn ? panel.data() : nullptr);
  QCOMPARE(panel->parentWidget(), separateOnReturn ? static_cast<QWidget *>(host) : editor);
  QCOMPARE(url->text(), QStringLiteral("https://example.test/detached"));
  QCOMPARE(editor->toJson(), source);
  QCOMPARE(editor->selectedNodeIds(), selection);
  QCOMPARE(editor->zoomFactor(), 1.375);
  QCOMPARE(editor->canUndo(), undo);
  QVERIFY(!foldedChildVisible());
  host->setPanel(nullptr, nullptr);
}

void TestMindMapViewWindowPrint::printsLayoutWithoutCameraOrSelection_data() {
  using Direction = m3::qt::MindMapEditor::LayoutDirection;
  QTest::addColumn<int>("direction");
  QTest::addColumn<bool>("readOnly");
  QTest::newRow("Balanced") << int(Direction::Balanced) << false;
  QTest::newRow("Left") << int(Direction::Left) << false;
  QTest::newRow("Right") << int(Direction::Right) << false;
  QTest::newRow("Outline-readonly") << int(Direction::Outline) << true;
}

void TestMindMapViewWindowPrint::printsLayoutWithoutCameraOrSelection() {
  QFETCH(int, direction);
  QFETCH(bool, readOnly);
  const auto buffer = openNote(QStringLiteral("map.emind"), fixture(), readOnly);
  QVERIFY(buffer.isValid());
  Window window(m_services, buffer);
  auto *editor = showWindow(window);
  QVERIFY(editor);
  QVERIFY(
      editor->setLayoutDirection(static_cast<m3::qt::MindMapEditor::LayoutDirection>(direction)));
  QVERIFY(editor->setExpanded(QStringLiteral("a"), false));
  QTRY_VERIFY(containsImageColor(mapImage(*editor)));
  QVERIFY(editor->selectNode(QStringLiteral("r")));
  editor->zoom(2.5 / editor->zoomFactor());
  auto *view = editor->findChild<QGraphicsView *>();
  QVERIFY(view);
  view->horizontalScrollBar()->setValue(view->horizontalScrollBar()->maximum());
  const auto source = editor->toJson();
  const auto selection = editor->selectedNodeIds();
  const auto camera = view->viewportTransform();
  const bool undo = editor->canUndo();
  QSignalSpy changed(editor, &MindMapEditor::documentChanged);
  const auto output = m_directory.filePath(QStringLiteral("map.pdf"));
  window.printDocument(pdfPrinter(output));
  QPdfDocument pdf;
  QCOMPARE(pdf.load(output), QPdfDocument::Error::None);
  QCOMPARE(pdf.pageCount(), 1);
  const auto text = pdf.getAllText(0).text();
  for (const auto *token :
       {"RootPrintToken", "ChildPrintToken", "OrphanPrintToken", "LinkPrintToken"}) {
    QVERIFY2(text.contains(QString::fromLatin1(token)), qPrintable(text));
  }
  QVERIFY(!text.contains(QStringLiteral("FoldedPrintToken")));
  const auto pixels = pdf.render(0, QSize(900, 1200));
  QVERIFY(containsImageColor(pixels));
  QCOMPARE(editor->toJson(), source);
  QCOMPARE(editor->selectedNodeIds(), selection);
  QCOMPARE(view->viewportTransform(), camera);
  QCOMPARE(editor->canUndo(), undo);
  QCOMPARE(changed.count(), 0);
  QVERIFY(editor->selectLink(QStringLiteral("l")));
  editor->zoom(0.8 / editor->zoomFactor());
  window.printDocument(pdfPrinter(m_directory.filePath(QStringLiteral("different-camera.pdf"))));
  QPdfDocument other;
  QCOMPARE(other.load(m_directory.filePath(QStringLiteral("different-camera.pdf"))),
           QPdfDocument::Error::None);
  QCOMPARE(other.render(0, QSize(900, 1200)), pixels);
}

void TestMindMapViewWindowPrint::printsCurrentDraftAndRejectsReplacement() {
  const auto buffer = openNote(QStringLiteral("draft.emind"), fixture());
  QVERIFY(buffer.isValid());
  Window window(m_services, buffer);
  auto *editor = showWindow(window);
  QVERIFY(editor);
  auto *input = beginDraft(*editor);
  QVERIFY(input);
  input->setPlainText(QStringLiteral("UnsavedDraftPrintToken"));
  const auto output = m_directory.filePath(QStringLiteral("draft.pdf"));
  window.printDocument(pdfPrinter(output));
  QPdfDocument pdf;
  QCOMPARE(pdf.load(output), QPdfDocument::Error::None);
  QVERIFY(pdf.getAllText(0).text().contains(QStringLiteral("UnsavedDraftPrintToken")));
  QVERIFY(editor->isModified());
  QVERIFY(input->isVisible());
  const auto source = editor->toJson();
  QSignalSpy changed(editor, &MindMapEditor::documentChanged);
  window.printDocument(pdfPrinter(m_directory.filePath(QStringLiteral("repeat.pdf"))));
  QCOMPARE(editor->toJson(), source);
  QCOMPARE(changed.count(), 0);
  input->setPlainText(QStringLiteral("ReplacementPrintToken"));
  connect(editor, &MindMapEditor::documentChanged, &window,
          [&window] { window.syncEditorFromBuffer(); });
  const auto rejected = m_directory.filePath(QStringLiteral("replacement.pdf"));
  window.printDocument(pdfPrinter(rejected));
  QVERIFY(!QFileInfo::exists(rejected));
}

void TestMindMapViewWindowPrint::rejectsUnavailableDocuments() {
  const auto buffer = openNote(QStringLiteral("valid.emind"), fixture());
  QVERIFY(buffer.isValid());
  Window window(m_services, buffer);
  auto *editor = showWindow(window);
  QVERIFY(editor);
  const auto source = editor->toJson();
  const auto output = m_directory.filePath(QStringLiteral("disabled.pdf"));
  window.printDocument({});
  window.setEnabled(false);
  window.printDocument(pdfPrinter(output));
  QVERIFY(!QFileInfo::exists(output));
  QCOMPARE(editor->toJson(), source);
  const auto invalid = openNote(QStringLiteral("invalid.emind"), QByteArrayLiteral("invalid JSON"));
  QVERIFY(invalid.isValid());
  Window invalidWindow(m_services, invalid);
  const auto invalidOutput = m_directory.filePath(QStringLiteral("invalid.pdf"));
  invalidWindow.printDocument(pdfPrinter(invalidOutput));
  QVERIFY(!QFileInfo::exists(invalidOutput));
  const auto protectedBuffer = openProtectedNote();
  QVERIFY(protectedBuffer.isValid() && protectedBuffer.isEncrypted());
  Window protectedWindow(m_services, protectedBuffer);
  const auto protectedOutput = m_directory.filePath(QStringLiteral("protected.pdf"));
  protectedWindow.printDocument(pdfPrinter(protectedOutput));
  QVERIFY(!QFileInfo::exists(protectedOutput));
}

void TestMindMapViewWindowPrint::respectsSinglePageRange() {
  const auto buffer = openNote(QStringLiteral("range.emind"), fixture());
  QVERIFY(buffer.isValid());
  Window window(m_services, buffer);
  QVERIFY(showWindow(window));
  const auto output = m_directory.filePath(QStringLiteral("range.pdf"));
  auto printer = pdfPrinter(output);
  printer->setFromTo(2, 3);
  printer->setPrintRange(QPrinter::PageRange);
  window.printDocument(printer);
  QVERIFY(!QFileInfo::exists(output));
  printer->setPrintRange(QPrinter::AllPages);
  window.printDocument(printer);
  QPdfDocument pdf;
  QCOMPARE(pdf.load(output), QPdfDocument::Error::None);
  QCOMPARE(pdf.pageCount(), 1);
  QVERIFY(pdf.getAllText(0).text().contains(QStringLiteral("RootPrintToken")));
}

void TestMindMapViewWindowPrint::renderFailurePreservesState() {
  const auto buffer = openNote(QStringLiteral("bounds.emind"), fixture());
  QVERIFY(buffer.isValid());
  Window window(m_services, buffer);
  auto *editor = showWindow(window);
  QVERIFY(editor);
  QVERIFY(editor->setExpanded(QStringLiteral("a"), false));
  const auto source = editor->toJson();
  const auto selected = editor->selectedNodeIds();
  const auto expected = mapImage(*editor);
  QPainter inactive;
  QVERIFY(!editor->renderMap(inactive, QRectF(0, 0, 100, 100)));
  QImage image(100, 100, QImage::Format_RGB32);
  image.fill(Qt::white);
  const auto untouched = image.copy();
  QPainter painter(&image);
  painter.translate(4, 7);
  const auto transform = painter.transform();
  QVERIFY(!editor->renderMap(painter, QRectF(0, 0, 0, 100)));
  QVERIFY(!editor->renderMap(painter, QRectF(0, 0, 100, std::numeric_limits<qreal>::infinity())));
  QCOMPARE(image, untouched);
  QCOMPARE(painter.transform(), transform);
  QVERIFY(editor->renderMap(painter, QRectF(-10, -20, 100, 100)));
  QCOMPARE(painter.transform(), transform);
  painter.end();
  QCOMPARE(editor->toJson(), source);
  QCOMPARE(editor->selectedNodeIds(), selected);
  QCOMPARE(mapImage(*editor), expected);
}
} // namespace tests

int main(int argc, char **argv) {
  QTemporaryDir storage;
  if (!storage.isValid()) {
    return 1;
  }
  const auto temporaryRoot = storage.path().toLocal8Bit();
  qputenv("TMP", temporaryRoot);
  qputenv("TEMP", temporaryRoot);
  qputenv("TMPDIR", temporaryRoot);
  QApplication app(argc, argv);
  tests::TestMindMapViewWindowPrint test;
  return QTest::qExec(&test, argc, argv);
}

#include "test_mindmapviewwindow_print.moc"
