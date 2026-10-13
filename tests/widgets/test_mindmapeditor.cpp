#include <QtTest>

#include <QAction>
#include <QBuffer>
#include <QClipboard>
#include <QComboBox>
#include <QContextMenuEvent>
#include <QDialogButtonBox>
#include <QDir>
#include <QDockWidget>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QElapsedTimer>
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
#include <QLineEdit>
#include <QListWidget>
#include <QMainWindow>
#include <QMenu>
#include <QMimeData>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPushButton>
#include <QRadioButton>
#include <QScopeGuard>
#include <QScrollArea>
#include <QShortcut>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <QtConcurrent/QtConcurrentRun>

#include <controllers/mindmapviewwindowcontroller.h>
#include <core/configmgr2.h>
#include <core/editorconfig.h>
#include <core/hookevents.h>
#include <core/hooknames.h>
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
#include <widgets/dialogs/imageinsertdialog.h>
#include <widgets/dialogs/linkinsertdialog.h>
#include <widgets/dialogs/selectdialog.h>
#include <widgets/editors/mindmapeditor.h>

#include <deque>
#include <functional>
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

// Drives the real modal UI without a production test hook. Each step waits for its
// expected widget/ready state; the deadline closes an unexpected dialog and fails the test.
class InsertionDialogs : public QObject {
public:
  using Step = std::function<bool(QDialog *)>;
  explicit InsertionDialogs(std::initializer_list<Step> p_steps) : m_steps(p_steps) {
    m_elapsed.start();
    connect(&m_timer, &QTimer::timeout, this, [this] {
      auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
      if (m_elapsed.elapsed() > 10000) {
        m_failed = true;
        qWarning() << "Insertion dialog did not reach the expected state" << dialog;
        if (auto *selector = qobject_cast<SelectDialog *>(dialog)) {
          const auto *list = selector->findChild<QListWidget *>();
          for (int index = 0; list && index < list->count(); ++index) {
            qWarning() << "Available choice:" << list->item(index)->text();
          }
        }
        if (dialog) {
          dialog->reject();
        }
        m_timer.stop();
        return;
      }
      if (!m_busy && dialog && !m_steps.empty()) {
        m_busy = true;
        const bool handled = m_steps.front()(dialog);
        m_busy = false;
        if (handled) {
          m_steps.pop_front();
          m_elapsed.restart();
          if (m_steps.empty()) {
            m_timer.stop();
          }
        }
      }
    });
    m_timer.start(0);
  }
  bool complete() const { return !m_failed && m_steps.empty(); }
  static Step choose(const QString &p_label, std::function<void()> p_before = {}) {
    return [p_label, p_before](QDialog *p_dialog) {
      auto *selector = qobject_cast<SelectDialog *>(p_dialog);
      if (!selector) {
        return false;
      }
      auto *list = selector->findChild<QListWidget *>();
      if (!list || !selector->isActiveWindow() || !list->hasFocus()) {
        selector->activateWindow();
        QApplication::setActiveWindow(selector);
        if (list) {
          list->setFocus();
        }
        return false;
      }
      for (int index = 0; index < list->count(); ++index) {
        if (list->item(index)->text() == p_label) {
          if (p_before) {
            p_before();
          }
          // Ctrl+V enters exec() from the key press before QTest can send its
          // release. A real user releases Ctrl before choosing a letter.
          QTest::keyRelease(list, Qt::Key_Control);
          QTest::keyClick(list, list->item(index)->data(Qt::UserRole).toInt() < 0
                                    ? Qt::Key_Z
                                    : static_cast<Qt::Key>(Qt::Key_A + index));
          return !selector->isVisible();
        }
      }
      return false;
    };
  }
  static Step image(bool p_base64, int p_width = 0, int p_height = 0) {
    return [=](QDialog *p_dialog) {
      auto *dialog = qobject_cast<ImageInsertDialog *>(p_dialog);
      if (!dialog || dialog->getImage().isNull()) {
        return false;
      }
      auto *radio = dialog->findChild<QRadioButton *>(p_base64 ? QStringLiteral("imageInsertBase64")
                                                               : QStringLiteral("imageInsertFile"));
      QTest::mouseClick(radio, Qt::LeftButton);
      auto *width = dialog->findChild<QLineEdit *>(QStringLiteral("imageWidthEdit"));
      auto *height = dialog->findChild<QLineEdit *>(QStringLiteral("imageHeightEdit"));
      if (!width->isEnabled() || !height->isEnabled()) {
        return false;
      }
      if (p_width) {
        width->setText(QString::number(p_width));
      }
      if (p_height) {
        height->setText(QString::number(p_height));
      }
      auto *ok = dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok);
      if (!ok->isEnabled()) {
        return false;
      }
      QTest::mouseClick(ok, Qt::LeftButton);
      return true;
    };
  }
  static Step link(const QString &p_url, bool p_replace = true) {
    return [=](QDialog *p_dialog) {
      auto *dialog = qobject_cast<LinkInsertDialog *>(p_dialog);
      if (!dialog) {
        return false;
      }
      QLineEdit *url = nullptr;
      for (auto *input : dialog->findChildren<QLineEdit *>()) {
        if (input->isVisible()) {
          if (url) {
            return false;
          }
          url = input;
        }
      }
      if (!url || !url->hasFocus()) {
        return false;
      }
      if (p_replace) {
        url->setText(p_url);
      }
      auto *ok = dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok);
      if (!ok->isEnabled()) {
        return false;
      }
      QTest::mouseClick(ok, Qt::LeftButton);
      return true;
    };
  }

private:
  std::deque<Step> m_steps;
  QTimer m_timer;
  QElapsedTimer m_elapsed;
  bool m_failed = false;
  bool m_busy = false;
};

QPoint topicPoint(QGraphicsView *p_view, const QString &p_topic) {
  for (auto *item : p_view->scene()->items()) {
    auto *label = dynamic_cast<QGraphicsTextItem *>(item);
    if (label && label->isVisible() && label->toPlainText() == p_topic) {
      return p_view->mapFromScene(label->sceneBoundingRect().center());
    }
  }
  return QPoint(-100, -100);
}

