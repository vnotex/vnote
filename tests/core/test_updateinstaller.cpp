// Tests for UpdateInstaller: the journaled swap, rollback, and crash recovery
// that apply a staged incremental update to the install tree.
//
// Two flavors of interruption are exercised:
//
//   * In-process FAULT POINTS. UpdateInstaller::testSetFaultPoint makes the
//     apply return FaultInjected at a precise, durable checkpoint WITHOUT
//     rolling back -- exactly what a process kill looks like. Deterministic and
//     fast, so every state boundary can be walked.
//   * A real SUBPROCESS that is hard-terminated mid-apply (see runChildMode).
//     Slower, but it proves the durability story holds across an actual process
//     death rather than a simulated one.
//
// This file defines its own main() so the same executable can run as the child.

#include <QtTest>

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QScopedPointer>
#include <QTemporaryDir>

#include <cstdio>
#include <cstdlib>

#include <core/updateinstaller.h>
#include <core/updatemanifest.h>

using namespace vnotex;

namespace tests {

namespace {

using FileMap = QVector<QPair<QString, QByteArray>>;

QString sha256Of(const QByteArray &p_bytes) {
  return QString::fromLatin1(
      QCryptographicHash::hash(p_bytes, QCryptographicHash::Sha256).toHex());
}

bool writeFileAt(const QString &p_path, const QByteArray &p_bytes) {
  QDir().mkpath(QFileInfo(p_path).absolutePath());
  QFile f(p_path);
  if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
    return false;
  }
  const bool ok = f.write(p_bytes) == p_bytes.size() && f.flush();
  f.close();
  return ok;
}

QByteArray readFileAt(const QString &p_path) {
  QFile f(p_path);
  if (!f.open(QIODevice::ReadOnly)) {
    return QByteArray();
  }
  return f.readAll();
}

QJsonObject buildManifest(const QString &p_version, const FileMap &p_files) {
  QJsonObject obj;
  obj[QStringLiteral("schema")] = 1;
  obj[QStringLiteral("product")] = QStringLiteral("VNote");
  obj[QStringLiteral("channel")] = QStringLiteral("stable");
  obj[QStringLiteral("version")] = p_version;
  obj[QStringLiteral("variant")] = QStringLiteral("win64");
  obj[QStringLiteral("platform")] = QStringLiteral("windows-x64");
  obj[QStringLiteral("commit")] = QStringLiteral("deadbeef");
  obj[QStringLiteral("generatedAt")] = QStringLiteral("2026-08-01T12:00:00Z");

  QJsonArray files;
  for (const auto &entry : p_files) {
    QJsonObject f;
    f[QStringLiteral("path")] = entry.first;
    f[QStringLiteral("size")] = static_cast<double>(entry.second.size());
    f[QStringLiteral("sha256")] = sha256Of(entry.second);
    files.append(f);
  }
  obj[QStringLiteral("files")] = files;
  return obj;
}

// Every FILE under p_root, relative + sorted, excluding the updater's own
// bookkeeping directories.
QStringList treeSnapshot(const QString &p_root) {
  QStringList out;
  QDirIterator it(p_root, QDir::Files | QDir::Hidden, QDirIterator::Subdirectories);
  while (it.hasNext()) {
    const QString relative = QDir(p_root).relativeFilePath(it.next());
    if (relative.startsWith(QStringLiteral(".vnote-update")) ||
        relative.startsWith(QStringLiteral(".vnote-old"))) {
      continue;
    }
    out.append(relative);
  }
  out.sort();
  return out;
}

// Path -> content hash for the whole install tree, so a rollback can be
// asserted to be byte-exact rather than merely path-exact.
QMap<QString, QString> treeHashes(const QString &p_root) {
  QMap<QString, QString> out;
  for (const QString &relative : treeSnapshot(p_root)) {
    out.insert(relative, sha256Of(readFileAt(p_root + QLatin1Char('/') + relative)));
  }
  return out;
}

const char *const c_modeApplyFault = "--apply-fault";

UpdateInstaller::FaultPoint faultPointFromName(const QString &p_name) {
  if (p_name == QLatin1String("AfterIntentCommit")) {
    return UpdateInstaller::FaultPoint::AfterIntentCommit;
  }
  if (p_name == QLatin1String("AfterBackupSyscall")) {
    return UpdateInstaller::FaultPoint::AfterBackupSyscall;
  }
  if (p_name == QLatin1String("AfterStagedToTargetMove")) {
    return UpdateInstaller::FaultPoint::AfterStagedToTargetMove;
  }
  if (p_name == QLatin1String("AfterDoneCommit")) {
    return UpdateInstaller::FaultPoint::AfterDoneCommit;
  }
  if (p_name == QLatin1String("BeforeManifestCommit")) {
    return UpdateInstaller::FaultPoint::BeforeManifestCommit;
  }
  if (p_name == QLatin1String("AfterManifestCommit")) {
    return UpdateInstaller::FaultPoint::AfterManifestCommit;
  }
  if (p_name == QLatin1String("ExecAfterIntentCommit")) {
    return UpdateInstaller::FaultPoint::ExecAfterIntentCommit;
  }
  if (p_name == QLatin1String("ExecAfterMoveAside")) {
    return UpdateInstaller::FaultPoint::ExecAfterMoveAside;
  }
  if (p_name == QLatin1String("ExecAfterMoveIn")) {
    return UpdateInstaller::FaultPoint::ExecAfterMoveIn;
  }
  return UpdateInstaller::FaultPoint::None;
}

} // namespace

// Child entry point: apply a pending update with a fault armed, then die
// WITHOUT unwinding, exactly like a kill -9 in the middle of the swap.
int runChildMode(int argc, char *argv[]) {
  if (argc < 4 || QString::fromLocal8Bit(argv[1]) != QLatin1String(c_modeApplyFault)) {
    return -1;
  }

  const QString installDir = QString::fromLocal8Bit(argv[2]);
  const QString faultName = QString::fromLocal8Bit(argv[3]);
  const int opIndex = argc >= 5 ? QString::fromLocal8Bit(argv[4]).toInt() : -1;

  UpdateInstaller::testSetRetryBackoffEnabled(false);
  UpdateInstaller::testSetFaultPoint(faultPointFromName(faultName), opIndex);
  UpdateInstaller::applyPending(installDir);

  // _Exit: no destructors, no atexit handlers, no flushing. The only thing that
  // survives is what was already durable on disk.
  ::_Exit(9);
}

class TestUpdateInstaller : public QObject {
  Q_OBJECT

private slots:
  void init();
  void cleanup();

  // Probes.
  void testModulePathHelpers();
  void testWritableProbe();
  void testProgramFilesDetection();
  void testSameVolume();
  void testCapabilityProbe();
  void testCapabilityProbeFailureIsHonored();
  void testWaitForWebEngineChildrenIgnoresForeignProcesses();

  // Plan / result persistence.
  void testPendingPlanRoundTrip();
  void testPendingPlanRejectsUnsafePaths();
  void testResultRoundTrip();

  // Happy path.
  void testSwapReplaceAddDelete();
  void testBackupContainsOriginals();
  void testManifestIsRewritten();
  void testAddsCreateDirectories();
  void testEmptyDirectoriesArePruned();
  void testExecutableIsReplacedViaTwoRenames();
  void testStagingRootRemovedOnCommit();

  // Path-type transitions.
  void testFileBecomesDirectory();
  void testDirectoryBecomesFile();

  // Preflight rejection (nothing may change).
  void testMissingStagedFileAbortsBeforeMutation();
  void testTamperedStagedFileAbortsBeforeMutation();
  void testNoPendingUpdate();

  // Rollback.
  void testForcedRenameFailureRollsBackToIdenticalTree();
  void testBlockingDeletionFailureRollsBack();
  void testCreatedDirectoriesRemovedOnRollback();
  void testRolledBackUpdateStaysRetryable();
  void testRollbackReplayAfterCrashMidReverse();
  void testRecoveryTextListsAddedPaths();
  void testRecoveryTextHandlesFileToDirectoryTransition();
  void testRecoveryTextHandlesDirectoryToFileTransition();
  void testAbortsWhenRecoveryTextCannotBeWritten();

  // Fault injection + recovery.
  void testRecoveryConvergesAfterFault();
  void testRecoveryConvergesAfterFault_data();
  void testCorruptJournalIsConservative();
  void testRecoveryIsIdempotent();

  // ReplaceExecutable recovery state table.
  void testExecutableRecoveryFromKilledChild();
  void testExecutableRecoveryFromKilledChild_data();
  void testExecutableRecoveryFromMoveAsideWindow();
  void testExecutableRecoveryWhenBackupIsLost();
  void testExecutableRevertsWhenStagedIsLost();
  void testExecutableUnrecoverable();

