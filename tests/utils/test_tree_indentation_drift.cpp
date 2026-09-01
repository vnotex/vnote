// SPDX-License-Identifier: LGPL-3.0-or-later
//
// test_tree_indentation_drift.cpp
//
// Grep-gate regression test for dock/sidebar tree indentation.
//
// === What this test does ===
//  1. Scans every .cpp under ${CMAKE_SOURCE_DIR}/src/ and FAILS on any call to
//     QTreeView::setIndentation(). The shared step lives in exactly one place,
//     vnotex::TreeViewUtils::applyIndentation (src/gui/utils/treeviewutils.h);
//     a literal anywhere else is how the docks drifted apart in the first
//     place.
//  2. Asserts that each of the five known tree call sites actually routes
//     through TreeViewUtils::applyIndentation, so a view cannot quietly go back
//     to the 20px QStyle::PM_TreeViewIndentation default by having its call
//     deleted.
//
// === Why a grep gate rather than a behavioural test ===
// Constructing the real views is disproportionate: NotebookNodeView needs its
// model + proxy + controller (plus a stub TU), TagView needs TagModel ->
// TagService, OutlineView needs OutlineModel -> OutlineProvider, and TaskPanel2
// needs a TitleBar plus a whole service graph. The helper itself is covered
// behaviourally by tests/gui/test_treeviewindentation.cpp.
//
// The scan is scoped to .cpp deliberately: treeviewutils.h is the one place
// setIndentation() is legitimately called.

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QString>
#include <QStringList>
#include <QtTest>

namespace tests {

class TestTreeIndentationDrift : public QObject {
  Q_OBJECT

private slots:
  void scanSrcForRawSetIndentation();
  void knownTreesRouteThroughHelper();

private:
  static QString readFile(const QString &p_path);
};

QString TestTreeIndentationDrift::readFile(const QString &p_path) {
  QFile file(p_path);
  if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
    return QString();
  }
  return QString::fromUtf8(file.readAll());
}

void TestTreeIndentationDrift::scanSrcForRawSetIndentation() {
  const QString srcDir = QStringLiteral(VNOTE_SRC_DIR);
  QVERIFY2(QDir(srcDir).exists(), qPrintable(QStringLiteral("src dir missing: %1").arg(srcDir)));

  QStringList offenders;
  QDirIterator it(srcDir, QStringList() << QStringLiteral("*.cpp"), QDir::Files,
                  QDirIterator::Subdirectories);
  while (it.hasNext()) {
    const QString path = it.next();
    const QString content = readFile(path);
    if (content.isEmpty()) {
      continue;
    }
    const QStringList lines = content.split(QLatin1Char('\n'));
    for (int i = 0; i < lines.size(); ++i) {
      if (lines.at(i).contains(QStringLiteral("setIndentation("))) {
        offenders.append(QStringLiteral("%1:%2: %3")
                             .arg(QDir(srcDir).relativeFilePath(path))
                             .arg(i + 1)
                             .arg(lines.at(i).trimmed()));
      }
    }
  }

  QVERIFY2(offenders.isEmpty(),
           qPrintable(QStringLiteral("Raw setIndentation() call(s) found. Use "
                                     "vnotex::TreeViewUtils::applyIndentation() "
                                     "(<gui/utils/treeviewutils.h>) so every dock tree keeps the "
                                     "same indentation step:\n%1")
                          .arg(offenders.join(QLatin1Char('\n')))));
}

void TestTreeIndentationDrift::knownTreesRouteThroughHelper() {
  const QString srcDir = QStringLiteral(VNOTE_SRC_DIR);
  const QStringList callSites = {
      QStringLiteral("views/notebooknodeview.cpp"), QStringLiteral("views/tagview.cpp"),
      QStringLiteral("views/searchresultview.cpp"), QStringLiteral("views/outlineview.cpp"),
      QStringLiteral("widgets/taskpanel2.cpp"),
  };

  for (const QString &relPath : callSites) {
    const QString path = QDir(srcDir).filePath(relPath);
    QVERIFY2(QFile::exists(path), qPrintable(QStringLiteral("missing: %1").arg(path)));
    const QString content = readFile(path);
    QVERIFY2(content.contains(QStringLiteral("TreeViewUtils::applyIndentation")),
             qPrintable(QStringLiteral("%1 no longer calls TreeViewUtils::applyIndentation; its "
                                       "tree would fall back to the QStyle default and stop "
                                       "lining up with the other docks.")
                            .arg(relPath)));
  }
}

} // namespace tests

QTEST_GUILESS_MAIN(tests::TestTreeIndentationDrift)
#include "test_tree_indentation_drift.moc"