bool dropResource(QGraphicsView *p_view, const QMimeData &p_mime, const QPoint &p_point,
                  Qt::DropActions p_actions = Qt::CopyAction) {
  QDragEnterEvent enter(p_point, p_actions, &p_mime, Qt::LeftButton, Qt::NoModifier);
  QCoreApplication::sendEvent(p_view->viewport(), &enter);
  if (!enter.isAccepted()) {
    return false;
  }
  QDropEvent drop(QPointF(p_point), p_actions, &p_mime, Qt::LeftButton, Qt::NoModifier);
  QCoreApplication::sendEvent(p_view->viewport(), &drop);
  return drop.isAccepted();
}

bool pasteResource(MindMapEditor &p_editor, QGraphicsView *p_view) {
  p_editor.activateWindow();
  QApplication::setActiveWindow(&p_editor);
  p_view->setFocus(Qt::OtherFocusReason);
  if (!QTest::qWaitFor([&] {
        return p_view->hasFocus() && p_editor.commandAction(QStringLiteral("paste"))->isEnabled();
      })) {
    qWarning() << "Canvas paste unavailable" << p_editor.canInsert() << p_editor.selectedNodeIds()
               << p_view->hasFocus();
    return false;
  }
  QTest::keyClick(p_view->viewport(), Qt::Key_V, Qt::ControlModifier);
  return true;
}

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
  void separatedPropertiesRespectAdapterPolicy();
  void propertyPlacementAppliesToNewEditors();
  void clipboardImageRoundTrip_data();
  void clipboardImageRoundTrip();
  void imageDropTargetsHitNode();
  void browserImageChoicesSnapshotClipboard();
  void embeddedImageAcquisition();
  void hyperlinkAndExplicitTextChoices();
  void batchInsertionIsAtomic();
  void insertionCancellationAndDrafts();
  void protectedInsertionCancelsOnLock();
  void insertionRetargetAndPolicy_data();
  void insertionRetargetAndPolicy();
  void batchAcquisitionRejectsStaleTarget();
  void attachmentFailureDoesNotFallback();
  void oversizedImageDoesNotPreview();
  void contextInsertionUsesSharedDialogs();

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

void TestMindMapEditor::separatedPropertiesRespectAdapterPolicy() {
  m_config->getEditorConfig().getMindMapEditorConfig().setSeparateNodePropertiesPanel(true);
  auto buffer = openNote(QStringLiteral("properties.mmm"), fixture());
  QVERIFY(buffer.isValid());
  QMainWindow window;
  auto *editor = new MindMapEditor(*m_services, buffer, &window);
  window.setCentralWidget(editor);
  QVERIFY(editor->loadContent(buffer.getContentRaw()));
  auto *panel = editor->nodePropertiesPanel();
  QVERIFY(panel);
  auto *dock = new QDockWidget(&window);
  dock->setWidget(panel);
  window.addDockWidget(Qt::RightDockWidgetArea, dock);
  window.resize(1100, 800);
  window.show();
  window.activateWindow();
  QVERIFY(QTest::qWaitForWindowExposed(&window));
  QCOMPARE(editor->nodePropertiesPanel(), panel);
  auto *url = panel->findChild<QLineEdit *>(QStringLiteral("nodeUrl"));
  auto *scroll = panel->findChild<QScrollArea *>();
  QVERIFY(url && scroll);
  scroll->ensureWidgetVisible(url);
  QVERIFY(editor->selectNode(QStringLiteral("a")));
  url->setFocus();
  QSignalSpy dirty(editor, &MindMapEditor::contentsChanged);
  QTest::keyClicks(url, "https://example.test/node");
  QCOMPARE(node(*editor, QStringLiteral("a")).value(QStringLiteral("hyperLink")).toString(),
           QStringLiteral("https://example.test/node"));
  QVERIFY(editor->isModified() && !dirty.isEmpty());
  const auto saved = editor->contentForSave();
  MindMapEditor reopened(*m_services, buffer);
  QVERIFY(reopened.loadContent(saved.toUtf8()));
  QCOMPARE(node(reopened, QStringLiteral("a")).value(QStringLiteral("hyperLink")).toString(),
           url->text());

  editor->setModified(false);
  dirty.clear();
  const auto committed = editor->toJson();
  editor->setEnabled(false);
  QTest::keyClicks(url, "blocked-disabled");
  QCOMPARE(editor->toJson(), committed);
  QVERIFY(dirty.isEmpty() && !editor->isModified());
  editor->setEnabled(true);
  editor->setReadOnly(true);
  QTest::keyClicks(url, "blocked-readonly");
  QCOMPARE(editor->toJson(), committed);
  QVERIFY(dirty.isEmpty());
  editor->setReadOnly(false);
  editor->clearSelection();
  QVERIFY(url->text().isEmpty());
  QTest::keyClicks(url, "blocked-no-selection");
  QCOMPARE(editor->toJson(), committed);
  QVERIFY(dirty.isEmpty());

  QVERIFY(editor->selectNode(QStringLiteral("a")));
  QVERIFY(!editor->loadContent(QByteArrayLiteral("invalid mind map")));
  QTest::keyClicks(url, "blocked-invalid");
  QCOMPARE(editor->toJson(), committed);
  QVERIFY(dirty.isEmpty());
  QVERIFY(editor->loadContent(saved.toUtf8()));
  QVERIFY(editor->selectNode(QStringLiteral("a")));
  url->setFocus();
  QTest::keyClicks(url, "/recovered");
  QVERIFY(node(*editor, QStringLiteral("a"))
              .value(QStringLiteral("hyperLink"))
              .toString()
              .endsWith(QStringLiteral("/recovered")));
  QVERIFY(editor->isModified());
}

