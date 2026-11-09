#include <QtTest>

#include <commandlineoptions.h>

using namespace vnotex;

namespace tests {
class TestCommandLineOptions : public QObject {
  Q_OBJECT

private slots:
  void modes_data();
  void modes();
  void conflictingModes_data();
  void conflictingModes();
  void filenameBoundary();
};

void TestCommandLineOptions::modes_data() {
  QTest::addColumn<QStringList>("options");
  QTest::addColumn<int>("mode");
  QTest::addColumn<bool>("detached");
  QTest::newRow("unspecified") << QStringList{} << int(ViewWindowMode::Invalid) << false;
  QTest::newRow("ordinary-detached")
      << QStringList{QStringLiteral("--detached-view")} << int(ViewWindowMode::Invalid) << true;
  QTest::newRow("read-detached") << QStringList{QStringLiteral("--read-mode"),
                                                QStringLiteral("--detached-view")}
                                 << int(ViewWindowMode::Read) << true;
  QTest::newRow("edit-main") << QStringList{QStringLiteral("--edit-mode")}
                             << int(ViewWindowMode::Edit) << false;
  QTest::newRow("read-main") << QStringList{QStringLiteral("--read-mode")}
                             << int(ViewWindowMode::Read) << false;
  QTest::newRow("edit-detached") << QStringList{QStringLiteral("--edit-mode"),
                                                QStringLiteral("--detached-view")}
                                 << int(ViewWindowMode::Edit) << true;
}

void TestCommandLineOptions::modes() {
  QFETCH(QStringList, options);
  QFETCH(int, mode);
  QFETCH(bool, detached);
  const QStringList files{QString::fromUtf8("notes/中文 note.md"), QStringLiteral("draft.md")};
  CommandLineOptions parsed;
  QCOMPARE(parsed.parse(QStringList{QStringLiteral("vnote")} + options +
                        QStringList{QStringLiteral("--")} + files),
           CommandLineOptions::Ok);
  QCOMPARE(parsed.m_mode, static_cast<ViewWindowMode>(mode));
  QCOMPARE(parsed.m_detachedView, detached);
  QCOMPARE(parsed.m_pathsToOpen, files);
  QVERIFY(!parsed.m_noSingleInstance);
}

void TestCommandLineOptions::conflictingModes_data() {
  QTest::addColumn<QStringList>("options");
  QTest::newRow("read-then-edit") << QStringList{QStringLiteral("--read-mode"),
                                                 QStringLiteral("--edit-mode")};
  QTest::newRow("edit-then-read-detached")
      << QStringList{QStringLiteral("--edit-mode"), QStringLiteral("--detached-view"),
                     QStringLiteral("--read-mode")};
}

void TestCommandLineOptions::conflictingModes() {
  QFETCH(QStringList, options);
  CommandLineOptions parsed;
  QCOMPARE(parsed.parse(QStringList{QStringLiteral("vnote")} + options +
                        QStringList{QStringLiteral("note.md")}),
           CommandLineOptions::Error);
  QVERIFY(parsed.m_pathsToOpen.isEmpty());
}

void TestCommandLineOptions::filenameBoundary() {
  // Explorer's -- separator must protect option-like filenames, not create a conflict.
  const QStringList files{QStringLiteral("--edit-mode"), QStringLiteral("--detached-view"),
                          QString::fromUtf8("中文 & spaces.md")};
  CommandLineOptions parsed;
  QCOMPARE(parsed.parse(QStringList{QStringLiteral("vnote"), QStringLiteral("--read-mode"),
                                    QStringLiteral("--")} +
                        files),
           CommandLineOptions::Ok);
  QCOMPARE(parsed.m_mode, ViewWindowMode::Read);
  QVERIFY(!parsed.m_detachedView);
  QCOMPARE(parsed.m_pathsToOpen, files);

  CommandLineOptions ordinary;
  QCOMPARE(ordinary.parse(
               {QStringLiteral("vnote"), QStringLiteral("--"), QStringLiteral("--read-mode")}),
           CommandLineOptions::Ok);
  QCOMPARE(ordinary.m_mode, ViewWindowMode::Invalid);
  QCOMPARE(ordinary.m_pathsToOpen, QStringList{QStringLiteral("--read-mode")});
}
} // namespace tests

QTEST_GUILESS_MAIN(tests::TestCommandLineOptions)
#include "test_commandlineoptions.moc"
