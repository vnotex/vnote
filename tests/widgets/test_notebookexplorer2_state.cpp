// Tests for NotebookExplorer2 saveState()/restoreState() serialization protocol.
// We test the QDataStream wire format directly (without instantiating NotebookExplorer2)
// because the widget has heavy GUI dependencies (ThemeService, ConfigMgr2, etc.).

#include <QtTest>

#include <QByteArray>
#include <QDataStream>
#include <QHash>

#include <core/nodeidentifier.h>
#include <views/inodeexplorer.h>

using namespace vnotex;

namespace {

// Must match notebookexplorer2.cpp
const quint32 c_sessionVersion = 2;

// Mirrors NotebookExplorer2::ExploreMode
enum ExploreMode { Combined = 0, TwoColumns = 1 };

// Produce bytes identical to NotebookExplorer2::saveState().
QByteArray buildNewFormatBytes(int p_mode, const QByteArray &p_splitterState,
                               const QHash<QString, NodeExplorerState> &p_cache) {
  QByteArray buf;
  QDataStream out(&buf, QIODevice::WriteOnly);
  out << c_sessionVersion;
  out << p_mode;
  out << p_splitterState;
  out << p_cache;
  return buf;
}

// Produce old-format bytes (pre-version-2): just an int written as quint32.
QByteArray buildOldFormatBytes(int p_mode) {
  QByteArray buf;
  QDataStream out(&buf, QIODevice::WriteOnly);
  out << static_cast<quint32>(p_mode);
  return buf;
}

// Parse bytes the same way NotebookExplorer2::restoreState() does.
// Returns true on success.
struct ParsedState {
  int mode = 0;
  QByteArray splitterState;
  QHash<QString, NodeExplorerState> cache;
  bool cacheRead = false;
};

bool parseState(const QByteArray &p_data, ParsedState &p_out) {
  if (p_data.isEmpty()) {
    return false;
  }

  QDataStream stream(p_data);

  quint32 versionOrMode = 0;
  stream >> versionOrMode;

  bool readCache = false;
  if (versionOrMode == c_sessionVersion) {
    stream >> p_out.mode;
    readCache = true;
  } else {
    p_out.mode = static_cast<int>(versionOrMode);
  }

  stream >> p_out.splitterState;

  if (readCache) {
    QHash<QString, NodeExplorerState> cache;
    stream >> cache;
    if (stream.status() == QDataStream::Ok) {
      p_out.cache = cache;
      p_out.cacheRead = true;
    }
  }

  return true;
}

} // anonymous namespace

namespace tests {

class TestNotebookExplorer2State : public QObject {
  Q_OBJECT

private slots:
  void testRoundTrip_emptyCache();
  void testRoundTrip_withCache();
  void testOldFormatCompatibility();
  void testEmptyData();
  void testNodeExplorerStateStreamRoundTrip();
};

void TestNotebookExplorer2State::testRoundTrip_emptyCache() {
  QHash<QString, NodeExplorerState> emptyCache;
  QByteArray data = buildNewFormatBytes(Combined, QByteArray(), emptyCache);

  ParsedState parsed;
  QVERIFY(parseState(data, parsed));
  QCOMPARE(parsed.mode, static_cast<int>(Combined));
  QVERIFY(parsed.splitterState.isEmpty());
  QVERIFY(parsed.cacheRead);
  QVERIFY(parsed.cache.isEmpty());
}

void TestNotebookExplorer2State::testRoundTrip_withCache() {
  NodeIdentifier folder1;
  folder1.notebookId = QStringLiteral("nb-001");
  folder1.relativePath = QStringLiteral("docs");

  NodeIdentifier folder2;
  folder2.notebookId = QStringLiteral("nb-001");
  folder2.relativePath = QStringLiteral("docs/sub");

  NodeIdentifier current;
  current.notebookId = QStringLiteral("nb-001");
  current.relativePath = QStringLiteral("docs/sub/note.md");

  NodeExplorerState state;
  state.expandedFolders = {folder1, folder2};
  state.currentNodeId = current;
  // displayRootId left default (invalid)

  QHash<QString, NodeExplorerState> cache;
  cache.insert(QStringLiteral("nb-001"), state);

  QByteArray splitter("fake-splitter-bytes");
  QByteArray data = buildNewFormatBytes(TwoColumns, splitter, cache);

  ParsedState parsed;
  QVERIFY(parseState(data, parsed));
  QCOMPARE(parsed.mode, static_cast<int>(TwoColumns));
  QCOMPARE(parsed.splitterState, splitter);
  QVERIFY(parsed.cacheRead);
  QCOMPARE(parsed.cache.size(), 1);
  QVERIFY(parsed.cache.contains(QStringLiteral("nb-001")));

  const auto &restored = parsed.cache.value(QStringLiteral("nb-001"));
  QCOMPARE(restored.expandedFolders.size(), 2);
  QCOMPARE(restored.expandedFolders[0], folder1);
  QCOMPARE(restored.expandedFolders[1], folder2);
  QCOMPARE(restored.currentNodeId, current);
  QVERIFY(!restored.displayRootId.isValid());
}

void TestNotebookExplorer2State::testOldFormatCompatibility() {
  // Old format: just a quint32 representing the mode (value != c_sessionVersion).
  // Combined=0, TwoColumns=1. Since c_sessionVersion==2, values 0 and 1 are old-format.
  QByteArray data = buildOldFormatBytes(TwoColumns);

  ParsedState parsed;
  QVERIFY(parseState(data, parsed));
  QCOMPARE(parsed.mode, static_cast<int>(TwoColumns));
  QVERIFY(!parsed.cacheRead);
  QVERIFY(parsed.cache.isEmpty());
}

void TestNotebookExplorer2State::testEmptyData() {
  ParsedState parsed;
  QVERIFY(!parseState(QByteArray(), parsed));
}

void TestNotebookExplorer2State::testNodeExplorerStateStreamRoundTrip() {
  // Verify NodeExplorerState QDataStream operators work correctly in isolation.
  NodeIdentifier id1;
  id1.notebookId = QStringLiteral("abc");
  id1.relativePath = QStringLiteral("x/y.md");

  NodeExplorerState original;
  original.expandedFolders = {id1};
  original.currentNodeId = id1;
  original.displayRootId.notebookId = QStringLiteral("abc");
  original.displayRootId.relativePath = QStringLiteral("x");

  QByteArray buf;
  {
    QDataStream out(&buf, QIODevice::WriteOnly);
    out << original;
  }

  NodeExplorerState restored;
  {
    QDataStream in(buf);
    in >> restored;
  }

  QCOMPARE(restored.expandedFolders.size(), 1);
  QCOMPARE(restored.expandedFolders[0], id1);
  QCOMPARE(restored.currentNodeId, id1);
  QCOMPARE(restored.displayRootId.notebookId, QStringLiteral("abc"));
  QCOMPARE(restored.displayRootId.relativePath, QStringLiteral("x"));
}

} // namespace tests

QTEST_GUILESS_MAIN(tests::TestNotebookExplorer2State)
#include "test_notebookexplorer2_state.moc"