void TestMindMapEditor::propertyPlacementAppliesToNewEditors() {
  auto &config = m_config->getEditorConfig().getMindMapEditorConfig();
  config.setSeparateNodePropertiesPanel(true);
  auto buffer = openNote(QStringLiteral("placement.mmm"), fixture());
  QVERIFY(buffer.isValid());
  MindMapEditor separated(*m_services, buffer);
  QVERIFY(separated.loadContent(buffer.getContentRaw()));
  auto *panel = separated.nodePropertiesPanel();
  QVERIFY(panel);
  config.setSeparateNodePropertiesPanel(false);
  separated.applyConfig();
  QCOMPARE(separated.nodePropertiesPanel(), panel);

  MindMapEditor embedded(*m_services, buffer);
  QVERIFY(embedded.loadContent(buffer.getContentRaw()));
  QVERIFY(!embedded.nodePropertiesPanel());
  QVERIFY(showEditor(embedded));
  auto *toggle = embedded.findChild<QToolButton *>(QStringLiteral("nodePropertiesToggle"));
  auto *card = embedded.findChild<QWidget *>(QStringLiteral("nodePropertiesPanel"));
  auto *scroll = card ? card->findChild<QScrollArea *>() : nullptr;
  QVERIFY(toggle && card && scroll);
  QVERIFY(toggle->isVisible() && !toggle->isChecked() && scroll->isHidden());
  toggle->click();
  QVERIFY(scroll->isVisible());
  config.setSeparateNodePropertiesPanel(true);
  embedded.applyConfig();
  QVERIFY(!embedded.nodePropertiesPanel());
  QVERIFY(scroll->isVisible());
  MindMapEditor separatedAgain(*m_services, buffer);
  QVERIFY(separatedAgain.nodePropertiesPanel());
}

void TestMindMapEditor::clipboardImageRoundTrip_data() {
  QTest::addColumn<QString>("format");
  QTest::addColumn<bool>("base64");
  QTest::newRow("embedded-lossless") << QStringLiteral("jpeg") << true;
  QTest::newRow("file-png-alpha") << QStringLiteral("png") << false;
  QTest::newRow("file-jpeg-white-alpha") << QStringLiteral("jpeg") << false;
}

void TestMindMapEditor::clipboardImageRoundTrip() {
  QFETCH(QString, format);
  QFETCH(bool, base64);
  auto buffer = openNote(QStringLiteral("clipboard.mmm"), fixture());
  MindMapEditor editor(*m_services, buffer);
  QVERIFY(editor.loadContent(buffer.getContentRaw()));
  auto *view = showEditor(editor);
  QVERIFY(view);
  QVERIFY(editor.selectNode(QStringLiteral("a")));
  QVERIFY(editor.updateNode(
      QStringLiteral("a"),
      QByteArrayLiteral(
          "{\"hyperLink\":\"https://example.test/keep\",\"style\":{\"fontWeight\":\"bold\"}}")));
  const auto before = node(editor, QStringLiteral("a"));
  const auto baseline = editor.toJson();
  editor.setModified(false);
  m_config->getEditorConfig().setDefaultImageFormat(format);
  QImage pixels(120, 60, QImage::Format_ARGB32);
  pixels.fill(Qt::transparent);
  const QColor color(28, 170, 62);
  {
    QPainter painter(&pixels);
    painter.fillRect(QRect(20, 10, 80, 40), color);
  }
  QApplication::clipboard()->setImage(pixels);
  InsertionDialogs dialogs({InsertionDialogs::image(base64, 90, 45)});
  QVERIFY(pasteResource(editor, view));
  QVERIFY(dialogs.complete());
  const auto after = node(editor, QStringLiteral("a"));
  QCOMPARE(after.value(QStringLiteral("topic")), before.value(QStringLiteral("topic")));
  QCOMPARE(after.value(QStringLiteral("style")), before.value(QStringLiteral("style")));
  QCOMPARE(after.value(QStringLiteral("hyperLink")), before.value(QStringLiteral("hyperLink")));
  const auto image = after.value(QStringLiteral("image")).toObject();
  QCOMPARE(image.value(QStringLiteral("width")).toInt(), 90);
  QCOMPARE(image.value(QStringLiteral("height")).toInt(), 45);
  const auto url = image.value(QStringLiteral("url")).toString();
  QByteArray bytes;
  if (base64) {
    QVERIFY(url.startsWith(QStringLiteral("data:image/png;base64,")));
    bytes = QByteArray::fromBase64(url.mid(url.indexOf(QLatin1Char(',')) + 1).toLatin1());
  } else {
    QVERIFY(QUrl(url).isRelative());
    bytes =
        readFile(QDir(buffer.getResourceBasePath()).filePath(QUrl(url).path(QUrl::FullyDecoded)));
  }
  const auto decoded = QImage::fromData(bytes);
  QVERIFY(!decoded.isNull());
  QCOMPARE(decoded.size(), pixels.size());
  if (base64 || format == QStringLiteral("png")) {
    QCOMPARE(decoded.pixelColor(0, 0).alpha(), 0);
    QCOMPARE(decoded.pixelColor(60, 30), color);
  } else {
    QVERIFY(bytes.startsWith(QByteArray::fromHex("ffd8")));
    QCOMPARE(decoded.pixelColor(0, 0), QColor(Qt::white));
  }
  QTRY_VERIFY(rendersColor(editor, decoded.pixelColor(60, 30)));
  QVERIFY(editor.isModified());
  const auto committed = editor.toJson();
  QVERIFY(editor.undo());
  QCOMPARE(editor.toJson(), baseline);
  QVERIFY(editor.redo());
  QCOMPARE(editor.toJson(), committed);
  QTRY_VERIFY(rendersColor(editor, decoded.pixelColor(60, 30)));
  QVERIFY(buffer.setContentRaw(editor.contentForSave().toUtf8()));
  QVERIFY(buffer.save());
  MindMapEditor reopened(*m_services, buffer);
  QVERIFY(reopened.loadContent(readFile(buffer.resolvedPath())));
  QVERIFY(showEditor(reopened));
  QCOMPARE(node(reopened, QStringLiteral("a")), after);
  QTRY_VERIFY(rendersColor(reopened, decoded.pixelColor(60, 30)));
}

