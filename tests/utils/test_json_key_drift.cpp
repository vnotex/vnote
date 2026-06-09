// SPDX-License-Identifier: LGPL-3.0-or-later
//
// test_json_key_drift.cpp
//
// Grep-gate regression test for shared-schema JSON key literals.
//
// === What this test does ===
// Scans ${CMAKE_SOURCE_DIR}/src/ recursively for inline string-literal
// occurrences of the JSON keys listed in kGatedLiterals(). FAILS if any are
// found in a file NOT on the kAllowedFiles() list AND not on a line skipped
// by the comment-line filter or the escape-hatch comment.
//
// === Why this test exists ===
// Commit 13680f8d fixed a silent 4-site bug where Qt read "root_path" while
// vxcore wrote "rootFolder". The lift-shared-json-keys plan moved all
// shared-schema constants into the public <vxcore/notebook_json_keys.h>
// header so both sides reference the SAME identifier. This test is the
// mechanical guardrail that keeps it that way.
//
// === Allow-list ===
// Edit kAllowedFiles() below. Each entry MUST have a one-line reason comment
// explaining why the file legitimately uses the literal (typically: the file
// reads/writes a Qt-internal JSON schema with no vxcore counterpart, or it
// is a known refactor follow-up scoped out of the original lift).
//
// Allow-list entries are matched against the file path (relative to src/)
// using endsWith(), so paths use forward slashes and exclude the src/
// prefix (e.g. "core/fileopensettings.h", NOT "src/core/fileopensettings.h").
//
// === Escape hatch ===
// To suppress a single line that legitimately uses a gated literal (e.g., a
// transient migration shim, an error message string that happens to spell
// the key name, or a non-shared schema field that coincidentally shares the
// name), append one of these magic comments to the line:
//
//   // json-key-drift-allow: <reason>
//   // NOLINT
//
// Example:
//   QString legacy = "rootFolder";  // json-key-drift-allow: migration shim
//
// Use the escape hatch for one-off cases; use the allow-list for files that
// systematically own a Qt-internal schema with naming overlap.
//
// === What this test does NOT do ===
// - Does NOT scan libs/, tests/, build*/, third_party/, _deps/, data/.
// - Does NOT gate generic literals like "id"/"name"/"type" (too many false
//   positives across folder/workspace/snippet schemas — drift on those keys
//   needs a different strategy).
// - Comment-only lines (first non-whitespace chars are "//") are skipped
//   wholesale. The regression risk lives in CODE, not in docs that happen
//   to mention the key name.

#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QString>
#include <QStringList>
#include <QTextStream>
#include <QtTest>