  // Backups.
  void testCleanupOldBackupsRemovesTerminalTransactions();
  void testCleanupOldBackupsKeepsUnfinishedTransactions();

private:
  QString installDir() const { return m_dir->path(); }
  QString at(const QString &p_relative) const {
    return installDir() + QLatin1Char('/') + p_relative;
  }
  QString stagedAt(const QString &p_relative) const {
    return UpdateInstaller::stagedDir(installDir()) + QLatin1Char('/') + p_relative;
  }

  // Lays down the "old" install tree.
  void seedInstall(const FileMap &p_files);

  // Lays down the staged tree and writes pending.json. p_targetFiles is the
  // FULL target manifest content (not just the staged subset).
  void seedPending(const FileMap &p_targetFiles, const QStringList &p_staged,
                   const QStringList &p_deletions = QStringList(),
                   const QStringList &p_conflicts = QStringList(),
                   const QString &p_executable = QStringLiteral("app.exe"));

  QProcess *runChildApply(const QString &p_faultName, int p_opIndex = -1);

  QScopedPointer<QTemporaryDir> m_dir;
  FileMap m_targetFiles;
};

void TestUpdateInstaller::init() {
  m_dir.reset(new QTemporaryDir());
  QVERIFY(m_dir->isValid());
  m_targetFiles.clear();
  UpdateInstaller::testClearFaultPoint();
  UpdateInstaller::testSetForcedRenameFailurePath(QString());
  UpdateInstaller::testSetForceAtomicRenameUnsupported(false);
  UpdateInstaller::testSetRetryBackoffEnabled(false);
}

void TestUpdateInstaller::cleanup() {
  UpdateInstaller::testClearFaultPoint();
  UpdateInstaller::testSetForcedRenameFailurePath(QString());
  UpdateInstaller::testSetForceAtomicRenameUnsupported(false);
  UpdateInstaller::testSetRetryBackoffEnabled(true);
  m_dir.reset();
}

void TestUpdateInstaller::seedInstall(const FileMap &p_files) {
  for (const auto &entry : p_files) {
    QVERIFY(writeFileAt(at(entry.first), entry.second));
  }
}

void TestUpdateInstaller::seedPending(const FileMap &p_targetFiles, const QStringList &p_staged,
                                      const QStringList &p_deletions,
                                      const QStringList &p_conflicts,
                                      const QString &p_executable) {
  m_targetFiles = p_targetFiles;

  QHash<QString, QByteArray> byPath;
  for (const auto &entry : p_targetFiles) {
    byPath.insert(UpdateManifest::pathKey(entry.first), entry.second);
  }

  for (const QString &relative : p_staged) {
    const QByteArray content = byPath.value(UpdateManifest::pathKey(relative));
    QVERIFY2(!content.isNull(),
             qPrintable(QStringLiteral("staged path '%1' is not in the target manifest")
                            .arg(relative)));
    QVERIFY(writeFileAt(stagedAt(relative), content));
  }

  UpdateInstaller::PendingPlan plan;
  plan.targetVersion = QStringLiteral("4.3.2");
  plan.variant = QStringLiteral("win64");
  plan.executablePath = p_executable;
  plan.staged = p_staged;
  plan.deletions = p_deletions;
  plan.conflicts = p_conflicts;
  plan.targetManifest = buildManifest(QStringLiteral("4.3.2"), p_targetFiles);

  QVERIFY(UpdateInstaller::writePending(installDir(), plan));
}

QProcess *TestUpdateInstaller::runChildApply(const QString &p_faultName, int p_opIndex) {
  auto *child = new QProcess(this);
  child->setProgram(QCoreApplication::applicationFilePath());
  child->setArguments(QStringList{QString::fromLatin1(c_modeApplyFault), installDir(), p_faultName,
                                  QString::number(p_opIndex)});
  child->start();
  return child;
}

// ================================================================== probes

void TestUpdateInstaller::testModulePathHelpers() {
  const QString exe = UpdateInstaller::exePathFromModulePath();
  QVERIFY2(!exe.isEmpty(), "module path must resolve without a QCoreApplication");
  QVERIFY(QFileInfo::exists(exe));
  // Deliberately NOT applicationFilePath(), but it must agree with it while a
  // QCoreApplication happens to exist.
  QCOMPARE(QFileInfo(exe).canonicalFilePath(),
           QFileInfo(QCoreApplication::applicationFilePath()).canonicalFilePath());

  QCOMPARE(QDir::cleanPath(UpdateInstaller::installDirFromModulePath()),
           QDir::cleanPath(QFileInfo(exe).absolutePath()));
}

void TestUpdateInstaller::testWritableProbe() {
  QVERIFY(UpdateInstaller::isInstallDirWritable(installDir()));
  // The probe must not leave anything behind.
  QVERIFY(treeSnapshot(installDir()).isEmpty());

  QVERIFY(!UpdateInstaller::isInstallDirWritable(at(QStringLiteral("does-not-exist"))));
  QVERIFY(!UpdateInstaller::isInstallDirWritable(QString()));
}

void TestUpdateInstaller::testProgramFilesDetection() {
#ifdef Q_OS_WIN
  const QByteArray pf = qgetenv("ProgramFiles");
  QVERIFY2(!pf.isEmpty(), "ProgramFiles is not set");
  const QString root = QDir::fromNativeSeparators(QString::fromLocal8Bit(pf));

  QVERIFY(UpdateInstaller::isUnderProgramFiles(root));
  QVERIFY(UpdateInstaller::isUnderProgramFiles(root + QStringLiteral("/VNote")));
  // Case-insensitive, like the filesystem.
  QVERIFY(UpdateInstaller::isUnderProgramFiles(root.toUpper() + QStringLiteral("/VNote")));
  // A sibling that merely shares a prefix is NOT under it.
  QVERIFY(!UpdateInstaller::isUnderProgramFiles(root + QStringLiteral("Extra/VNote")));
  QVERIFY(!UpdateInstaller::isUnderProgramFiles(installDir()));
#else
  QSKIP("Program Files detection is Windows-only");
#endif
}

void TestUpdateInstaller::testSameVolume() {
  QVERIFY(UpdateInstaller::isSameVolume(installDir(), UpdateInstaller::stagingRoot(installDir())));
  // Works for a path that does not exist yet, by walking up to a real ancestor.
  QVERIFY(UpdateInstaller::isSameVolume(installDir(), at(QStringLiteral("a/b/c/d"))));
  QVERIFY(!UpdateInstaller::isSameVolume(installDir(), QString()));
}

void TestUpdateInstaller::testCapabilityProbe() {
  // The probe must give a definitive answer and must not leave the probe
  // directory behind. On any developer/CI machine running a modern Windows on
  // NTFS this is expected to succeed, but the assertion here is about hygiene
  // and determinism, not about the platform's answer.
  const bool first = UpdateInstaller::probeAtomicRenameSupport(installDir());
  const bool second = UpdateInstaller::probeAtomicRenameSupport(installDir());
  QCOMPARE(first, second);
  QVERIFY(!QFileInfo::exists(UpdateInstaller::stagingRoot(installDir()) +
                             QStringLiteral("/probe")));

#ifdef Q_OS_WIN
  QVERIFY2(first, "POSIX-semantics rename should be available on this host; if this fails the "
                  "install is legitimately ineligible for in-process self-update");
#endif
}

void TestUpdateInstaller::testCapabilityProbeFailureIsHonored() {
  UpdateInstaller::testSetForceAtomicRenameUnsupported(true);
  QVERIFY2(!UpdateInstaller::probeAtomicRenameSupport(installDir()),
           "a host without POSIX-semantics rename must be reported ineligible");
}

void TestUpdateInstaller::testWaitForWebEngineChildrenIgnoresForeignProcesses() {
  // This test process may well have child processes (other test helpers), but
  // none of them run out of the fake install directory, so they must not be
  // waited on. Matching by image path is the whole point.
  const auto result = UpdateInstaller::waitForWebEngineChildren(installDir(), 2000);
  QVERIFY2(result == UpdateInstaller::WaitResult::NoChildren,
           qPrintable(UpdateInstaller::waitResultToString(result)));
}

// ========================================================= plan persistence

void TestUpdateInstaller::testPendingPlanRoundTrip() {
  UpdateInstaller::PendingPlan plan;
  plan.targetVersion = QStringLiteral("4.3.2");
  plan.variant = QStringLiteral("win64");
  plan.executablePath = QStringLiteral("app.exe");
  plan.staged = QStringList{QStringLiteral("app.exe"), QStringLiteral("a/b.dll")};
  plan.deletions = QStringList{QStringLiteral("old.dll")};
  plan.conflicts = QStringList{QStringLiteral("conflict")};
  plan.targetManifest =
      buildManifest(QStringLiteral("4.3.2"), FileMap{{QStringLiteral("app.exe"), "X"}});
  QVERIFY(plan.isValid());

  QVERIFY(UpdateInstaller::writePending(installDir(), plan));

  QString error;
  const auto loaded = UpdateInstaller::readPending(installDir(), &error);
  QVERIFY2(loaded.isValid(), qPrintable(error));
  QCOMPARE(loaded.targetVersion, plan.targetVersion);
  QCOMPARE(loaded.variant, plan.variant);
  QCOMPARE(loaded.executablePath, plan.executablePath);
  QCOMPARE(loaded.staged, plan.staged);
  QCOMPARE(loaded.deletions, plan.deletions);
  QCOMPARE(loaded.conflicts, plan.conflicts);
  QCOMPARE(loaded.targetManifest, plan.targetManifest);

  QVERIFY(UpdateInstaller::clearPending(installDir()));
  QVERIFY(!UpdateInstaller::readPending(installDir()).isValid());
}