void TestMindMapEditor::imageDropTargetsHitNode() {
  const auto buffer = openNote(QStringLiteral("drop.mmm"), fixture());
  MindMapEditor editor(*m_services, buffer);
  QVERIFY(editor.loadContent(buffer.getContentRaw()));
  auto *view = showEditor(editor);
  QVERIFY(view);
  const QColor sourceColor(220, 45, 70), thumbnailColor(20, 180, 90);
  const auto path = m_directory->filePath(QStringLiteral("image # 25%.png"));
  QVERIFY(writeFile(path, imageBytes(sourceColor)));
  const auto selected = editor.selectedNodeId();
  const auto root = node(editor, selected);
  const auto camera = view->transform();
  QMimeData mime;
  mime.setUrls({QUrl::fromLocalFile(path)});
  mime.setImageData(QImage::fromData(imageBytes(thumbnailColor)));
  const auto point = topicPoint(view, QStringLiteral("Child"));
  InsertionDialogs dialogs(
      {InsertionDialogs::choose(QStringLiteral("Insert As Image")), InsertionDialogs::image(true)});
  QVERIFY(dropResource(view, mime, point));
  QVERIFY(dialogs.complete());
  QCOMPARE(node(editor, selected), root);
  QCOMPARE(editor.selectedNodeId(), selected);
  QCOMPARE(view->transform(), camera);
  QTRY_VERIFY(rendersColor(editor, sourceColor));
  QVERIFY(!rendersColor(editor, thumbnailColor));
  const auto committed = editor.toJson();
  QVERIFY(!dropResource(view, mime, QPoint(-10, -10)));
  QVERIFY(!dropResource(view, mime, point, Qt::MoveAction));
  QCOMPARE(editor.toJson(), committed);
  QVERIFY(editor.undo());
  mime.removeFormat(QStringLiteral("application/x-qt-image"));
  InsertionDialogs urlOnly(
      {InsertionDialogs::choose(QStringLiteral("Insert As Image")), InsertionDialogs::image(true)});
  QVERIFY(dropResource(view, mime, topicPoint(view, QStringLiteral("Child"))));
  QVERIFY(urlOnly.complete());
  QTRY_VERIFY(rendersColor(editor, sourceColor));
}

void TestMindMapEditor::browserImageChoicesSnapshotClipboard() {
  const auto buffer = openNote(QStringLiteral("browser.mmm"), fixture());
  MindMapEditor editor(*m_services, buffer);
  QVERIFY(editor.loadContent(buffer.getContentRaw()));
  auto *view = showEditor(editor);
  QVERIFY(view);
  ImageServer server;
  QVERIFY(server.isListening());
  const QColor captured(222, 38, 66), downloaded(24, 172, 64), replacement(36, 62, 212);
  const auto setBrowserClipboard = [&] {
    auto *mime = new QMimeData();
    mime->setHtml(QStringLiteral("<img src=\"%1\">").arg(server.url()));
    mime->setImageData(QImage::fromData(imageBytes(captured)));
    QApplication::clipboard()->setMimeData(mime);
  };
  setBrowserClipboard();
  InsertionDialogs pixels(
      {InsertionDialogs::choose(
           QStringLiteral("Insert From Image Data"),
           [&] { QApplication::clipboard()->setImage(QImage::fromData(imageBytes(replacement))); }),
       InsertionDialogs::image(true)});
  QVERIFY(pasteResource(editor, view));
  QVERIFY(pixels.complete());
  QTRY_VERIFY(rendersColor(editor, captured));
  QCOMPARE(server.requestCount(), 0);
  QVERIFY(editor.undo());

  // Direct links do not acquire/store an asset. The normal asynchronous display loader
  // may request pixels only after the semantic image link has been committed.
  setBrowserClipboard();
  const auto assetsBefore = QDir(buffer.getAssetsFolder()).entryList(QDir::Files);
  InsertionDialogs links({InsertionDialogs::choose(QStringLiteral("Insert As Image Link"))});
  QVERIFY(pasteResource(editor, view));
  QVERIFY(links.complete());
  QCOMPARE(node(editor, QStringLiteral("r"))
               .value(QStringLiteral("image"))
               .toObject()
               .value(QStringLiteral("url"))
               .toString(),
           server.url());
  QCOMPARE(QDir(buffer.getAssetsFolder()).entryList(QDir::Files), assetsBefore);
  QTRY_COMPARE(server.requestCount(), 1);
  QVERIFY(server.reply(0, imageBytes(downloaded)));
  QTRY_VERIFY(rendersColor(editor, downloaded));
  QVERIFY(editor.undo());

  setBrowserClipboard();
  QTimer respond;
  connect(&respond, &QTimer::timeout, this, [&] {
    if (server.requestCount() == 2) {
      server.reply(1, imageBytes(downloaded));
      respond.stop();
    }
  });
  respond.start(0);
  InsertionDialogs urls(
      {InsertionDialogs::choose(QStringLiteral("Insert From URL")), InsertionDialogs::image(true)});
  QVERIFY(pasteResource(editor, view));
  QVERIFY(urls.complete());
  QTRY_VERIFY(rendersColor(editor, downloaded));
  QCOMPARE(server.requestCount(), 2);
}

void TestMindMapEditor::embeddedImageAcquisition() {
  const auto buffer = openNote(QStringLiteral("embedded.mmm"), fixture());
  MindMapEditor editor(*m_services, buffer);
  QVERIFY(editor.loadContent(buffer.getContentRaw()));
  auto *view = showEditor(editor);
  QVERIFY(view);
  const QColor color(216, 36, 65);
  const auto source = dataUrl(imageBytes(color));
  QApplication::clipboard()->setText(source);
  InsertionDialogs paste({InsertionDialogs::choose(QStringLiteral("Insert as Base64")),
                          InsertionDialogs::image(true)});
  QVERIFY(pasteResource(editor, view));
  QVERIFY(paste.complete());
  QTRY_VERIFY(rendersColor(editor, color));
  QVERIFY(editor.undo());
  QApplication::clipboard()->clear();
  InsertionDialogs typed({[&](QDialog *p_dialog) {
                            auto *dialog = qobject_cast<ImageInsertDialog *>(p_dialog);
                            if (!dialog) {
                              return false;
                            }
                            // From is the first editable field; paste through its real native input
                            // path.
                            auto *from = dialog->findChildren<QLineEdit *>().first();
                            from->setFocus();
                            QApplication::clipboard()->setText(source);
                            QTest::keyClick(from, Qt::Key_V, Qt::ControlModifier);
                            return true;
                          },
                          InsertionDialogs::image(true)});
  editor.typeImage();
  QVERIFY(typed.complete());
  QTRY_VERIFY(rendersColor(editor, color));
  QVERIFY(editor.undo());
  const auto before = editor.toJson();
  QSignalSpy errors(&editor, &MindMapEditor::statusMessageRequested);
  QApplication::clipboard()->setText(QStringLiteral("data:image/png;base64,@@invalid"));
  InsertionDialogs invalid({InsertionDialogs::choose(QStringLiteral("Insert as Base64"))});
  QVERIFY(pasteResource(editor, view));
  QVERIFY(invalid.complete());
  QCOMPARE(editor.toJson(), before);
  QVERIFY(!errors.isEmpty());
}

