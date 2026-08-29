#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QSet>
#include <QtTest>

#include <controllers/notebooknodecontroller.h>
#include <core/nodeidentifier.h>
#include <core/nodeinfo.h>
#include <core/services/hookmanager.h>
#include <core/services/notebookcoreservice.h>
#include <core/services/tagcoreservice.h>
#include <core/services/tagservice.h>
#include <temp_dir_fixture.h>

#include <vxcore/vxcore.h>

using namespace vnotex;

namespace tests {

// Test-only mirror of NotebookNodeController::resolveSelection (Qt right-click
// convention). Only this trivial step is mirrored; the eligibility filter under
// test is the REAL production function, linked from
// src/controllers/notebooknodecontroller_tagseam.cpp.
static QList<NodeIdentifier> resolveSelectionForTest(const QList<NodeIdentifier> &p_selection,
                                                     const NodeIdentifier &p_clicked) {
  if (p_selection.isEmpty() || !p_selection.contains(p_clicked)) {
    return {p_clicked};
  }
  return p_selection;
}

static NodeIdentifier makeId(const QString &p_nb, const QString &p_path) {
  NodeIdentifier id;
  id.notebookId = p_nb;
  id.relativePath = p_path;
  return id;
}

static QString infoKey(const NodeIdentifier &p_id) {
  return p_id.notebookId + QLatin1Char('|') + p_id.relativePath;
}

static void registerInfo(QHash<QString, NodeInfo> &p_infos, const NodeIdentifier &p_id,
                         bool p_isFolder) {
  NodeInfo info;
  info.id = p_id;
  info.isFolder = p_isFolder;
  p_infos.insert(infoKey(p_id), info);
}

// Stands in for NotebookNodeController::getNodeInfo(): an unregistered id yields
// a DEFAULT NodeInfo, exactly as the controller sees a stale/unresolved node.
static NotebookNodeController::NodeInfoLookup lookupFor(const QHash<QString, NodeInfo> &p_infos) {
  return [&p_infos](const NodeIdentifier &p_id) { return p_infos.value(infoKey(p_id)); };
}

// Drives the REAL production filter through the same pipeline
// NotebookNodeController::resolveTagTargets uses.
static QList<NodeIdentifier> resolveTagTargets(const QList<NodeIdentifier> &p_selection,
                                               const NodeIdentifier &p_clicked,
                                               const QHash<QString, NodeInfo> &p_infos) {
  return NotebookNodeController::filterTagTargets(resolveSelectionForTest(p_selection, p_clicked),
                                                  p_clicked, lookupFor(p_infos));
}

class TestNotebookNodeControllerTagTargets : public QObject {
  Q_OBJECT

private slots:
  void initTestCase();
  void cleanupTestCase();

  // filterTagTargets eligibility (real production predicate).
  void filesAndFolderSelected_onlyFilesAreTargets();
  void folderOnlySelection_noTargets();
  void foreignNotebookId_isFilteredOut();
  void staleUnresolvedId_isFilteredOut();
  void clickedOutsideSelection_onlyClickedIsTarget();
  void rootNode_isFilteredOut();

  // planTagDelta: the real per-file op planner handleTagDeltaResult issues from.
  void planTagDelta_dropsOpsThatWouldBeNoOpsOnThisFile();
  void planTagDelta_partialTagYieldsDifferentOpsPerFile();
  void planTagDelta_neverTouchesUnrelatedTags();

  // applyTagDelta: the real pre-read -> plan -> issue -> aggregate orchestration.
  void applyTagDelta_failedPreReadIssuesNothingAndFails();
  void applyTagDelta_noOpsAreNeverIssued();
  void applyTagDelta_plannedOpsAreIssued();
  void applyTagDelta_anyIssuedFailureFailsTheFile();
  void applyTagDelta_emptyDeltaSucceedsWithoutReading();

  // The delta apply primitive's SERVICE semantics, against a real notebook.
  void tagDelta_appliesAddAndRemoveAndPreservesUnrelatedTags();
  void tagPrimitivesAreNotIdempotent_soRedundantOpsMustBeFilteredBeforeDispatch();

private:
  QString createBundledNotebook(const QString &p_path);