void TestUpdateInstaller::testPendingPlanRejectsUnsafePaths() {
  UpdateInstaller::PendingPlan plan;
  plan.targetVersion = QStringLiteral("4.3.2");
  plan.variant = QStringLiteral("win64");
  plan.executablePath = QStringLiteral("app.exe");
  plan.staged = QStringList{QStringLiteral("app.exe")};
  plan.targetManifest =
      buildManifest(QStringLiteral("4.3.2"), FileMap{{QStringLiteral("app.exe"), "X"}});

  auto mutate = [&](const QString &p_key, const QString &p_value) {
    QJsonObject obj = plan.toJson();
    obj[p_key] = QJsonArray{p_value};
    QString error;
    const auto parsed = UpdateInstaller::PendingPlan::fromJson(obj, &error);
    QVERIFY2(!parsed.isValid(), qPrintable(QStringLiteral("%1=%2 was accepted").arg(p_key, p_value)));
  };

  mutate(QStringLiteral("staged"), QStringLiteral("../escape.dll"));
  mutate(QStringLiteral("staged"), QStringLiteral("C:/windows/evil.dll"));
  mutate(QStringLiteral("staged"), QStringLiteral(".vnote-update/staged/x.dll"));
  mutate(QStringLiteral("deletions"), QStringLiteral("../../boot.ini"));
  mutate(QStringLiteral("conflicts"), QStringLiteral("/etc/passwd"));

  // An unsafe executablePath is fatal too.
  QJsonObject obj = plan.toJson();
  obj[QStringLiteral("executablePath")] = QStringLiteral("../app.exe");
  QVERIFY(!UpdateInstaller::PendingPlan::fromJson(obj).isValid());

  // Wrong schema.
  obj = plan.toJson();
  obj[QStringLiteral("schema")] = 99;
  QVERIFY(!UpdateInstaller::PendingPlan::fromJson(obj).isValid());
}

void TestUpdateInstaller::testResultRoundTrip() {
  QVERIFY(!UpdateInstaller::readResult(installDir()).isValid());

  QVERIFY(UpdateInstaller::writeRetryableResult(installDir(), QStringLiteral("busy")));
  auto stored = UpdateInstaller::readResult(installDir());
  QVERIFY(stored.isValid());
  QCOMPARE(stored.outcome, UpdateInstaller::ResultOutcome::Retryable);
  QCOMPARE(stored.reason, QStringLiteral("busy"));
  QVERIFY(!stored.timestamp.isEmpty());

  QVERIFY(UpdateInstaller::writeSpawnFailure(installDir()));
  stored = UpdateInstaller::readResult(installDir());
  QCOMPARE(stored.outcome, UpdateInstaller::ResultOutcome::SpawnFailed);

  QVERIFY(UpdateInstaller::clearResult(installDir()));
  QVERIFY(!UpdateInstaller::readResult(installDir()).isValid());
}

// ============================================================== happy path

void TestUpdateInstaller::testSwapReplaceAddDelete() {
  seedInstall(FileMap{{QStringLiteral("app.exe"), "OLD-EXE"},
                      {QStringLiteral("keep.dll"), "KEEP"},
                      {QStringLiteral("changed.dll"), "OLD"},
                      {QStringLiteral("obsolete/gone.dll"), "GONE"}});

  seedPending(FileMap{{QStringLiteral("app.exe"), "NEW-EXE"},
                      {QStringLiteral("keep.dll"), "KEEP"},
                      {QStringLiteral("changed.dll"), "NEW"},
                      {QStringLiteral("added/fresh.dll"), "FRESH"}},
              QStringList{QStringLiteral("app.exe"), QStringLiteral("changed.dll"),
                          QStringLiteral("added/fresh.dll")},
              QStringList{QStringLiteral("obsolete/gone.dll")});

  const auto result = UpdateInstaller::applyPending(installDir());
  QVERIFY2(result.isCommitted(), qPrintable(result.message));
  QCOMPARE(result.targetVersion, QStringLiteral("4.3.2"));

  QCOMPARE(treeSnapshot(installDir()),
           (QStringList{QStringLiteral("added/fresh.dll"), QStringLiteral("app.exe"),
                        QStringLiteral("changed.dll"), QStringLiteral("keep.dll"),
                        QStringLiteral("manifest.json")}));
  QCOMPARE(readFileAt(at(QStringLiteral("app.exe"))), QByteArrayLiteral("NEW-EXE"));
  QCOMPARE(readFileAt(at(QStringLiteral("changed.dll"))), QByteArrayLiteral("NEW"));
  QCOMPARE(readFileAt(at(QStringLiteral("added/fresh.dll"))), QByteArrayLiteral("FRESH"));
  QCOMPARE(readFileAt(at(QStringLiteral("keep.dll"))), QByteArrayLiteral("KEEP"));
  QVERIFY(!QFileInfo::exists(at(QStringLiteral("obsolete/gone.dll"))));
}

void TestUpdateInstaller::testBackupContainsOriginals() {
  seedInstall(FileMap{{QStringLiteral("app.exe"), "OLD-EXE"},
                      {QStringLiteral("changed.dll"), "OLD"},
                      {QStringLiteral("obsolete/gone.dll"), "GONE"}});
  seedPending(FileMap{{QStringLiteral("app.exe"), "NEW-EXE"},
                      {QStringLiteral("changed.dll"), "NEW"}},
              QStringList{QStringLiteral("app.exe"), QStringLiteral("changed.dll")},
              QStringList{QStringLiteral("obsolete/gone.dll")});

  QVERIFY(UpdateInstaller::applyPending(installDir()).isCommitted());

  const QString backupRoot = UpdateInstaller::backupRoot(installDir());
  const QFileInfoList stamps =
      QDir(backupRoot).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden);
  QCOMPARE(stamps.size(), 1);
  const QString backup = stamps.first().absoluteFilePath();

  // The originals, laid out exactly like the install directory.
  QCOMPARE(readFileAt(backup + QStringLiteral("/app.exe")), QByteArrayLiteral("OLD-EXE"));
  QCOMPARE(readFileAt(backup + QStringLiteral("/changed.dll")), QByteArrayLiteral("OLD"));
  QCOMPARE(readFileAt(backup + QStringLiteral("/obsolete/gone.dll")), QByteArrayLiteral("GONE"));

  // Plus the manual-recovery instructions demanded by residual risk 1.
  const QByteArray recovery = readFileAt(backup + QStringLiteral("/RECOVERY.txt"));
  QVERIFY(!recovery.isEmpty());
  QVERIFY(recovery.contains("4.3.2"));
}

void TestUpdateInstaller::testManifestIsRewritten() {
  seedInstall(FileMap{{QStringLiteral("app.exe"), "OLD-EXE"}});
  writeFileAt(at(UpdateManifest::manifestFileName()),
              QJsonDocument(buildManifest(QStringLiteral("4.3.1"),
                                          FileMap{{QStringLiteral("app.exe"), "OLD-EXE"}}))
                  .toJson());

  seedPending(FileMap{{QStringLiteral("app.exe"), "NEW-EXE"}},
              QStringList{QStringLiteral("app.exe")});

  QVERIFY(UpdateInstaller::applyPending(installDir()).isCommitted());

  QString error;
  const auto committed = UpdateManifest::fromJsonBytes(
      readFileAt(at(UpdateManifest::manifestFileName())), &error);
  QVERIFY2(committed.isValid(), qPrintable(error));
  QCOMPARE(committed.version(), QStringLiteral("4.3.2"));
  UpdateManifestFile entry;
  QVERIFY(committed.lookup(QStringLiteral("app.exe"), &entry));
  QCOMPARE(entry.sha256, sha256Of(QByteArrayLiteral("NEW-EXE")));
}

void TestUpdateInstaller::testAddsCreateDirectories() {
  seedInstall(FileMap{{QStringLiteral("app.exe"), "OLD-EXE"}});
  seedPending(FileMap{{QStringLiteral("app.exe"), "OLD-EXE"},
                      {QStringLiteral("a/b/c/deep.dll"), "DEEP"}},
              QStringList{QStringLiteral("a/b/c/deep.dll")});

  QVERIFY(UpdateInstaller::applyPending(installDir()).isCommitted());
  QCOMPARE(readFileAt(at(QStringLiteral("a/b/c/deep.dll"))), QByteArrayLiteral("DEEP"));
}