namespace tests {

class TestJsonKeyDrift : public QObject {
  Q_OBJECT

private slots:
  void scanSrcForSharedSchemaLiterals();

private:
  static const QStringList &gatedLiterals();
  static const QStringList &allowedFiles();
  static bool isAllowed(const QString &p_relPath);
  static bool isCommentOnly(const QString &p_line);
  static bool hasEscapeHatch(const QString &p_line);
};

// Shared-schema literals that MUST come from <vxcore/notebook_json_keys.h>.
// Generic keys ("id"/"name"/"type"/"metadata"/"tags"/etc.) are NOT gated
// because they have many legitimate non-shared uses across Qt-internal
// schemas (folder, workspace, snippet, search-query, etc.).
const QStringList &TestJsonKeyDrift::gatedLiterals() {
  static const QStringList lits = {
      // Maps to constants in <vxcore/notebook_json_keys.h>:
      QStringLiteral("\"rootFolder\""),      // kJsonKeyRootFolder
      QStringLiteral("\"assetsFolder\""),    // kJsonKeyAssetsFolder
      QStringLiteral("\"tagsModifiedUtc\""), // kJsonKeyTagsModifiedUtc
      QStringLiteral("\"notebookId\""),      // kJsonKeyNotebookId
      QStringLiteral("\"isReadOnly\""),      // kJsonKeyIsReadOnly
      QStringLiteral("\"targetDir\""),       // kJsonKeyTargetDir / kJsonKeyCloneTargetDir
      QStringLiteral("\"readOnly\""),        // kJsonKeyReadOnly (NotebookRecord)
  };
  return lits;
}

// Files that legitimately use one or more gated literals because they
// read/write Qt-internal JSON schemas that do NOT cross the vxcore boundary.
//
// EACH ENTRY MUST CARRY A ONE-LINE REASON. Reviewers should be able to grok
// the allow-list at a glance.
const QStringList &TestJsonKeyDrift::allowedFiles() {
  static const QStringList allow = {
      // core/fileopensettings.h ⇒ FileOpenSettings is a Qt-internal transient
      // open-options struct serialized into hook payloads. Its "readOnly" key
      // is NOT the NotebookRecord.readOnly schema; it just happens to share
      // the name. Same for the entire struct's other keys.
      QStringLiteral("core/fileopensettings.h"),
  };
  return allow;
}

bool TestJsonKeyDrift::isAllowed(const QString &p_relPath) {
  // QDir::relativeFilePath returns forward-slash on Qt by convention.
  for (const QString &allowed : allowedFiles()) {
    if (p_relPath.endsWith(allowed, Qt::CaseInsensitive)) {
      return true;
    }
  }
  return false;
}

bool TestJsonKeyDrift::isCommentOnly(const QString &p_line) {
  // A line that begins with "//" (after leading whitespace) is a single-line
  // C++ comment. Doc-comments naming the key are intentional and safe.
  const QString trimmed = p_line.trimmed();
  return trimmed.startsWith(QStringLiteral("//"));
}

bool TestJsonKeyDrift::hasEscapeHatch(const QString &p_line) {
  // Honor the project-specific magic comment AND the standard clang-tidy
  // NOLINT marker for forward-compatibility with static-analysis suppression.
  return p_line.contains(QStringLiteral("// json-key-drift-allow")) ||
         p_line.contains(QStringLiteral("// NOLINT"));
}

void TestJsonKeyDrift::scanSrcForSharedSchemaLiterals() {
  // VNOTE_SRC_DIR is injected by CMake via target_compile_definitions; see
  // tests/utils/CMakeLists.txt for the registration of this test target.
#ifdef VNOTE_SRC_DIR
  const QString srcRoot = QStringLiteral(VNOTE_SRC_DIR);
#else
  // Fallback for IDE / out-of-build invocation; uses the conventional
  // tests/utils ↔ src/ relationship from the build dir.
  const QString srcRoot = QDir::currentPath() + QStringLiteral("/../../../src");
#endif
  QVERIFY2(QDir(srcRoot).exists(),
           qPrintable(QStringLiteral("src/ root not found: %1").arg(srcRoot)));

  const QDir srcDir(srcRoot);
  QStringList violations;
  int filesScanned = 0;

  QDirIterator it(srcRoot, {QStringLiteral("*.cpp"), QStringLiteral("*.h")}, QDir::Files,
                  QDirIterator::Subdirectories);

  while (it.hasNext()) {
    const QString filePath = it.next();
    ++filesScanned;
    const QString relPath = srcDir.relativeFilePath(filePath);

    if (isAllowed(relPath)) {
      continue;
    }

    QFile f(filePath);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
      // Unreadable file is not a test failure. Skip silently and move on.
      continue;
    }

    QTextStream ts(&f);
    int lineNo = 0;
    while (!ts.atEnd()) {
      ++lineNo;
      const QString line = ts.readLine();

      if (isCommentOnly(line) || hasEscapeHatch(line)) {
        continue;
      }

      for (const QString &lit : gatedLiterals()) {
        if (line.contains(lit)) {
          violations.append(
              QStringLiteral("%1:%2: %3").arg(relPath).arg(lineNo).arg(line.trimmed()));
          // One violation report per line is sufficient — multiple keys on
          // one line would point to the same root cause.
          break;
        }
      }
    }
  }

  if (!violations.isEmpty()) {
    qWarning() << "Found" << violations.size() << "shared-schema literal(s) across" << filesScanned
               << "scanned file(s):";
    for (const QString &v : violations) {
      qWarning().noquote() << "  " << v;
    }
  }

  QVERIFY2(
      violations.isEmpty(),
      qPrintable(QStringLiteral("Found %1 inline shared-schema literal(s) in src/. "
                                "Use the kJsonKey* constants from <vxcore/notebook_json_keys.h> "
                                "(e.g., QLatin1String(vxcore::kJsonKeyRootFolder)). "
                                "If the literal is genuinely Qt-internal (different schema with "
                                "the same key name), add the file to kAllowedFiles() in this test "
                                "with a one-line reason; OR append the comment "
                                "'// json-key-drift-allow: <reason>' to suppress that single line.")
                     .arg(violations.size())));
}

} // namespace tests

QTEST_GUILESS_MAIN(tests::TestJsonKeyDrift)
#include "test_json_key_drift.moc"