  VxCoreContextHandle m_context = nullptr;
  NotebookCoreService *m_notebookService = nullptr;
  TagCoreService *m_tagCoreService = nullptr;
  HookManager *m_hookMgr = nullptr;
  TagService *m_tagService = nullptr;
  TempDirFixture m_tempDir;
};

void TestNotebookNodeControllerTagTargets::initTestCase() {
  QVERIFY(m_tempDir.isValid());
  vxcore_set_test_mode(1);

  QCOMPARE(vxcore_context_create("{}", &m_context), VXCORE_OK);
  QVERIFY(m_context != nullptr);

  m_notebookService = new NotebookCoreService(m_context, this);
  m_tagCoreService = new TagCoreService(m_context, this);
  m_hookMgr = new HookManager(this);
  m_tagService = new TagService(m_context, m_hookMgr, this);
}

void TestNotebookNodeControllerTagTargets::cleanupTestCase() {
  delete m_tagService;
  m_tagService = nullptr;
  delete m_hookMgr;
  m_hookMgr = nullptr;
  delete m_tagCoreService;
  m_tagCoreService = nullptr;
  delete m_notebookService;
  m_notebookService = nullptr;

  if (m_context) {
    vxcore_context_destroy(m_context);
    m_context = nullptr;
  }
}

QString TestNotebookNodeControllerTagTargets::createBundledNotebook(const QString &p_path) {
  const QString configJson = R"({
    "name": "Tag Targets Test Notebook",
    "description": "Test notebook for batch tag editing",
    "version": "1"
  })";
  return m_notebookService->createNotebook(p_path, configJson, NotebookType::Bundled);
}

// ===== filterTagTargets eligibility (real production predicate) =====

void TestNotebookNodeControllerTagTargets::filesAndFolderSelected_onlyFilesAreTargets() {
  const auto a = makeId("nb1", "a.md");
  const auto b = makeId("nb1", "b.md");
  const auto folder = makeId("nb1", "sub");

  QHash<QString, NodeInfo> infos;
  registerInfo(infos, a, false);
  registerInfo(infos, b, false);
  registerInfo(infos, folder, true);

  const auto targets = resolveTagTargets({a, b, folder}, a, infos);
  QCOMPARE(targets.size(), 2);
  QVERIFY(targets.contains(a));
  QVERIFY(targets.contains(b));
  QVERIFY(!targets.contains(folder));
}

void TestNotebookNodeControllerTagTargets::folderOnlySelection_noTargets() {
  const auto f1 = makeId("nb1", "sub1");
  const auto f2 = makeId("nb1", "sub2");

  QHash<QString, NodeInfo> infos;
  registerInfo(infos, f1, true);
  registerInfo(infos, f2, true);

  // Empty target list is what makes manageNodeTags return without emitting and
  // what greys the Tags action out.
  QVERIFY(resolveTagTargets({f1, f2}, f1, infos).isEmpty());
}

void TestNotebookNodeControllerTagTargets::foreignNotebookId_isFilteredOut() {
  const auto a = makeId("nb1", "a.md");
  const auto foreign = makeId("nb2", "x.md");

  QHash<QString, NodeInfo> infos;
  registerInfo(infos, a, false);
  registerInfo(infos, foreign, false);

  const auto targets = resolveTagTargets({a, foreign}, a, infos);
  QCOMPARE(targets.size(), 1);
  QCOMPARE(targets.first(), a);
}

void TestNotebookNodeControllerTagTargets::staleUnresolvedId_isFilteredOut() {
  const auto a = makeId("nb1", "a.md");
  const auto stale = makeId("nb1", "gone.md");

  QHash<QString, NodeInfo> infos;
  registerInfo(infos, a, false);
  // 'stale' is deliberately NOT registered: getNodeInfo returns a default
  // NodeInfo whose isFolder is false, so only NodeInfo::isValid() rejects it.
  QVERIFY(stale.isValid());

  const auto targets = resolveTagTargets({a, stale}, a, infos);
  QCOMPARE(targets.size(), 1);
  QCOMPARE(targets.first(), a);
}

void TestNotebookNodeControllerTagTargets::clickedOutsideSelection_onlyClickedIsTarget() {
  const auto a = makeId("nb1", "a.md");
  const auto b = makeId("nb1", "b.md");
  const auto clicked = makeId("nb1", "c.md");

  QHash<QString, NodeInfo> infos;
  registerInfo(infos, a, false);
  registerInfo(infos, b, false);
  registerInfo(infos, clicked, false);

  const auto targets = resolveTagTargets({a, b}, clicked, infos);
  QCOMPARE(targets.size(), 1);
  QCOMPARE(targets.first(), clicked);
}

void TestNotebookNodeControllerTagTargets::rootNode_isFilteredOut() {
  const auto root = makeId("nb1", "");
  QVERIFY(root.isRoot());

  QHash<QString, NodeInfo> infos;
  registerInfo(infos, root, false);

  QVERIFY(resolveTagTargets({root}, root, infos).isEmpty());
}

// ===== planTagDelta (real production planner) =====

