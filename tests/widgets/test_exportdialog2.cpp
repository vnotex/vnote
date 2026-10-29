#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDirIterator>
#include <QFile>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QPdfDocument>
#include <QPdfSelection>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QScopeGuard>
#include <QSemaphore>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>
#include <QToolButton>
#include <QtTest>
#include <atomic>
#include <memory>

#include <core/configmgr2.h>
#include <core/servicelocator.h>
#include <core/services/buffer2.h>
#include <core/services/bufferservice.h>
#include <core/services/configcoreservice.h>
#include <core/services/filetypecoreservice.h>
#include <core/services/hookmanager.h>
#include <core/services/htmltemplateservice.h>
#include <core/services/notebookcoreservice.h>
#include <core/services/notebookiogate.h>
#include <core/services/syncworkqueuemanager.h>
#include <core/services/workspacecoreservice.h>
#include <core/sessionconfig.h>
#include <gui/services/themeservice.h>
#include <gui/services/vxpdfschemehandler.h>
#include <gui/services/webengineprofileservice.h>
#include <vtextedit/typstruntime.h>
#include <vxcore/vxcore.h>
#include <widgets/dialogs/exportdialog2.h>
#include <widgets/locationinputwithbrowsebutton.h>

using namespace vnotex;

namespace {
bool writeBytes(const QString &path, const QByteArray &bytes) {
  QFile file(path);
  return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}

QByteArray readBytes(const QString &path) {
  QFile file(path);
  return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

QStringList outputFiles(const QString &path) {
  QStringList result;
  QDirIterator it(path, QDir::Files, QDirIterator::Subdirectories);
  while (it.hasNext())
    result.append(it.next());
  return result;
}

QVector<int> formats(std::initializer_list<ExportFormat> values) {
  QVector<int> result;
  for (const auto value : values)
    result.append(static_cast<int>(value));
  return result;
}

// Occupy the real compiler lanes, not a renderer substitute. The bounded wait and
// RAII release keep even a failed assertion from leaving the native runtime held.
class RuntimeLaneGate {
public:
  explicit RuntimeLaneGate(vte::TypstRuntime &runtime) : m_state(std::make_shared<State>()) {
    for (const auto &name :
         {QStringLiteral("typst-compile-0"), QStringLiteral("typst-compile-1")}) {
      auto *thread = runtime.findChild<QThread *>(name);
      auto *worker = thread ? thread->property("worker").value<QObject *>() : nullptr;
      if (worker && QMetaObject::invokeMethod(
                        worker,
                        [state = m_state] {
                          state->entered.release();
                          if (!state->resume.tryAcquire(1, 30000))
                            state->timedOut = true;
                        },
                        Qt::QueuedConnection)) {
        ++m_scheduled;
      }
    }
  }
  ~RuntimeLaneGate() { release(); }
  bool entered() { return m_scheduled == 2 && m_state->entered.tryAcquire(2, 10000); }
  bool timedOut() const { return m_state->timedOut; }
  void release() {
    if (m_scheduled) {
      m_state->resume.release(m_scheduled);
      m_scheduled = 0;
    }
  }

private:
  struct State {
    QSemaphore entered;
    QSemaphore resume;
    std::atomic_bool timedOut = false;
  };
  std::shared_ptr<State> m_state;
  int m_scheduled = 0;
};

const QStringList kHtmlControls{
    QStringLiteral("embedStylesCheck"), QStringLiteral("embedImagesCheck"),
    QStringLiteral("completePageCheck"), QStringLiteral("addOutlinePanelCheck"),
    QStringLiteral("scrollableCheck")};
const QStringList kPdfFields{
    QStringLiteral("wkhtmltopdfExePathEdit"), QStringLiteral("wkhtmltopdfArgsEdit"),
    QStringLiteral("pdfHeaderLeftEdit"),      QStringLiteral("pdfHeaderCenterEdit"),
    QStringLiteral("pdfHeaderRightEdit"),     QStringLiteral("pdfFooterLeftEdit"),
    QStringLiteral("pdfFooterCenterEdit"),    QStringLiteral("pdfFooterRightEdit")};
const QStringList kEditingControls{QStringLiteral("sourceCombo"),
                                   QStringLiteral("formatCombo"),
                                   QStringLiteral("recursiveCheck"),
                                   QStringLiteral("exportAttachmentsCheck"),
                                   QStringLiteral("outputDirInput"),
                                   QStringLiteral("renderingStyleCombo"),
                                   QStringLiteral("syntaxStyleCombo"),
                                   QStringLiteral("followThemeButton"),
                                   QStringLiteral("transparentBgCheck"),
                                   QStringLiteral("pageLayoutButton"),
                                   QStringLiteral("addTableOfContentsCheck"),
                                   QStringLiteral("useWkhtmltopdfCheck"),
                                   QStringLiteral("pdfAllInOneCheck"),
                                   QStringLiteral("customSchemeCombo"),
                                   QStringLiteral("customSchemeMenuButton"),
                                   QStringLiteral("targetSuffixEdit"),
                                   QStringLiteral("resourceSeparatorEdit"),
                                   QStringLiteral("useHtmlInputCheck"),
                                   QStringLiteral("customAllInOneCheck"),
                                   QStringLiteral("targetScrollableCheck"),
                                   QStringLiteral("customCommandEdit")};
const QStringList kSchemeActions{QStringLiteral("addSchemeAction"),
                                 QStringLiteral("duplicateSchemeAction"),
                                 QStringLiteral("deleteSchemeAction")};
} // namespace

class TestExportDialog2 : public QObject {
  Q_OBJECT

private slots:
  void initTestCase();
  void cleanupTestCase();
  void init();
  void cleanup();
  void targetOrderAndExplicitPages();
  void selectedSourceWinsOverActiveBuffer();
  void folderRecursionNotebookAndWorkspace();
  void emptySelections_data();
  void emptySelections();
  void protectedSelection();
  void unsupportedRememberedTarget_data();
  void unsupportedRememberedTarget();
  void nativeAndWebOptionVisibility();
  void mixedOptionUnion();
  void nativePdfEffectiveOptionsAndPreference();
  void customStylesAndReadonlySchemes();
  void hiddenPreferencesSurviveReopen();
  void editingDisabledAndCancel_data();
  void editingDisabledAndCancel();

private:
  template <typename T = QWidget> T *control(const char *name) const {
    return m_dialog->findChild<T *>(QLatin1String(name));
  }
  bool openDialog(const ExportContext &context);
  bool selectSource(ExportSource source, const QString &workspace = QString());
  bool selectFormat(ExportFormat format);
  QVector<int> targets() const;
  void verifyPage(ExportFormat format);
  void verifyVisible(const QStringList &names, bool visible);
  void verifyStyles(bool rendering, bool syntax);
  void verifyPdfRows(bool layout, bool toc, bool wkhtmltopdf);
  QString createWorkspace(const QString &name, const QStringList &relativePaths);
  bool createNote(const QString &folder, const QString &name, const QByteArray &content);
  ExportContext context() const;
  ExportOption option() const;
  QString path(const QString &relativePath) const;
  QString logs() const { return control<QPlainTextEdit>("logEdit")->toPlainText(); }

