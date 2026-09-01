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

  // Same mode -> all false (no-op).
  void testModeTransition_sameModeSameRead();
  void testModeTransition_sameModeSameEdit();

  // Invalid->Read transitions.
  void testModeTransition_invalidToRead_noViewer();
  void testModeTransition_invalidToRead_noViewer_noSync();
  void testModeTransition_invalidToRead_hasViewer();

  // Invalid->Edit transitions.
  void testModeTransition_invalidToEdit_noEditorNoViewer();
  void testModeTransition_invalidToEdit_noEditorHasViewer();
  void testModeTransition_invalidToEdit_noEditor_noSync();
  void testModeTransition_invalidToEdit_hasEditor();

  // Read->Edit transitions.
  void testModeTransition_readToEdit_noEditor_noViewer();
  void testModeTransition_readToEdit_noEditor_hasViewer();
  void testModeTransition_readToEdit_hasEditor();

  // Edit->Read transitions.
  void testModeTransition_editToRead_hasViewer();
  void testModeTransition_editToRead_noViewer();

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
  void testContextMenu_noCopyImage();
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

// --- Same mode -> no-op ---

void TestMarkdownViewWindowController::testModeTransition_sameModeSameRead() {
  auto t = MarkdownViewWindowController::computeModeTransition(c_readMode, c_readMode, false, false,
                                                               true);

  QCOMPARE(t.needSetupEditor, false);
  QCOMPARE(t.needSetupViewer, false);
  QCOMPARE(t.syncEditorFromBuffer, false);
  QCOMPARE(t.syncViewerFromBuffer, false);
  QCOMPARE(t.syncPositionFromPrevMode, false);
  QCOMPARE(t.restoreEditViewMode, false);
  QCOMPARE(t.syncBufferToActiveView, false);
  QCOMPARE(t.adoptInitialRevision, false);
}

void TestMarkdownViewWindowController::testModeTransition_sameModeSameEdit() {
  auto t =
      MarkdownViewWindowController::computeModeTransition(c_editMode, c_editMode, true, true, true);

  QCOMPARE(t.needSetupEditor, false);
  QCOMPARE(t.needSetupViewer, false);
  QCOMPARE(t.syncEditorFromBuffer, false);
  QCOMPARE(t.syncViewerFromBuffer, false);
  QCOMPARE(t.syncPositionFromPrevMode, false);
  QCOMPARE(t.restoreEditViewMode, false);
  QCOMPARE(t.syncBufferToActiveView, false);
  QCOMPARE(t.adoptInitialRevision, false);
}

// --- Invalid->Read ---

void TestMarkdownViewWindowController::testModeTransition_invalidToRead_noViewer() {
  auto t = MarkdownViewWindowController::computeModeTransition(c_invalidMode, c_readMode, false,
                                                               false, true);

  QCOMPARE(t.needSetupViewer, true);
  QCOMPARE(t.syncViewerFromBuffer, true);
  QCOMPARE(t.syncBufferToActiveView, true);
  // No previous valid mode, so no position sync.
  QCOMPARE(t.syncPositionFromPrevMode, false);
  QCOMPARE(t.needSetupEditor, false);
  QCOMPARE(t.syncEditorFromBuffer, false);
  QCOMPARE(t.restoreEditViewMode, false);
  // Initial transition with a buffer read: adopt the post-load revision.
  QCOMPARE(t.adoptInitialRevision, true);
}

void TestMarkdownViewWindowController::testModeTransition_invalidToRead_noViewer_noSync() {
  auto t = MarkdownViewWindowController::computeModeTransition(c_invalidMode, c_readMode, false,
                                                               false, false);

  QCOMPARE(t.needSetupViewer, true);
  // syncBuffer=false -> no content sync from buffer.
  QCOMPARE(t.syncViewerFromBuffer, false);
  QCOMPARE(t.syncBufferToActiveView, false);
  QCOMPARE(t.syncPositionFromPrevMode, false);
  // No buffer read happened, so nothing bumped the revision.
  QCOMPARE(t.adoptInitialRevision, false);
}