void TestNotebookNodeControllerTagTargets::planTagDelta_dropsOpsThatWouldBeNoOpsOnThisFile() {
  const QSet<QString> current{QStringLiteral("has"), QStringLiteral("keep")};
  const auto ops =
      NotebookNodeController::planTagDelta(current, {QStringLiteral("has"), QStringLiteral("new")},
                                           {QStringLiteral("keep"), QStringLiteral("missing")});

  // "has" is already present and "missing" is already absent — issuing either
  // would return a non-OK code from vxcore and be misread as a failure.
  QCOMPARE(ops.toAdd, QStringList{QStringLiteral("new")});
  QCOMPARE(ops.toRemove, QStringList{QStringLiteral("keep")});
}

void TestNotebookNodeControllerTagTargets::planTagDelta_partialTagYieldsDifferentOpsPerFile() {
  // The Partial -> All case: one file already carries the tag, the other does not.
  const QSet<QString> added{QStringLiteral("half")};
  const QSet<QString> removed;

  const auto tagged =
      NotebookNodeController::planTagDelta({QStringLiteral("half")}, added, removed);
  QVERIFY(tagged.toAdd.isEmpty());
  QVERIFY(tagged.toRemove.isEmpty());

  const auto untagged = NotebookNodeController::planTagDelta({}, added, removed);
  QCOMPARE(untagged.toAdd, QStringList{QStringLiteral("half")});
  QVERIFY(untagged.toRemove.isEmpty());
}

void TestNotebookNodeControllerTagTargets::planTagDelta_neverTouchesUnrelatedTags() {
  const QSet<QString> current{QStringLiteral("unrelated")};
  const auto ops =
      NotebookNodeController::planTagDelta(current, {QStringLiteral("add")}, {QStringLiteral("x")});

  QCOMPARE(ops.toAdd, QStringList{QStringLiteral("add")});
  QVERIFY(ops.toRemove.isEmpty());
  QVERIFY(!ops.toAdd.contains(QStringLiteral("unrelated")));
  QVERIFY(!ops.toRemove.contains(QStringLiteral("unrelated")));
}

// ===== applyTagDelta (real production orchestration) =====

namespace {

// Records what the production orchestration actually issued.
struct RecordingIo {
  QSet<QString> currentTags;
  bool readOk = true;
  bool tagOk = true;
  bool untagOk = true;

  int readCount = 0;
  QStringList tagged;
  QStringList untagged;

  NotebookNodeController::TagDeltaIo io() {
    NotebookNodeController::TagDeltaIo io;
    io.readTags = [this](QSet<QString> &p_out) {
      ++readCount;
      if (!readOk) {
        return false;
      }
      p_out = currentTags;
      return true;
    };
    io.tagFile = [this](const QString &p_tag) {
      tagged.append(p_tag);
      return tagOk;
    };
    io.untagFile = [this](const QString &p_tag) {
      untagged.append(p_tag);
      return untagOk;
    };
    return io;
  }
};

} // namespace

void TestNotebookNodeControllerTagTargets::applyTagDelta_failedPreReadIssuesNothingAndFails() {
  RecordingIo rec;
  rec.readOk = false;

  QVERIFY(!NotebookNodeController::applyTagDelta(rec.io(), {QStringLiteral("add")},
                                                 {QStringLiteral("drop")}));
  QVERIFY(rec.tagged.isEmpty());
  QVERIFY(rec.untagged.isEmpty());
}

void TestNotebookNodeControllerTagTargets::applyTagDelta_noOpsAreNeverIssued() {
  RecordingIo rec;
  rec.currentTags = {QStringLiteral("has")};

  // "has" is already present, "missing" is already absent — issuing either would
  // return a non-OK code from vxcore and be misread as a failure.
  QVERIFY(NotebookNodeController::applyTagDelta(rec.io(), {QStringLiteral("has")},
                                                {QStringLiteral("missing")}));
  QVERIFY(rec.tagged.isEmpty());
  QVERIFY(rec.untagged.isEmpty());
}

void TestNotebookNodeControllerTagTargets::applyTagDelta_plannedOpsAreIssued() {
  RecordingIo rec;
  rec.currentTags = {QStringLiteral("keep"), QStringLiteral("drop")};

  QVERIFY(NotebookNodeController::applyTagDelta(rec.io(), {QStringLiteral("add")},
                                                {QStringLiteral("drop")}));
  QCOMPARE(rec.readCount, 1);
  QCOMPARE(rec.tagged, QStringList{QStringLiteral("add")});
  QCOMPARE(rec.untagged, QStringList{QStringLiteral("drop")});
  // The unrelated pre-existing tag is never touched.
  QVERIFY(!rec.untagged.contains(QStringLiteral("keep")));
}