void TestUpdateInstaller::testEmptyDirectoriesArePruned() {
  seedInstall(FileMap{{QStringLiteral("app.exe"), "OLD-EXE"},
                      {QStringLiteral("dead/nested/leaf.dll"), "LEAF"}});
  seedPending(FileMap{{QStringLiteral("app.exe"), "OLD-EXE"}}, QStringList(),
              QStringList{QStringLiteral("dead/nested/leaf.dll")});

  QVERIFY(UpdateInstaller::applyPending(installDir()).isCommitted());
  QVERIFY(!QFileInfo::exists(at(QStringLiteral("dead/nested/leaf.dll"))));
  QVERIFY2(!QFileInfo::exists(at(QStringLiteral("dead/nested"))),
           "an emptied directory must be pruned bottom-up");
  QVERIFY(!QFileInfo::exists(at(QStringLiteral("dead"))));
}

void TestUpdateInstaller::testExecutableIsReplacedViaTwoRenames() {
  seedInstall(FileMap{{QStringLiteral("app.exe"), "OLD-EXE"}});
  seedPending(FileMap{{QStringLiteral("app.exe"), "NEW-EXE"}},
              QStringList{QStringLiteral("app.exe")});

  QVERIFY(UpdateInstaller::applyPending(installDir()).isCommitted());
  QCOMPARE(readFileAt(at(QStringLiteral("app.exe"))), QByteArrayLiteral("NEW-EXE"));

  // The OLD image is preserved COMPLETE in the backup, because step 1 of the
  // swap moved it there rather than overwriting it in place.
  const QFileInfoList stamps =
      QDir(UpdateInstaller::backupRoot(installDir()))
          .entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden);
  QCOMPARE(stamps.size(), 1);
  QCOMPARE(readFileAt(stamps.first().absoluteFilePath() + QStringLiteral("/app.exe")),
           QByteArrayLiteral("OLD-EXE"));

  // The staged copy is consumed, not left behind as a duplicate.
  QVERIFY(!QFileInfo::exists(stagedAt(QStringLiteral("app.exe"))));
}

void TestUpdateInstaller::testStagingRootRemovedOnCommit() {
  seedInstall(FileMap{{QStringLiteral("app.exe"), "OLD-EXE"}});
  seedPending(FileMap{{QStringLiteral("app.exe"), "NEW-EXE"}},
              QStringList{QStringLiteral("app.exe")});

  QVERIFY(UpdateInstaller::applyPending(installDir()).isCommitted());

  QVERIFY(!QFileInfo::exists(UpdateInstaller::stagedDir(installDir())));
  QVERIFY(!QFileInfo::exists(UpdateInstaller::pendingPath(installDir())));
  QVERIFY(!QFileInfo::exists(UpdateInstaller::journalPath(installDir())));

  // result.json is deliberately the ONLY survivor, so the next launch can turn
  // the outcome into a notification.
  const auto stored = UpdateInstaller::readResult(installDir());
  QCOMPARE(stored.outcome, UpdateInstaller::ResultOutcome::Applied);
  QCOMPARE(stored.targetVersion, QStringLiteral("4.3.2"));
}

// ================================================== path-type transitions

void TestUpdateInstaller::testFileBecomesDirectory() {
  // "plugins" ships as a FILE today and must become a DIRECTORY.
  seedInstall(FileMap{{QStringLiteral("app.exe"), "OLD-EXE"},
                      {QStringLiteral("plugins"), "I-AM-A-FILE"}});
  seedPending(FileMap{{QStringLiteral("app.exe"), "OLD-EXE"},
                      {QStringLiteral("plugins/real.dll"), "PLUGIN"}},
              QStringList{QStringLiteral("plugins/real.dll")},
              QStringList{QStringLiteral("plugins")});

  const auto result = UpdateInstaller::applyPending(installDir());
  QVERIFY2(result.isCommitted(), qPrintable(result.message));
  QVERIFY(QFileInfo(at(QStringLiteral("plugins"))).isDir());
  QCOMPARE(readFileAt(at(QStringLiteral("plugins/real.dll"))), QByteArrayLiteral("PLUGIN"));
}

void TestUpdateInstaller::testDirectoryBecomesFile() {
  // "legacy" is a DIRECTORY today and must become a FILE.
  seedInstall(FileMap{{QStringLiteral("app.exe"), "OLD-EXE"},
                      {QStringLiteral("legacy/inner.dll"), "INNER"}});
  seedPending(FileMap{{QStringLiteral("app.exe"), "OLD-EXE"},
                      {QStringLiteral("legacy"), "I-AM-A-FILE-NOW"}},
              QStringList{QStringLiteral("legacy")});

  const auto result = UpdateInstaller::applyPending(installDir());
  QVERIFY2(result.isCommitted(), qPrintable(result.message));
  QVERIFY(QFileInfo(at(QStringLiteral("legacy"))).isFile());
  QCOMPARE(readFileAt(at(QStringLiteral("legacy"))), QByteArrayLiteral("I-AM-A-FILE-NOW"));

  // The displaced directory survives in the backup.
  const QFileInfoList stamps =
      QDir(UpdateInstaller::backupRoot(installDir()))
          .entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden);
  QCOMPARE(stamps.size(), 1);
  QCOMPARE(readFileAt(stamps.first().absoluteFilePath() + QStringLiteral("/legacy/inner.dll")),
           QByteArrayLiteral("INNER"));
}

// ================================================== preflight rejection

void TestUpdateInstaller::testMissingStagedFileAbortsBeforeMutation() {
  seedInstall(FileMap{{QStringLiteral("app.exe"), "OLD-EXE"},
                      {QStringLiteral("changed.dll"), "OLD"}});
  seedPending(FileMap{{QStringLiteral("app.exe"), "OLD-EXE"},
                      {QStringLiteral("changed.dll"), "NEW"}},
              QStringList{QStringLiteral("changed.dll")});

  const auto before = treeHashes(installDir());
  QVERIFY(QFile::remove(stagedAt(QStringLiteral("changed.dll"))));

  const auto result = UpdateInstaller::applyPending(installDir());
  QCOMPARE(result.status, UpdateInstaller::ApplyStatus::AbortedBeforeMutation);
  QCOMPARE(treeHashes(installDir()), before);
  QVERIFY2(!QFileInfo::exists(UpdateInstaller::journalPath(installDir())),
           "aborting before mutation must not create a journal");
  QCOMPARE(UpdateInstaller::readResult(installDir()).outcome,
           UpdateInstaller::ResultOutcome::Retryable);
}

void TestUpdateInstaller::testTamperedStagedFileAbortsBeforeMutation() {
  seedInstall(FileMap{{QStringLiteral("app.exe"), "OLD-EXE"},
                      {QStringLiteral("changed.dll"), "OLD"}});
  seedPending(FileMap{{QStringLiteral("app.exe"), "OLD-EXE"},
                      {QStringLiteral("changed.dll"), "NEW"}},
              QStringList{QStringLiteral("changed.dll")});

  const auto before = treeHashes(installDir());
  QVERIFY(writeFileAt(stagedAt(QStringLiteral("changed.dll")), QByteArrayLiteral("TAMPERED")));

  const auto result = UpdateInstaller::applyPending(installDir());
  QCOMPARE(result.status, UpdateInstaller::ApplyStatus::AbortedBeforeMutation);
  QCOMPARE(treeHashes(installDir()), before);
  QVERIFY(!QFileInfo::exists(UpdateInstaller::journalPath(installDir())));
}

void TestUpdateInstaller::testNoPendingUpdate() {
  seedInstall(FileMap{{QStringLiteral("app.exe"), "OLD-EXE"}});
  const auto result = UpdateInstaller::applyPending(installDir());
  QCOMPARE(result.status, UpdateInstaller::ApplyStatus::NoPendingUpdate);
  QCOMPARE(treeSnapshot(installDir()), QStringList{QStringLiteral("app.exe")});
}

// ================================================================ rollback

void TestUpdateInstaller::testForcedRenameFailureRollsBackToIdenticalTree() {
  seedInstall(FileMap{{QStringLiteral("app.exe"), "OLD-EXE"},
                      {QStringLiteral("a.dll"), "OLD-A"},
                      {QStringLiteral("b.dll"), "OLD-B"},
                      {QStringLiteral("c.dll"), "OLD-C"}});
  seedPending(FileMap{{QStringLiteral("app.exe"), "NEW-EXE"},
                      {QStringLiteral("a.dll"), "NEW-A"},
                      {QStringLiteral("b.dll"), "NEW-B"},
                      {QStringLiteral("c.dll"), "NEW-C"}},
              QStringList{QStringLiteral("a.dll"), QStringLiteral("b.dll"),
                          QStringLiteral("c.dll"), QStringLiteral("app.exe")});

  const auto before = treeHashes(installDir());

  // Simulate an AV scanner holding a handle on b.dll for the whole apply.
  UpdateInstaller::testSetForcedRenameFailurePath(QStringLiteral("b.dll"));

  const auto result = UpdateInstaller::applyPending(installDir());
  QCOMPARE(result.status, UpdateInstaller::ApplyStatus::RolledBack);

  QVERIFY2(treeHashes(installDir()) == before,
           "a rolled back transaction must leave the tree byte-for-byte identical");
  QCOMPARE(UpdateInstaller::readResult(installDir()).outcome,
           UpdateInstaller::ResultOutcome::Failed);
}

