// SPDX-License-Identifier: LGPL-3.0-or-later
//
// test_cmark_pin_drift.cpp
//
// Drift gate between the two vendored copies of the cmark fork.
//
// === What this test does ===
// VNote vendors https://github.com/vnotex/cmark.git TWICE, through two
// different submodules:
//
//   libs/vtextedit  ->  libs/cmark
//   libs/vxcore     ->  third_party/cmark
//
// This test runs `git ls-tree HEAD <subPath>` inside each parent submodule,
// reads the RECORDED gitlink SHA of its nested cmark submodule, and FAILS if
// the two SHAs differ.
//
// === Why this test exists ===
// Only ONE of the two copies is ever compiled into VNote. libs/CMakeLists.txt
// adds vtextedit first, and libs/vtextedit/libs/CMakeLists.txt defines the
// `cmark` target unconditionally (no `if(NOT TARGET cmark)` guard). By the
// time vxcore is added, its own guard at
// libs/vxcore/third_party/CMakeLists.txt sees the target already exists and
// skips vxcore's copy entirely. vxcore then LINKS vtextedit's cmark.
//
// So if the two pins ever diverge, VNote silently compiles vxcore's
// content_processor against vtextedit's cmark, while vxcore's own standalone
// build (libs/vxcore/build_test, and the ci-linux-tsan.yml job) compiles it
// against its own. No error, no warning -- the winner is vtextedit's copy,
// because vtextedit is added first AND is the unguarded one. (The two are not
// symmetric: adding vxcore first would not "switch" the winner, it would make
// vtextedit's unguarded add_subdirectory(cmark) collide with the already
// defined target and fail at configure time.) This gate is what turns that
// silent divergence into a red test.
//
// === Contract: committed HEAD, not index, not worktree ===
// The gate compares each CHECKED-OUT parent submodule's *committed* HEAD tree
// -- not its index, and not the nested cmark worktree. In a clean CI checkout
// each parent's HEAD is exactly the commit VNote pins, so committed drift
// cannot slip through. Locally, a gitlink that is staged-but-uncommitted
// inside libs/vtextedit or libs/vxcore still reads as the OLD pin and can
// pass; enforcement is definitive once the parent submodule commits exist and
// CI checks out VNote's gitlinks. Dirty files inside the nested cmark
// checkout are ignored by design -- they are not pins.
//
// === What this test does NOT do ===
// - Does NOT require the nested cmark submodules to be initialized. Only
//   libs/vtextedit and libs/vxcore need checkouts.
// - Does NOT compare checked-out HEADs of the nested cmark worktrees.
// - Does NOT change the build graph, dedupe the vendoring, or verify which
//   copy CMake actually configured.
//
// === Adding another duplicated submodule ===
// Append one row to kPins() below. The comparison is pairwise against the
// first entry, so every entry must pin the same upstream commit.

#include <QByteArray>
#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QtTest>