void TestNotebookNodeControllerTagTargets::applyTagDelta_anyIssuedFailureFailsTheFile() {
  {
    RecordingIo rec;
    rec.tagOk = false;
    QVERIFY(!NotebookNodeController::applyTagDelta(rec.io(), {QStringLiteral("add")}, {}));
    QCOMPARE(rec.tagged, QStringList{QStringLiteral("add")});
  }
  {
    RecordingIo rec;
    rec.currentTags = {QStringLiteral("drop")};
    rec.untagOk = false;
    QVERIFY(!NotebookNodeController::applyTagDelta(rec.io(), {}, {QStringLiteral("drop")}));
    QCOMPARE(rec.untagged, QStringList{QStringLiteral("drop")});
  }
}

void TestNotebookNodeControllerTagTargets::applyTagDelta_emptyDeltaSucceedsWithoutReading() {
  RecordingIo rec;
  QVERIFY(NotebookNodeController::applyTagDelta(rec.io(), {}, {}));
  QCOMPARE(rec.readCount, 0);
  QVERIFY(rec.tagged.isEmpty());
  QVERIFY(rec.untagged.isEmpty());
}

// ===== Delta apply semantics (real services) =====

void TestNotebookNodeControllerTagTargets::tagDelta_appliesAddAndRemoveAndPreservesUnrelatedTags() {
  const QString nbId = createBundledNotebook(m_tempDir.filePath("tag_delta_nb"));
  QVERIFY(!nbId.isEmpty());

  QVERIFY(!m_notebookService->createFile(nbId, "", "note.md").isEmpty());
  QVERIFY(m_tagCoreService->createTag(nbId, "keep"));
  QVERIFY(m_tagCoreService->createTag(nbId, "drop"));
  QVERIFY(m_tagCoreService->createTag(nbId, "add"));
  QVERIFY(m_tagCoreService->tagFile(nbId, "note.md", "keep"));
  QVERIFY(m_tagCoreService->tagFile(nbId, "note.md", "drop"));

  // The body of NotebookNodeController::handleTagDeltaResult: incremental
  // tagFile/untagFile, never a whole-array updateFileTags rewrite.
  QVERIFY(m_tagService->tagFile(nbId, "note.md", "add"));
  QVERIFY(m_tagService->untagFile(nbId, "note.md", "drop"));

  const auto tagsArray =
      m_notebookService->getFileInfo(nbId, "note.md").value(QStringLiteral("tags")).toArray();
  QSet<QString> tags;
  for (const auto &v : tagsArray) {
    tags.insert(v.toString());
  }

  QVERIFY(tags.contains(QStringLiteral("add")));
  QVERIFY(!tags.contains(QStringLiteral("drop")));
  // The unrelated pre-existing tag survives — this is what a read-modify-write
  // of the whole tag array could not guarantee.
  QVERIFY(tags.contains(QStringLiteral("keep")));

  m_notebookService->closeNotebook(nbId);
}

// The load-bearing assumption behind planTagDelta: vxcore's incremental
// primitives are NOT idempotent. A delta derived from a Partial tag necessarily
// implies redundant ops (a tagFile on a file that already carries the tag, an
// untagFile on one that does not), and both report failure — which is why
// applyTagDelta FILTERS them out BEFORE dispatch instead of issuing them and
// trying to interpret the failure afterwards.
void TestNotebookNodeControllerTagTargets::
    tagPrimitivesAreNotIdempotent_soRedundantOpsMustBeFilteredBeforeDispatch() {
  const QString nbId = createBundledNotebook(m_tempDir.filePath("tag_idempotency_nb"));
  QVERIFY(!nbId.isEmpty());

  QVERIFY(!m_notebookService->createFile(nbId, "", "note.md").isEmpty());
  QVERIFY(m_tagCoreService->createTag(nbId, "present"));
  QVERIFY(m_tagCoreService->createTag(nbId, "absent"));
  QVERIFY(m_tagService->tagFile(nbId, "note.md", "present"));

  // Redundant add and redundant remove both report failure...
  QVERIFY(!m_tagService->tagFile(nbId, "note.md", "present"));
  QVERIFY(!m_tagService->untagFile(nbId, "note.md", "absent"));

  // ...yet the file is already in the requested end state in both cases, which
  // is exactly why planTagDelta() drops such ops before they are dispatched.
  const auto tagsArray =
      m_notebookService->getFileInfo(nbId, "note.md").value(QStringLiteral("tags")).toArray();
  QSet<QString> tags;
  for (const auto &v : tagsArray) {
    tags.insert(v.toString());
  }
  QVERIFY(tags.contains(QStringLiteral("present")));
  QVERIFY(!tags.contains(QStringLiteral("absent")));

  m_notebookService->closeNotebook(nbId);
}

} // namespace tests

QTEST_GUILESS_MAIN(tests::TestNotebookNodeControllerTagTargets)
#include "test_notebooknodecontroller_tagtargets.moc"