void TestUpdateInstaller::testBlockingDeletionFailureRollsBack() {
  // Deletions are BLOCKING: a stale DLL that survived would also vanish from
  // the new manifest and could never be cleaned up later, so a failed deletion
  // must roll the whole transaction back rather than be skipped.
  seedInstall(FileMap{{QStringLiteral("app.exe"), "OLD-EXE"},
                      {QStringLiteral("a.dll"), "OLD-A"},
                      {QStringLiteral("stale.dll"), "STALE"}});
  seedPending(FileMap{{QStringLiteral("app.exe"), "OLD-EXE"}, {QStringLiteral("a.dll"), "NEW-A"}},
              QStringList{QStringLiteral("a.dll")}, QStringList{QStringLiteral("stale.dll")});

  const auto before = treeHashes(installDir());
  UpdateInstaller::testSetForcedRenameFailurePath(QStringLiteral("stale.dll"));

  const auto result = UpdateInstaller::applyPending(installDir());
  QCOMPARE(result.status, UpdateInstaller::ApplyStatus::RolledBack);
  QCOMPARE(treeHashes(installDir()), before);
  QCOMPARE(readFileAt(at(QStringLiteral("stale.dll"))), QByteArrayLiteral("STALE"));
}

void TestUpdateInstaller::testCreatedDirectoriesRemovedOnRollback() {
  seedInstall(FileMap{{QStringLiteral("app.exe"), "OLD-EXE"}, {QStringLiteral("z.dll"), "OLD-Z"}});
  seedPending(FileMap{{QStringLiteral("app.exe"), "OLD-EXE"},
                      {QStringLiteral("brand/new/dir/x.dll"), "X"},
                      {QStringLiteral("z.dll"), "NEW-Z"}},
              QStringList{QStringLiteral("brand/new/dir/x.dll"), QStringLiteral("z.dll")});

  const auto before = treeHashes(installDir());
  UpdateInstaller::testSetForcedRenameFailurePath(QStringLiteral("z.dll"));

  QCOMPARE(UpdateInstaller::applyPending(installDir()).status,
           UpdateInstaller::ApplyStatus::RolledBack);

  QCOMPARE(treeHashes(installDir()), before);
  QVERIFY2(!QFileInfo::exists(at(QStringLiteral("brand"))),
           "directories created by the transaction must be removed bottom-up on rollback");
}

void TestUpdateInstaller::testRolledBackUpdateStaysRetryable() {
  seedInstall(FileMap{{QStringLiteral("app.exe"), "OLD-EXE"},
                      {QStringLiteral("a.dll"), "OLD-A"},
                      {QStringLiteral("b.dll"), "OLD-B"}});
  seedPending(FileMap{{QStringLiteral("app.exe"), "OLD-EXE"},
                      {QStringLiteral("a.dll"), "NEW-A"},
                      {QStringLiteral("b.dll"), "NEW-B"}},
              QStringList{QStringLiteral("a.dll"), QStringLiteral("b.dll")});

  UpdateInstaller::testSetForcedRenameFailurePath(QStringLiteral("b.dll"));
  QCOMPARE(UpdateInstaller::applyPending(installDir()).status,
           UpdateInstaller::ApplyStatus::RolledBack);

  // The staged tree and pending.json survive so the next quit can retry, and
  // the live journal is gone so applyPending does not just re-enter recovery
  // forever.
  QVERIFY(QFileInfo::exists(stagedAt(QStringLiteral("a.dll"))));
  QVERIFY(QFileInfo::exists(stagedAt(QStringLiteral("b.dll"))));
  QVERIFY(UpdateInstaller::readPending(installDir()).isValid());
  QVERIFY(!QFileInfo::exists(UpdateInstaller::journalPath(installDir())));

  // Retry without the injected failure: it must now succeed.
  UpdateInstaller::testSetForcedRenameFailurePath(QString());
  const auto retry = UpdateInstaller::applyPending(installDir());
  QVERIFY2(retry.isCommitted(), qPrintable(retry.message));
  QCOMPARE(readFileAt(at(QStringLiteral("a.dll"))), QByteArrayLiteral("NEW-A"));
  QCOMPARE(readFileAt(at(QStringLiteral("b.dll"))), QByteArrayLiteral("NEW-B"));
}

// REGRESSION: rollback can only journal `Reverted` AFTER the filesystem work
// returns, so a crash in between replays performReverse() against a
// half-reversed tree. Naively re-running the `Done` path would move the freshly
// RESTORED original into staging, find no backup left to put back, and delete a
// production file outright.
void TestUpdateInstaller::testRollbackReplayAfterCrashMidReverse() {
  seedInstall(FileMap{{QStringLiteral("app.exe"), "OLD-EXE"},
                      {QStringLiteral("a.dll"), "OLD-A"},
                      {QStringLiteral("b.dll"), "OLD-B"}});
  seedPending(FileMap{{QStringLiteral("app.exe"), "OLD-EXE"},
                      {QStringLiteral("a.dll"), "NEW-A"},
                      {QStringLiteral("b.dll"), "NEW-B"}},
              QStringList{QStringLiteral("a.dll"), QStringLiteral("b.dll")});

  const auto before = treeHashes(installDir());

  // Force a failure on b.dll so the transaction rolls back, and arm a kill
  // INSIDE the reverse of a Replace -- AFTER the backup has been restored onto
  // the target but BEFORE `Reverted` is journaled. That is the window where the
  // journal still says `Done` while the restore has already happened.
  UpdateInstaller::testSetForcedRenameFailurePath(QStringLiteral("b.dll"));
  UpdateInstaller::testSetFaultPoint(UpdateInstaller::FaultPoint::RollbackAfterBackupRestored,
                                     -1);
  const auto interrupted = UpdateInstaller::applyPending(installDir());
  QCOMPARE(interrupted.status, UpdateInstaller::ApplyStatus::FaultInjected);
  UpdateInstaller::testClearFaultPoint();
  UpdateInstaller::testSetForcedRenameFailurePath(QString());

  // The restore already landed, so the ORIGINAL is back at the target while the
  // journal still records the op as applied.
  QCOMPARE(readFileAt(at(QStringLiteral("a.dll"))), QByteArrayLiteral("OLD-A"));

  // Recovery replays the reverse from that half-reversed state. The durable
  // phase is already ROLLING_BACK, so recovery MUST resume the rollback --
  // committing forward from here would violate that recorded decision.
  const auto recovered = UpdateInstaller::recoverInterrupted(installDir());
  QCOMPARE(recovered.status, UpdateInstaller::ApplyStatus::RolledBack);

  // The original must come back byte-for-byte, not be swept into staging and
  // lost. This is the exact corruption the backup-existence invariant prevents.
  QVERIFY2(QFileInfo::exists(at(QStringLiteral("a.dll"))),
           "replaying a partially reversed Replace deleted a production file");
  QCOMPARE(treeHashes(installDir()), before);
}

// RECOVERY.txt must name the files this update ADDS: overlaying the backup can
// restore replaced/deleted originals but cannot remove additions, so without
// this list the documented manual recovery leaves a mixed tree.
void TestUpdateInstaller::testRecoveryTextListsAddedPaths() {
  seedInstall(FileMap{{QStringLiteral("app.exe"), "OLD-EXE"},
                      {QStringLiteral("changed.dll"), "OLD"}});
  seedPending(FileMap{{QStringLiteral("app.exe"), "OLD-EXE"},
                      {QStringLiteral("changed.dll"), "NEW"},
                      {QStringLiteral("brand/new.dll"), "FRESH"}},
              QStringList{QStringLiteral("changed.dll"), QStringLiteral("brand/new.dll")});

  QVERIFY(UpdateInstaller::applyPending(installDir()).isCommitted());

  const QFileInfoList stamps =
      QDir(UpdateInstaller::backupRoot(installDir()))
          .entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden);
  QCOMPARE(stamps.size(), 1);
  const QString recovery =
      QString::fromUtf8(readFileAt(stamps.first().absoluteFilePath() +
                                   QStringLiteral("/RECOVERY.txt")));

  QVERIFY(!recovery.isEmpty());
  QVERIFY2(recovery.contains(QStringLiteral("new.dll")),
           qPrintable(QStringLiteral("RECOVERY.txt does not list the added file:\n%1")
                          .arg(recovery)));
  // A merely REPLACED file is restored by the overlay and must not be listed
  // for deletion.
  QVERIFY(!recovery.contains(QStringLiteral("changed.dll")));

  // The removal step must come BEFORE the overlay step, otherwise deleting the
  // additions would destroy what the overlay just restored.
  const int removeAt = recovery.indexOf(QStringLiteral("Delete these paths"));
  const int overlayAt = recovery.indexOf(QStringLiteral("back over the install directory"));
  QVERIFY(removeAt > 0);
  QVERIFY(overlayAt > 0);
  QVERIFY2(removeAt < overlayAt,
           "RECOVERY.txt must tell the user to delete additions BEFORE overlaying the backup");
}

