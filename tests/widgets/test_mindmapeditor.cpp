#include <QtTest>

#include <QAction>
#include <QBuffer>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFontDatabase>
#include <QGraphicsScene>
#include <QGraphicsTextItem>
#include <QGraphicsView>
#include <QHostAddress>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMenu>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPointer>
#include <QScopeGuard>
#include <QShortcut>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QToolBar>
#include <QUrl>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <QtConcurrent/QtConcurrentRun>

#include <controllers/mindmapviewwindowcontroller.h>
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
#include <vxcore/vxcore.h>
#include <widgets/editors/mindmapeditor.h>

#include <memory>
#include <stdexcept>

using namespace vnotex;

namespace tests {
namespace {

QByteArray readFile(const QString &p_path) {
  QFile file(p_path);
  return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

bool writeFile(const QString &p_path, const QByteArray &p_bytes) {
  QFile file(p_path);
  return file.open(QIODevice::WriteOnly | QIODevice::Truncate) &&
         file.write(p_bytes) == p_bytes.size();
}

QByteArray fixture(const QString &p_imageUrl = QString()) {
  auto document = QJsonDocument::fromJson(QByteArrayLiteral(R"({
    "schemaVersion":1,"rootId":"r","nodes":[
      {"id":"r","topic":"Root","children":["a"]},
      {"id":"a","topic":"Child","expanded":false,"children":["b"],
       "style":{"opaque":{"retain":[1,true,"metadata"]}}},
      {"id":"b","topic":"Hidden descendant"}],
    "crossLinks":[]})"))
                      .object();
  if (!p_imageUrl.isEmpty()) {
    auto nodes = document.value(QStringLiteral("nodes")).toArray();
    auto root = nodes.at(0).toObject();
    root.insert(QStringLiteral("image"), QJsonObject{{QStringLiteral("url"), p_imageUrl},
                                                     {QStringLiteral("width"), 0},
                                                     {QStringLiteral("height"), 0}});
    nodes[0] = root;
    document.insert(QStringLiteral("nodes"), nodes);
  }
  return QJsonDocument(document).toJson(QJsonDocument::Compact);
}

QJsonObject node(const MindMapEditor &p_editor, const QString &p_id) {
  return QJsonDocument::fromJson(p_editor.nodeJson(p_id)).object();
}

QByteArray imageBytes(const QColor &p_color) {
  QImage image(120, 60, QImage::Format_RGB32);
  image.fill(p_color);
  QByteArray bytes;
  QBuffer buffer(&bytes);
  if (!buffer.open(QIODevice::WriteOnly) || !image.save(&buffer, "PNG")) {
    return {};
  }
  return bytes;
}

QString dataUrl(const QByteArray &p_bytes) {
  return QStringLiteral("data:image/png;base64,") + QString::fromLatin1(p_bytes.toBase64());
}

QGraphicsView *showEditor(MindMapEditor &p_editor) {
  p_editor.resize(1000, 700);
  p_editor.show();
  p_editor.activateWindow();
  QApplication::setActiveWindow(&p_editor);
  auto *view = p_editor.findChild<QGraphicsView *>();
  if (!view || !view->scene()) {
    return nullptr;
  }
  view->setFocus(Qt::OtherFocusReason);
  if (!QTest::qWaitFor([view]() { return view->isVisible() && view->hasFocus(); })) {
    return nullptr;
  }
  // Let the initial queued centering settle before tests start a user interaction.
  QCoreApplication::processEvents();
  return view;
}

QPlainTextEdit *beginDraft(MindMapEditor &p_editor) {
  auto *view = p_editor.findChild<QGraphicsView *>();
  if (!view) {
    return nullptr;
  }
  p_editor.activateWindow();
  QApplication::setActiveWindow(&p_editor);
  view->setFocus(Qt::OtherFocusReason);
  if (!QTest::qWaitFor([view]() { return view->hasFocus(); })) {
    return nullptr;
  }
  QTest::keyClick(view->viewport(), Qt::Key_F2);
  QPlainTextEdit *result = nullptr;
  QTest::qWaitFor([&]() {
    for (auto *input : view->findChildren<QPlainTextEdit *>(QStringLiteral("topicEditor"))) {
      if (input->isVisible() && input->hasFocus()) {
        result = input;
        return true;
      }
    }
    return false;
  });
  return result;
}

// Observe actual scene pixels, not private image-cache fields or synthetic deliveries.
bool rendersColor(MindMapEditor &p_editor, const QColor &p_color) {
  auto *view = p_editor.findChild<QGraphicsView *>();
  if (!view || !view->scene()) {
    return false;
  }
  QImage pixels(1000, 700, QImage::Format_RGB32);
  pixels.fill(Qt::white);
  {
    QPainter painter(&pixels);
    view->scene()->render(&painter, QRectF(pixels.rect()),
                          view->scene()->itemsBoundingRect().adjusted(-10, -10, 10, 10));
  }
  int matches = 0;
  for (int y = 0; y < pixels.height(); ++y) {
    const auto *line = reinterpret_cast<const QRgb *>(pixels.constScanLine(y));
    for (int x = 0; x < pixels.width(); ++x) {
      if (line[x] == p_color.rgb() && ++matches == 32) {
        return true;
      }
    }
  }
  return false;
}

// An actual loopback HTTP peer with explicit response barriers. No real network,
// production network mocks, timing sleeps, or QNetworkReply implementation doubles.
class ImageServer : public QObject {
public:
  ImageServer() {
    connect(&m_server, &QTcpServer::newConnection, this, [this]() {
      while (auto *socket = m_server.nextPendingConnection()) {
        const int index = m_requests.size();
        m_requests.append({socket, {}, false});
        connect(socket, &QTcpSocket::readyRead, this, [this, index]() {
          auto &request = m_requests[index];
          request.bytes += request.socket->readAll();
          request.complete = request.bytes.contains("\r\n\r\n");
        });
      }
    });
    m_listening = m_server.listen(QHostAddress::LocalHost, 0);
  }

  bool isListening() const { return m_listening; }

  QString url() const {
    return QStringLiteral("http://127.0.0.1:%1/image.png").arg(m_server.serverPort());
  }

  int requestCount() const {
    int count = 0;
    for (const auto &request : m_requests) {
      count += request.complete;
    }
    return count;
  }

  bool disconnected(int p_index) const {
    return p_index < m_requests.size() &&
           (!m_requests[p_index].socket ||
            m_requests[p_index].socket->state() == QAbstractSocket::UnconnectedState);
  }

  bool reply(int p_index, const QByteArray &p_body, int p_status = 200) {
    if (p_index >= m_requests.size() || disconnected(p_index)) {
      return false;
    }
    auto *socket = m_requests[p_index].socket.data();
    const auto response =
        QByteArrayLiteral("HTTP/1.1 ") + QByteArray::number(p_status) +
        QByteArrayLiteral(" Response\r\nContent-Type: image/png\r\nContent-Length: ") +
        QByteArray::number(p_body.size()) + QByteArrayLiteral("\r\nConnection: close\r\n\r\n") +
        p_body;
    const bool queued = socket->write(response) == response.size();
    socket->disconnectFromHost();
    return queued;
  }

private:
  struct Request {
    QPointer<QTcpSocket> socket;
    QByteArray bytes;
    bool complete;
  };
  QTcpServer m_server;
  QVector<Request> m_requests;
  bool m_listening = false;
};

} // namespace

class TestMindMapEditor : public QObject {
  Q_OBJECT

private slots:
  void initTestCase();
  void init();
  void cleanup();
  void nativeSaveRoundTripKeepsHistory_data();
  void defaultZoomSurvivesHostLayout();
  void nativeSaveRoundTripKeepsHistory();
  void invalidContentFailsClosedAndRecovers();
  void pendingDraftAndRejectedSnapshot();
  void autosaveKeepsInlineEditor_data();
  void autosaveKeepsInlineEditor();
  void readOnlyStillNavigates();
  void zoomStateTracksCanvasWithoutEditing();
  void retargetRefreshesRelativeImagesWithoutReloading();
  void reloadAndRetargetCancelStaleHttpImages();
  void protectedImagesRefuseExternalAndRevokeOnLock();
  void configuredShortcutsDriveRealCommands();
  void livePreferencesPreserveDraftAndReadOnlyBuffer();

private:
  Buffer2 openNote(const QString &p_relativePath, const QByteArray &p_content,
                   bool p_readOnly = false);
  Buffer2 openProtectedNote(const QByteArray &p_content);

