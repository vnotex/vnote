#include <QtTest>

#include <QBuffer>
#include <QDir>
#include <QDockWidget>
#include <QFile>
#include <QFileInfo>
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
#include <QTemporaryDir>
#include <QToolButton>
#include <QtConcurrent/QtConcurrentRun>

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
#include <widgets/editors/mindmapeditor.h>
#include <widgets/mindmapviewwindow2.h>
#include <widgets/toolspanelhost.h>

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