void TestMarkdownViewWindowController::testModeTransition_invalidToRead_hasViewer() {
  auto t = MarkdownViewWindowController::computeModeTransition(c_invalidMode, c_readMode, false,
                                                               true, true);

  // Viewer already exists, no setup needed.
  QCOMPARE(t.needSetupViewer, false);
  QCOMPARE(t.syncViewerFromBuffer, false);
  QCOMPARE(t.syncBufferToActiveView, true);
  QCOMPARE(t.syncPositionFromPrevMode, false);
}

// --- Invalid->Edit ---

void TestMarkdownViewWindowController::testModeTransition_invalidToEdit_noEditorNoViewer() {
  auto t = MarkdownViewWindowController::computeModeTransition(c_invalidMode, c_editMode, false,
                                                               false, true);

  QCOMPARE(t.needSetupViewer, true);
  QCOMPARE(t.needSetupEditor, true);
  QCOMPARE(t.syncEditorFromBuffer, true);
  QCOMPARE(t.syncBufferToActiveView, true);
  // No previous valid mode.
  QCOMPARE(t.syncPositionFromPrevMode, false);
  QCOMPARE(t.restoreEditViewMode, false);
  // Viewer is newly created -> sync its template so Read mode works later.
  QCOMPARE(t.syncViewerFromBuffer, true);
  QCOMPARE(t.adoptInitialRevision, true);
}

void TestMarkdownViewWindowController::testModeTransition_invalidToEdit_noEditorHasViewer() {
  auto t = MarkdownViewWindowController::computeModeTransition(c_invalidMode, c_editMode, false,
                                                               true, true);

  // Viewer already exists -> no viewer setup.
  QCOMPARE(t.needSetupViewer, false);
  QCOMPARE(t.needSetupEditor, true);
  QCOMPARE(t.syncEditorFromBuffer, true);
  QCOMPARE(t.syncBufferToActiveView, true);
  QCOMPARE(t.syncPositionFromPrevMode, false);
}

void TestMarkdownViewWindowController::testModeTransition_invalidToEdit_noEditor_noSync() {
  auto t = MarkdownViewWindowController::computeModeTransition(c_invalidMode, c_editMode, false,
                                                               false, false);

  QCOMPARE(t.needSetupViewer, true);
  QCOMPARE(t.needSetupEditor, true);
  // syncBuffer=false -> no content sync.
  QCOMPARE(t.syncEditorFromBuffer, false);
  QCOMPARE(t.syncBufferToActiveView, false);
}

void TestMarkdownViewWindowController::testModeTransition_invalidToEdit_hasEditor() {
  auto t = MarkdownViewWindowController::computeModeTransition(c_invalidMode, c_editMode, true,
                                                               false, true);

  // Editor exists -> restore, not setup.
  QCOMPARE(t.needSetupEditor, false);
  QCOMPARE(t.restoreEditViewMode, true);
  QCOMPARE(t.syncBufferToActiveView, true);
  QCOMPARE(t.syncPositionFromPrevMode, false);
}

// --- Read->Edit ---

void TestMarkdownViewWindowController::testModeTransition_readToEdit_noEditor_noViewer() {
  auto t = MarkdownViewWindowController::computeModeTransition(c_readMode, c_editMode, false, false,
                                                               true);

  QCOMPARE(t.needSetupViewer, true);
  QCOMPARE(t.needSetupEditor, true);
  QCOMPARE(t.syncEditorFromBuffer, true);
  QCOMPARE(t.syncBufferToActiveView, true);
  // Coming from Read -> sync position.
  QCOMPARE(t.syncPositionFromPrevMode, true);
  QCOMPARE(t.restoreEditViewMode, false);
  // Viewer is newly created -> sync its template so Read mode works later.
  QCOMPARE(t.syncViewerFromBuffer, true);
  // Not the initial transition: a revision change here is a genuine external
  // change and must stay detectable.
  QCOMPARE(t.adoptInitialRevision, false);
}

