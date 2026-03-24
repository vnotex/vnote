#include <QtTest>

#include <controllers/markdownviewwindowcontroller.h>
#include <core/global.h>
#include <core/markdowneditorconfig.h>

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

private:
  static const int c_readMode = static_cast<int>(ViewWindowMode::Read);
  static const int c_editMode = static_cast<int>(ViewWindowMode::Edit);
  static const int c_invalidMode = static_cast<int>(ViewWindowMode::Invalid);
};

// ============ computeModeTransition ============

// --- Same mode -> no-op ---

void TestMarkdownViewWindowController::testModeTransition_sameModeSameRead() {
  auto t = MarkdownViewWindowController::computeModeTransition(
      c_readMode, c_readMode, false, false, true);

  QCOMPARE(t.needSetupEditor, false);
  QCOMPARE(t.needSetupViewer, false);
  QCOMPARE(t.syncEditorFromBuffer, false);
  QCOMPARE(t.syncViewerFromBuffer, false);
  QCOMPARE(t.syncPositionFromPrevMode, false);
  QCOMPARE(t.restoreEditViewMode, false);
  QCOMPARE(t.syncBufferToActiveView, false);
}

void TestMarkdownViewWindowController::testModeTransition_sameModeSameEdit() {
  auto t = MarkdownViewWindowController::computeModeTransition(
      c_editMode, c_editMode, true, true, true);

  QCOMPARE(t.needSetupEditor, false);
  QCOMPARE(t.needSetupViewer, false);
  QCOMPARE(t.syncEditorFromBuffer, false);
  QCOMPARE(t.syncViewerFromBuffer, false);
  QCOMPARE(t.syncPositionFromPrevMode, false);
  QCOMPARE(t.restoreEditViewMode, false);
  QCOMPARE(t.syncBufferToActiveView, false);
}

// --- Invalid->Read ---

void TestMarkdownViewWindowController::testModeTransition_invalidToRead_noViewer() {
  auto t = MarkdownViewWindowController::computeModeTransition(
      c_invalidMode, c_readMode, false, false, true);

  QCOMPARE(t.needSetupViewer, true);
  QCOMPARE(t.syncViewerFromBuffer, true);
  QCOMPARE(t.syncBufferToActiveView, true);
  // No previous valid mode, so no position sync.
  QCOMPARE(t.syncPositionFromPrevMode, false);
  QCOMPARE(t.needSetupEditor, false);
  QCOMPARE(t.syncEditorFromBuffer, false);
  QCOMPARE(t.restoreEditViewMode, false);
}

void TestMarkdownViewWindowController::testModeTransition_invalidToRead_noViewer_noSync() {
  auto t = MarkdownViewWindowController::computeModeTransition(
      c_invalidMode, c_readMode, false, false, false);

  QCOMPARE(t.needSetupViewer, true);
  // syncBuffer=false -> no content sync from buffer.
  QCOMPARE(t.syncViewerFromBuffer, false);
  QCOMPARE(t.syncBufferToActiveView, false);
  QCOMPARE(t.syncPositionFromPrevMode, false);
}

void TestMarkdownViewWindowController::testModeTransition_invalidToRead_hasViewer() {
  auto t = MarkdownViewWindowController::computeModeTransition(
      c_invalidMode, c_readMode, false, true, true);

  // Viewer already exists, no setup needed.
  QCOMPARE(t.needSetupViewer, false);
  QCOMPARE(t.syncViewerFromBuffer, false);
  QCOMPARE(t.syncBufferToActiveView, true);
  QCOMPARE(t.syncPositionFromPrevMode, false);
}

// --- Invalid->Edit ---

void TestMarkdownViewWindowController::testModeTransition_invalidToEdit_noEditorNoViewer() {
  auto t = MarkdownViewWindowController::computeModeTransition(
      c_invalidMode, c_editMode, false, false, true);

  QCOMPARE(t.needSetupViewer, true);
  QCOMPARE(t.needSetupEditor, true);
  QCOMPARE(t.syncEditorFromBuffer, true);
  QCOMPARE(t.syncBufferToActiveView, true);
  // No previous valid mode.
  QCOMPARE(t.syncPositionFromPrevMode, false);
  QCOMPARE(t.restoreEditViewMode, false);
  // Viewer is newly created -> sync its template so Read mode works later.
  QCOMPARE(t.syncViewerFromBuffer, true);
}

