// SPDX-License-Identifier: LGPL-3.0-or-later
//
// test_itemheight_drift.cpp
//
// Grep-gate regression test for delegate-driven item row heights.
//
// === The rule being enforced ===
// A custom item delegate owns the CONTENT height of a row; the active theme owns
// the PADDING around it. Qt applies a QSS `::item { padding: ...; }` box inside
// QStyleSheetStyle::sizeFromContents(CT_ItemViewItem, ...), reachable only via
// QStyledItemDelegate::sizeHint(). A delegate that computes its own height never
// gets there, so the theme's padding is silently dropped: the dock rows drift
// apart, and `native`'s deliberately tighter 2px never applies at all.
//
// === What this test does ===
//  1. Asserts each of the five overriding delegates routes its sizeHint()
//     through vnotex::ItemViewUtils::verticalChrome (src/gui/utils/itemviewutils.h).
//  2. Fails on any surviving hardcoded row-padding member in those delegates'
//     headers, which is how the drift arose in the first place.
//
// === Why a grep gate rather than a behavioural test ===
// Same reason as test_tree_indentation_drift.cpp: constructing the real views
// needs the full model/service graph (NotebookNodeView wants model + proxy +
// controller, the united entry wants a whole service locator). The helper itself
// is covered behaviourally by tests/gui/test_itemviewutils.cpp, and the Qt
// cache-invalidation assumption by tests/gui/test_uniformrowheight_invalidation.cpp.

#include <QDir>
#include <QFile>
#include <QString>
#include <QStringList>
#include <QtTest>

namespace tests {

class TestItemHeightDrift : public QObject {
  Q_OBJECT

private slots:
  void delegatesRouteThroughHelper();
  void noHardcodedRowPaddingMembers();

private:
  static QString readFile(const QString &p_path);
  static QStringList delegateSources();
  static QStringList delegateHeaders();
};

QString TestItemHeightDrift::readFile(const QString &p_path) {
  QFile file(p_path);
  if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
    return QString();
  }
  return QString::fromUtf8(file.readAll());
}

QStringList TestItemHeightDrift::delegateSources() {
  return QStringList{
      QStringLiteral("views/searchresultdelegate.cpp"),
      QStringLiteral("views/notebooknodedelegate.cpp"),
      QStringLiteral("views/filenodedelegate.cpp"),
      QStringLiteral("unitedentry/taskentrydelegate.cpp"),
      QStringLiteral("widgets/styleditemdelegate.cpp"),
  };
}

QStringList TestItemHeightDrift::delegateHeaders() {
  return QStringList{
      QStringLiteral("views/searchresultdelegate.h"),
      QStringLiteral("views/notebooknodedelegate.h"),
      QStringLiteral("views/filenodedelegate.h"),
      QStringLiteral("unitedentry/taskentrydelegate.h"),
      QStringLiteral("widgets/styleditemdelegate.h"),
  };
}

void TestItemHeightDrift::delegatesRouteThroughHelper() {
  const QString srcDir = QStringLiteral(VNOTE_SRC_DIR);
  QVERIFY2(QDir(srcDir).exists(), qPrintable(QStringLiteral("src dir missing: %1").arg(srcDir)));

  for (const QString &relPath : delegateSources()) {
    const QString path = QDir(srcDir).filePath(relPath);
    QVERIFY2(QFile::exists(path), qPrintable(QStringLiteral("missing: %1").arg(path)));
    const QString content = readFile(path);

    // Scope the check to the sizeHint() definition: a delegate that kept the
    // helper only in paint(), a comment, or dead code while restoring hardcoded
    // sizing would otherwise still pass.
    const int start = content.indexOf(QStringLiteral("::sizeHint("));
    QVERIFY2(start >= 0,
             qPrintable(QStringLiteral("%1 no longer defines sizeHint()").arg(relPath)));
    const int end = content.indexOf(QStringLiteral("\n}"), start);
    QVERIFY2(end > start,
             qPrintable(QStringLiteral("%1: could not delimit sizeHint()").arg(relPath)));
    const QString body = content.mid(start, end - start);

    QVERIFY2(body.contains(QStringLiteral("ItemViewUtils::verticalChrome")),
             qPrintable(QStringLiteral("%1::sizeHint() no longer calls "
                                       "ItemViewUtils::verticalChrome; its rows would stop picking "
                                       "up the active theme's ::item padding and drift away from "
                                       "the plain-delegate docks.")
                            .arg(relPath)));
  }
}

void TestItemHeightDrift::noHardcodedRowPaddingMembers() {
  const QString srcDir = QStringLiteral(VNOTE_SRC_DIR);

  // Vertical padding only: horizontal padding is genuinely the delegate's, since
  // it lays the row's columns out itself and CT_ItemViewItem cannot know about
  // them.
  const QStringList banned = {
      QStringLiteral("m_vPadding"),
      QStringLiteral("m_verticalPadding"),
      QStringLiteral("m_rowPadding"),
  };

  QStringList offenders;
  for (const QString &relPath : delegateHeaders()) {
    const QString path = QDir(srcDir).filePath(relPath);
    QVERIFY2(QFile::exists(path), qPrintable(QStringLiteral("missing: %1").arg(path)));
    const QString content = readFile(path);
    const QStringList lines = content.split(QLatin1Char('\n'));
    for (int i = 0; i < lines.size(); ++i) {
      for (const QString &needle : banned) {
        if (lines.at(i).contains(needle)) {
          offenders.append(
              QStringLiteral("%1:%2: %3").arg(relPath).arg(i + 1).arg(lines.at(i).trimmed()));
        }
      }
    }
  }

  QVERIFY2(offenders.isEmpty(),
           qPrintable(QStringLiteral("Hardcoded vertical row padding found in an item delegate. "
                                     "The theme owns the padding: take it from "
                                     "vnotex::ItemViewUtils::verticalChrome() "
                                     "(<gui/utils/itemviewutils.h>) instead.\n%1")
                          .arg(offenders.join(QLatin1Char('\n')))));
}

} // namespace tests

QTEST_GUILESS_MAIN(tests::TestItemHeightDrift)
#include "test_itemheight_drift.moc"