// A file becoming a directory is applied as ConflictRemove(plugins) +
// Add(plugins/real.dll). The backup holds the old `plugins` FILE, so the
// instructions must clear the new `plugins` DIRECTORY before the overlay --
// otherwise the file cannot be copied back over it.
void TestUpdateInstaller::testRecoveryTextHandlesFileToDirectoryTransition() {
  seedInstall(FileMap{{QStringLiteral("app.exe"), "OLD-EXE"},
                      {QStringLiteral("plugins"), "I-AM-A-FILE"}});
  seedPending(FileMap{{QStringLiteral("app.exe"), "OLD-EXE"},
                      {QStringLiteral("plugins/real.dll"), "PLUGIN"}},
              QStringList{QStringLiteral("plugins/real.dll")},
              QStringList(), QStringList{QStringLiteral("plugins")});

  QVERIFY(UpdateInstaller::applyPending(installDir()).isCommitted());

  const QFileInfoList stamps =
      QDir(UpdateInstaller::backupRoot(installDir()))
          .entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden);
  QCOMPARE(stamps.size(), 1);
  const QString backup = stamps.first().absoluteFilePath();
  const QString recovery =
      QString::fromUtf8(readFileAt(backup + QStringLiteral("/RECOVERY.txt")));

  // The backup holds the ORIGINAL file at `plugins`.
  QCOMPARE(readFileAt(backup + QStringLiteral("/plugins")), QByteArrayLiteral("I-AM-A-FILE"));

  // The conflict ROOT must be listed, so deleting it recursively clears the new
  // directory. Listing only the nested addition would leave the directory in
  // place and block the overlay.
  const QString removalBlock = recovery.mid(recovery.indexOf(QStringLiteral("Delete these paths")),
                                            recovery.indexOf(QStringLiteral("3. Copy every file")) -
                                                recovery.indexOf(QStringLiteral("Delete these paths")));
  QVERIFY2(removalBlock.contains(QStringLiteral("plugins")),
           qPrintable(QStringLiteral("conflict root missing from the removal list:\n%1")
                          .arg(recovery)));
  QVERIFY2(!removalBlock.contains(QStringLiteral("plugins\\real.dll")) &&
               !removalBlock.contains(QStringLiteral("plugins/real.dll")),
           qPrintable(QStringLiteral("nested addition should be covered by its root:\n%1")
                          .arg(removalBlock)));
}

// A directory becoming a file is applied as ConflictRemove(legacy) +
// Add(legacy). The backup holds the old `legacy/` DIRECTORY, so the new `legacy`
// FILE must be deleted before the overlay -- and the instructions must not tell
// the user to delete `legacy` AFTER restoring it.
void TestUpdateInstaller::testRecoveryTextHandlesDirectoryToFileTransition() {
  seedInstall(FileMap{{QStringLiteral("app.exe"), "OLD-EXE"},
                      {QStringLiteral("legacy/inner.dll"), "INNER"}});
  seedPending(FileMap{{QStringLiteral("app.exe"), "OLD-EXE"},
                      {QStringLiteral("legacy"), "I-AM-A-FILE-NOW"}},
              QStringList{QStringLiteral("legacy")});

  QVERIFY(UpdateInstaller::applyPending(installDir()).isCommitted());

  const QFileInfoList stamps =
      QDir(UpdateInstaller::backupRoot(installDir()))
          .entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden);
  QCOMPARE(stamps.size(), 1);
  const QString backup = stamps.first().absoluteFilePath();
  const QString recovery =
      QString::fromUtf8(readFileAt(backup + QStringLiteral("/RECOVERY.txt")));

  // The backup holds the ORIGINAL directory.
  QCOMPARE(readFileAt(backup + QStringLiteral("/legacy/inner.dll")), QByteArrayLiteral("INNER"));

  const int removeAt = recovery.indexOf(QStringLiteral("Delete these paths"));
  const int overlayAt = recovery.indexOf(QStringLiteral("3. Copy every file"));
  QVERIFY(removeAt > 0 && overlayAt > removeAt);

  const QString removalBlock = recovery.mid(removeAt, overlayAt - removeAt);
  QVERIFY2(removalBlock.contains(QStringLiteral("legacy")),
           qPrintable(QStringLiteral("the new file must be deleted before the overlay:\n%1")
                          .arg(recovery)));
  // Nothing may instruct a deletion AFTER the overlay: that would destroy the
  // restored directory.
  QVERIFY2(!recovery.mid(overlayAt).contains(QStringLiteral("legacy")),
           qPrintable(QStringLiteral("must not delete '%1' after restoring it:\n%2")
                          .arg(QStringLiteral("legacy"), recovery.mid(overlayAt))));
}

// RECOVERY.txt is the only mitigation for residual risks 1 and 4, so a
// transaction that cannot write it must not start at all.
void TestUpdateInstaller::testAbortsWhenRecoveryTextCannotBeWritten() {
  seedInstall(FileMap{{QStringLiteral("app.exe"), "OLD-EXE"},
                      {QStringLiteral("a.dll"), "OLD-A"}});
  seedPending(FileMap{{QStringLiteral("app.exe"), "OLD-EXE"},
                      {QStringLiteral("a.dll"), "NEW-A"}},
              QStringList{QStringLiteral("a.dll")});

  const auto before = treeHashes(installDir());

  // Occupy the RECOVERY.txt name with a DIRECTORY so the file write cannot
  // succeed. The backup root is created by applyPending, so pre-create the path
  // it will use is not possible; instead make the whole backup root a file,
  // which makes mkpath fail and is the same class of failure.
  const QString backupRoot = UpdateInstaller::backupRoot(installDir());
  QVERIFY(writeFileAt(backupRoot, QByteArrayLiteral("not a directory")));

  const auto result = UpdateInstaller::applyPending(installDir());
  QCOMPARE(result.status, UpdateInstaller::ApplyStatus::AbortedBeforeMutation);
  QCOMPARE(treeHashes(installDir()), before);
  QVERIFY2(!QFileInfo::exists(UpdateInstaller::journalPath(installDir())),
           "aborting before mutation must not create a journal");
}

// ================================================ fault injection + recovery
void TestUpdateInstaller::testRecoveryConvergesAfterFault_data() {
  QTest::addColumn<int>("faultPoint");
  QTest::addColumn<int>("opIndex");

  auto add = [](const char *p_name, UpdateInstaller::FaultPoint p_point, int p_opIndex) {
    QTest::newRow(p_name) << static_cast<int>(p_point) << p_opIndex;
  };

  add("intent-op0-delete", UpdateInstaller::FaultPoint::AfterIntentCommit, 0);
  add("intent-op1-replace", UpdateInstaller::FaultPoint::AfterIntentCommit, 1);
  add("backup-op0-delete", UpdateInstaller::FaultPoint::AfterBackupSyscall, 0);
  add("backup-op1-replace", UpdateInstaller::FaultPoint::AfterBackupSyscall, 1);
  // AfterStagedToTargetMove only exists for ops that actually move a file, so
  // op0 (the deletion) is deliberately not used here.
  add("move-op1-replace", UpdateInstaller::FaultPoint::AfterStagedToTargetMove, 1);
  add("move-op2-replace", UpdateInstaller::FaultPoint::AfterStagedToTargetMove, 2);
  add("done-op0-delete", UpdateInstaller::FaultPoint::AfterDoneCommit, 0);
  add("done-op1-replace", UpdateInstaller::FaultPoint::AfterDoneCommit, 1);
  add("exec-intent", UpdateInstaller::FaultPoint::ExecAfterIntentCommit, -1);
  add("exec-move-aside", UpdateInstaller::FaultPoint::ExecAfterMoveAside, -1);
  add("exec-move-in", UpdateInstaller::FaultPoint::ExecAfterMoveIn, -1);
  add("before-manifest", UpdateInstaller::FaultPoint::BeforeManifestCommit, -1);
  add("after-manifest", UpdateInstaller::FaultPoint::AfterManifestCommit, -1);
}

