// Tests for NotebookExplorer2 fs-watcher suppression API.
// Tests the expectFsChange / consumeExpectedFsChange mechanism that prevents
// the delayed fs-watcher reload from wiping selection after app-initiated
// folder/note/import operations.

#include <QtTest>

#include <vxcore/vxcore.h>

#include <core/nodeidentifier.h>

using namespace vnotex;

namespace tests {

class TestNotebookExplorer2FsSuppression : public QObject {
  Q_OBJECT

private slots:
  // S1.4: Expected-change registration auto-expires
  void testExpectationExpires();

  // S1.3: External fs change still triggers reload (suppression is selective)
  // Mock version testing the logic without full widget
  void testExternalChangeNotSuppressed();
};

// Minimal mock to test expectFsChange/consumeExpectedFsChange logic.
// We extract the core logic into inline test code since NotebookExplorer2
// is tightly coupled to GUI infrastructure.
class FsChangeSuppressor {
public:
  void expectFsChange(const QString &p_absolutePath, int p_windowMs = 3000) {
    if (p_absolutePath.isEmpty()) {
      return;
    }
    const QString key = QDir::cleanPath(QDir(p_absolutePath).absolutePath());
    const qint64 deadline = QDateTime::currentMSecsSinceEpoch() + qMax(p_windowMs, 0);
    m_expectedFsChangesDeadline.insert(key, deadline);
  }

  bool consumeExpectedFsChange(const QString &p_absolutePath) {
    if (m_expectedFsChangesDeadline.isEmpty()) {
      return false;
    }
    const qint64 now = QDateTime::currentMSecsSinceEpoch();

    // Opportunistic purge of expired entries.
    for (auto it = m_expectedFsChangesDeadline.begin(); it != m_expectedFsChangesDeadline.end();) {
      if (it.value() < now) {
        it = m_expectedFsChangesDeadline.erase(it);
      } else {
        ++it;
      }
    }

    const QString key = QDir::cleanPath(QDir(p_absolutePath).absolutePath());
    auto it = m_expectedFsChangesDeadline.find(key);
    if (it == m_expectedFsChangesDeadline.end()) {
      return false;
    }
    m_expectedFsChangesDeadline.erase(it);
    return true;
  }

private:
  QHash<QString, qint64> m_expectedFsChangesDeadline;
};

void TestNotebookExplorer2FsSuppression::testExpectationExpires() {
  // S1.4: Expected-change registration auto-expires
  FsChangeSuppressor suppressor;

  // Register with a short 100 ms window
  suppressor.expectFsChange(QStringLiteral("/some/path"), 100);

  // Immediate consume should succeed
  QVERIFY(suppressor.consumeExpectedFsChange(QStringLiteral("/some/path")));

  // Register again with short window
  suppressor.expectFsChange(QStringLiteral("/some/path"), 100);

  // Wait for expiry
  QTest::qWait(250);

  // Consume after expiry should fail (entry expired and purged)
  QVERIFY(!suppressor.consumeExpectedFsChange(QStringLiteral("/some/path")));
}

void TestNotebookExplorer2FsSuppression::testExternalChangeNotSuppressed() {
  // S1.3: External fs change still triggers reload (suppression is selective)
  FsChangeSuppressor suppressor;

  // Register expectation for /path1
  suppressor.expectFsChange(QStringLiteral("/path1"), 3000);

  // Consume a DIFFERENT path (external change on /path2)
  // Should return false because /path2 was never registered
  QVERIFY(!suppressor.consumeExpectedFsChange(QStringLiteral("/path2")));

  // The registered path should still be there
  QVERIFY(suppressor.consumeExpectedFsChange(QStringLiteral("/path1")));

  // After consuming, path1 should be gone
  QVERIFY(!suppressor.consumeExpectedFsChange(QStringLiteral("/path1")));
}

} // namespace tests

QTEST_GUILESS_MAIN(tests::TestNotebookExplorer2FsSuppression)
#include "test_notebookexplorer2_fs_suppression.moc"