  QTemporaryDir m_root;
  std::unique_ptr<QTemporaryDir> m_case;
  ServiceLocator m_services;
  VxCoreContextHandle m_core = nullptr;
  QStringList m_appSearchPaths;
  std::unique_ptr<ConfigCoreService> m_configCore;
  std::unique_ptr<ConfigMgr2> m_config;
  std::unique_ptr<NotebookCoreService> m_notebooks;
  std::unique_ptr<FileTypeCoreService> m_fileTypes;
  std::unique_ptr<HookManager> m_hooks;
  std::unique_ptr<NotebookIoGate> m_ioGate;
  std::unique_ptr<BufferService> m_buffers;
  std::unique_ptr<WorkspaceCoreService> m_workspaces;
  std::unique_ptr<HtmlTemplateService> m_templates;
  std::unique_ptr<WebEngineProfileService> m_profiles;
  std::unique_ptr<ThemeService> m_theme;
  std::unique_ptr<vte::TypstRuntime> m_runtime;
  std::unique_ptr<ExportDialog2> m_dialog;
  QString m_notebook, m_output;
  QStringList m_bufferIds, m_workspaceIds;
  QString m_typstWorkspace, m_markdownWorkspace, m_mindmapWorkspace, m_mixedWorkspace;
  QString m_mindmapTypstWorkspace, m_markdownMindmapWorkspace, m_emptyWorkspace;
  ExportCustomOption m_custom;
};

void TestExportDialog2::initTestCase() {
  QStandardPaths::setTestModeEnabled(true);
  vxcore_set_test_mode(1);
  vxcore_set_app_info("VNote", "VNoteTestExportDialog2");
  QVERIFY(m_root.isValid());
  const QString themes = QStringLiteral(VNOTE_SRC_DIR "/data/extra");
  QVERIFY(QDir(themes + QStringLiteral("/themes/pure")).exists());
  const QString rcc = QStringLiteral(VNOTE_TEST_EXTRA_RCC);
  QVERIFY(QFileInfo::exists(rcc));
  m_appSearchPaths = QDir::searchPaths(QStringLiteral("app"));
  QDir::setSearchPaths(QStringLiteral("app"), {QFileInfo(rcc).absolutePath()});
  QCOMPARE(vxcore_context_create(nullptr, &m_core), VXCORE_OK);
  m_configCore = std::make_unique<ConfigCoreService>(m_core);
  m_config = std::make_unique<ConfigMgr2>(m_configCore.get());
  m_config->init();
  QDir::setSearchPaths(QStringLiteral("app"), {QFileInfo(rcc).absolutePath()});
  m_config->initAfterQtAppStarted();
  QVERIFY(m_config->extraDataCopyFailures().isEmpty());
  m_notebooks = std::make_unique<NotebookCoreService>(m_core);
  m_fileTypes = std::make_unique<FileTypeCoreService>(m_core, QStringLiteral("en_US"));
  m_hooks = std::make_unique<HookManager>();
  m_ioGate = std::make_unique<NotebookIoGate>();
  m_buffers = std::make_unique<BufferService>(m_core, m_hooks.get(), m_ioGate.get());
  m_workspaces = std::make_unique<WorkspaceCoreService>(m_core);
  m_templates = std::make_unique<HtmlTemplateService>(m_config.get());
  m_profiles = std::make_unique<WebEngineProfileService>(m_root.filePath(QStringLiteral("profile")),
                                                         m_config.get(), m_templates.get());
  ThemeServiceConfig theme;
  theme.themeName = QStringLiteral("pure");
  theme.locale = QStringLiteral("en_US");
  theme.appDataPath = themes;
  m_theme = std::make_unique<ThemeService>(theme);
  QVERIFY(!m_theme->fetchTextEditorStyle().isEmpty());
  m_runtime = std::make_unique<vte::TypstRuntime>();
  QVERIFY(m_runtime->isAvailable());
  m_services.registerService(m_configCore.get());
  m_services.registerService(m_config.get());
  m_services.registerService(m_notebooks.get());
  m_services.registerService(m_fileTypes.get());
  m_services.registerService(m_buffers.get());
  m_services.registerService(m_workspaces.get());
  m_services.registerService(m_templates.get());
  m_services.registerService(m_profiles.get());
  m_services.registerService(m_theme.get());
  m_services.registerService(m_runtime.get());
}

void TestExportDialog2::cleanupTestCase() {
  m_dialog.reset();
  m_profiles.reset();
  m_templates.reset();
  m_runtime.reset();
  m_theme.reset();
  m_workspaces.reset();
  m_buffers.reset();
  m_ioGate.reset();
  m_hooks.reset();
  m_fileTypes.reset();
  m_notebooks.reset();
  m_config.reset();
  m_configCore.reset();
  if (m_core) {
    vxcore_context_destroy(m_core);
    m_core = nullptr;
  }
  QDir::setSearchPaths(QStringLiteral("app"), m_appSearchPaths);
}

void TestExportDialog2::init() {
  m_case = std::make_unique<QTemporaryDir>(m_root.filePath(QStringLiteral("case-XXXXXX")));
  QVERIFY(m_case->isValid());
  m_notebook = m_notebooks->createNotebook(m_case->filePath(QStringLiteral("notebook")),
                                           QStringLiteral("{\"name\":\"Dialog selection\"}"),
                                           NotebookType::Bundled);
  QVERIFY(!m_notebook.isEmpty());
  m_output = m_case->filePath(QStringLiteral("output"));
  QVERIFY(createNote({}, QStringLiteral("main.typst"), "= Saved main heading\n"));
  QVERIFY(!m_notebooks->createFolder(m_notebook, {}, QStringLiteral("selected")).isEmpty());
  QVERIFY(createNote(QStringLiteral("selected"), QStringLiteral("a.md"), "# Markdown note\n"));
  QVERIFY(createNote(QStringLiteral("selected"), QStringLiteral("b.markdown"), "# Second note\n"));
  QVERIFY(
      !m_notebooks->createFolder(m_notebook, QStringLiteral("selected"), QStringLiteral("nested"))
           .isEmpty());
  QVERIFY(createNote(QStringLiteral("selected/nested"), QStringLiteral("map.mmm"),
                     "{\"schemaVersion\":1,\"rootId\":\"root\",\"nodes\":[{\"id\":\"root\","
                     "\"topic\":\"MindMap note\"}],\"crossLinks\":[]}"));
  QVERIFY(createNote(QStringLiteral("selected/nested"), QStringLiteral("second.typst"),
                     "= Second native heading\n"));
  QVERIFY(!m_notebooks->createFolder(m_notebook, {}, QStringLiteral("empty")).isEmpty());
  m_typstWorkspace =
      createWorkspace(QStringLiteral("Native"), {QStringLiteral("main.typst"),
                                                 QStringLiteral("selected/nested/second.typst")});
  m_markdownWorkspace =
      createWorkspace(QStringLiteral("Markdown"),
                      {QStringLiteral("selected/a.md"), QStringLiteral("selected/b.markdown")});
  m_mindmapWorkspace =
      createWorkspace(QStringLiteral("MindMap"), {QStringLiteral("selected/nested/map.mmm")});
  m_mixedWorkspace =
      createWorkspace(QStringLiteral("Markdown and Typst"),
                      {QStringLiteral("selected/a.md"), QStringLiteral("main.typst")});
  m_mindmapTypstWorkspace =
      createWorkspace(QStringLiteral("MindMap and Typst"),
                      {QStringLiteral("selected/nested/map.mmm"), QStringLiteral("main.typst")});
  m_markdownMindmapWorkspace =
      createWorkspace(QStringLiteral("MindMap and Markdown"),
                      {QStringLiteral("selected/nested/map.mmm"), QStringLiteral("selected/a.md")});
  m_emptyWorkspace = createWorkspace(QStringLiteral("Empty"), {});
  QVERIFY(!m_typstWorkspace.isEmpty());
  QVERIFY(!m_markdownWorkspace.isEmpty());
  QVERIFY(!m_mindmapWorkspace.isEmpty());
  QVERIFY(!m_mixedWorkspace.isEmpty());
  QVERIFY(!m_mindmapTypstWorkspace.isEmpty());
  QVERIFY(!m_markdownMindmapWorkspace.isEmpty());
  QVERIFY(!m_emptyWorkspace.isEmpty());
  m_custom = ExportCustomOption();
  m_custom.m_name = QStringLiteral("Dialog copying scheme");
  m_custom.m_targetSuffix = QStringLiteral("copied");
  QString executable = QCoreApplication::applicationDirPath() + QStringLiteral("/test_typstexport");
#ifdef Q_OS_WIN
  executable += QStringLiteral(".exe");
#endif
  m_custom.m_command =
      QLatin1Char('"') + executable + QStringLiteral("\" --copy-export-input %1 %5");
  m_custom.m_useHtmlInput = false;
  m_custom.m_allInOne = true;
  m_custom.m_targetPageScrollable = true;
  m_config->getSessionConfig().setCustomExportOptions({m_custom});
  auto settings = option();
  settings.m_customExport = m_custom.m_name;
  m_config->getSessionConfig().setExportOption(settings);
}

void TestExportDialog2::cleanup() {
  m_dialog.reset();
  for (const auto &workspace : m_workspaceIds)
    m_workspaces->deleteWorkspace(workspace);
  m_workspaceIds.clear();
  for (const auto &buffer : m_bufferIds)
    m_buffers->closeBuffer(buffer);
  m_bufferIds.clear();
  m_notebooks->lockAllEncryption();
  if (!m_notebook.isEmpty())
    m_notebooks->closeNotebook(m_notebook);
  m_notebook.clear();
  m_case.reset();
}

bool TestExportDialog2::createNote(const QString &folder, const QString &name,
                                   const QByteArray &content) {
  if (m_notebooks->createFile(m_notebook, folder, name).isEmpty())
    return false;
  return writeBytes(path(folder.isEmpty() ? name : folder + QLatin1Char('/') + name), content);
}

QString TestExportDialog2::createWorkspace(const QString &name, const QStringList &relativePaths) {
  const auto workspace = m_workspaces->createWorkspace(name);
  if (workspace.isEmpty())
    return {};
  m_workspaceIds.append(workspace);
  for (const auto &relativePath : relativePaths) {
    const auto buffer = m_buffers->openBuffer({m_notebook, relativePath});
    if (!buffer.isValid())
      return {};
    if (!m_bufferIds.contains(buffer.id()))
      m_bufferIds.append(buffer.id());
    if (!m_workspaces->addBuffer(workspace, buffer.id()))
      return {};
  }
  return workspace;
}

QString TestExportDialog2::path(const QString &relativePath) const {
  return m_notebooks->buildAbsolutePath(m_notebook, relativePath);
}

ExportContext TestExportDialog2::context() const {
  ExportContext result;
  result.currentNodeId = {m_notebook, QStringLiteral("main.typst")};
  result.currentFolderId = {m_notebook, QStringLiteral("selected")};
  result.notebookId = m_notebook;
  result.bufferPath = path(QStringLiteral("main.typst"));
  result.bufferName = QStringLiteral("main.typst");
  result.bufferContent = QStringLiteral("= Live dialog heading\n");
  return result;
}

ExportOption TestExportDialog2::option() const {
  ExportOption result;
  result.m_outputDir = m_output;
  result.m_recursive = false;
  result.m_exportAttachments = false;
  return result;
}

bool TestExportDialog2::openDialog(const ExportContext &context) {
  m_dialog = std::make_unique<ExportDialog2>(m_services, context);
  const auto names = kEditingControls + kHtmlControls + kPdfFields +
                     QStringList{QStringLiteral("optionsGroupBox"),
                                 QStringLiteral("formatGroupBox"),
                                 QStringLiteral("sourceOptionsPage"),
                                 QStringLiteral("htmlOptionsPage"),
                                 QStringLiteral("pdfOptionsPage"),
                                 QStringLiteral("customOptionsPage"),
                                 QStringLiteral("nativeFormatInfo"),
                                 QStringLiteral("mixedFormatInfo"),
                                 QStringLiteral("renderingStyleLabel"),
                                 QStringLiteral("syntaxStyleLabel"),
                                 QStringLiteral("useMimeHtmlFormatCheck"),
                                 QStringLiteral("exportButton"),
                                 QStringLiteral("cancelButton"),
                                 QStringLiteral("logEdit"),
                                 QStringLiteral("progressBar")};
  for (const auto &name : names) {
    if (!m_dialog->findChild<QWidget *>(name)) {
      qWarning() << "Missing named export widget:" << name;
      return false;
    }
  }
  for (const auto &name : kSchemeActions) {
    if (!m_dialog->findChild<QAction *>(name)) {
      qWarning() << "Missing named export action:" << name;
      return false;
    }
  }
  m_dialog->show();
  return true;
}

bool TestExportDialog2::selectSource(ExportSource source, const QString &workspace) {
  auto *combo = control<QComboBox>("sourceCombo");
  const int index = source == ExportSource::Workspace ? combo->findData(workspace, Qt::UserRole + 1)
                                                      : combo->findData(static_cast<int>(source));
  if (index < 0)
    return false;
  combo->setCurrentIndex(index);
  return combo->currentData().toInt() == static_cast<int>(source);
}

bool TestExportDialog2::selectFormat(ExportFormat format) {
  auto *combo = control<QComboBox>("formatCombo");
  const int index = combo->findData(static_cast<int>(format));
  if (index < 0)
    return false;
  combo->setCurrentIndex(index);
  return combo->currentData().toInt() == static_cast<int>(format);
}

QVector<int> TestExportDialog2::targets() const {
  QVector<int> result;
  const auto *combo = control<QComboBox>("formatCombo");
  for (int i = 0; i < combo->count(); ++i)
    result.append(combo->itemData(i).toInt());
  return result;
}

void TestExportDialog2::verifyPage(ExportFormat format) {
  QCOMPARE(control<QComboBox>("formatCombo")->currentData().toInt(), static_cast<int>(format));
  QCOMPARE(control<QGroupBox>("formatGroupBox")->title(),
           QStringLiteral("%1 options").arg(exportFormatString(format)));
  const char *expected = "sourceOptionsPage";
  if (format == ExportFormat::HTML)
    expected = "htmlOptionsPage";
  if (format == ExportFormat::PDF)
    expected = "pdfOptionsPage";
  if (format == ExportFormat::Custom)
    expected = "customOptionsPage";
  for (const auto *name :
       {"sourceOptionsPage", "htmlOptionsPage", "pdfOptionsPage", "customOptionsPage"}) {
    QCOMPARE(control(name)->isVisibleTo(m_dialog.get()), QByteArray(name) == expected);
  }
  QVERIFY(control("exportAttachmentsCheck")->isVisibleTo(m_dialog.get()));
  QVERIFY(!control("useMimeHtmlFormatCheck")->isVisibleTo(m_dialog.get()));
}

void TestExportDialog2::verifyVisible(const QStringList &names, bool visible) {
  for (const auto &name : names) {
    auto *widget = m_dialog->findChild<QWidget *>(name);
    QVERIFY2(widget, qPrintable(name));
    QVERIFY2(widget->isVisibleTo(m_dialog.get()) == visible, qPrintable(name));
  }
}

void TestExportDialog2::verifyStyles(bool rendering, bool syntax) {
  verifyVisible({QStringLiteral("renderingStyleCombo"), QStringLiteral("renderingStyleLabel")},
                rendering);
  verifyVisible({QStringLiteral("syntaxStyleCombo"), QStringLiteral("syntaxStyleLabel")}, syntax);
  QCOMPARE(control("followThemeButton")->isVisibleTo(m_dialog.get()), rendering || syntax);
}

void TestExportDialog2::verifyPdfRows(bool layout, bool toc, bool wkhtmltopdf) {
  auto *form = qobject_cast<QFormLayout *>(control("pdfOptionsPage")->layout());
  QVERIFY(form);
  auto *pageLayout = control("pageLayoutButton");
  auto *layoutLabel = form->labelForField(pageLayout);
  QVERIFY(layoutLabel);
  QCOMPARE(pageLayout->isVisibleTo(m_dialog.get()), layout);
  QCOMPARE(layoutLabel->isVisibleTo(m_dialog.get()), layout);
  QCOMPARE(control("addTableOfContentsCheck")->isVisibleTo(m_dialog.get()), toc);
  QCOMPARE(control("useWkhtmltopdfCheck")->isVisibleTo(m_dialog.get()), wkhtmltopdf);
  for (const auto &name : kPdfFields) {
    auto *field = m_dialog->findChild<QWidget *>(name);
    auto *label = form->labelForField(field);
    QVERIFY2(label, qPrintable(name));
    QVERIFY2(field->isVisibleTo(m_dialog.get()) == wkhtmltopdf, qPrintable(name));
    QVERIFY2(label->isVisibleTo(m_dialog.get()) == wkhtmltopdf, qPrintable(name));
  }
}

void TestExportDialog2::targetOrderAndExplicitPages() {
  QVERIFY(openDialog(context()));
  QCOMPARE(targets(), formats({ExportFormat::Typst, ExportFormat::HTML, ExportFormat::PDF,
                               ExportFormat::Custom}));
  for (const auto format :
       {ExportFormat::Typst, ExportFormat::HTML, ExportFormat::PDF, ExportFormat::Custom}) {
    QVERIFY(selectFormat(format));
    verifyPage(format);
  }
  QVERIFY(selectFormat(ExportFormat::Typst));
  QVERIFY(selectSource(ExportSource::CurrentFolder));
  QCOMPARE(targets(), formats({ExportFormat::Markdown, ExportFormat::HTML, ExportFormat::PDF,
                               ExportFormat::Custom}));
  verifyPage(ExportFormat::HTML); // Unsupported native preference falls back to HTML.
  QVERIFY(selectFormat(ExportFormat::Markdown));
  verifyPage(ExportFormat::Markdown);
  QVERIFY(selectSource(ExportSource::Workspace, m_mindmapWorkspace));
  QCOMPARE(targets(), formats({ExportFormat::MindMap, ExportFormat::Markdown, ExportFormat::HTML,
                               ExportFormat::PDF, ExportFormat::Custom}));
  verifyPage(ExportFormat::Markdown); // Preserve the supported projection, even at index 1.
  for (const auto format : {ExportFormat::MindMap, ExportFormat::Markdown, ExportFormat::HTML,
                            ExportFormat::PDF, ExportFormat::Custom}) {
    QVERIFY(selectFormat(format));
    verifyPage(format); // HTML/PDF/Custom no longer share their combo and stack indexes.
  }
  QVERIFY(selectSource(ExportSource::CurrentBuffer));
  verifyPage(ExportFormat::Custom);
  QVERIFY(control("exportButton")->isEnabled());
}

void TestExportDialog2::selectedSourceWinsOverActiveBuffer() {
  auto selection = context();
  selection.bufferPath = path(QStringLiteral("selected/a.md"));
  selection.bufferName = QStringLiteral("misleading.md");
  selection.bufferContent.clear();
  const auto buffer = m_buffers->openBuffer(selection.currentNodeId);
  QVERIFY(buffer.isValid());
  int snapshotRequests = 0;
  m_buffers->registerActiveWriter(buffer.id(), 1, [&] {
    ++snapshotRequests;
    return QStringLiteral("unsaved native editor text");
  });
  m_buffers->markDirty(buffer.id());
  const auto clearWriter = qScopeGuard([&] { m_buffers->unregisterActiveWriter(buffer.id(), 1); });
  const auto saved = readBytes(path(QStringLiteral("main.typst")));
  QVERIFY(openDialog(selection));
  QCOMPARE(targets().first(), static_cast<int>(ExportFormat::Typst));
  QVERIFY(selectSource(ExportSource::CurrentNote));
  QCOMPARE(targets().first(), static_cast<int>(ExportFormat::Typst));
  QVERIFY(selectSource(ExportSource::CurrentFolder));
  QCOMPARE(targets().first(), static_cast<int>(ExportFormat::Markdown));
  QVERIFY(selectSource(ExportSource::Workspace, m_mindmapWorkspace));
  QCOMPARE(targets().first(), static_cast<int>(ExportFormat::MindMap));
  QCOMPARE(snapshotRequests, 0);
  QVERIFY(m_buffers->isDirty(buffer.id()));
  QCOMPARE(readBytes(path(QStringLiteral("main.typst"))), saved);
  QVERIFY(!QFileInfo::exists(m_output));
  QVERIFY(logs().isEmpty());
  m_dialog.reset();
  selection.currentNodeId = {};
  QVERIFY(openDialog(selection));
  QCOMPARE(targets().first(), static_cast<int>(ExportFormat::Markdown));
}

void TestExportDialog2::folderRecursionNotebookAndWorkspace() {
  QVERIFY(openDialog(context()));
  QVERIFY(!control("recursiveCheck")->isVisibleTo(m_dialog.get()));
  QVERIFY(selectSource(ExportSource::CurrentFolder));
  QVERIFY(control("recursiveCheck")->isVisibleTo(m_dialog.get()));
  QCOMPARE(targets().first(), static_cast<int>(ExportFormat::Markdown));
  QVERIFY(selectFormat(ExportFormat::Markdown));
  control<QCheckBox>("recursiveCheck")->setChecked(true);
  QCOMPARE(targets(), formats({ExportFormat::HTML, ExportFormat::PDF, ExportFormat::Custom}));
  verifyPage(ExportFormat::HTML);
  control<QCheckBox>("recursiveCheck")->setChecked(false);
  QCOMPARE(targets().first(), static_cast<int>(ExportFormat::Markdown));
  verifyPage(ExportFormat::HTML); // Do not resurrect an unsupported earlier selection.
  QVERIFY(selectSource(ExportSource::CurrentNotebook));
  QCOMPARE(targets().first(), static_cast<int>(ExportFormat::Typst));
  QVERIFY(control("recursiveCheck")->isVisibleTo(m_dialog.get()));
  control<QCheckBox>("recursiveCheck")->setChecked(true);
  QCOMPARE(targets(), formats({ExportFormat::HTML, ExportFormat::PDF, ExportFormat::Custom}));
  QVERIFY(selectSource(ExportSource::Workspace, m_mindmapWorkspace));
  QCOMPARE(targets().first(), static_cast<int>(ExportFormat::MindMap));
  QVERIFY(!control("recursiveCheck")->isVisibleTo(m_dialog.get()));
  QVERIFY(selectSource(ExportSource::Workspace, m_mixedWorkspace));
  QCOMPARE(targets(), formats({ExportFormat::HTML, ExportFormat::PDF, ExportFormat::Custom}));
  QVERIFY(!QFileInfo::exists(m_output));
}

void TestExportDialog2::emptySelections_data() {
  QTest::addColumn<int>("selection");
  QTest::newRow("no-context") << 0;
  QTest::newRow("empty-folder") << 1;
  QTest::newRow("empty-workspace") << 2;
  QTest::newRow("missing-file") << 3;
  QTest::newRow("protected-external-path") << 4;
  QTest::newRow("empty-notebook") << 5;
}

void TestExportDialog2::emptySelections() {
  QFETCH(int, selection);
  auto selected = context();
  QString emptyNotebook;
  const auto closeEmpty = qScopeGuard([&] {
    m_dialog.reset();
    if (!emptyNotebook.isEmpty())
      m_notebooks->closeNotebook(emptyNotebook);
  });
  if (selection == 0)
    selected = {};
  if (selection == 1) {
    selected.currentFolderId.relativePath = QStringLiteral("empty");
    selected.presetSource = ExportSource::CurrentFolder;
  }
  if (selection == 3 || selection == 4) {
    selected = {};
    selected.bufferPath = m_case->filePath(selection == 3 ? QStringLiteral("missing.typst")
                                                          : QStringLiteral("protected.VNE"));
    if (selection == 4)
      QVERIFY(writeBytes(selected.bufferPath, {}));
  }
  if (selection == 5) {
    emptyNotebook =
        m_notebooks->createNotebook(m_case->filePath(QStringLiteral("empty-notebook")),
                                    QStringLiteral("{\"name\":\"Empty\"}"), NotebookType::Bundled);
    QVERIFY(!emptyNotebook.isEmpty());
    selected.notebookId = emptyNotebook;
    selected.presetSource = ExportSource::CurrentNotebook;
  }
  QVERIFY(openDialog(selected));
  if (selection == 2)
    QVERIFY(selectSource(ExportSource::Workspace, m_emptyWorkspace));
  QVERIFY(targets().isEmpty());
  QCOMPARE(control<QComboBox>("formatCombo")->currentIndex(), -1);
  QVERIFY(!control("exportButton")->isEnabled());
  QVERIFY(control("cancelButton")->isEnabled());
  QVERIFY(logs().isEmpty());
  control<QPushButton>("exportButton")->click();
  QVERIFY(!QFileInfo::exists(m_output));
}

void TestExportDialog2::protectedSelection() {
  auto setup = m_notebooks->prepareNotebookEncryption(m_notebook, {},
                                                      QByteArrayLiteral("dialog-test-password"));
  QVERIFY(setup.isValid());
  SyncWorkQueueManager queues;
  QString noteId;
  {
    auto maintenance = queues.tryAcquireMaintenance({m_notebook});
    QVERIFY(maintenance.isValid());
    NotebookIoGate::ScopedLock lock(*m_ioGate, m_notebook);
    QCOMPARE(m_notebooks->commitNotebookEncryption(setup), VXCORE_OK);
    QCOMPARE(m_notebooks->createEncryptedNote(m_notebook, {}, QStringLiteral("private.md"),
                                              QStringLiteral("markdown"),
                                              QByteArrayLiteral("# Decrypted secret"), &noteId),
             VXCORE_OK);
  }
  const auto note = m_buffers->openBufferByNodeId(noteId);
  QVERIFY(note.isValid());
  m_bufferIds.append(note.id());
  QVERIFY(m_workspaces->addBuffer(m_typstWorkspace, note.id()));
  auto selected = context();
  selected.currentNodeId = note.nodeId();
  selected.currentFolderId = {m_notebook, QString()};
  selected.bufferPath = note.resolvedPath();
  selected.bufferContent = QStringLiteral("# Decrypted secret");
  QVERIFY(openDialog(selected));
  for (const auto source :
       {ExportSource::CurrentBuffer, ExportSource::CurrentNote, ExportSource::CurrentFolder,
        ExportSource::CurrentNotebook, ExportSource::Workspace}) {
    QVERIFY(selectSource(source, m_typstWorkspace));
    QVERIFY(targets().isEmpty());
    QVERIFY(!control("exportButton")->isEnabled());
  }
  QVERIFY(logs().isEmpty());
  QVERIFY(!QFileInfo::exists(m_output));
}

void TestExportDialog2::unsupportedRememberedTarget_data() {
  QTest::addColumn<int>("remembered");
  QTest::addColumn<int>("source");
  QTest::newRow("markdown-on-typst")
      << int(ExportFormat::Markdown) << int(ExportSource::CurrentBuffer);
  QTest::newRow("mindmap-on-typst")
      << int(ExportFormat::MindMap) << int(ExportSource::CurrentBuffer);
  QTest::newRow("typst-on-markdown")
      << int(ExportFormat::Typst) << int(ExportSource::CurrentFolder);
}

void TestExportDialog2::unsupportedRememberedTarget() {
  QFETCH(int, remembered);
  QFETCH(int, source);
  auto settings = option();
  settings.m_targetFormat = static_cast<ExportFormat>(remembered);
  settings.m_source = ExportSource::CurrentNotebook;
  m_config->getSessionConfig().setExportOption(settings);
  auto selected = context();
  selected.presetSource = static_cast<ExportSource>(source);
  QVERIFY(openDialog(selected));
  QCOMPARE(control<QComboBox>("sourceCombo")->currentData().toInt(), source);
  verifyPage(ExportFormat::HTML);
  m_dialog->close();
  QCOMPARE(m_config->getSessionConfig().getExportOption().m_targetFormat, ExportFormat::HTML);
  QVERIFY(openDialog(selected));
  verifyPage(ExportFormat::HTML);
  QVERIFY(control("exportButton")->isEnabled());
}

void TestExportDialog2::nativeAndWebOptionVisibility() {
  auto settings = option();
  settings.m_pdfOption.m_useWkhtmltopdf = true;
  m_config->getSessionConfig().setExportOption(settings);
  QVERIFY(openDialog(context()));
  QVERIFY(selectFormat(ExportFormat::Typst));
  verifyStyles(false, false);
  QVERIFY(control("nativeFormatInfo")->isVisibleTo(m_dialog.get()));
  const auto sourceInfo = control<QLabel>("nativeFormatInfo")->text();
  QVERIFY(sourceInfo.contains(QStringLiteral("relative"), Qt::CaseInsensitive));
  QVERIFY(sourceInfo.contains(QStringLiteral("images"), Qt::CaseInsensitive));
  QVERIFY(sourceInfo.contains(QStringLiteral("import"), Qt::CaseInsensitive));
  QVERIFY(sourceInfo.contains(QStringLiteral("dynamic"), Qt::CaseInsensitive));
  QVERIFY(!control("transparentBgCheck")->isVisibleTo(m_dialog.get()));
  QVERIFY(selectFormat(ExportFormat::HTML));
  verifyStyles(false, false);
  verifyVisible(kHtmlControls, false);
  QCOMPARE(control<QLabel>("nativeFormatInfo")->text(),
           QStringLiteral("HTML is compiled by Typst; document styles control the output."));
  QVERIFY(control("nativeFormatInfo")->isVisibleTo(m_dialog.get()));
  QVERIFY(selectFormat(ExportFormat::PDF));
  verifyStyles(false, false);
  verifyPdfRows(false, false, false);
  QVERIFY(!control("pdfAllInOneCheck")->isVisibleTo(m_dialog.get()));
  QCOMPARE(control<QLabel>("nativeFormatInfo")->text(),
           QStringLiteral(
               "PDF is compiled by Typst; page layout and numbering come from the document."));
  QVERIFY(selectSource(ExportSource::Workspace, m_mindmapWorkspace));
  verifyPdfRows(true, false, true);
  verifyStyles(false, false);
  QVERIFY(!control("transparentBgCheck")->isVisibleTo(m_dialog.get()));
  QVERIFY(selectFormat(ExportFormat::HTML));
  verifyVisible(kHtmlControls, false);
  verifyStyles(false, false);
  QVERIFY(selectFormat(ExportFormat::MindMap));
  verifyStyles(false, false);
  QVERIFY(control("nativeFormatInfo")->isVisibleTo(m_dialog.get()));
  QVERIFY(control<QLabel>("nativeFormatInfo")->text().contains(QStringLiteral("images")));
  QVERIFY(selectSource(ExportSource::CurrentFolder));
  QVERIFY(selectFormat(ExportFormat::HTML));
  verifyStyles(true, true);
  verifyVisible(kHtmlControls, true);
  QVERIFY(control("transparentBgCheck")->isVisibleTo(m_dialog.get()));
  QVERIFY(!control("nativeFormatInfo")->isVisibleTo(m_dialog.get()));
  QVERIFY(selectFormat(ExportFormat::PDF));
  verifyStyles(true, true);
  verifyPdfRows(true, true, true);
  control<QCheckBox>("useWkhtmltopdfCheck")->setChecked(false);
  auto *form = qobject_cast<QFormLayout *>(control("pdfOptionsPage")->layout());
  QVERIFY(form);
  for (const auto &name : kPdfFields) {
    auto *field = m_dialog->findChild<QWidget *>(name);
    QVERIFY(!field->isEnabled());
    QVERIFY(!form->labelForField(field)->isEnabled());
  }
  QVERIFY(!control("pdfAllInOneCheck")->isVisibleTo(m_dialog.get()));
}

void TestExportDialog2::mixedOptionUnion() {
  QVERIFY(openDialog(context()));
  QVERIFY(selectSource(ExportSource::Workspace, m_markdownMindmapWorkspace));
  QCOMPARE(targets(), formats({ExportFormat::Markdown, ExportFormat::HTML, ExportFormat::PDF,
                               ExportFormat::Custom}));
  QVERIFY(selectFormat(ExportFormat::Markdown));
  verifyPage(ExportFormat::Markdown);
  verifyStyles(false, false);
  QVERIFY(selectFormat(ExportFormat::HTML));
  verifyStyles(true, true);
  verifyVisible(kHtmlControls, true);
  QVERIFY(control("mixedFormatInfo")->isVisibleTo(m_dialog.get()));
  QVERIFY(selectSource(ExportSource::Workspace, m_mixedWorkspace));
  QCOMPARE(targets(), formats({ExportFormat::HTML, ExportFormat::PDF, ExportFormat::Custom}));
  QVERIFY(selectFormat(ExportFormat::HTML));
  verifyStyles(true, true);
  verifyVisible(kHtmlControls, true);
  QVERIFY(control("mixedFormatInfo")->isVisibleTo(m_dialog.get()));
  QCOMPARE(control<QLabel>("mixedFormatInfo")->text(),
           QStringLiteral("Some options apply only to compatible source types."));
  QVERIFY(selectFormat(ExportFormat::PDF));
  verifyPdfRows(true, true, true);
  verifyStyles(true, true);
  control<QCheckBox>("useWkhtmltopdfCheck")->setChecked(true);
  QVERIFY(!control("pdfAllInOneCheck")->isVisibleTo(m_dialog.get()));
  QVERIFY(selectSource(ExportSource::Workspace, m_mindmapTypstWorkspace));
  verifyPdfRows(true, false, true);
  verifyStyles(false, false);
  QVERIFY(control("mixedFormatInfo")->isVisibleTo(m_dialog.get()));
  QVERIFY(!control("pdfAllInOneCheck")->isVisibleTo(m_dialog.get()));
  QVERIFY(selectSource(ExportSource::Workspace, m_typstWorkspace));
  verifyPdfRows(false, false, false);
  QVERIFY(!control("mixedFormatInfo")->isVisibleTo(m_dialog.get()));
  QVERIFY(selectSource(ExportSource::Workspace, m_markdownWorkspace));
  QVERIFY(control("pdfAllInOneCheck")->isVisibleTo(m_dialog.get()));
  QVERIFY(!control("mixedFormatInfo")->isVisibleTo(m_dialog.get()));
  QVERIFY(selectSource(ExportSource::CurrentBuffer));
  QVERIFY(!control("pdfAllInOneCheck")->isVisibleTo(m_dialog.get()));
}

void TestExportDialog2::nativePdfEffectiveOptionsAndPreference() {
  auto settings = option();
  settings.m_targetFormat = ExportFormat::PDF;
  settings.m_pdfOption.m_useWkhtmltopdf = true;
  settings.m_pdfOption.m_allInOne = true;
  settings.m_pdfOption.m_wkhtmltopdfExePath =
      m_case->filePath(QStringLiteral("must-not-run-wkhtmltopdf"));
  m_config->getSessionConfig().setExportOption(settings);
  QVERIFY(openDialog(context()));
  QVERIFY(selectSource(ExportSource::Workspace, m_typstWorkspace));
  verifyPage(ExportFormat::PDF);
  verifyPdfRows(false, false, false);
  QVERIFY(control<QCheckBox>("pdfAllInOneCheck")->isChecked());
  QVERIFY(!control("pdfAllInOneCheck")->isVisibleTo(m_dialog.get()));
  control<QPushButton>("exportButton")->click();
  const auto files = outputFiles(m_output);
  QVERIFY2(files.size() == 2, qPrintable(logs()));
  QString text;
  for (const auto &file : files) {
    QCOMPARE(QFileInfo(file).suffix(), QStringLiteral("pdf"));
    QPdfDocument pdf;
    QCOMPARE(pdf.load(file), QPdfDocument::Error::None);
    QVERIFY(pdf.pageCount() > 0);
    for (int page = 0; page < pdf.pageCount(); ++page)
      text += pdf.getAllText(page).text();
  }
  QVERIFY(text.contains(QStringLiteral("Saved main heading")));
  QVERIFY(text.contains(QStringLiteral("Second native heading")));
  QVERIFY(!logs().contains(QStringLiteral("All-in-one PDF is not supported")));
  QVERIFY(control<QCheckBox>("pdfAllInOneCheck")->isChecked());
  QVERIFY(control<QCheckBox>("useWkhtmltopdfCheck")->isChecked());
  QVERIFY(selectSource(ExportSource::Workspace, m_markdownWorkspace));
  QVERIFY(control("pdfAllInOneCheck")->isVisibleTo(m_dialog.get()));
  QVERIFY(control<QCheckBox>("pdfAllInOneCheck")->isChecked());
  control<QCheckBox>("useWkhtmltopdfCheck")->setChecked(false);
  QVERIFY(!control("pdfAllInOneCheck")->isVisibleTo(m_dialog.get()));
  QVERIFY(control<QCheckBox>("pdfAllInOneCheck")->isChecked());
  control<QCheckBox>("useWkhtmltopdfCheck")->setChecked(true);
  QVERIFY(selectSource(ExportSource::Workspace, m_typstWorkspace));
  m_dialog->close();
  QVERIFY(m_config->getSessionConfig().getExportOption().m_pdfOption.m_allInOne);
  QVERIFY(openDialog(context()));
  QVERIFY(control<QCheckBox>("pdfAllInOneCheck")->isChecked());
  QVERIFY(selectSource(ExportSource::Workspace, m_markdownWorkspace));
  QVERIFY(control("pdfAllInOneCheck")->isVisibleTo(m_dialog.get()));
}

void TestExportDialog2::customStylesAndReadonlySchemes() {
  QVERIFY(openDialog(context()));
  QVERIFY(selectFormat(ExportFormat::Custom));
  auto *scheme = control<QComboBox>("customSchemeCombo");
  scheme->setCurrentIndex(scheme->findData(m_custom.m_name));
  QCOMPARE(scheme->currentData().toString(), m_custom.m_name);
  auto *command = control<QPlainTextEdit>("customCommandEdit");
  auto *html = control<QCheckBox>("useHtmlInputCheck");
  QVERIFY(!command->isReadOnly());
  QVERIFY(!html->isChecked());
  QCOMPARE(html->toolTip(), QStringLiteral("Pass original files from disk to the command; enable "
                                           "HTML input to render the current buffer snapshot"));
  verifyStyles(false, false);
  QVERIFY(!control("transparentBgCheck")->isVisibleTo(m_dialog.get()));
  QVERIFY(!control("targetScrollableCheck")->isVisibleTo(m_dialog.get()));
  QVERIFY(!control("customAllInOneCheck")->isVisibleTo(m_dialog.get()));
  QVERIFY(control<QCheckBox>("customAllInOneCheck")->isChecked());
  command->setPlainText(m_custom.m_command + QStringLiteral(" %3"));
  verifyStyles(true, false);
  command->setPlainText(m_custom.m_command + QStringLiteral(" %4"));
  verifyStyles(false, true);
  command->setPlainText(m_custom.m_command + QStringLiteral(" %3 %4"));
  verifyStyles(true, true);
  command->setPlainText(m_custom.m_command);
  html->setChecked(true);
  verifyStyles(false, false);
  QVERIFY(!control("targetScrollableCheck")->isVisibleTo(m_dialog.get()));
  QVERIFY(selectSource(ExportSource::Workspace, m_mindmapWorkspace));
  QVERIFY(control("customAllInOneCheck")->isVisibleTo(m_dialog.get()));
  QVERIFY(control<QCheckBox>("customAllInOneCheck")->isChecked());
  QVERIFY(!control("targetScrollableCheck")->isVisibleTo(m_dialog.get()));
  html->setChecked(false);
  command->setPlainText(m_custom.m_command + QStringLiteral(" %3"));
  verifyStyles(true, false);
  command->setPlainText(m_custom.m_command + QStringLiteral(" %4"));
  verifyStyles(false, true);
  command->setPlainText(m_custom.m_command);
  html->setChecked(true);
  verifyStyles(false, false);
  QVERIFY(selectSource(ExportSource::Workspace, m_mixedWorkspace));
  verifyStyles(true, true);
  QVERIFY(control("transparentBgCheck")->isVisibleTo(m_dialog.get()));
  QVERIFY(control("targetScrollableCheck")->isVisibleTo(m_dialog.get()));
  QVERIFY(control<QCheckBox>("targetScrollableCheck")->isChecked());
  html->setChecked(false);
  verifyStyles(false, false);
  QVERIFY(!control("transparentBgCheck")->isVisibleTo(m_dialog.get()));
  QVERIFY(!control("targetScrollableCheck")->isVisibleTo(m_dialog.get()));
  QVERIFY(selectSource(ExportSource::CurrentBuffer));
  const auto builtins = ExportCustomOption::builtinSchemes();
  QVERIFY(!builtins.isEmpty());
  const auto builtin = builtins.first();
  const int builtinIndex = scheme->findData(builtin.m_name);
  QVERIFY(builtinIndex >= 0);
  scheme->setCurrentIndex(builtinIndex);
  QVERIFY(command->isReadOnly());
  QVERIFY(control<QLineEdit>("targetSuffixEdit")->isReadOnly());
  QVERIFY(control<QLineEdit>("resourceSeparatorEdit")->isReadOnly());
  QVERIFY(!html->isEnabled());
  QVERIFY(!control("customAllInOneCheck")->isEnabled());
  QVERIFY(!control("targetScrollableCheck")->isEnabled());
  QVERIFY(!m_dialog->findChild<QAction *>(QStringLiteral("deleteSchemeAction"))->isEnabled());
  QVERIFY(m_dialog->findChild<QAction *>(QStringLiteral("duplicateSchemeAction"))->isEnabled());
  QCOMPARE(command->toPlainText(), builtin.m_command);
  scheme->setCurrentIndex(scheme->findData(m_custom.m_name));
  QVERIFY(!command->isReadOnly());
  QVERIFY(html->isEnabled());
  QVERIFY(m_dialog->findChild<QAction *>(QStringLiteral("deleteSchemeAction"))->isEnabled());
  m_dialog->close();
  const auto saved = m_config->getSessionConfig().getCustomExportOptions();
  QCOMPARE(saved.size(), 1);
  QCOMPARE(saved.first().m_name, m_custom.m_name);
  QVERIFY(!saved.first().m_builtin);
  QVERIFY(saved.first().m_allInOne);
  QVERIFY(saved.first().m_targetPageScrollable);
}

void TestExportDialog2::hiddenPreferencesSurviveReopen() {
  QVERIFY(openDialog(context()));
  QVERIFY(selectSource(ExportSource::CurrentFolder));
  QVERIFY(selectFormat(ExportFormat::HTML));
  auto *rendering = control<QComboBox>("renderingStyleCombo");
  auto *syntax = control<QComboBox>("syntaxStyleCombo");
  QVERIFY(rendering->count() > 1);
  QVERIFY(syntax->count() > 1);
  rendering->setCurrentIndex((rendering->currentIndex() + 1) % rendering->count());
  syntax->setCurrentIndex((syntax->currentIndex() + 1) % syntax->count());
  const auto renderingPath = rendering->currentData().toString();
  const auto syntaxPath = syntax->currentData().toString();
  control<QCheckBox>("transparentBgCheck")->setChecked(true);
  control<QCheckBox>("exportAttachmentsCheck")->setChecked(true);
  control<QCheckBox>("embedStylesCheck")->setChecked(false);
  control<QCheckBox>("completePageCheck")->setChecked(false);
  control<QCheckBox>("addOutlinePanelCheck")->setChecked(false);
  control<QCheckBox>("scrollableCheck")->setChecked(false);
  QVERIFY(selectFormat(ExportFormat::PDF));
  control<QCheckBox>("addTableOfContentsCheck")->setChecked(true);
  control<QCheckBox>("useWkhtmltopdfCheck")->setChecked(true);
  control<QCheckBox>("pdfAllInOneCheck")->setChecked(true);
  control<QLineEdit>("wkhtmltopdfArgsEdit")->setText(QStringLiteral("--quiet"));
  control<QLineEdit>("pdfHeaderLeftEdit")->setText(QStringLiteral("Retained header"));
  control<QLineEdit>("pdfFooterCenterEdit")->setText(QStringLiteral("[page] of [topage]"));
  QVERIFY(selectSource(ExportSource::CurrentBuffer));
  verifyStyles(false, false);
  verifyPdfRows(false, false, false);
  QCOMPARE(rendering->currentData().toString(), renderingPath);
  QCOMPARE(syntax->currentData().toString(), syntaxPath);
  m_dialog->close();
  const auto saved = m_config->getSessionConfig().getExportOption();
  QVERIFY(saved.m_useTransparentBg);
  QVERIFY(saved.m_exportAttachments);
  QVERIFY(!saved.m_htmlOption.m_embedStyles);
  QVERIFY(!saved.m_htmlOption.m_completePage);
  QVERIFY(!saved.m_htmlOption.m_addOutlinePanel);
  QVERIFY(!saved.m_htmlOption.m_scrollable);
  QVERIFY(saved.m_pdfOption.m_addTableOfContents);
  QVERIFY(saved.m_pdfOption.m_allInOne);
  QCOMPARE(saved.m_renderingStyleFile, renderingPath);
  QCOMPARE(saved.m_syntaxHighlightStyleFile, syntaxPath);
  QVERIFY(openDialog(context()));
  verifyPdfRows(false, false, false);
  QVERIFY(selectSource(ExportSource::CurrentFolder));
  verifyPdfRows(true, true, true);
  QCOMPARE(control<QLineEdit>("wkhtmltopdfArgsEdit")->text(), QStringLiteral("--quiet"));
  QCOMPARE(control<QLineEdit>("pdfHeaderLeftEdit")->text(), QStringLiteral("Retained header"));
  QCOMPARE(control<QLineEdit>("pdfFooterCenterEdit")->text(), QStringLiteral("[page] of [topage]"));
  QVERIFY(control<QCheckBox>("pdfAllInOneCheck")->isChecked());
  QVERIFY(selectFormat(ExportFormat::HTML));
  QCOMPARE(control<QComboBox>("renderingStyleCombo")->currentData().toString(), renderingPath);
  QCOMPARE(control<QComboBox>("syntaxStyleCombo")->currentData().toString(), syntaxPath);
  QVERIFY(!control<QCheckBox>("completePageCheck")->isChecked());
  QVERIFY(!control("embedImagesCheck")->isEnabled());
  QVERIFY(control<QCheckBox>("transparentBgCheck")->isChecked());
  QVERIFY(control<QCheckBox>("exportAttachmentsCheck")->isChecked());
}

void TestExportDialog2::editingDisabledAndCancel_data() {
  QTest::addColumn<int>("schemeMode");
  QTest::newRow("native-pdf") << 0;
  QTest::newRow("native-custom-html") << 1;
  QTest::newRow("native-builtin-html-cancel") << 2;
}

void TestExportDialog2::editingDisabledAndCancel() {
  QFETCH(int, schemeMode);
  QVERIFY(openDialog(context()));
  QVERIFY(selectFormat(schemeMode ? ExportFormat::Custom : ExportFormat::PDF));
  if (schemeMode == 1) {
    control<QCheckBox>("useHtmlInputCheck")->setChecked(true);
    QVERIFY(!control<QPlainTextEdit>("customCommandEdit")->isReadOnly());
  } else if (schemeMode == 2) {
    const auto builtin = ExportCustomOption::builtinSchemes().first();
    auto *schemes = control<QComboBox>("customSchemeCombo");
    schemes->setCurrentIndex(schemes->findData(builtin.m_name));
    QVERIFY(control<QCheckBox>("useHtmlInputCheck")->isChecked());
    QVERIFY(control<QPlainTextEdit>("customCommandEdit")->isReadOnly());
  }
  RuntimeLaneGate gate(*m_runtime);
  QVERIFY(gate.entered());
  const int outerLoopLevel = QThread::currentThread()->loopLevel();
  bool observed = false;
  QTimer inspect;
  inspect.setInterval(0);
  connect(&inspect, &QTimer::timeout, m_dialog.get(), [&] {
    // Timers run inside the actual compiler's event loop, unlike posted export
    // log callbacks that can run before the renderer has started its wait.
    if (QThread::currentThread()->loopLevel() <= outerLoopLevel)
      return;
    inspect.stop();
    observed = true;
    const auto cancelAndRelease = qScopeGuard([&] {
      control<QPushButton>("cancelButton")->click();
      gate.release();
    });
    for (const auto &name : kEditingControls + kHtmlControls + kPdfFields) {
      QVERIFY2(!m_dialog->findChild<QWidget *>(name)->isEnabled(), qPrintable(name));
    }
    for (const auto &name : kSchemeActions) {
      QVERIFY2(!m_dialog->findChild<QAction *>(name)->isEnabled(), qPrintable(name));
    }
    QVERIFY(!control("exportButton")->isEnabled());
    QVERIFY(control("cancelButton")->isEnabled());
    QVERIFY(control("logEdit")->isEnabled());
    QVERIFY(control<QPlainTextEdit>("logEdit")->isReadOnly());
    QVERIFY(control("progressBar")->isVisibleTo(m_dialog.get()));
    QVERIFY(outputFiles(m_output).isEmpty());
  });
  inspect.start();
  control<QPushButton>("exportButton")->click();
  inspect.stop();
  gate.release();
  QVERIFY(observed);
  QVERIFY(!gate.timedOut());
  QVERIFY(m_dialog->isVisible()); // Cancel retires export, not the dialog.
  QVERIFY2(outputFiles(m_output).isEmpty(), qPrintable(logs()));
  QVERIFY(control("exportButton")->isEnabled());
  QVERIFY(control("sourceCombo")->isEnabled());
  QVERIFY(control("formatCombo")->isEnabled());
  QVERIFY(control("outputDirInput")->isEnabled());
  QVERIFY(!control("progressBar")->isVisibleTo(m_dialog.get()));
  if (schemeMode) {
    QCOMPARE(control<QPlainTextEdit>("customCommandEdit")->isReadOnly(), schemeMode == 2);
    QCOMPARE(control("useHtmlInputCheck")->isEnabled(), schemeMode == 1);
    QCOMPARE(m_dialog->findChild<QAction *>(QStringLiteral("deleteSchemeAction"))->isEnabled(),
             schemeMode == 1);
  }
  QVERIFY(selectFormat(ExportFormat::HTML));
  control<QPushButton>("exportButton")->click();
  const auto files = outputFiles(m_output);
  QVERIFY2(files.size() == 1, qPrintable(logs()));
  QCOMPARE(QFileInfo(files.first()).suffix(), QStringLiteral("html"));
  const auto html = readBytes(files.first());
  QVERIFY(html.contains("Live dialog heading"));
  QVERIFY(!html.contains("Saved main heading"));
  QVERIFY(html.contains("<h"));
  QVERIFY(selectFormat(ExportFormat::Custom));
  const auto builtin = ExportCustomOption::builtinSchemes().first();
  auto *schemes = control<QComboBox>("customSchemeCombo");
  schemes->setCurrentIndex(schemes->findData(builtin.m_name));
  QVERIFY(control<QPlainTextEdit>("customCommandEdit")->isReadOnly());
  QVERIFY(!control("useHtmlInputCheck")->isEnabled());
  QVERIFY(!m_dialog->findChild<QAction *>(QStringLiteral("deleteSchemeAction"))->isEnabled());
  control<QPushButton>("cancelButton")->click();
  QVERIFY(!m_dialog->isVisible());
}

int main(int argc, char **argv) {
  // Every vxcore test-mode path and Qt/WebEngine profile belongs to this process.
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
  QApplication application(argc, argv);
  TestExportDialog2 test;
  return QTest::qExec(&test, argc, argv);
}

#include "test_exportdialog2.moc"