void TestUpdateInstaller::testRecoveryConvergesAfterFault() {
  QFETCH(int, faultPoint);
  QFETCH(int, opIndex);

  seedInstall(FileMap{{QStringLiteral("app.exe"), "OLD-EXE"},
                      {QStringLiteral("a.dll"), "OLD-A"},
                      {QStringLiteral("b.dll"), "OLD-B"},
                      {QStringLiteral("stale.dll"), "STALE"}});
  seedPending(FileMap{{QStringLiteral("app.exe"), "NEW-EXE"},
                      {QStringLiteral("a.dll"), "NEW-A"},
                      {QStringLiteral("b.dll"), "NEW-B"}},
              QStringList{QStringLiteral("a.dll"), QStringLiteral("b.dll"),
                          QStringLiteral("app.exe")},
              QStringList{QStringLiteral("stale.dll")});

  UpdateInstaller::testSetFaultPoint(static_cast<UpdateInstaller::FaultPoint>(faultPoint),
                                     opIndex);
  const auto interrupted = UpdateInstaller::applyPending(installDir());
  QCOMPARE(interrupted.status, UpdateInstaller::ApplyStatus::FaultInjected);

  // "Reboot": recovery runs as the first action of the next main().
  UpdateInstaller::testClearFaultPoint();
  const auto recovered = UpdateInstaller::recoverInterrupted(installDir());
  QVERIFY2(recovered.isCommitted(), qPrintable(QStringLiteral("%1 (status %2)")
                                                   .arg(recovered.message)
                                                   .arg(static_cast<int>(recovered.status))));

  // Convergence: the tree must be exactly the target, regardless of where the
  // process died.
  QCOMPARE(treeSnapshot(installDir()),
           (QStringList{QStringLiteral("a.dll"), QStringLiteral("app.exe"),
                        QStringLiteral("b.dll"), QStringLiteral("manifest.json")}));
  QCOMPARE(readFileAt(at(QStringLiteral("app.exe"))), QByteArrayLiteral("NEW-EXE"));
  QCOMPARE(readFileAt(at(QStringLiteral("a.dll"))), QByteArrayLiteral("NEW-A"));
  QCOMPARE(readFileAt(at(QStringLiteral("b.dll"))), QByteArrayLiteral("NEW-B"));
  QVERIFY(!QFileInfo::exists(at(QStringLiteral("stale.dll"))));
}

void TestUpdateInstaller::testCorruptJournalIsConservative() {
  seedInstall(FileMap{{QStringLiteral("app.exe"), "OLD-EXE"}, {QStringLiteral("a.dll"), "OLD-A"}});
  seedPending(FileMap{{QStringLiteral("app.exe"), "OLD-EXE"}, {QStringLiteral("a.dll"), "NEW-A"}},
              QStringList{QStringLiteral("a.dll")});

  const auto before = treeHashes(installDir());

  // A journal we cannot parse means we cannot know what was already done. The
  // ONLY safe action is to change nothing and shout.
  QVERIFY(writeFileAt(UpdateInstaller::journalPath(installDir()),
                      QByteArrayLiteral("{ this is not valid json")));

  const auto result = UpdateInstaller::recoverInterrupted(installDir());
  QCOMPARE(result.status, UpdateInstaller::ApplyStatus::RollbackFailed);
  QCOMPARE(treeHashes(installDir()), before);
  QCOMPARE(UpdateInstaller::readResult(installDir()).outcome,
           UpdateInstaller::ResultOutcome::ManualRecovery);

  // applyPending must defer to recovery rather than start a second transaction
  // on top of an unknown one.
  QCOMPARE(UpdateInstaller::applyPending(installDir()).status,
           UpdateInstaller::ApplyStatus::RollbackFailed);
  QCOMPARE(treeHashes(installDir()), before);
}

void TestUpdateInstaller::testRecoveryIsIdempotent() {
  seedInstall(FileMap{{QStringLiteral("app.exe"), "OLD-EXE"}, {QStringLiteral("a.dll"), "OLD-A"}});
  seedPending(FileMap{{QStringLiteral("app.exe"), "NEW-EXE"}, {QStringLiteral("a.dll"), "NEW-A"}},
              QStringList{QStringLiteral("a.dll"), QStringLiteral("app.exe")});

  UpdateInstaller::testSetFaultPoint(UpdateInstaller::FaultPoint::AfterBackupSyscall, 0);
  QCOMPARE(UpdateInstaller::applyPending(installDir()).status,
           UpdateInstaller::ApplyStatus::FaultInjected);
  UpdateInstaller::testClearFaultPoint();

  // A crash DURING recovery must be safe, so recovery is run repeatedly.
  QVERIFY(UpdateInstaller::recoverInterrupted(installDir()).isCommitted());
  const auto afterFirst = treeHashes(installDir());

  for (int i = 0; i < 3; ++i) {
    UpdateInstaller::recoverInterrupted(installDir());
    QCOMPARE(treeHashes(installDir()), afterFirst);
  }

  QCOMPARE(readFileAt(at(QStringLiteral("app.exe"))), QByteArrayLiteral("NEW-EXE"));
  QCOMPARE(readFileAt(at(QStringLiteral("a.dll"))), QByteArrayLiteral("NEW-A"));
}

// ================================ ReplaceExecutable recovery state table

void TestUpdateInstaller::testExecutableRecoveryFromKilledChild_data() {
  QTest::addColumn<QString>("faultName");
  QTest::addColumn<bool>("canonicalMayBeAbsent");

  // Every state boundary of the ReplaceExecutable state machine, exercised by
  // hard-terminating a REAL process rather than simulating the kill in-process.
  QTest::newRow("exec-intent") << QStringLiteral("ExecAfterIntentCommit") << false;
  // The one boundary where the canonical name legitimately does not exist:
  // Windows will not let us unlink a mapped image, so the swap is two renames
  // and this is the gap between them.
  QTest::newRow("exec-move-aside") << QStringLiteral("ExecAfterMoveAside") << true;
  QTest::newRow("exec-move-in") << QStringLiteral("ExecAfterMoveIn") << false;
  QTest::newRow("before-manifest") << QStringLiteral("BeforeManifestCommit") << false;
  QTest::newRow("after-manifest") << QStringLiteral("AfterManifestCommit") << false;
}

void TestUpdateInstaller::testExecutableRecoveryFromKilledChild() {
  QFETCH(QString, faultName);
  QFETCH(bool, canonicalMayBeAbsent);

  seedInstall(FileMap{{QStringLiteral("app.exe"), "OLD-EXE"}});
  seedPending(FileMap{{QStringLiteral("app.exe"), "NEW-EXE"}},
              QStringList{QStringLiteral("app.exe")});

  QProcess *child = runChildApply(faultName);
  QVERIFY2(child->waitForStarted(10000), "child did not start");
  QVERIFY2(child->waitForFinished(30000), "child did not finish");
  QCOMPARE(child->exitCode(), 9);

  // Whatever happened, the canonical executable is either absent (only inside
  // the documented one-syscall move-aside window) or one of the two COMPLETE
  // images -- never truncated, never a half-written file.
  const bool canonicalPresent = QFileInfo::exists(at(QStringLiteral("app.exe")));
  if (!canonicalPresent) {
    QVERIFY2(canonicalMayBeAbsent,
             "the canonical executable disappeared outside the move-aside window");
  } else {
    const QByteArray canonical = readFileAt(at(QStringLiteral("app.exe")));
    QVERIFY2(canonical == QByteArrayLiteral("OLD-EXE") ||
                 canonical == QByteArrayLiteral("NEW-EXE"),
             qPrintable(QStringLiteral("canonical executable is '%1'")
                            .arg(QString::fromUtf8(canonical))));
  }

  const auto recovered = UpdateInstaller::recoverInterrupted(installDir());
  QVERIFY2(recovered.isCommitted(), qPrintable(recovered.message));
  QCOMPARE(readFileAt(at(QStringLiteral("app.exe"))), QByteArrayLiteral("NEW-EXE"));
}

// The accepted window: between "move the running image aside" and "move the new
// image in" the canonical name does not exist. Windows leaves no alternative
// (see UpdateInstaller::probeAtomicRenameSupport), so the contract is that the
// journal plus the retained backup always make it recoverable.
void TestUpdateInstaller::testExecutableRecoveryFromMoveAsideWindow() {
  seedInstall(FileMap{{QStringLiteral("app.exe"), "OLD-EXE"}, {QStringLiteral("a.dll"), "OLD-A"}});
  seedPending(FileMap{{QStringLiteral("app.exe"), "NEW-EXE"}, {QStringLiteral("a.dll"), "NEW-A"}},
              QStringList{QStringLiteral("a.dll"), QStringLiteral("app.exe")});

  UpdateInstaller::testSetFaultPoint(UpdateInstaller::FaultPoint::ExecAfterMoveAside, -1);
  QCOMPARE(UpdateInstaller::applyPending(installDir()).status,
           UpdateInstaller::ApplyStatus::FaultInjected);
  UpdateInstaller::testClearFaultPoint();

  QVERIFY2(!QFileInfo::exists(at(QStringLiteral("app.exe"))),
           "the move-aside window is precisely when the canonical name is vacant");

  // The old image is intact in the backup, which is what makes RECOVERY.txt a
  // real escape hatch rather than a formality.
  const QFileInfoList stamps =
      QDir(UpdateInstaller::backupRoot(installDir()))
          .entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden);
  QCOMPARE(stamps.size(), 1);
  QCOMPARE(readFileAt(stamps.first().absoluteFilePath() + QStringLiteral("/app.exe")),
           QByteArrayLiteral("OLD-EXE"));

  // Recovery resumes forward and closes the window.
  const auto recovered = UpdateInstaller::recoverInterrupted(installDir());
  QVERIFY2(recovered.isCommitted(), qPrintable(recovered.message));
  QCOMPARE(readFileAt(at(QStringLiteral("app.exe"))), QByteArrayLiteral("NEW-EXE"));
}