void TestMarkdownViewWindowController::testModeTransition_readToEdit_noEditor_hasViewer() {
  auto t = MarkdownViewWindowController::computeModeTransition(c_readMode, c_editMode, false, true,
                                                               true);

  QCOMPARE(t.needSetupViewer, false);
  QCOMPARE(t.needSetupEditor, true);
  QCOMPARE(t.syncEditorFromBuffer, true);
  QCOMPARE(t.syncPositionFromPrevMode, true);
}

void TestMarkdownViewWindowController::testModeTransition_readToEdit_hasEditor() {
  auto t =
      MarkdownViewWindowController::computeModeTransition(c_readMode, c_editMode, true, true, true);

  QCOMPARE(t.needSetupEditor, false);
  QCOMPARE(t.restoreEditViewMode, true);
  QCOMPARE(t.syncPositionFromPrevMode, true);
  QCOMPARE(t.syncBufferToActiveView, true);
}

// --- Edit->Read ---

void TestMarkdownViewWindowController::testModeTransition_editToRead_hasViewer() {
  auto t =
      MarkdownViewWindowController::computeModeTransition(c_editMode, c_readMode, true, true, true);

  QCOMPARE(t.needSetupViewer, false);
  QCOMPARE(t.syncPositionFromPrevMode, true);
  QCOMPARE(t.syncBufferToActiveView, true);
  QCOMPARE(t.needSetupEditor, false);
  QCOMPARE(t.restoreEditViewMode, false);
}

void TestMarkdownViewWindowController::testModeTransition_editToRead_noViewer() {
  auto t = MarkdownViewWindowController::computeModeTransition(c_editMode, c_readMode, true, false,
                                                               true);

  QCOMPARE(t.needSetupViewer, true);
  QCOMPARE(t.syncViewerFromBuffer, true);
  QCOMPARE(t.syncPositionFromPrevMode, true);
  QCOMPARE(t.syncBufferToActiveView, true);
  // Not the initial transition: a revision change here is a genuine external
  // change and must stay detectable.
  QCOMPARE(t.adoptInitialRevision, false);
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
  auto *defaultCopyImageAct = menu.addAction("Copy image");

  MarkdownViewerContextInfo info;
  info.defaultCopyImageAction = defaultCopyImageAct;

  bool copyImageCalled = false;
  auto *result = controller.createContextMenu(
      info, &menu, [&copyImageCalled]() { copyImageCalled = true; }, []() {},
      [](const QString &) {}, []() {}, []() {});

  QVERIFY(!defaultCopyImageAct->isVisible());

  bool foundReplacement = false;
  for (auto *act : result->actions()) {
    if (act != defaultCopyImageAct && act->text() == "Copy") {
      foundReplacement = true;
      act->trigger();
    }
  }

  QVERIFY(foundReplacement);
  QVERIFY(copyImageCalled);
}

void TestMarkdownViewWindowController::testContextMenu_noCopyImage() {
  ServiceLocator services;
  MarkdownViewWindowController controller(services);
  QMenu menu;
  menu.addAction("Dummy");

  MarkdownViewerContextInfo info;

  auto *result = controller.createContextMenu(
      info, &menu, []() {}, []() {}, [](const QString &) {}, []() {}, []() {});

  int copyImageCount = 0;
  for (auto *act : result->actions()) {
    if (act->text() == "Copy Image") {
      ++copyImageCount;
    }
  }
  QCOMPARE(copyImageCount, 0);
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
  // "View" should be inserted before the default copy-image action.
  QVERIFY(viewIdx >= 0);
  QVERIFY(copyImageIdx >= 0);
  QVERIFY(viewIdx < copyImageIdx);

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