  VxCoreContextHandle m_context = nullptr;
  std::unique_ptr<QTemporaryDir> m_directory;
  std::unique_ptr<ConfigCoreService> m_configCore;
  std::unique_ptr<ConfigMgr2> m_config;
  std::unique_ptr<HookManager> m_hooks;
  std::unique_ptr<NotebookIoGate> m_gate;
  std::unique_ptr<NotebookCoreService> m_notebooks;
  std::unique_ptr<BufferService> m_buffers;
  std::unique_ptr<ServiceLocator> m_services;
  QString m_notebookId;
  QJsonObject m_originalMindMapConfig;
};

void TestMindMapEditor::initTestCase() { vxcore_set_test_mode(1); }

void TestMindMapEditor::init() {
  m_directory = std::make_unique<QTemporaryDir>();
  QVERIFY(m_directory->isValid());
  QCOMPARE(vxcore_context_create("{}", &m_context), VXCORE_OK);
  QVERIFY(m_context);
  m_configCore = std::make_unique<ConfigCoreService>(m_context);
  m_config = std::make_unique<ConfigMgr2>(m_configCore.get());
  m_config->init();
  m_originalMindMapConfig = m_config->getEditorConfig().getMindMapEditorConfig().toJson();
  m_hooks = std::make_unique<HookManager>();
  m_gate = std::make_unique<NotebookIoGate>();
  m_notebooks = std::make_unique<NotebookCoreService>(m_context);
  m_buffers =
      std::make_unique<BufferService>(m_context, m_hooks.get(), m_gate.get(), AutoSavePolicy::None);
  m_services = std::make_unique<ServiceLocator>();
  m_services->registerService(m_configCore.get());
  m_services->registerService(m_config.get());
  m_services->registerService(m_hooks.get());
  m_services->registerService(m_notebooks.get());
  m_services->registerService(m_buffers.get());
  m_notebookId = m_notebooks->createNotebook(
      m_directory->filePath(QStringLiteral("notebook")),
      QStringLiteral("{\"name\":\"Native mind-map regression\"}"), NotebookType::Bundled);
  QVERIFY(!m_notebookId.isEmpty());
}

void TestMindMapEditor::cleanup() {
  if (m_config) {
    m_config->getEditorConfig().getMindMapEditorConfig().fromJson(m_originalMindMapConfig);
    m_config->updateMainConfig(m_config->getConfig().toJson());
  }
  m_services.reset();
  if (m_buffers) {
    m_buffers->cancelProtectedLocking();
  }
  m_buffers.reset();
  if (m_notebooks) {
    m_notebooks->lockAllEncryption();
  }
  m_notebooks.reset();
  m_gate.reset();
  m_hooks.reset();
  m_config.reset();
  m_configCore.reset();
  if (m_context) {
    vxcore_context_destroy(m_context);
    m_context = nullptr;
  }
  m_directory.reset();
  m_notebookId.clear();
}

Buffer2 TestMindMapEditor::openNote(const QString &p_relativePath, const QByteArray &p_content,
                                    bool p_readOnly) {
  const QFileInfo info(p_relativePath);
  const auto parent = info.path() == QStringLiteral(".") ? QString() : info.path();
  if (m_notebooks->createFile(m_notebookId, parent, info.fileName()).isEmpty() ||
      !writeFile(m_notebooks->buildAbsolutePath(m_notebookId, p_relativePath), p_content)) {
    return {};
  }
  FileOpenSettings settings;
  settings.m_readOnly = p_readOnly;
  return m_buffers->openBuffer({m_notebookId, p_relativePath}, settings);
}

Buffer2 TestMindMapEditor::openProtectedNote(const QByteArray &p_content) {
  PreparedNotebookEncryption setup;
  const auto prepared =
      QtConcurrent::run([&]() {
        setup = m_notebooks->prepareNotebookEncryption(
            m_notebookId, QString(), QByteArrayLiteral("native-map-fixture-password"));
        return setup.m_error;
      }).result();
  if (prepared != VXCORE_OK || !setup.isValid()) {
    return {};
  }
  SyncWorkQueueManager queues;
  QString noteId;
  const auto error = QtConcurrent::run([&]() {
                       auto maintenance = queues.tryAcquireMaintenance({m_notebookId});
                       if (!maintenance.isValid()) {
                         return VXCORE_ERR_INVALID_STATE;
                       }
                       NotebookIoGate::ScopedLock lock(*m_gate, m_notebookId);
                       const auto committed = m_notebooks->commitNotebookEncryption(setup);
                       return committed == VXCORE_OK
                                  ? m_notebooks->createEncryptedNote(
                                        m_notebookId, QString(), QStringLiteral("private.emind"),
                                        QStringLiteral("mindmap"), p_content, &noteId)
                                  : committed;
                     }).result();
  return error == VXCORE_OK ? m_buffers->openBufferByNodeId(noteId) : Buffer2();
}

void TestMindMapEditor::defaultZoomSurvivesHostLayout() {
  const auto buffer = openNote(QStringLiteral("default-zoom.mmm"), {});
  QVERIFY(buffer.isValid());
  QWidget host;
  QVBoxLayout layout(&host);
  host.resize(1000, 700);
  host.show();
  QCoreApplication::processEvents();
  MindMapEditor editor(*m_services, buffer, &host);
  QSignalSpy dirty(&editor, &MindMapEditor::contentsChanged);
  // loadContent() shows the child before ViewArea2 inserts its enclosing tab.
  QVERIFY(editor.loadContent(buffer.getContentRaw()));
  layout.addWidget(&editor);
  QCoreApplication::processEvents();
  QCoreApplication::processEvents();
  auto *view = editor.findChild<QGraphicsView *>();
  QVERIFY(view && view->isVisible());
  QGraphicsTextItem *root = nullptr;
  for (auto *item : view->scene()->items()) {
    auto *label = dynamic_cast<QGraphicsTextItem *>(item);
    if (label && label->toPlainText() == QStringLiteral("Central topic")) {
      root = label;
      break;
    }
  }
  QVERIFY(root && root->isVisible());
  const QRect rootRect = view->mapFromScene(root->sceneBoundingRect()).boundingRect();
  QVERIFY(rootRect.height() >= root->boundingRect().height());
  QVERIFY(view->viewport()->rect().contains(rootRect));
  QCOMPARE(editor.zoomFactor(), 1.0);
  QCOMPARE(editor.selectedNodeId(), QStringLiteral("root"));
  QVERIFY(!editor.isZoomFit() && !editor.isModified() && dirty.isEmpty());
  QCOMPARE(buffer.getContentRaw(), QByteArray());
  QCOMPARE(readFile(buffer.resolvedPath()), QByteArray());
}

void TestMindMapEditor::nativeSaveRoundTripKeepsHistory_data() {
  QTest::addColumn<QByteArray>("input");
  QTest::newRow("empty") << QByteArray();
  QTest::newRow("json-whitespace") << QByteArray(" \t\r\n");
}

void TestMindMapEditor::nativeSaveRoundTripKeepsHistory() {
  QFETCH(QByteArray, input);
  auto buffer = openNote(QStringLiteral("empty.emind"), input);
  QVERIFY(buffer.isValid());
  MindMapEditor editor(*m_services, buffer);
  QSignalSpy dirty(&editor, &MindMapEditor::contentsChanged);
  QVERIFY(editor.loadContent(buffer.getContentRaw()));
  QVERIFY(showEditor(editor));
  QVERIFY(dirty.isEmpty());
  QVERIFY(!editor.isModified());
  QCOMPARE(buffer.getContentRaw(), input);
  QCOMPARE(readFile(buffer.resolvedPath()), input);

  const auto rootId =
      QJsonDocument::fromJson(editor.toJson()).object().value(QStringLiteral("rootId")).toString();
  QCOMPARE(rootId, QStringLiteral("root"));
  QCOMPARE(editor.selectedNodeId(), rootId);
  QCOMPARE(node(editor, rootId).value(QStringLiteral("topic")).toString(),
           QStringLiteral("Central topic"));
  const auto baseline = editor.contentForSave();
  const auto childId = editor.addNode(rootId, QString::fromUtf8("Native saved 世界"));
  QVERIFY(!childId.isEmpty());
  QVERIFY(editor.isModified());
  QVERIFY(!dirty.isEmpty());
  const auto unformatted = editor.contentForSave();
  QVERIFY(editor.selectNode(childId));
  editor.setModified(false);
  dirty.clear();
  auto *bold = editor.commandAction(QStringLiteral("toggleBold"));
  QVERIFY(bold && bold->isEnabled());
  bold->trigger();
  QCOMPARE(node(editor, childId)
               .value(QStringLiteral("style"))
               .toObject()
               .value(QStringLiteral("fontWeight"))
               .toString(),
           QStringLiteral("bold"));
  QVERIFY(editor.isModified());
  QCOMPARE(dirty.count(), 1);
  const auto saved = editor.contentForSave();
  QVERIFY(buffer.setContentRaw(saved.toUtf8()));
  QVERIFY(buffer.save());
  QCOMPARE(readFile(buffer.resolvedPath()), saved.toUtf8());
  editor.setModified(false);
  QVERIFY(!editor.isModified());
  QVERIFY(editor.undo());
  QCOMPARE(editor.contentForSave(), unformatted);
  QVERIFY(!bold->isChecked());
  QVERIFY(editor.undo());
  QCOMPARE(editor.contentForSave(), baseline);
  QVERIFY(editor.redo());
  QCOMPARE(editor.contentForSave(), unformatted);
  QVERIFY(editor.redo());
  QCOMPARE(editor.contentForSave(), saved);
  QVERIFY(bold->isChecked());
  QCOMPARE(readFile(buffer.resolvedPath()), saved.toUtf8());

  MindMapEditor reopened(*m_services, buffer);
  QVERIFY(reopened.loadContent(readFile(buffer.resolvedPath())));
  QCOMPARE(reopened.contentForSave(), saved);
  QCOMPARE(node(reopened, childId).value(QStringLiteral("topic")).toString(),
           QString::fromUtf8("Native saved 世界"));
  QCOMPARE(node(reopened, childId)
               .value(QStringLiteral("style"))
               .toObject()
               .value(QStringLiteral("fontWeight"))
               .toString(),
           QStringLiteral("bold"));
}

void TestMindMapEditor::invalidContentFailsClosedAndRecovers() {
  const auto original = fixture();
  auto buffer = openNote(QStringLiteral("invalid.emind"), original);
  QVERIFY(buffer.isValid());
  MindMapEditor editor(*m_services, buffer);
  QVERIFY(editor.loadContent(buffer.getContentRaw()));
  QVERIFY(showEditor(editor));
  QToolBar toolbar;
  const QStringList formattingNames = {
      QStringLiteral("fontSize"),       QStringLiteral("toggleBold"),
      QStringLiteral("toggleItalic"),   QStringLiteral("resetStyle"),
      QStringLiteral("textColorPopup"), QStringLiteral("fillColorPopup"),
      QStringLiteral("iconsPopup")};
  for (const auto &name : formattingNames) {
    auto *action = editor.commandAction(name);
    QVERIFY(action);
    toolbar.addAction(action);
  }
  auto *size = toolbar.findChild<QComboBox *>(QStringLiteral("toolbarFontSize"));
  QVERIFY(size);
  const auto baseline = editor.toJson();
  QVERIFY(editor.renameNode(QStringLiteral("r"), QStringLiteral("First edit")));
  QVERIFY(editor.renameNode(QStringLiteral("r"), QStringLiteral("Second edit")));
  QVERIFY(editor.undo());
  QVERIFY(editor.canUndo() && editor.canRedo());
  editor.setModified(false);
  const auto retained = editor.toJson();
  const QByteArray malformed("{\"schemaVersion\":1,\"nodes\":[invalid");
  QVERIFY(writeFile(buffer.resolvedPath(), malformed));
  QVERIFY(buffer.reload());
  const auto revision = buffer.getRevision();
  QSignalSpy status(&editor, &MindMapEditor::statusMessageRequested);
  QSignalSpy dirty(&editor, &MindMapEditor::contentsChanged);
  QVERIFY(!editor.loadContent(buffer.getContentRaw()));
  QVERIFY(!status.isEmpty());
  QVERIFY(!editor.isVisible() || !editor.isEnabled());
  QVERIFY_EXCEPTION_THROWN(editor.contentForSave(), std::runtime_error);
  QVERIFY(!editor.commandAction(QStringLiteral("undo"))->isEnabled());
  QVERIFY(!editor.commandAction(QStringLiteral("redo"))->isEnabled());
  QVERIFY(!editor.undo());
  QVERIFY(!editor.redo());
  for (const auto &name : formattingNames) {
    auto *action = editor.commandAction(name);
    QVERIFY(!action->isEnabled());
    action->trigger();
    if (auto *menu = action->menu()) {
      menu->popup(editor.mapToGlobal(QPoint(0, 0)));
      QCoreApplication::processEvents();
      QVERIFY(!menu->isVisible());
    }
  }
  const int sizeIndex = size->findData(24.0);
  QVERIFY(sizeIndex >= 0);
  size->setCurrentIndex(sizeIndex);
  QVERIFY(QMetaObject::invokeMethod(size, "activated", Q_ARG(int, sizeIndex)));
  editor.setBuffer(buffer);
  QVERIFY(!editor.commandAction(QStringLiteral("undo"))->isEnabled());
  QVERIFY(!editor.commandAction(QStringLiteral("redo"))->isEnabled());
  QCOMPARE(editor.toJson(), retained);
  QCOMPARE(buffer.getContentRaw(), malformed);
  QCOMPARE(readFile(buffer.resolvedPath()), malformed);
  QCOMPARE(buffer.getRevision(), revision);
  QVERIFY(dirty.isEmpty());
  QVERIFY(!editor.isModified());

  QVERIFY(writeFile(buffer.resolvedPath(), original));
  QVERIFY(buffer.reload());
  QVERIFY(editor.loadContent(buffer.getContentRaw()));
  QVERIFY(editor.isVisible() && editor.isEnabled());
  QCOMPARE(editor.contentForSave().toUtf8(), baseline);
  QCOMPARE(readFile(buffer.resolvedPath()), original);
  QVERIFY(dirty.isEmpty());
  for (const auto &name : formattingNames) {
    QVERIFY(editor.commandAction(name)->isEnabled());
  }
  size->setCurrentIndex(sizeIndex);
  QVERIFY(QMetaObject::invokeMethod(size, "activated", Q_ARG(int, sizeIndex)));
  QCOMPARE(node(editor, QStringLiteral("r"))
               .value(QStringLiteral("style"))
               .toObject()
               .value(QStringLiteral("fontSize"))
               .toDouble(),
           24.0);
  QVERIFY(editor.isModified());
  QVERIFY(!dirty.isEmpty());
  QVERIFY(editor.undo());
  QCOMPARE(editor.toJson(), baseline);
  QVERIFY(editor.renameNode(QStringLiteral("r"), QStringLiteral("Recovered edit")));
  QVERIFY(editor.undo());
  QCOMPARE(editor.toJson(), baseline);
}

void TestMindMapEditor::pendingDraftAndRejectedSnapshot() {
  const auto original = fixture();
  auto buffer = openNote(QStringLiteral("draft.emind"), original);
  QVERIFY(buffer.isValid());
  MindMapEditor editor(*m_services, buffer);
  QVERIFY(editor.loadContent(buffer.getContentRaw()));
  auto *view = showEditor(editor);
  QVERIFY(view);
  QVERIFY(editor.selectNode(QStringLiteral("a")));
  editor.zoom(0.8);
  const auto transform = view->transform();
  const auto committed = editor.toJson();
  const auto opaqueStyle = node(editor, QStringLiteral("a")).value(QStringLiteral("style"));
  QSignalSpy dirty(&editor, &MindMapEditor::contentsChanged);
  QSignalSpy semantic(&editor, &m3::qt::MindMapEditor::documentChanged);
  auto *input = beginDraft(editor);
  QVERIFY(input);
  input->selectAll();
  QTest::keyClicks(input, "Pending native save");
  QVERIFY(editor.hasPendingEdit());
  QVERIFY(editor.isModified());
  QVERIFY(!dirty.isEmpty());
  QVERIFY(semantic.isEmpty());
  QCOMPARE(editor.toJson(), committed);
  QCOMPARE(buffer.getContentRaw(), original);

  const QPointer<QPlainTextEdit> retainedInput(input);
  const int cursorPosition = input->textCursor().position();
  const auto saved = editor.contentForSave();
  QVERIFY(retainedInput && retainedInput->isVisible() && retainedInput->hasFocus());
  QCOMPARE(retainedInput->textCursor().position(), cursorPosition);
  QCOMPARE(editor.contentForSave(), saved);
  QVERIFY(retainedInput && retainedInput->isVisible() && retainedInput->hasFocus());
  QVERIFY(!editor.hasPendingEdit());
  QCOMPARE(node(editor, QStringLiteral("a")).value(QStringLiteral("topic")).toString(),
           QStringLiteral("Pending native save"));
  QCOMPARE(editor.selectedNodeId(), QStringLiteral("a"));
  QCOMPARE(view->transform(), transform);
  QCOMPARE(node(editor, QStringLiteral("a")).value(QStringLiteral("style")), opaqueStyle);
  QCOMPARE(semantic.count(), 1);

  editor.setModified(false);
  input = retainedInput;
  QVERIFY(input);
  const auto rejected = QStringLiteral("Rejected") + QChar(0) + QStringLiteral("draft");
  input->setPlainText(rejected);
  QVERIFY(editor.isModified());
  QVERIFY_EXCEPTION_THROWN(editor.contentForSave(), std::runtime_error);
  QVERIFY(editor.hasPendingEdit());
  QVERIFY(input->hasFocus());
  QCOMPARE(input->toPlainText(), rejected);
  QCOMPARE(editor.toJson(), saved.toUtf8());
  QCOMPARE(semantic.count(), 1);
  QCOMPARE(buffer.getContentRaw(), original);
  QCOMPARE(readFile(buffer.resolvedPath()), original);

  input->setPlainText(QStringLiteral("Corrected draft"));
  const auto corrected = editor.contentForSave();
  QCOMPARE(node(editor, QStringLiteral("a")).value(QStringLiteral("topic")).toString(),
           QStringLiteral("Corrected draft"));
  QVERIFY(!editor.hasPendingEdit());
  QVERIFY(retainedInput && retainedInput->isVisible() && retainedInput->hasFocus());
  input = retainedInput;
  input->setPlainText(QStringLiteral("Discard this draft only"));
  QTest::keyClick(input, Qt::Key_Escape);
  QVERIFY(!editor.hasPendingEdit());
  QVERIFY(editor.isModified());
  QCOMPARE(editor.contentForSave(), corrected);
}

void TestMindMapEditor::autosaveKeepsInlineEditor_data() {
  QTest::addColumn<bool>("link");
  QTest::newRow("node") << false;
  QTest::newRow("link") << true;
}

void TestMindMapEditor::autosaveKeepsInlineEditor() {
  QFETCH(bool, link);
  auto buffer = openNote(QStringLiteral("autosave.emind"), fixture());
  QVERIFY(buffer.isValid());
  MindMapEditor editor(*m_services, buffer);
  QVERIFY(editor.loadContent(buffer.getContentRaw()));
  auto *view = showEditor(editor);
  QVERIFY(view);
  const QString target = link ? editor.addLink(QStringLiteral("r"), QStringLiteral("a"), false,
                                               QStringLiteral("Initial link"))
                              : QStringLiteral("a");
  QVERIFY(!target.isEmpty());
  QVERIFY(link ? editor.selectLink(target) : editor.selectNode(target));
  const auto selectedNodes = editor.selectedNodeIds();
  const auto selectedLink = editor.selectedLinkId();
  const auto zoom = view->transform();
  auto *input = beginDraft(editor);
  QVERIFY(input);
  const QPointer<QPlainTextEdit> retained(input);
  const auto writerKey = reinterpret_cast<quintptr>(&editor);
  m_buffers->registerActiveWriter(buffer.id(), writerKey,
                                  [&editor] { return editor.contentForSave(); });
  const auto unregister =
      qScopeGuard([&] { m_buffers->unregisterActiveWriter(buffer.id(), writerKey); });
  connect(&editor, &MindMapEditor::contentsChanged, &editor,
          [&] { m_buffers->markDirty(buffer.id()); });
  QSignalSpy saved(m_buffers->asQObject(), SIGNAL(bufferAutoSaved(QString)));
  QVERIFY(saved.isValid());
  m_buffers->setAutoSavePolicy(AutoSavePolicy::AutoSave);
  const auto verifyLiveInput = [&] {
    return retained && retained->isVisible() && retained->hasFocus() &&
           editor.selectedNodeIds() == selectedNodes && editor.selectedLinkId() == selectedLink &&
           view->transform() == zoom;
  };
  input->setPlainText(QStringLiteral("First autosaved topic"));
  auto caret = input->textCursor();
  caret.setPosition(6);
  caret.setPosition(15, QTextCursor::KeepAnchor);
  input->setTextCursor(caret);
  QTRY_VERIFY_WITH_TIMEOUT(saved.count() >= 1, 5000);
  QVERIFY(verifyLiveInput());
  QCOMPARE(input->textCursor().position(), 15);
  QCOMPARE(input->textCursor().anchor(), 6);
  QVERIFY(!editor.hasPendingEdit());
  QVERIFY(readFile(buffer.resolvedPath()).contains("First autosaved topic"));
  QVERIFY(!m_buffers->isDirty(buffer.id()));
  editor.setModified(false);
  const auto revision = m_buffers->currentRevision(buffer.id());
  const auto snapshot = editor.contentForSave();
  QCOMPARE(editor.contentForSave(), snapshot);
  QVERIFY(verifyLiveInput());
  QCOMPARE(m_buffers->currentRevision(buffer.id()), revision);

  input->moveCursor(QTextCursor::End);
  QTest::keyClicks(input, " continued");
  QVERIFY(editor.hasPendingEdit() && editor.isModified() && m_buffers->isDirty(buffer.id()));
  QTRY_VERIFY_WITH_TIMEOUT(saved.count() >= 2, 5000);
  QVERIFY(verifyLiveInput());
  QCOMPARE(input->toPlainText(), QStringLiteral("First autosaved topic continued"));
  QVERIFY(readFile(buffer.resolvedPath()).contains("First autosaved topic continued"));
  QVERIFY(!editor.hasPendingEdit());
  m_buffers->setAutoSavePolicy(AutoSavePolicy::None);

  bool captured = false;
  bool saveOk = false;
  auto save = [&] {
    captured = true;
    saveOk = m_buffers->pullActiveWriterContent(buffer.id()) && buffer.save();
    if (saveOk)
      editor.setModified(false);
  };
  QAction saveAction(&editor);
  saveAction.setShortcut(QKeySequence::Save);
  saveAction.setShortcutContext(Qt::WindowShortcut);
  editor.addAction(&saveAction);
  connect(&saveAction, &QAction::triggered, &editor, save);
  QShortcut alternate(QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_S), &editor);
  alternate.setContext(Qt::WindowShortcut);
  connect(&alternate, &QShortcut::activated, &editor, save);
  QTest::keyClicks(input, " manual");
  QTest::keyClick(input, Qt::Key_S, Qt::ControlModifier);
  QTRY_VERIFY(captured);
  QVERIFY(saveOk && verifyLiveInput());
  QVERIFY(readFile(buffer.resolvedPath()).contains("continued manual"));
  captured = false;
  QTest::keyClicks(input, " custom");
  QTest::keyClick(input, Qt::Key_S, Qt::ControlModifier | Qt::AltModifier);
  QTRY_VERIFY(captured);
  QVERIFY(saveOk && verifyLiveInput());
  QVERIFY(readFile(buffer.resolvedPath()).contains("continued manual custom"));
  const auto persisted = editor.toJson();
  QTest::keyClicks(input, " unsaved");
  QTest::keyClick(input, Qt::Key_Escape);
  QVERIFY(!retained || !retained->isVisible());
  QCOMPARE(editor.toJson(), persisted);
  QVERIFY(!readFile(buffer.resolvedPath()).contains("unsaved"));
}

void TestMindMapEditor::readOnlyStillNavigates() {
  const auto original = fixture();
  auto buffer = openNote(QStringLiteral("readonly.emind"), original, true);
  QVERIFY(buffer.isValid());
  QVERIFY(buffer.isReadOnly());
  MindMapEditor editor(*m_services, buffer);
  QVERIFY(editor.loadContent(buffer.getContentRaw()));
  auto *view = showEditor(editor);
  QVERIFY(view);
  QVERIFY(editor.isReadOnly());
  const auto committed = editor.contentForSave();
  QSignalSpy dirty(&editor, &MindMapEditor::contentsChanged);
  QVERIFY(!editor.renameNode(QStringLiteral("a"), QStringLiteral("Denied")));
  QVERIFY(editor.addNode(QStringLiteral("r"), QStringLiteral("Denied")).isEmpty());
  QVERIFY(!editor.updateNode(QStringLiteral("a"), QByteArrayLiteral("{\"hyperLink\":\"denied\"}")));
  QVERIFY(!editor.undo());
  QTest::keyClick(view->viewport(), Qt::Key_F2);
  QTest::keyClick(view->viewport(), Qt::Key_Insert);
  QVERIFY(!editor.hasPendingEdit());
  const auto found = editor.findText(QStringLiteral("Hidden"), Qt::CaseSensitive);
  QCOMPARE(found.totalMatches, 1);
  QCOMPARE(editor.selectedNodeId(), QStringLiteral("b"));
  QCOMPARE(editor.selectedText(), QStringLiteral("Hidden descendant"));
  QVERIFY(editor.revealNode(QStringLiteral("a")));
  const auto transform = view->transform();
  editor.zoom(0.8);
  QVERIFY(view->transform() != transform);
  QCOMPARE(editor.contentForSave(), committed);
  QCOMPARE(buffer.getContentRaw(), original);
  QCOMPARE(readFile(buffer.resolvedPath()), original);
  QVERIFY(dirty.isEmpty());
}

void TestMindMapEditor::retargetRefreshesRelativeImagesWithoutReloading() {
  const QColor oldColor(231, 37, 53), newColor(13, 181, 67);
  QVERIFY(!m_notebooks->createFolder(m_notebookId, QString(), QStringLiteral("before")).isEmpty());
  const auto original = fixture(QStringLiteral("picture%20one.png"));
  auto buffer = openNote(QStringLiteral("before/map.emind"), original);
  QVERIFY(buffer.isValid());
  QVERIFY(writeFile(QDir(buffer.getResourceBasePath()).filePath(QStringLiteral("picture one.png")),
                    imageBytes(oldColor)));
  MindMapEditor editor(*m_services, buffer);
  QVERIFY(editor.loadContent(buffer.getContentRaw()));
  auto *view = showEditor(editor);
  QVERIFY(view);
  QTRY_VERIFY(rendersColor(editor, oldColor));
  QVERIFY(editor.renameNode(QStringLiteral("a"), QStringLiteral("Unsaved child")));
  QVERIFY(editor.selectNode(QStringLiteral("a")));
  auto *input = beginDraft(editor);
  QVERIFY(input);
  input->setPlainText(QStringLiteral("Draft across rename"));
  const auto committed = editor.toJson();
  const auto transform = view->transform();
  QSignalSpy dirty(&editor, &MindMapEditor::contentsChanged);
  QVERIFY(
      m_notebooks->renameFolder(m_notebookId, QStringLiteral("before"), QStringLiteral("after")));
  buffer = m_buffers->getBufferHandle(buffer.id());
  QVERIFY(buffer.isValid());
  QCOMPARE(buffer.nodeId().relativePath, QStringLiteral("after/map.emind"));
  QVERIFY(writeFile(QDir(buffer.getResourceBasePath()).filePath(QStringLiteral("picture one.png")),
                    imageBytes(newColor)));
  editor.setBuffer(buffer);
  QTRY_VERIFY(rendersColor(editor, newColor));
  QVERIFY(!rendersColor(editor, oldColor));
  QCOMPARE(editor.toJson(), committed);
  QCOMPARE(input->toPlainText(), QStringLiteral("Draft across rename"));
  QVERIFY(editor.hasPendingEdit());
  QCOMPARE(editor.selectedNodeId(), QStringLiteral("a"));
  QCOMPARE(view->transform(), transform);
  QVERIFY(dirty.isEmpty());
  QCOMPARE(readFile(buffer.resolvedPath()), original);
  editor.contentForSave();
  QCOMPARE(node(editor, QStringLiteral("a")).value(QStringLiteral("topic")).toString(),
           QStringLiteral("Draft across rename"));
  QVERIFY(editor.undo());
  QCOMPARE(node(editor, QStringLiteral("a")).value(QStringLiteral("topic")).toString(),
           QStringLiteral("Unsaved child"));
}

void TestMindMapEditor::reloadAndRetargetCancelStaleHttpImages() {
  ImageServer server;
  QVERIFY(server.isListening());
  const QColor staleColor(231, 37, 53), currentColor(13, 181, 67);
  const auto original = fixture(server.url());
  auto first = openNote(QStringLiteral("network-first.emind"), original);
  auto second = openNote(QStringLiteral("network-second.emind"), original);
  QVERIFY(first.isValid() && second.isValid());
  MindMapEditor editor(*m_services, first);
  auto *policy = editor.findChild<MindMapViewWindowController *>();
  QVERIFY(policy);
  QSignalSpy deliveries(policy, &MindMapViewWindowController::imageReady);
  QVERIFY(editor.loadContent(first.getContentRaw()));
  QVERIFY(showEditor(editor));
  QTRY_COMPARE(server.requestCount(), 1);

  // Reload the same URL while the first server response is deliberately withheld.
  QVERIFY(editor.loadContent(first.getContentRaw()));
  QTRY_VERIFY(server.disconnected(0));
  QTRY_COMPARE(server.requestCount(), 2);
  editor.setBuffer(second);
  QVERIFY(editor.loadContent(second.getContentRaw()));
  QTRY_VERIFY(server.disconnected(1));
  QTRY_COMPARE(server.requestCount(), 3);
  QVERIFY(server.reply(2, imageBytes(currentColor)));
  QTRY_VERIFY(rendersColor(editor, currentColor));
  QVERIFY(!server.reply(0, imageBytes(staleColor)));
  QVERIFY(!server.reply(1, imageBytes(staleColor)));
  QVERIFY(!rendersColor(editor, staleColor));
  QVERIFY(!editor.isModified());
  QCOMPARE(readFile(first.resolvedPath()), original);
  QCOMPARE(readFile(second.resolvedPath()), original);

  // An HTTP error is not a successful image merely because its body decodes.
  const auto previousDeliveries = deliveries.count();
  editor.reloadImages();
  QTRY_COMPARE(server.requestCount(), 4);
  QVERIFY(server.reply(3, imageBytes(staleColor), 404));
  QTRY_VERIFY(server.disconnected(3));
  QTRY_COMPARE(deliveries.count(), previousDeliveries + 1);
  QVERIFY(qvariant_cast<QImage>(deliveries.last().at(2)).isNull());
  QTRY_VERIFY(!rendersColor(editor, currentColor));
  QVERIFY(!rendersColor(editor, staleColor));
}

void TestMindMapEditor::protectedImagesRefuseExternalAndRevokeOnLock() {
  ImageServer server;
  QVERIFY(server.isListening());
  const QColor embeddedColor(231, 37, 53), relativeColor(13, 181, 67);
  const auto embeddedBytes = imageBytes(embeddedColor);
  QVERIFY(!embeddedBytes.isEmpty());
  auto buffer = openProtectedNote(fixture(server.url()));
  QVERIFY(buffer.isValid());
  QVERIFY(buffer.isEncrypted());
  QCOMPARE(buffer.editorType(), QStringLiteral("mindmap"));
  const auto body = buffer.getContentRaw();
  const auto ciphertext = readFile(buffer.resolvedPath());
  QVERIFY(ciphertext != body);
  MindMapEditor editor(*m_services, buffer);
  auto *policy = editor.findChild<MindMapViewWindowController *>();
  QVERIFY(policy);
  QSignalSpy deliveries(policy, &MindMapViewWindowController::imageReady);
  QSignalSpy requests(&editor, &m3::qt::MindMapEditor::imageRequested);
  QVERIFY(editor.loadContent(body));
  QVERIFY(showEditor(editor));
  QTRY_COMPARE(requests.count(), 1);
  QTRY_COMPARE(deliveries.count(), 1);
  QVERIFY(qvariant_cast<QImage>(deliveries.last().at(2)).isNull());
  QCOMPARE(server.requestCount(), 0);
  QVERIFY(!rendersColor(editor, embeddedColor));

  const auto imagePath = m_directory->filePath(QStringLiteral("outside.png"));
  QVERIFY(writeFile(imagePath, embeddedBytes));
  QVERIFY(editor.loadContent(fixture(QUrl::fromLocalFile(imagePath).toString())));
  QTRY_COMPARE(requests.count(), 2);
  QTRY_COMPARE(deliveries.count(), 2);
  QVERIFY(qvariant_cast<QImage>(deliveries.last().at(2)).isNull());
  QVERIFY(!rendersColor(editor, embeddedColor));
  QCOMPARE(server.requestCount(), 0);

  // Relative assets are plaintext, but may only be reached through Buffer2's
  // contained resource policy; data URIs require a live protected lease too.
  QVERIFY(
      writeFile(QDir(buffer.getResourceBasePath()).filePath(QStringLiteral("relative image.png")),
                imageBytes(relativeColor)));
  QVERIFY(editor.loadContent(fixture(QStringLiteral("relative%20image.png"))));
  QTRY_VERIFY(rendersColor(editor, relativeColor));
  QVERIFY(editor.loadContent(fixture(dataUrl(embeddedBytes))));
  QTRY_VERIFY(rendersColor(editor, embeddedColor));
  const auto committed = editor.toJson();
  QSignalSpy dirty(&editor, &MindMapEditor::contentsChanged);
  const int priorRequests = requests.count();
  const auto priorDeliveries = deliveries.count();
  editor.reloadImages();
  QVERIFY(m_buffers->beginProtectedLocking());
  QTRY_VERIFY(requests.count() > priorRequests);
  QTRY_VERIFY(deliveries.count() > priorDeliveries);
  QVERIFY(qvariant_cast<QImage>(deliveries.last().at(2)).isNull());
  QTRY_VERIFY(!rendersColor(editor, embeddedColor));
  QVERIFY(!rendersColor(editor, relativeColor));
  QCOMPARE(server.requestCount(), 0);
  QCOMPARE(editor.toJson(), committed);
  VxCoreError lockedError = VXCORE_OK;
  QVERIFY(buffer.getContentRaw(&lockedError).isEmpty());
  QVERIFY(lockedError != VXCORE_OK);
  QCOMPARE(readFile(buffer.resolvedPath()), ciphertext);
  QVERIFY(dirty.isEmpty());

  m_buffers->cancelProtectedLocking();
  QCOMPARE(buffer.getContentRaw(), body);
  editor.reloadImages();
  QTRY_VERIFY(rendersColor(editor, embeddedColor));
  QCOMPARE(server.requestCount(), 0);
  QCOMPARE(readFile(buffer.resolvedPath()), ciphertext);
}

void TestMindMapEditor::zoomStateTracksCanvasWithoutEditing() {
  const auto content = fixture();
  const auto buffer = openNote(QStringLiteral("zoom.mmm"), content, true);
  QVERIFY(buffer.isValid());
  MindMapEditor editor(*m_services, buffer);
  editor.resize(1000, 700);
  QSignalSpy changed(&editor, &m3::qt::MindMapEditor::zoomChanged);
  QVERIFY(editor.loadContent(content));
  QVERIFY(!editor.isZoomFit());
  QCOMPARE(editor.zoomFactor(), 1.0);
  const auto committed = editor.toJson();
  QSignalSpy dirty(&editor, &MindMapEditor::contentsChanged);
  auto *view = showEditor(editor);
  QVERIFY(view);
  QVERIFY(!editor.isZoomFit());
  QCOMPARE(editor.zoomFactor(), view->transform().m11());

  editor.resetZoom();
  QCOMPARE(editor.zoomFactor(), 1.0);
  changed.clear();
  editor.resetZoom();
  editor.zoom(1.0);
  QVERIFY(changed.isEmpty());

  editor.zoom(1.2);
  QCOMPARE(changed.count(), 1);
  QCOMPARE(editor.zoomFactor(), 1.2);
  QVERIFY(!editor.isZoomFit());
  QCOMPARE(changed.last().at(1).toBool(), false);
  QCOMPARE(changed.last().at(0).toDouble(), view->transform().m11());
  changed.clear();
  editor.zoom(100.0);
  QCOMPARE(editor.zoomFactor(), 4.0);
  QCOMPARE(changed.count(), 1);
  editor.zoom(1.2);
  QCOMPARE(changed.count(), 1);
  editor.zoom(0.001);
  QVERIFY(qFuzzyCompare(editor.zoomFactor(), 0.1));
  QCOMPARE(changed.count(), 2);
  editor.zoom(0.5);
  QCOMPARE(changed.count(), 2);

  editor.resetZoom();
  changed.clear();
  const QPoint position = view->viewport()->rect().center();
  QWheelEvent wheel(position, view->viewport()->mapToGlobal(position), QPoint(), QPoint(0, 120),
                    Qt::NoButton, Qt::ControlModifier, Qt::NoScrollPhase, false);
  QApplication::sendEvent(view->viewport(), &wheel);
  QCOMPARE(changed.count(), 1);
  QCOMPARE(editor.zoomFactor(), 1.2);
  QCOMPARE(changed.last().at(0).toDouble(), view->transform().m11());

  changed.clear();
  editor.commandAction(QStringLiteral("fit"))->trigger();
  QCOMPARE(changed.count(), 1);
  QVERIFY(editor.isZoomFit());
  QCOMPARE(changed.last().at(1).toBool(), true);
  QCOMPARE(changed.last().at(0).toDouble(), editor.zoomFactor());
  QCOMPARE(editor.zoomFactor(), view->transform().m11());
  changed.clear();
  editor.scrollSteps(1, 1);
  QVERIFY(changed.isEmpty());

  // Fit/manual intent must remain observable even before geometry can change.
  editor.hide();
  editor.resetZoom();
  changed.clear();
  editor.fitToContents();
  QVERIFY(editor.isZoomFit());
  QCOMPARE(editor.zoomFactor(), 1.0);
  QCOMPARE(changed.count(), 1);
  QCOMPARE(changed.last().at(1).toBool(), true);
  editor.fitToContents();
  QCOMPARE(changed.count(), 1);
  editor.zoom(1.0);
  QVERIFY(!editor.isZoomFit());
  QCOMPARE(editor.zoomFactor(), 1.0);
  QCOMPARE(changed.count(), 2);
  QCOMPARE(changed.last().at(1).toBool(), false);
  editor.fitToContents();
  changed.clear();
  editor.resetZoom();
  QVERIFY(!editor.isZoomFit());
  QCOMPARE(editor.zoomFactor(), 1.0);
  QCOMPARE(changed.count(), 1);
  QCOMPARE(changed.last().at(1).toBool(), false);
  editor.resetZoom();
  QCOMPARE(changed.count(), 1);
  editor.fitToContents();
  QVERIFY(showEditor(editor));
  QVERIFY(editor.isZoomFit());
  QCOMPARE(editor.zoomFactor(), view->transform().m11());
  QCOMPARE(editor.toJson(), committed);
  QCOMPARE(buffer.getContentRaw(), content);
  QVERIFY(dirty.isEmpty());
}

void TestMindMapEditor::configuredShortcutsDriveRealCommands() {
  auto &settings = m_config->getEditorConfig().getMindMapEditorConfig();
  settings.fromJson(QJsonObject{
      {QStringLiteral("style"), QJsonObject{{QStringLiteral("font-size"), 18}}},
      {QStringLiteral("undoLimit"), 2},
      {QStringLiteral("confirmSubtreeDeletion"), false},
      {QStringLiteral("autoRandomBranchColor"), false},
      {QStringLiteral("shortcuts"),
       QJsonObject{{QStringLiteral("addChild"),
                    QJsonArray{QStringLiteral("Ctrl+J"), 7, QStringLiteral("not a key")}},
                   {QStringLiteral("toggleBold"), QJsonArray{}}}}});
  auto buffer = openNote(QStringLiteral("configured.emind"), fixture());
  QVERIFY(buffer.isValid());
  MindMapEditor editor(*m_services, buffer);
  QVERIFY(editor.loadContent(buffer.getContentRaw()));
  auto *view = showEditor(editor);
  QVERIFY(view);
  QVERIFY(editor.selectNode(QStringLiteral("a")));
  const auto before = editor.toJson();
  QTest::keyClick(view->viewport(), Qt::Key_B);
  QCOMPARE(editor.toJson(), before);
  QVERIFY(editor.selectNode(QStringLiteral("r")));
  QTest::keyClick(view->viewport(), Qt::Key_J, Qt::ControlModifier);
  const auto created = editor.selectedNodeId();
  QVERIFY(!created.isEmpty() && created != QStringLiteral("r"));
  QCOMPARE(node(editor, QStringLiteral("r")).value(QStringLiteral("children")).toArray().size(), 2);
  QVERIFY(!node(editor, created)
               .value(QStringLiteral("style"))
               .toObject()
               .contains(QStringLiteral("branchColor")));
  auto *input = view->findChild<QPlainTextEdit *>(QStringLiteral("topicEditor"));
  QVERIFY(input && input->isVisible());
  QCOMPARE(input->font().pointSize(), 18);
  QTest::keyClick(input, Qt::Key_Escape);
  QVERIFY(editor.selectNode(created));
  QTest::keyClick(view->viewport(), Qt::Key_Delete);
  QVERIFY(editor.nodeJson(created).isEmpty());
}

void TestMindMapEditor::livePreferencesPreserveDraftAndReadOnlyBuffer() {
  const QByteArray content = QByteArrayLiteral(R"({"schemaVersion":1,"rootId":"r","nodes":[
    {"id":"r","topic":"Root","children":["a","b"]},
    {"id":"a","topic":"Inherited"},
    {"id":"b","topic":"Pixel override","style":{"fontSize":24}}],"crossLinks":[]})");
  auto buffer = openNote(QStringLiteral("live.emind"), content);
  auto readOnlyBuffer = openNote(QStringLiteral("readonly.emind"), content, true);
  QVERIFY(buffer.isValid() && readOnlyBuffer.isValid());
  MindMapEditor editor(*m_services, buffer), readOnly(*m_services, readOnlyBuffer);
  QVERIFY(editor.loadContent(buffer.getContentRaw()));
  QVERIFY(readOnly.loadContent(readOnlyBuffer.getContentRaw()));
  QVERIFY(showEditor(readOnly));
  auto *view = showEditor(editor);
  QVERIFY(view);
  QVERIFY(editor.selectNode(QStringLiteral("a")));
  QPointer<QPlainTextEdit> input = beginDraft(editor);
  QVERIFY(input);
  input->selectAll();
  QTest::keyClicks(input, "Unsaved preference draft");
  const auto cursor = input->textCursor();
  const auto committed = editor.toJson();
  const auto bufferRevision = buffer.getRevision();
  QSignalSpy dirty(&editor, &MindMapEditor::contentsChanged);
  QSignalSpy readOnlyDirty(&readOnly, &MindMapEditor::contentsChanged);
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
  const auto families = QFontDatabase::families();