void TestMindMapEditor::hyperlinkAndExplicitTextChoices() {
  auto buffer = openNote(QStringLiteral("links.mmm"), fixture());
  MindMapEditor editor(*m_services, buffer);
  QVERIFY(editor.loadContent(buffer.getContentRaw()));
  auto *view = showEditor(editor);
  QVERIFY(view);
  QVERIFY(editor.selectNode(QStringLiteral("a")));
  const auto original = node(editor, QStringLiteral("a"));
  QApplication::clipboard()->setText(QStringLiteral("https://example.test/original"));
  InsertionDialogs toolbar({InsertionDialogs::link(QStringLiteral("https://example.test/chosen"))});
  editor.typeLink();
  QVERIFY(toolbar.complete());
  QCOMPARE(node(editor, QStringLiteral("a")).value(QStringLiteral("topic")),
           original.value(QStringLiteral("topic")));
  InsertionDialogs remove({InsertionDialogs::link(QString())});
  editor.typeLink();
  QVERIFY(remove.complete());
  QVERIFY(
      node(editor, QStringLiteral("a")).value(QStringLiteral("hyperLink")).toString().isEmpty());
  QCOMPARE(node(editor, QStringLiteral("a")).value(QStringLiteral("topic")),
           original.value(QStringLiteral("topic")));

  const auto path = QDir(buffer.getResourceBasePath()).filePath(QStringLiteral("file # 25%.txt"));
  const QByteArray content("Chosen file content\nSecond line");
  QVERIFY(writeFile(path, content));
  auto url = QUrl::fromLocalFile(path);
  url.setFragment(QStringLiteral("Heading # 20%"));
  const auto setUrl = [&] {
    auto *mime = new QMimeData();
    mime->setUrls({url});
    QApplication::clipboard()->setMimeData(mime);
  };
  setUrl();
  InsertionDialogs relative({InsertionDialogs::choose(QStringLiteral("Insert As Relative Link")),
                             InsertionDialogs::link(QString(), false)});
  QVERIFY(pasteResource(editor, view));
  QVERIFY(relative.complete());
  const auto relativeUrl =
      QUrl(node(editor, QStringLiteral("a")).value(QStringLiteral("hyperLink")).toString());
  QVERIFY(relativeUrl.isRelative());
  QCOMPARE(QFileInfo(relativeUrl.path(QUrl::FullyDecoded)).fileName(), QFileInfo(path).fileName());
  QCOMPARE(relativeUrl.fragment(QUrl::FullyDecoded), url.fragment(QUrl::FullyDecoded));
  QCOMPARE(node(editor, QStringLiteral("a")).value(QStringLiteral("topic")),
           original.value(QStringLiteral("topic")));

  setUrl();
  InsertionDialogs attach({InsertionDialogs::choose(QStringLiteral("Attach And Insert Link"))});
  QVERIFY(pasteResource(editor, view));
  QVERIFY(attach.complete());
  const auto attachmentLink =
      QUrl(node(editor, QStringLiteral("a")).value(QStringLiteral("hyperLink")).toString());
  QVERIFY(attachmentLink.isRelative());
  QVERIFY(attachmentLink.fragment().isEmpty());
  const auto copied =
      QDir(buffer.getResourceBasePath()).filePath(attachmentLink.path(QUrl::FullyDecoded));
  QVERIFY(copied != path);
  QCOMPARE(readFile(copied), content);
  QCOMPARE(node(editor, QStringLiteral("a")).value(QStringLiteral("topic")),
           original.value(QStringLiteral("topic")));
  const auto previous = node(editor, QStringLiteral("a"));
  setUrl();
  InsertionDialogs fileText({InsertionDialogs::choose(QStringLiteral("Insert File Content"))});
  QVERIFY(pasteResource(editor, view));
  QVERIFY(fileText.complete());
  QCOMPARE(node(editor, QStringLiteral("a")).value(QStringLiteral("topic")).toString(),
           QString::fromUtf8(content));
  QCOMPARE(node(editor, QStringLiteral("a")).value(QStringLiteral("hyperLink")),
           previous.value(QStringLiteral("hyperLink")));
  QCOMPARE(node(editor, QStringLiteral("a")).value(QStringLiteral("style")),
           original.value(QStringLiteral("style")));
  auto *textMime = new QMimeData();
  textMime->setUrls({url});
  const auto sourceText = QStringLiteral("Explicit clipboard text # 25%");
  textMime->setText(sourceText);
  QApplication::clipboard()->setMimeData(textMime);
  InsertionDialogs text({InsertionDialogs::choose(QStringLiteral("Insert As Text"))});
  QVERIFY(pasteResource(editor, view));
  QVERIFY(text.complete());
  QCOMPARE(node(editor, QStringLiteral("a")).value(QStringLiteral("topic")).toString(), sourceText);
}