void TestUpdateInstaller::testExecutableRecoveryWhenBackupIsLost() {
  // Row: canonical == new, backup absent/invalid -> commit only; reverting is
  // impossible and must not be attempted.
  seedInstall(FileMap{{QStringLiteral("app.exe"), "OLD-EXE"}, {QStringLiteral("a.dll"), "OLD-A"}});
  seedPending(FileMap{{QStringLiteral("app.exe"), "NEW-EXE"}, {QStringLiteral("a.dll"), "NEW-A"}},
              QStringList{QStringLiteral("a.dll"), QStringLiteral("app.exe")});

  UpdateInstaller::testSetFaultPoint(UpdateInstaller::FaultPoint::ExecAfterMoveIn, -1);
  QCOMPARE(UpdateInstaller::applyPending(installDir()).status,
           UpdateInstaller::ApplyStatus::FaultInjected);
  UpdateInstaller::testClearFaultPoint();
  QCOMPARE(readFileAt(at(QStringLiteral("app.exe"))), QByteArrayLiteral("NEW-EXE"));

  // Someone deleted the backup between the crash and the next launch.
  const QFileInfoList stamps =
      QDir(UpdateInstaller::backupRoot(installDir()))
          .entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden);
  QCOMPARE(stamps.size(), 1);
  QVERIFY(QFile::remove(stamps.first().absoluteFilePath() + QStringLiteral("/app.exe")));

  const auto recovered = UpdateInstaller::recoverInterrupted(installDir());
  QVERIFY2(recovered.isCommitted(), qPrintable(recovered.message));
  QCOMPARE(readFileAt(at(QStringLiteral("app.exe"))), QByteArrayLiteral("NEW-EXE"));
}

void TestUpdateInstaller::testExecutableRevertsWhenStagedIsLost() {
  // Row: canonical absent, backup == old, staged invalid, phase APPLYING
  //      -> revert from backup.
  seedInstall(FileMap{{QStringLiteral("app.exe"), "OLD-EXE"}, {QStringLiteral("a.dll"), "OLD-A"}});
  seedPending(FileMap{{QStringLiteral("app.exe"), "NEW-EXE"}, {QStringLiteral("a.dll"), "NEW-A"}},
              QStringList{QStringLiteral("a.dll"), QStringLiteral("app.exe")});

  UpdateInstaller::testSetFaultPoint(UpdateInstaller::FaultPoint::ExecAfterMoveAside, -1);
  QCOMPARE(UpdateInstaller::applyPending(installDir()).status,
           UpdateInstaller::ApplyStatus::FaultInjected);
  UpdateInstaller::testClearFaultPoint();

  // The staged executable is destroyed while the canonical name is vacant:
  // going forward is impossible, so recovery must go back.
  QVERIFY(QFile::remove(stagedAt(QStringLiteral("app.exe"))));

  const auto recovered = UpdateInstaller::recoverInterrupted(installDir());
  QVERIFY2(recovered.status == UpdateInstaller::ApplyStatus::RolledBack,
           qPrintable(QStringLiteral("%1 (status %2)")
                          .arg(recovered.message)
                          .arg(static_cast<int>(recovered.status))));
  QCOMPARE(readFileAt(at(QStringLiteral("app.exe"))), QByteArrayLiteral("OLD-EXE"));
  QCOMPARE(readFileAt(at(QStringLiteral("a.dll"))), QByteArrayLiteral("OLD-A"));
}

void TestUpdateInstaller::testExecutableUnrecoverable() {
  // Row: canonical absent AND backup absent/invalid -> unrecoverable. Preserve
  // every artifact, never attempt a generic canonical move.
  seedInstall(FileMap{{QStringLiteral("app.exe"), "OLD-EXE"}, {QStringLiteral("a.dll"), "OLD-A"}});
  seedPending(FileMap{{QStringLiteral("app.exe"), "NEW-EXE"}, {QStringLiteral("a.dll"), "NEW-A"}},
              QStringList{QStringLiteral("a.dll"), QStringLiteral("app.exe")});

  UpdateInstaller::testSetFaultPoint(UpdateInstaller::FaultPoint::ExecAfterMoveAside, -1);
  QCOMPARE(UpdateInstaller::applyPending(installDir()).status,
           UpdateInstaller::ApplyStatus::FaultInjected);
  UpdateInstaller::testClearFaultPoint();

  const QFileInfoList stamps =
      QDir(UpdateInstaller::backupRoot(installDir()))
          .entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden);
  QCOMPARE(stamps.size(), 1);
  QVERIFY(!QFileInfo::exists(at(QStringLiteral("app.exe"))));
  QVERIFY(QFile::remove(stamps.first().absoluteFilePath() + QStringLiteral("/app.exe")));
  QVERIFY(QFile::remove(stagedAt(QStringLiteral("app.exe"))));

  const auto recovered = UpdateInstaller::recoverInterrupted(installDir());
  QCOMPARE(recovered.status, UpdateInstaller::ApplyStatus::RollbackFailed);
  QCOMPARE(UpdateInstaller::readResult(installDir()).outcome,
           UpdateInstaller::ResultOutcome::ManualRecovery);

  // Evidence must be preserved for a human.
  QVERIFY(QFileInfo::exists(UpdateInstaller::journalPath(installDir())));
  QVERIFY(QFileInfo::exists(stamps.first().absoluteFilePath() + QStringLiteral("/RECOVERY.txt")));
}

// ================================================================= backups

void TestUpdateInstaller::testCleanupOldBackupsRemovesTerminalTransactions() {
  seedInstall(FileMap{{QStringLiteral("app.exe"), "OLD-EXE"}});
  seedPending(FileMap{{QStringLiteral("app.exe"), "NEW-EXE"}},
              QStringList{QStringLiteral("app.exe")});
  QVERIFY(UpdateInstaller::applyPending(installDir()).isCommitted());

  QVERIFY(QFileInfo::exists(UpdateInstaller::backupRoot(installDir())));

  // First successful post-update launch reclaims the backup.
  UpdateInstaller::cleanupOldBackups(installDir());
  QVERIFY2(!QFileInfo::exists(UpdateInstaller::backupRoot(installDir())),
           "a committed transaction's backup must be reclaimed on the next launch");

  // Idempotent.
  UpdateInstaller::cleanupOldBackups(installDir());
}

void TestUpdateInstaller::testCleanupOldBackupsKeepsUnfinishedTransactions() {
  seedInstall(FileMap{{QStringLiteral("app.exe"), "OLD-EXE"}, {QStringLiteral("a.dll"), "OLD-A"}});
  seedPending(FileMap{{QStringLiteral("app.exe"), "NEW-EXE"}, {QStringLiteral("a.dll"), "NEW-A"}},
              QStringList{QStringLiteral("a.dll"), QStringLiteral("app.exe")});

  UpdateInstaller::testSetFaultPoint(UpdateInstaller::FaultPoint::AfterBackupSyscall, 0);
  QCOMPARE(UpdateInstaller::applyPending(installDir()).status,
           UpdateInstaller::ApplyStatus::FaultInjected);
  UpdateInstaller::testClearFaultPoint();

  const QString backupRoot = UpdateInstaller::backupRoot(installDir());
  QVERIFY(QFileInfo::exists(backupRoot));

  // Recovery still owns these backups; reclaiming them would destroy the only
  // copy of the originals.
  UpdateInstaller::cleanupOldBackups(installDir());
  QVERIFY2(QFileInfo::exists(backupRoot),
           "backups of a non-terminal transaction must never be reclaimed");
}

} // namespace tests

int main(int argc, char *argv[]) {
  const int childResult = tests::runChildMode(argc, argv);
  if (childResult >= 0) {
    return childResult;
  }

  QCoreApplication app(argc, argv);
  QTEST_SET_MAIN_SOURCE_PATH
  tests::TestUpdateInstaller testObject;
  return QTest::qExec(&testObject, argc, argv);
}

#include "test_updateinstaller.moc"