#else
  const auto families = QFontDatabase().families();
#endif
  QVERIFY(!families.isEmpty());
  const auto family = families.first().section(QLatin1Char('['), 0, 0).trimmed();
  auto &settings = m_config->getEditorConfig().getMindMapEditorConfig();
  settings.setFontFamily(QStringLiteral("missing-vnote-font, ") + family);
  settings.setFontPointSize(18);
  settings.setUndoLimit(2);
  settings.setConfirmSubtreeDeletion(false);
  settings.setAutoRandomBranchColor(false);
  editor.applyConfig();
  readOnly.applyConfig();
  for (auto *map : {&editor, &readOnly}) {
    auto *scene = map->findChild<QGraphicsView *>()->scene();
    bool inherited = false, overridden = false;
    for (auto *item : scene->items()) {
      auto *text = qgraphicsitem_cast<QGraphicsTextItem *>(item);
      if (!text) {
        continue;
      }
      if (text->toPlainText() == QStringLiteral("Root")) {
        QCOMPARE(text->font().pointSize(), 18);
        QCOMPARE(text->font().family(), family);
        inherited = true;
      } else if (text->toPlainText() == QStringLiteral("Pixel override")) {
        QCOMPARE(text->font().pixelSize(), 24);
        overridden = true;
      }
    }
    QVERIFY(inherited && overridden);
    QCOMPARE(map->toJson(), committed);
  }
  QVERIFY(input && input->isVisible() && input->hasFocus());
  QCOMPARE(input->font().pointSize(), 18);
  QCOMPARE(input->toPlainText(), QStringLiteral("Unsaved preference draft"));
  QCOMPARE(input->textCursor().position(), cursor.position());
  QCOMPARE(input->textCursor().anchor(), cursor.anchor());
  QVERIFY(editor.hasPendingEdit() && editor.isModified());
  QVERIFY(readOnly.isReadOnly() && !readOnly.isModified());
  QVERIFY(dirty.isEmpty() && readOnlyDirty.isEmpty());
  QCOMPARE(buffer.getContentRaw(), content);
  QCOMPARE(readOnlyBuffer.getContentRaw(), content);
  QCOMPARE(buffer.getRevision(), bufferRevision);
  const auto saved = editor.contentForSave();
  QVERIFY(saved.contains(QStringLiteral("Unsaved preference draft")));
  QVERIFY(input && input->hasFocus());
}

} // namespace tests

QTEST_MAIN(tests::TestMindMapEditor)
#include "test_mindmapeditor.moc"