void TestMindMapEditor::batchInsertionIsAtomic() {
  const auto buffer = openNote(QStringLiteral("batch.mmm"), fixture());
  MindMapEditor editor(*m_services, buffer);
  QVERIFY(editor.loadContent(buffer.getContentRaw()));
  auto *view = showEditor(editor);
  QVERIFY(view);
  QVERIFY(editor.selectNode(QStringLiteral("a")));
  QVERIFY(editor.setExpanded(QStringLiteral("a"), false));
  const auto parent = node(editor, QStringLiteral("a"));
  const auto baseline = editor.toJson();
  const auto first = m_directory->filePath(QStringLiteral("first.png"));
  const auto second = m_directory->filePath(QStringLiteral("second.png"));
  const QColor red(215, 38, 56), green(21, 175, 61);
  QVERIFY(writeFile(first, imageBytes(red)));
  QVERIFY(writeFile(second, imageBytes(green)));
  const auto setUrls = [&](const QString &p_second) {
    auto *mime = new QMimeData();
    mime->setUrls({QUrl::fromLocalFile(first), QUrl::fromLocalFile(p_second)});
    QApplication::clipboard()->setMimeData(mime);
  };
  setUrls(second);
  InsertionDialogs batch({InsertionDialogs::choose(QStringLiteral("Insert as Base64"))});
  QVERIFY(pasteResource(editor, view));
  QVERIFY(batch.complete());
  const auto changedParent = node(editor, QStringLiteral("a"));
  QCOMPARE(changedParent.value(QStringLiteral("topic")), parent.value(QStringLiteral("topic")));
  QCOMPARE(changedParent.value(QStringLiteral("style")), parent.value(QStringLiteral("style")));
  const auto children = changedParent.value(QStringLiteral("children")).toArray();
  QCOMPARE(children.size(), 3);
  QCOMPARE(children.first().toString(), QStringLiteral("b"));
  QCOMPARE(node(editor, children.at(1).toString()).value(QStringLiteral("topic")).toString(),
           QStringLiteral("first.png"));
  QCOMPARE(node(editor, children.at(2).toString()).value(QStringLiteral("topic")).toString(),
           QStringLiteral("second.png"));
  QCOMPARE(editor.selectedNodeId(), QStringLiteral("a"));
  QTRY_VERIFY(rendersColor(editor, red) && rendersColor(editor, green));
  const auto committed = editor.toJson();
  QVERIFY(editor.undo());
  QCOMPARE(editor.toJson(), baseline);
  QVERIFY(editor.redo());
  QCOMPARE(editor.toJson(), committed);
  QVERIFY(editor.undo());
  const auto invalid = m_directory->filePath(QStringLiteral("bad.png"));
  QVERIFY(writeFile(invalid, QByteArrayLiteral("not an image")));
  const auto assetsBefore = QDir(buffer.getAssetsFolder()).entryList(QDir::Files);
  setUrls(invalid);
  QSignalSpy errors(&editor, &MindMapEditor::statusMessageRequested);
  InsertionDialogs failed({InsertionDialogs::choose(QStringLiteral("Insert As Image"))});
  QVERIFY(pasteResource(editor, view));
  QVERIFY(failed.complete());
  QCOMPARE(editor.toJson(), baseline);
  QCOMPARE(QDir(buffer.getAssetsFolder()).entryList(QDir::Files), assetsBefore);
  QVERIFY(!errors.isEmpty());
}

void TestMindMapEditor::insertionCancellationAndDrafts() {
  const auto buffer = openNote(QStringLiteral("lifecycle.mmm"), fixture());
  auto editor = std::make_unique<MindMapEditor>(*m_services, buffer);
  QVERIFY(editor->loadContent(buffer.getContentRaw()));
  auto *view = showEditor(*editor);
  QVERIFY(view);
  const auto baseline = editor->toJson();
  QApplication::clipboard()->setText(QStringLiteral("https://example.test/source"));
  InsertionDialogs canceled({InsertionDialogs::choose(QStringLiteral("Cancel"))});
  QVERIFY(pasteResource(*editor, view));
  QVERIFY(canceled.complete());
  QCOMPARE(editor->toJson(), baseline);

  QApplication::clipboard()->setImage(QImage::fromData(imageBytes(QColor(224, 35, 54))));
  InsertionDialogs readonly({[&](QDialog *p_dialog) {
    if (!qobject_cast<ImageInsertDialog *>(p_dialog)) {
      return false;
    }
    editor->setReadOnly(true);
    return true;
  }});
  editor->typeImage();
  QVERIFY(readonly.complete());
  QCOMPARE(editor->toJson(), baseline);
  QVERIFY(!editor->canInsert());
  editor->setReadOnly(false);

  InsertionDialogs stale({[&](QDialog *p_dialog) {
    if (!qobject_cast<ImageInsertDialog *>(p_dialog)) {
      return false;
    }
    editor->renameNode(QStringLiteral("r"), QStringLiteral("Changed while acquiring"));
    p_dialog->accept();
    return true;
  }});
  editor->typeImage();
  QVERIFY(stale.complete());
  QVERIFY(node(*editor, QStringLiteral("r")).value(QStringLiteral("image")).toObject().isEmpty());
  QCOMPARE(node(*editor, QStringLiteral("r")).value(QStringLiteral("topic")).toString(),
           QStringLiteral("Changed while acquiring"));

  auto *draft = beginDraft(*editor);
  QVERIFY(draft);
  draft->setPlainText(QStringLiteral("Durable initiating draft"));
  InsertionDialogs committed({InsertionDialogs::image(true)});
  editor->typeImage();
  QVERIFY(committed.complete());
  QCOMPARE(node(*editor, QStringLiteral("r")).value(QStringLiteral("topic")).toString(),
           QStringLiteral("Durable initiating draft"));
  QVERIFY(!node(*editor, QStringLiteral("r")).value(QStringLiteral("image")).toObject().isEmpty());
  draft = beginDraft(*editor);
  QVERIFY(draft);
  QString invalid = QStringLiteral("invalid");
  invalid.append(QChar::Null);
  draft->setPlainText(invalid);
  const auto beforeRejected = editor->toJson();
  editor->typeImage();
  QCOMPARE(editor->toJson(), beforeRejected);
  QVERIFY(!QApplication::activeModalWidget());
  QTest::keyClick(draft, Qt::Key_Escape);

  InsertionDialogs destroyed({[&](QDialog *p_dialog) {
    if (!qobject_cast<ImageInsertDialog *>(p_dialog)) {
      return false;
    }
    editor.reset();
    return true;
  }});
  editor->typeImage();
  QVERIFY(destroyed.complete());
  QVERIFY(!editor);
}

