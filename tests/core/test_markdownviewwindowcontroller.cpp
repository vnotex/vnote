#include <QtTest>

#include <QAction>
#include <QKeySequence>
#include <QMenu>

#include <controllers/markdownviewwindowcontroller.h>
#include <core/global.h>
#include <core/markdowneditorconfig.h>
#include <core/servicelocator.h>

using namespace vnotex;

namespace tests {

class TestMarkdownViewWindowController : public QObject {
  Q_OBJECT

private slots:
  // ============ computeModeTransition ============

  void testModeTransition_data();
  void testModeTransition();

  // ============ previewSyncIntervalMs ============

  void testPreviewSyncIntervalMs();

  // ============ getEditViewMode ============

  void testGetEditViewMode_editOnly();
  void testGetEditViewMode_editPreview();

  // ============ createContextMenu ============

  void testContextMenu_readModeNoSelection();
  void testContextMenu_readModeWithSelection();
  void testContextMenu_editMode();
  void testContextMenu_crossCopyTargets();
  void testContextMenu_noCrossCopyTargets();
  void testContextMenu_copyImagePresent();
  void testContextMenu_viewImageReadMode();
  void testContextMenu_viewImageNotInReadMode();
  void testContextMenu_viewImageInvalidUrl();
  void testContextMenu_exportReadModeNoSelection();
  void testContextMenu_exportAbsentWithSelection();
  void testContextMenu_exportAbsentWithoutHandler();

  // ============ rewriteTaskListLine ============