namespace tests {

namespace {

// One vendored copy of cmark: the parent submodule that records it, and the
// path of the nested cmark gitlink inside that parent.
struct CmarkPin {
  const char *parentDir; // relative to the VNote repo root
  const char *subPath;   // relative to parentDir
};

const QVector<CmarkPin> &kPins() {
  static const QVector<CmarkPin> pins = {
      {"libs/vtextedit", "libs/cmark"},
      {"libs/vxcore", "third_party/cmark"},
  };
  return pins;
}

// `git ls-tree` reports a gitlink (submodule) entry with this file mode.
const char *const kGitlinkMode = "160000";

// Bounded per-process waits. The ctest TIMEOUT for add_qt_test targets is 60s,
// and the worst case here is kPins().size() * (start + finish + reap). Keep the
// total well inside the budget so a hung `git` still reports THIS test's own
// timeout diagnostic instead of being killed by ctest.
const int kGitStartTimeoutMs = 3000;
const int kGitFinishTimeoutMs = 3000;
const int kGitReapTimeoutMs = 1000;

} // namespace

class TestCmarkPinDrift : public QObject {
  Q_OBJECT

private slots:
  void vendoredCmarkPinsAgree();

private:
  static QString repoRoot();
  // Returns the gitlink SHA for p_pin, or an empty string on failure with
  // p_error set to a diagnostic message.
  static QString readGitlinkSha(const QString &p_root, const CmarkPin &p_pin, QString &p_error);
};

QString TestCmarkPinDrift::repoRoot() {
  // VNOTE_REPO_ROOT is injected by CMake via target_compile_definitions; see
  // tests/utils/CMakeLists.txt for the registration of this test target.
#ifdef VNOTE_REPO_ROOT
  return QStringLiteral(VNOTE_REPO_ROOT);
#else
  // Fallback for IDE / out-of-build invocation; uses the conventional
  // build/tests/utils <-> repo-root relationship.
  return QDir::currentPath() + QStringLiteral("/../../..");
#endif
}

QString TestCmarkPinDrift::readGitlinkSha(const QString &p_root, const CmarkPin &p_pin,
                                          QString &p_error) {
  const QString parent = p_root + QLatin1Char('/') + QLatin1String(p_pin.parentDir);
  const QString sub = QLatin1String(p_pin.subPath);

  QStringList args;
  args << QStringLiteral("-C") << parent << QStringLiteral("ls-tree") << QStringLiteral("HEAD")
       << sub;

  QProcess git;
  git.start(QStringLiteral("git"), args);
  if (!git.waitForStarted(kGitStartTimeoutMs)) {
    // The process may still be in Starting state; kill and reap it explicitly
    // rather than leaving teardown to ~QProcess (which blocks unbounded).
    git.kill();
    git.waitForFinished(kGitReapTimeoutMs);
    p_error = QStringLiteral("failed to start `git %1`: %2. Is git on PATH?")
                  .arg(args.join(QLatin1Char(' ')), git.errorString());
    return QString();
  }
  if (!git.waitForFinished(kGitFinishTimeoutMs)) {
    git.kill();
    git.waitForFinished(kGitReapTimeoutMs);
    p_error = QStringLiteral("`git %1` did not finish within %2 ms")
                  .arg(args.join(QLatin1Char(' ')))
                  .arg(kGitFinishTimeoutMs);
    return QString();
  }

  const QString out = QString::fromUtf8(git.readAllStandardOutput()).trimmed();
  const QString err = QString::fromUtf8(git.readAllStandardError()).trimmed();

  if (git.exitStatus() != QProcess::NormalExit || git.exitCode() != 0) {
    p_error = QStringLiteral("`git %1` failed (exit code %2): %3. "
                             "Are the submodules initialized? Run scripts/init.sh "
                             "(scripts\\init.cmd on Windows).")
                  .arg(args.join(QLatin1Char(' ')))
                  .arg(git.exitCode())
                  .arg(err.isEmpty() ? QStringLiteral("<no stderr>") : err);
    return QString();
  }

  // `git ls-tree HEAD <path>` exits 0 with EMPTY stdout when the path is not
  // in the tree. That is the main false-pass risk, hence the explicit check.
  //
  // An UNINITIALIZED submodule also lands here rather than in the non-zero-exit
  // branch above: the parent dir is then an empty directory inside the VNote
  // repo, so git walks up, discovers the VNote repo, resolves the pathspec
  // relative to the cwd prefix, finds nothing, and exits 0. Distinguish the two
  // causes so the remediation does not send anyone off to edit kPins() (which
  // would narrow or disable this gate).
  if (out.isEmpty()) {
    if (!QFileInfo::exists(parent + QStringLiteral("/.git"))) {
      p_error = QStringLiteral("submodule %1 is not initialized (no %1/.git), so `git %2` ran "
                               "against the parent VNote repo and found nothing. "
                               "Run scripts/init.sh (scripts\\init.cmd on Windows).")
                    .arg(QLatin1String(p_pin.parentDir), args.join(QLatin1Char(' ')));
      return QString();
    }
    p_error = QStringLiteral("`git %1` produced no output: '%2' is not present in the HEAD "
                             "tree of %3. Did the submodule move or get removed? "
                             "Update kPins() in this test.")
                  .arg(args.join(QLatin1Char(' ')), sub, QLatin1String(p_pin.parentDir));
    return QString();
  }

  // Expected shape: "160000 commit <sha>\t<path>".
  const QStringList fields =
      out.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
  if (fields.size() < 3) {
    p_error = QStringLiteral("unparsable `git ls-tree` output for %1/%2: '%3'")
                  .arg(QLatin1String(p_pin.parentDir), sub, out);
    return QString();
  }

  if (fields.at(0) != QLatin1String(kGitlinkMode)) {
    p_error = QStringLiteral("%1/%2 is no longer a submodule: expected mode %3 (gitlink), "
                             "got mode %4. Full `git ls-tree` output: '%5'")
                  .arg(QLatin1String(p_pin.parentDir), sub, QLatin1String(kGitlinkMode),
                       fields.at(0), out);
    return QString();
  }

  return fields.at(2);
}

void TestCmarkPinDrift::vendoredCmarkPinsAgree() {
  const QString root = repoRoot();

  // A git worktree (e.g. Kilo Agent Manager worktrees) has a `.git` FILE, not
  // a directory, so this must be QFileInfo::exists() -- QDir::exists() would
  // make the gate silently QSKIP in every worktree.
  if (!QFileInfo::exists(root + QStringLiteral("/.git"))) {
    QSKIP("not a git checkout (source tarball); pin drift cannot be verified");
  }

  QVector<QString> shas;
  shas.reserve(kPins().size());

  for (const CmarkPin &pin : kPins()) {
    QString error;
    const QString sha = readGitlinkSha(root, pin, error);
    if (sha.isEmpty()) {
      QFAIL(qPrintable(QStringLiteral("cmark pin lookup failed: %1").arg(error)));
    }
    shas.append(sha);
  }

  QVERIFY2(shas.size() >= 2, "kPins() must list at least two vendored copies to compare");

  // Compare every entry against the first so a future third copy is covered
  // by the same loop.
  for (int i = 1; i < shas.size(); ++i) {
    if (shas.at(i) == shas.at(0)) {
      continue;
    }

    const CmarkPin &a = kPins().at(0);
    const CmarkPin &b = kPins().at(i);
    // Deliberately NOT a two-argument QCOMPARE: that has no custom-message
    // parameter and would print the two SHAs without the paths or the fix.
    const QString message =
        QStringLiteral(
            "Vendored cmark pins have DRIFTED.\n"
            "  %1/%2 -> %3\n"
            "  %4/%5 -> %6\n"
            "Only ONE of these is compiled into VNote: libs/CMakeLists.txt adds vtextedit "
            "before vxcore, vtextedit defines the `cmark` target unconditionally, and "
            "vxcore's if(NOT TARGET cmark) guard then skips its own copy. So vxcore would "
            "be built against a cmark it was never tested with, while its standalone build "
            "(libs/vxcore/build_test, ci-linux-tsan.yml) uses the other one.\n"
            "Fix: bump BOTH submodules to the SAME cmark commit in one change. Per the root "
            "AGENTS.md 'Submodule Push Discipline', push each submodule remote FIRST, then "
            "the parent vnote repo.")
            .arg(QLatin1String(a.parentDir), QLatin1String(a.subPath), shas.at(0),
                 QLatin1String(b.parentDir), QLatin1String(b.subPath), shas.at(i));
    QFAIL(qPrintable(message));
  }
}

} // namespace tests

QTEST_GUILESS_MAIN(tests::TestCmarkPinDrift)
#include "test_cmark_pin_drift.moc"