void TestMindMapEditor::protectedInsertionCancelsOnLock() {
  const auto buffer = openProtectedNote(fixture());
  QVERIFY(buffer.isValid());
  MindMapEditor editor(*m_services, buffer);
  QVERIFY(editor.loadContent(buffer.getContentRaw()));
  auto *view = showEditor(editor);
  QVERIFY(view);
  const auto baseline = editor.toJson();
  QApplication::clipboard()->setImage(QImage::fromData(imageBytes(QColor(217, 37, 67))));
  bool locked = false;
  InsertionDialogs lock({[&](QDialog *p_dialog) {
    auto *dialog = qobject_cast<ImageInsertDialog *>(p_dialog);
    if (!dialog || !dialog->insertAsBase64()) {
      return false;
    }
    locked = m_buffers->beginProtectedLocking();
    return true;
  }});
  QVERIFY(pasteResource(editor, view));
  QVERIFY(lock.complete());
  QVERIFY(locked);
  QCOMPARE(editor.toJson(), baseline);
  QVERIFY(!editor.canInsert());
  m_buffers->cancelProtectedLocking();
  QVERIFY(editor.canInsert());
}

void TestMindMapEditor::insertionRetargetAndPolicy_data() {
  QTest::addColumn<int>("transition");
  QTest::newRow("reload") << 0;
  QTest::newRow("retarget") << 1;
  QTest::newRow("disable") << 2;
  QTest::newRow("remove-target") << 3;
  QTest::newRow("selection-keeps-captured-target") << 4;
}

void TestMindMapEditor::insertionRetargetAndPolicy() {
  QFETCH(int, transition);
  const auto buffer = openNote(QStringLiteral("retarget.mmm"), fixture());
  const auto replacement = openNote(QStringLiteral("replacement.mmm"), fixture());
  MindMapEditor editor(*m_services, buffer);
  QVERIFY(editor.loadContent(buffer.getContentRaw()));
  QVERIFY(showEditor(editor));
  QVERIFY(editor.selectNode(QStringLiteral("a")));
  QApplication::clipboard()->setImage(QImage::fromData(imageBytes(QColor(220, 36, 64))));
  const auto assetsBefore = QDir(buffer.getAssetsFolder()).entryList(QDir::Files);
  bool changed = false;
  InsertionDialogs dialogs({[&](QDialog *p_dialog) {
    if (!qobject_cast<ImageInsertDialog *>(p_dialog)) {
      return false;
    }
    if (transition == 0) {
      changed = editor.loadContent(fixture());
    } else if (transition == 1) {
      editor.setBuffer(replacement);
      changed = editor.loadContent(replacement.getContentRaw());
    } else if (transition == 2) {
      editor.setEnabled(false);
      changed = !editor.isEnabled();
    } else if (transition == 3) {
      changed = editor.removeNode(QStringLiteral("a"));
      p_dialog->accept();
    } else {
      changed = editor.selectNode(QStringLiteral("r"));
      return InsertionDialogs::image(true)(p_dialog);
    }
    return true;
  }});
  editor.typeImage();
  QVERIFY(dialogs.complete());
  QVERIFY(changed);
  if (transition == 4) {
    QVERIFY(!node(editor, QStringLiteral("a")).value(QStringLiteral("image")).toObject().isEmpty());
    QVERIFY(node(editor, QStringLiteral("r")).value(QStringLiteral("image")).toObject().isEmpty());
    QCOMPARE(editor.selectedNodeId(), QStringLiteral("r"));
  } else {
    QVERIFY(node(editor, QStringLiteral("a")).value(QStringLiteral("image")).toObject().isEmpty());
    QCOMPARE(QDir(buffer.getAssetsFolder()).entryList(QDir::Files), assetsBefore);
  }
}

void TestMindMapEditor::batchAcquisitionRejectsStaleTarget() {
  const auto buffer = openNote(QStringLiteral("batch-stale.mmm"), fixture());
  MindMapEditor editor(*m_services, buffer);
  QVERIFY(editor.loadContent(buffer.getContentRaw()));
  auto *view = showEditor(editor);
  QVERIFY(view);
  ImageServer server;
  QVERIFY(server.isListening());
  const auto first = m_directory->filePath(QStringLiteral("prepared.png"));
  const auto bytes = imageBytes(QColor(216, 35, 62));
  QVERIFY(writeFile(first, bytes));
  const auto assetsBefore = QDir(buffer.getAssetsFolder()).entryList(QDir::Files);
  auto *mime = new QMimeData();
  mime->setUrls({QUrl::fromLocalFile(first), QUrl(server.url())});
  QApplication::clipboard()->setMimeData(mime);
  bool changed = false;
  QTimer response;
  connect(&response, &QTimer::timeout, this, [&] {
    if (server.requestCount() == 1) {
      response.stop();
      changed = editor.renameNode(QStringLiteral("r"), QStringLiteral("Changed during download"));
      server.reply(0, bytes);
    }
  });
  response.start(0);
  InsertionDialogs dialogs({InsertionDialogs::choose(QStringLiteral("Insert As Image"))});
  QVERIFY(pasteResource(editor, view));
  QVERIFY(dialogs.complete());
  QVERIFY(changed);
  QCOMPARE(node(editor, QStringLiteral("r")).value(QStringLiteral("children")).toArray(),
           QJsonArray{QStringLiteral("a")});
  QCOMPARE(node(editor, QStringLiteral("r")).value(QStringLiteral("topic")).toString(),
           QStringLiteral("Changed during download"));
  QCOMPARE(QDir(buffer.getAssetsFolder()).entryList(QDir::Files), assetsBefore);
}

