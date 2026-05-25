#include <QtTest>

namespace tests {

// T11 widget-level smoke test placeholder.
//
// The plan T11 (lines 775-787 of
// .sisyphus/plans/notebookexplorer2-multiselect-fix.md) calls for a smoke test
// that constructs NotebookExplorer2, selects three nodes via the view's
// selectionModel, drives contextMenuRequested for an in-selection clicked
// node, and asserts the controller signal fires with a 3-element list.
//
// NotebookExplorer2 construction requires a fully wired ServiceLocator with
// NotebookCoreService, ConfigCoreService, BufferService, ConfigMgr2,
// FileTypeCoreService, ThemeService, NotebookNodeController, and more.
// This matches the established blocker documented across T3-T10 of this plan
// where the controller itself was deemed too entangled for direct
// construction (the existing test_notebooknodecontroller_multiselect.cpp uses
// pure-helper mirroring for the same reason).
//
// Per plan T11's explicit guidance: "Do not spend more than 30 minutes
// attempting full construction. If construction fails with >2 missing
// services, document the blocker and create the file as a single QSKIP
// stub registered in tests/widgets/CMakeLists.txt." See
// .sisyphus/notepads/notebookexplorer2-multiselect-fix/issues.md for the
// blocker note. The slot is registered so the smoke test can be expanded
// in a follow-up plan once a NotebookExplorer2 test harness exists.

class TestNotebookExplorer2MultiSelect : public QObject {
  Q_OBJECT

private slots:
  void contextMenuMultiSelect_emitsListSignal();
};

void TestNotebookExplorer2MultiSelect::contextMenuMultiSelect_emitsListSignal() {
  QSKIP("blocked: NotebookExplorer2 construction requires full ServiceLocator wiring "
        "(NotebookCoreService, ConfigCoreService, BufferService, ConfigMgr2, "
        "FileTypeCoreService, ThemeService, NotebookNodeController, etc.); "
        "see .sisyphus/notepads/notebookexplorer2-multiselect-fix/issues.md");
}

} // namespace tests

QTEST_MAIN(tests::TestNotebookExplorer2MultiSelect)
#include "test_notebookexplorer2_multiselect.moc"