  void testRewriteTaskList_check();
  void testRewriteTaskList_uncheck();
  void testRewriteTaskList_orderedItem();
  void testRewriteTaskList_upperCaseX();
  void testRewriteTaskList_indented();
  void testRewriteTaskList_blockquoted();
  void testRewriteTaskList_nonTaskLine();
  void testRewriteTaskList_stateMismatch();
  void testRewriteTaskList_preservesTrailingMarkup();
  void testRewriteTaskList_preservesCr();

private:
  static const int c_readMode = static_cast<int>(ViewWindowMode::Read);
  static const int c_editMode = static_cast<int>(ViewWindowMode::Edit);
  static const int c_invalidMode = static_cast<int>(ViewWindowMode::Invalid);
};

// ============ computeModeTransition ============

namespace {
enum TransitionFlag {
  SetupEditor = 1,
  SetupViewer = 2,
  SyncEditor = 4,
  SyncViewer = 8,
  SyncPosition = 16,
  RestoreEditor = 32,
  SyncActive = 64,
  AdoptRevision = 128
};
}

void TestMarkdownViewWindowController::testModeTransition_data() {
  QTest::addColumn<int>("current");
  QTest::addColumn<int>("target");
  QTest::addColumn<int>("editView");
  QTest::addColumn<bool>("editor");
  QTest::addColumn<bool>("viewer");
  QTest::addColumn<bool>("sync");
  QTest::addColumn<int>("expected");
  using EditView = MarkdownEditorConfig::EditViewMode;
  for (const int current : {c_invalidMode, c_readMode, c_editMode}) {
    for (const int target : {c_invalidMode, c_readMode, c_editMode}) {
      for (const auto editView : {EditView::EditOnly, EditView::EditPreview}) {
        for (const bool editor : {false, true}) {
          for (const bool viewer : {false, true}) {
            for (const bool sync : {false, true}) {
              int expected = 0;
              if (current != target && target != c_invalidMode) {
                const bool readerNeeded = target == c_readMode || editView == EditView::EditPreview;
                if (readerNeeded && !viewer) {
                  expected |= SetupViewer;
                  if (sync)
                    expected |= SyncViewer;
                }
                if (target == c_editMode) {
                  expected |= editor ? RestoreEditor : SetupEditor;
                  if (sync && !editor)
                    expected |= SyncEditor;
                }
                if (sync) {
                  expected |= SyncActive;
                  if (current != c_invalidMode)
                    expected |= SyncPosition;
                  if (current == c_invalidMode && (expected & (SyncEditor | SyncViewer))) {
                    expected |= AdoptRevision;
                  }
                }
              }
              const auto name = QStringLiteral("%1-%2-layout%3-editor%4-viewer%5-sync%6")
                                    .arg(current)
                                    .arg(target)
                                    .arg(static_cast<int>(editView))
                                    .arg(editor)
                                    .arg(viewer)
                                    .arg(sync)
                                    .toLatin1();
              QTest::newRow(name.constData()) << current << target << static_cast<int>(editView)
                                              << editor << viewer << sync << expected;
            }
          }
        }
      }
    }
  }
}

void TestMarkdownViewWindowController::testModeTransition() {
  QFETCH(int, current);
  QFETCH(int, target);
  QFETCH(int, editView);
  QFETCH(bool, editor);
  QFETCH(bool, viewer);
  QFETCH(bool, sync);
  QFETCH(int, expected);
  const auto result = MarkdownViewWindowController::computeModeTransition(
      current, target, static_cast<MarkdownEditorConfig::EditViewMode>(editView), editor, viewer,
      sync);
  QCOMPARE(result.needSetupEditor, bool(expected & SetupEditor));
  QCOMPARE(result.needSetupViewer, bool(expected & SetupViewer));
  QCOMPARE(result.syncEditorFromBuffer, bool(expected & SyncEditor));
  QCOMPARE(result.syncViewerFromBuffer, bool(expected & SyncViewer));
  QCOMPARE(result.syncPositionFromPrevMode, bool(expected & SyncPosition));
  QCOMPARE(result.restoreEditViewMode, bool(expected & RestoreEditor));
  QCOMPARE(result.syncBufferToActiveView, bool(expected & SyncActive));
  QCOMPARE(result.adoptInitialRevision, bool(expected & AdoptRevision));
}

// ============ previewSyncIntervalMs ============

void TestMarkdownViewWindowController::testPreviewSyncIntervalMs() {
  QCOMPARE(MarkdownViewWindowController::previewSyncIntervalMs(), 500);
}

// ============ getEditViewMode ============

void TestMarkdownViewWindowController::testGetEditViewMode_editOnly() {
  MarkdownEditorConfig mdConfig(nullptr, nullptr, QSharedPointer<TextEditorConfig>());
  mdConfig.setEditViewMode(MarkdownEditorConfig::EditViewMode::EditOnly);

  auto mode = MarkdownViewWindowController::getEditViewMode(mdConfig);
  QCOMPARE(mode, MarkdownEditorConfig::EditViewMode::EditOnly);
}

void TestMarkdownViewWindowController::testGetEditViewMode_editPreview() {
  MarkdownEditorConfig mdConfig(nullptr, nullptr, QSharedPointer<TextEditorConfig>());
  mdConfig.setEditViewMode(MarkdownEditorConfig::EditViewMode::EditPreview);

  auto mode = MarkdownViewWindowController::getEditViewMode(mdConfig);
  QCOMPARE(mode, MarkdownEditorConfig::EditViewMode::EditPreview);
}

// ============ createContextMenu ============

void TestMarkdownViewWindowController::testContextMenu_readModeNoSelection() {
  ServiceLocator services;
  MarkdownViewWindowController controller(services);
  QMenu menu;
  menu.addAction("Dummy");

  MarkdownViewerContextInfo info;
  info.hasSelection = false;
  info.inReadMode = true;
  info.editShortcutText = "Ctrl+T";

  auto *result = controller.createContextMenu(
      info, &menu, []() {}, []() {}, [](const QString &) {}, []() {}, []() {});

  QCOMPARE(result, &menu);
  auto actions = result->actions();
  QVERIFY(actions.size() >= 2);
  QVERIFY(actions[0]->text().contains("Edit"));
}

void TestMarkdownViewWindowController::testContextMenu_readModeWithSelection() {
  ServiceLocator services;
  MarkdownViewWindowController controller(services);
  QMenu menu;
  menu.addAction("Dummy");

  MarkdownViewerContextInfo info;
  info.hasSelection = true;
  info.inReadMode = true;

  auto *result = controller.createContextMenu(
      info, &menu, []() {}, []() {}, [](const QString &) {}, []() {}, []() {});

  for (auto *act : result->actions()) {
    QVERIFY(!act->text().contains("Edit"));
  }
}

void TestMarkdownViewWindowController::testContextMenu_editMode() {
  ServiceLocator services;
  MarkdownViewWindowController controller(services);
  QMenu menu;
  menu.addAction("Dummy");

  MarkdownViewerContextInfo info;
  info.hasSelection = false;
  info.inReadMode = false;

  auto *result = controller.createContextMenu(
      info, &menu, []() {}, []() {}, [](const QString &) {}, []() {}, []() {});

  for (auto *act : result->actions()) {
    QVERIFY(!act->text().contains("Edit"));
  }
}

void TestMarkdownViewWindowController::testContextMenu_crossCopyTargets() {
  ServiceLocator services;
  MarkdownViewWindowController controller(services);
  QMenu menu;
  auto *copyAct = menu.addAction("Copy");

  MarkdownViewerContextInfo info;
  info.hasSelection = true;
  info.copyAction = copyAct;
  info.crossCopyTargets = {"html", "text"};
  info.crossCopyDisplayNames = {"HTML", "Plain Text"};

  auto *result = controller.createContextMenu(
      info, &menu, []() {}, []() {}, [](const QString &) {}, []() {}, []() {});

  bool foundCrossCopy = false;
  for (auto *act : result->actions()) {
    if (act->menu() && act->text() == "Cross Copy") {
      foundCrossCopy = true;
      auto subActions = act->menu()->actions();
      QCOMPARE(subActions.size(), 2);
      QCOMPARE(subActions[0]->text(), QString("HTML"));
      QCOMPARE(subActions[1]->text(), QString("Plain Text"));
    }
  }
  QVERIFY(foundCrossCopy);
}

void TestMarkdownViewWindowController::testContextMenu_noCrossCopyTargets() {
  ServiceLocator services;
  MarkdownViewWindowController controller(services);
  QMenu menu;
  auto *copyAct = menu.addAction("Copy");

  MarkdownViewerContextInfo info;
  info.hasSelection = true;
  info.copyAction = copyAct;

  auto *result = controller.createContextMenu(
      info, &menu, []() {}, []() {}, [](const QString &) {}, []() {}, []() {});

  for (auto *act : result->actions()) {
    if (act->menu()) {
      QVERIFY(act->text() != "Cross Copy");
    }
  }
}

void TestMarkdownViewWindowController::testContextMenu_copyImagePresent() {
  ServiceLocator services;
  MarkdownViewWindowController controller(services);
  QMenu menu;
  auto *copyTextAct = menu.addAction("&Copy");
  auto *defaultCopyImageAct = menu.addAction("Copy image");
  bool copyTextCalled = false;
  bool defaultCopyImageCalled = false;
  connect(copyTextAct, &QAction::triggered, &menu, [&]() { copyTextCalled = true; });
  connect(defaultCopyImageAct, &QAction::triggered, &menu,
          [&]() { defaultCopyImageCalled = true; });

  MarkdownViewerContextInfo info;
  info.copyAction = copyTextAct;
  info.defaultCopyImageAction = defaultCopyImageAct;

  bool copyImageCalled = false;
  auto *result = controller.createContextMenu(
      info, &menu, [&copyImageCalled]() { copyImageCalled = true; }, []() {},
      [](const QString &) {}, []() {}, []() {});

  QAction *copyImageAct = nullptr;
  for (auto *act : result->actions()) {
    if (act != copyTextAct && act != defaultCopyImageAct && !act->isSeparator()) {
      QVERIFY(!copyImageAct);
      copyImageAct = act;
    }
  }
  QVERIFY(copyImageAct);
  QVERIFY(copyImageAct->isVisible());
  QVERIFY(copyImageAct->isEnabled());
  QVERIFY(!defaultCopyImageAct->isVisible());
  copyImageAct->trigger();
  QVERIFY(copyImageCalled);
  QVERIFY(!copyTextCalled);
  QVERIFY(!defaultCopyImageCalled);
}

void TestMarkdownViewWindowController::testContextMenu_viewImageReadMode() {
  ServiceLocator services;
  MarkdownViewWindowController controller(services);
  QMenu menu;
  auto *defaultCopyImageAct = menu.addAction("Copy image");

  MarkdownViewerContextInfo info;
  info.inReadMode = true;
  info.imageUrl = QUrl("file:///tmp/pic.png");
  info.defaultCopyImageAction = defaultCopyImageAct;

  bool viewCalled = false;
  auto *result = controller.createContextMenu(
      info, &menu, []() {}, []() {}, [](const QString &) {}, [&viewCalled]() { viewCalled = true; },
      []() {});

  QAction *viewAct = nullptr;
  int viewIdx = -1;
  int copyImageIdx = -1;
  const auto acts = result->actions();
  for (int i = 0; i < acts.size(); ++i) {
    if (acts[i]->text().contains("View")) {
      viewAct = acts[i];
      viewIdx = i;
    }
    if (acts[i] == defaultCopyImageAct) {
      copyImageIdx = i;
    }
  }
  QVERIFY(viewAct);
  // The view action should precede the default copy-image action.
  QVERIFY(viewIdx >= 0);
  QVERIFY(copyImageIdx >= 0);
  QVERIFY(viewIdx < copyImageIdx);

  QVERIFY(viewAct->isVisible());
  QVERIFY(viewAct->isEnabled());
  viewAct->trigger();
  QVERIFY(viewCalled);
}

void TestMarkdownViewWindowController::testContextMenu_viewImageNotInReadMode() {
  ServiceLocator services;
  MarkdownViewWindowController controller(services);
  QMenu menu;
  menu.addAction("Dummy");

  MarkdownViewerContextInfo info;
  info.inReadMode = false;
  info.imageUrl = QUrl("file:///tmp/pic.png");

  auto *result = controller.createContextMenu(
      info, &menu, []() {}, []() {}, [](const QString &) {}, []() {}, []() {});

  for (auto *act : result->actions()) {
    QVERIFY(!act->text().contains("View"));
  }
}

void TestMarkdownViewWindowController::testContextMenu_viewImageInvalidUrl() {
  ServiceLocator services;
  MarkdownViewWindowController controller(services);
  QMenu menu;
  menu.addAction("Dummy");

  MarkdownViewerContextInfo info;
  info.inReadMode = true;
  // imageUrl left default-constructed (invalid).

  auto *result = controller.createContextMenu(
      info, &menu, []() {}, []() {}, [](const QString &) {}, []() {}, []() {});

  for (auto *act : result->actions()) {
    QVERIFY(!act->text().contains("View"));
  }
}

// Menu texts carry a '&' mnemonic and an appended shortcut hint, so compare
// against the mnemonic-stripped text with contains().
static QString strippedText(const QAction *p_act) {
  return QString(p_act->text()).remove(QLatin1Char('&'));
}

void TestMarkdownViewWindowController::testContextMenu_exportReadModeNoSelection() {
  ServiceLocator services;
  MarkdownViewWindowController controller(services);
  QMenu menu;
  menu.addAction("Dummy");

  MarkdownViewerContextInfo info;
  info.hasSelection = false;
  info.inReadMode = true;
  info.editShortcutText = "Ctrl+T";
  info.exportShortcutText = "Ctrl+G, T";

  bool exportCalled = false;
  auto *result = controller.createContextMenu(
      info, &menu, []() {}, []() {}, [](const QString &) {}, []() {},
      [&exportCalled]() { exportCalled = true; });

  const auto acts = result->actions();
  int editIdx = -1;
  int exportIdx = -1;
  for (int i = 0; i < acts.size(); ++i) {
    const QString text = strippedText(acts[i]);
    if (editIdx < 0 && text.contains("Edit")) {
      editIdx = i;
    }
    if (exportIdx < 0 && text.contains("Export")) {
      exportIdx = i;
    }
  }
  QVERIFY(editIdx >= 0);
  QVERIFY(exportIdx >= 0);
  QCOMPARE(exportIdx, editIdx + 1);
  // The menu text renders the shortcut with QKeySequence::NativeText, which
  // differs per platform (macOS uses glyphs), so compare against the native
  // rendering rather than a literal.
  const QString expectedShortcut =
      QKeySequence(info.exportShortcutText).toString(QKeySequence::NativeText);
  QVERIFY(!expectedShortcut.isEmpty());
  QVERIFY(strippedText(acts[exportIdx]).contains(expectedShortcut));

  acts[exportIdx]->trigger();
  QVERIFY(exportCalled);
}

void TestMarkdownViewWindowController::testContextMenu_exportAbsentWithSelection() {
  ServiceLocator services;
  MarkdownViewWindowController controller(services);
  QMenu menu;
  menu.addAction("Dummy");

  MarkdownViewerContextInfo info;
  info.hasSelection = true;
  info.inReadMode = true;
  info.exportShortcutText = "Ctrl+G, T";

  auto *result = controller.createContextMenu(
      info, &menu, []() {}, []() {}, [](const QString &) {}, []() {}, []() {});

  for (auto *act : result->actions()) {
    const QString text = strippedText(act);
    QVERIFY(!text.contains("Export"));
    QVERIFY(!text.contains("Edit"));
  }
}

void TestMarkdownViewWindowController::testContextMenu_exportAbsentWithoutHandler() {
  ServiceLocator services;
  MarkdownViewWindowController controller(services);
  QMenu menu;
  menu.addAction("Dummy");

  MarkdownViewerContextInfo info;
  info.hasSelection = false;
  info.inReadMode = true;

  auto *result = controller.createContextMenu(
      info, &menu, []() {}, []() {}, [](const QString &) {}, []() {}, nullptr);

  for (auto *act : result->actions()) {
    QVERIFY(!strippedText(act).contains("Export"));
  }
  // Edit is still offered.
  QVERIFY(strippedText(result->actions()[0]).contains("Edit"));
}

// ============ rewriteTaskListLine ============

void TestMarkdownViewWindowController::testRewriteTaskList_check() {
  QCOMPARE(MarkdownViewWindowController::rewriteTaskListLine(QStringLiteral("- [ ] milk"), true),
           QStringLiteral("- [x] milk"));
}

void TestMarkdownViewWindowController::testRewriteTaskList_uncheck() {
  QCOMPARE(MarkdownViewWindowController::rewriteTaskListLine(QStringLiteral("* [x] milk"), false),
           QStringLiteral("* [ ] milk"));
}

void TestMarkdownViewWindowController::testRewriteTaskList_orderedItem() {
  QCOMPARE(MarkdownViewWindowController::rewriteTaskListLine(QStringLiteral("1. [ ] milk"), true),
           QStringLiteral("1. [x] milk"));
  QCOMPARE(MarkdownViewWindowController::rewriteTaskListLine(QStringLiteral("2) [ ] milk"), true),
           QStringLiteral("2) [x] milk"));
}

void TestMarkdownViewWindowController::testRewriteTaskList_upperCaseX() {
  QCOMPARE(MarkdownViewWindowController::rewriteTaskListLine(QStringLiteral("+ [X] milk"), false),
           QStringLiteral("+ [ ] milk"));
}

void TestMarkdownViewWindowController::testRewriteTaskList_indented() {
  QCOMPARE(
      MarkdownViewWindowController::rewriteTaskListLine(QStringLiteral("    - [ ] nested"), true),
      QStringLiteral("    - [x] nested"));
  QCOMPARE(
      MarkdownViewWindowController::rewriteTaskListLine(QStringLiteral("\t- [ ] nested"), true),
      QStringLiteral("\t- [x] nested"));
}

void TestMarkdownViewWindowController::testRewriteTaskList_blockquoted() {
  QCOMPARE(
      MarkdownViewWindowController::rewriteTaskListLine(QStringLiteral("> - [ ] quoted"), true),
      QStringLiteral("> - [x] quoted"));
  QCOMPARE(
      MarkdownViewWindowController::rewriteTaskListLine(QStringLiteral("> > - [x] quoted"), false),
      QStringLiteral("> > - [ ] quoted"));
  // markdown-it renders ">  > " (extra intra-prefix whitespace) as a nested
  // quote too, so the prefix must not be limited to one space per '>'.
  QCOMPARE(
      MarkdownViewWindowController::rewriteTaskListLine(QStringLiteral(">  > - [ ] quoted"), true),
      QStringLiteral(">  > - [x] quoted"));
}

void TestMarkdownViewWindowController::testRewriteTaskList_nonTaskLine() {
  QVERIFY(MarkdownViewWindowController::rewriteTaskListLine(QStringLiteral("just text"), true)
              .isNull());
  QVERIFY(MarkdownViewWindowController::rewriteTaskListLine(QStringLiteral("- plain item"), true)
              .isNull());
  // No space between the bullet and the checkbox.
  QVERIFY(MarkdownViewWindowController::rewriteTaskListLine(QStringLiteral("-[ ] milk"), true)
              .isNull());
}

void TestMarkdownViewWindowController::testRewriteTaskList_stateMismatch() {
  QVERIFY(MarkdownViewWindowController::rewriteTaskListLine(QStringLiteral("- [x] milk"), true)
              .isNull());
  QVERIFY(MarkdownViewWindowController::rewriteTaskListLine(QStringLiteral("- [ ] milk"), false)
              .isNull());
}

void TestMarkdownViewWindowController::testRewriteTaskList_preservesTrailingMarkup() {
  QCOMPARE(MarkdownViewWindowController::rewriteTaskListLine(
               QStringLiteral("- [ ] **bold** and [link](a.md) [x]"), true),
           QStringLiteral("- [x] **bold** and [link](a.md) [x]"));
}

void TestMarkdownViewWindowController::testRewriteTaskList_preservesCr() {
  QCOMPARE(MarkdownViewWindowController::rewriteTaskListLine(QStringLiteral("- [ ] a\r"), true),
           QStringLiteral("- [x] a\r"));
}

} // namespace tests
QTEST_MAIN(tests::TestMarkdownViewWindowController)
#include "test_markdownviewwindowcontroller.moc"