void TestMindMapEditor::attachmentFailureDoesNotFallback() {
  const auto buffer = openNote(QStringLiteral("attachment.mmm"), fixture());
  MindMapEditor editor(*m_services, buffer);
  QVERIFY(editor.loadContent(buffer.getContentRaw()));
  auto *view = showEditor(editor);
  QVERIFY(view);
  const auto source = m_directory->filePath(QStringLiteral("attachment.txt"));
  QVERIFY(writeFile(source, QByteArrayLiteral("Attachment contents")));
  const auto before = editor.toJson();
  auto *mime = new QMimeData();
  mime->setUrls({QUrl::fromLocalFile(source)});
  QApplication::clipboard()->setMimeData(mime);
  QSignalSpy errors(&editor, &MindMapEditor::statusMessageRequested);
  InsertionDialogs failed({InsertionDialogs::choose(QStringLiteral("Attach And Insert Link"),
                                                    [&] { QFile::remove(source); })});
  QVERIFY(pasteResource(editor, view));
  QVERIFY(failed.complete());
  QCOMPARE(editor.toJson(), before);
  QVERIFY(buffer.listAttachments().isEmpty());
  QVERIFY(!errors.isEmpty());

  // A post-copy hook changes the captured node. Roll back only the just-created attachment.
  QVERIFY(writeFile(source, QByteArrayLiteral("Attachment contents")));
  mime = new QMimeData();
  mime->setUrls({QUrl::fromLocalFile(source)});
  QApplication::clipboard()->setMimeData(mime);
  const auto hook = m_hooks->addAction<AttachmentAddEvent>(
      HookNames::AttachmentAfterAdd,
      [&](HookContext &, const AttachmentAddEvent &) {
        editor.renameNode(QStringLiteral("r"), QStringLiteral("Changed in attachment hook"));
      },
      10);
  const auto removeHook = qScopeGuard([&] { m_hooks->removeAction(hook); });
  InsertionDialogs stale({InsertionDialogs::choose(QStringLiteral("Attach And Insert Link"))});
  QVERIFY(pasteResource(editor, view));
  QVERIFY(stale.complete());
  QCOMPARE(node(editor, QStringLiteral("r")).value(QStringLiteral("topic")).toString(),
           QStringLiteral("Changed in attachment hook"));
  QVERIFY(
      node(editor, QStringLiteral("r")).value(QStringLiteral("hyperLink")).toString().isEmpty());
  QVERIFY(buffer.listAttachments().isEmpty());
  QVERIFY(QDir(buffer.getAttachmentsFolder()).entryList(QDir::Files).isEmpty());
}

void TestMindMapEditor::oversizedImageDoesNotPreview() {
  const auto buffer = openNote(QStringLiteral("oversized.mmm"), fixture());
  MindMapEditor editor(*m_services, buffer);
  QVERIFY(editor.loadContent(buffer.getContentRaw()));
  QVERIFY(showEditor(editor));
  const auto source = m_directory->filePath(QStringLiteral("too-large.png"));
  QFile oversized(source);
  QVERIFY(oversized.open(QIODevice::WriteOnly));
  QVERIFY(oversized.resize(32 * 1024 * 1024 + 1));
  oversized.close();
  auto *mime = new QMimeData();
  mime->setUrls({QUrl::fromLocalFile(source)});
  QApplication::clipboard()->setMimeData(mime);
  const auto before = editor.toJson();
  InsertionDialogs dialogs({[](QDialog *p_dialog) {
    auto *dialog = qobject_cast<ImageInsertDialog *>(p_dialog);
    if (!dialog) {
      return false;
    }
    // Trigger the real queued From-field acquisition, then observe disabled acceptance.
    QMetaObject::invokeMethod(dialog, "checkImagePathInput", Qt::DirectConnection);
    auto *ok = dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok);
    if (!dialog->getImage().isNull() || ok->isEnabled()) {
      return false;
    }
    QTest::mouseClick(dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Cancel),
                      Qt::LeftButton);
    return true;
  }});
  editor.typeImage();
  QVERIFY(dialogs.complete());
  QCOMPARE(editor.toJson(), before);
}

void TestMindMapEditor::contextInsertionUsesSharedDialogs() {
  const auto buffer = openNote(QStringLiteral("context.mmm"), fixture());
  MindMapEditor editor(*m_services, buffer);
  QVERIFY(editor.loadContent(buffer.getContentRaw()));
  auto *view = showEditor(editor);
  QVERIFY(view);
  QVERIFY(editor.selectNode(QStringLiteral("a")));
  const auto topic = node(editor, QStringLiteral("a")).value(QStringLiteral("topic"));
  const auto openContextAction = [&](const QString &p_label) {
    bool triggered = false;
    QTimer choose;
    connect(&choose, &QTimer::timeout, this, [&] {
      auto *menu = qobject_cast<QMenu *>(QApplication::activePopupWidget());
      if (!menu) {
        return;
      }
      choose.stop();
      for (auto *action : menu->actions()) {
        if (action->text() == p_label && action->isEnabled()) {
          menu->hide();
          triggered = true;
          action->trigger();
          return;
        }
      }
      menu->close();
    });
    const auto point = topicPoint(view, QStringLiteral("Child"));
    choose.start(0);
    QContextMenuEvent event(QContextMenuEvent::Mouse, point, view->viewport()->mapToGlobal(point));
    QCoreApplication::sendEvent(view->viewport(), &event);
    return triggered;
  };
  QApplication::clipboard()->setText(QStringLiteral("https://example.test/context"));
  InsertionDialogs link({InsertionDialogs::link(QString(), false)});
  QVERIFY(openContextAction(QStringLiteral("Add URL")));
  QVERIFY(link.complete());
  QCOMPARE(node(editor, QStringLiteral("a")).value(QStringLiteral("hyperLink")).toString(),
           QStringLiteral("https://example.test/context"));
  QCOMPARE(node(editor, QStringLiteral("a")).value(QStringLiteral("topic")), topic);
  const QColor color(218, 37, 62);
  QApplication::clipboard()->setImage(QImage::fromData(imageBytes(color)));
  InsertionDialogs image({InsertionDialogs::image(true)});
  QVERIFY(openContextAction(QStringLiteral("Add Image")));
  QVERIFY(image.complete());
  QTRY_VERIFY(rendersColor(editor, color));
  QCOMPARE(node(editor, QStringLiteral("a")).value(QStringLiteral("topic")), topic);
  QCOMPARE(node(editor, QStringLiteral("a")).value(QStringLiteral("hyperLink")).toString(),
           QStringLiteral("https://example.test/context"));
}

} // namespace tests

QTEST_MAIN(tests::TestMindMapEditor)
#include "test_mindmapeditor.moc"