void TestMarkdownViewWindowController::testModeTransition_invalidToEdit_noEditorHasViewer() {
  auto t = MarkdownViewWindowController::computeModeTransition(
      c_invalidMode, c_editMode, false, true, true);

  // Viewer already exists -> no viewer setup.
  QCOMPARE(t.needSetupViewer, false);
  QCOMPARE(t.needSetupEditor, true);
  QCOMPARE(t.syncEditorFromBuffer, true);
  QCOMPARE(t.syncBufferToActiveView, true);
  QCOMPARE(t.syncPositionFromPrevMode, false);
}

void TestMarkdownViewWindowController::testModeTransition_invalidToEdit_noEditor_noSync() {
  auto t = MarkdownViewWindowController::computeModeTransition(
      c_invalidMode, c_editMode, false, false, false);

  QCOMPARE(t.needSetupViewer, true);
  QCOMPARE(t.needSetupEditor, true);
  // syncBuffer=false -> no content sync.
  QCOMPARE(t.syncEditorFromBuffer, false);
  QCOMPARE(t.syncBufferToActiveView, false);
}

void TestMarkdownViewWindowController::testModeTransition_invalidToEdit_hasEditor() {
  auto t = MarkdownViewWindowController::computeModeTransition(
      c_invalidMode, c_editMode, true, false, true);

  // Editor exists -> restore, not setup.
  QCOMPARE(t.needSetupEditor, false);
  QCOMPARE(t.restoreEditViewMode, true);
  QCOMPARE(t.syncBufferToActiveView, true);
  QCOMPARE(t.syncPositionFromPrevMode, false);
}

// --- Read->Edit ---

void TestMarkdownViewWindowController::testModeTransition_readToEdit_noEditor_noViewer() {
  auto t = MarkdownViewWindowController::computeModeTransition(
      c_readMode, c_editMode, false, false, true);

  QCOMPARE(t.needSetupViewer, true);
  QCOMPARE(t.needSetupEditor, true);
  QCOMPARE(t.syncEditorFromBuffer, true);
  QCOMPARE(t.syncBufferToActiveView, true);
  // Coming from Read -> sync position.
  QCOMPARE(t.syncPositionFromPrevMode, true);
  QCOMPARE(t.restoreEditViewMode, false);
  // Viewer is newly created -> sync its template so Read mode works later.
  QCOMPARE(t.syncViewerFromBuffer, true);
}

void TestMarkdownViewWindowController::testModeTransition_readToEdit_noEditor_hasViewer() {
  auto t = MarkdownViewWindowController::computeModeTransition(
      c_readMode, c_editMode, false, true, true);

  QCOMPARE(t.needSetupViewer, false);
  QCOMPARE(t.needSetupEditor, true);
  QCOMPARE(t.syncEditorFromBuffer, true);
  QCOMPARE(t.syncPositionFromPrevMode, true);
}

void TestMarkdownViewWindowController::testModeTransition_readToEdit_hasEditor() {
  auto t = MarkdownViewWindowController::computeModeTransition(
      c_readMode, c_editMode, true, true, true);

  QCOMPARE(t.needSetupEditor, false);
  QCOMPARE(t.restoreEditViewMode, true);
  QCOMPARE(t.syncPositionFromPrevMode, true);
  QCOMPARE(t.syncBufferToActiveView, true);
}

// --- Edit->Read ---

void TestMarkdownViewWindowController::testModeTransition_editToRead_hasViewer() {
  auto t = MarkdownViewWindowController::computeModeTransition(
      c_editMode, c_readMode, true, true, true);

  QCOMPARE(t.needSetupViewer, false);
  QCOMPARE(t.syncPositionFromPrevMode, true);
  QCOMPARE(t.syncBufferToActiveView, true);
  QCOMPARE(t.needSetupEditor, false);
  QCOMPARE(t.restoreEditViewMode, false);
}

void TestMarkdownViewWindowController::testModeTransition_editToRead_noViewer() {
  auto t = MarkdownViewWindowController::computeModeTransition(
      c_editMode, c_readMode, true, false, true);

  QCOMPARE(t.needSetupViewer, true);
  QCOMPARE(t.syncViewerFromBuffer, true);
  QCOMPARE(t.syncPositionFromPrevMode, true);
  QCOMPARE(t.syncBufferToActiveView, true);
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

} // namespace tests

QTEST_GUILESS_MAIN(tests::TestMarkdownViewWindowController)
#include "test_markdownviewwindowcontroller.moc"
