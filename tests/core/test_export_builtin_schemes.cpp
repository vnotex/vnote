#include <QtTest>
#include <QJsonObject>
#include <QVector>

#include <export/exportdata.h>

using namespace vnotex;

namespace tests {

class TestExportBuiltinSchemes : public QObject {
  Q_OBJECT

private slots:
  void testBuiltinNamesAndSuffixes();
  void testBuiltinPlatformSeparator();
  void testBuiltinFlaggedBuiltin();
  void testBuiltinNotPersistedThroughJson();
  void testFilterRemovesBuiltins();
  void testUserSchemeSuppressesBuiltin();
};

void TestExportBuiltinSchemes::testBuiltinNamesAndSuffixes() {
  const auto schemes = ExportCustomOption::builtinSchemes();
  QCOMPARE(schemes.size(), 2);

  QCOMPARE(schemes[0].m_name, QStringLiteral("Docx"));
  QCOMPARE(schemes[0].m_targetSuffix, QStringLiteral("docx"));
  QVERIFY(schemes[0].m_useHtmlInput);
  QVERIFY(schemes[0].m_command.contains(QStringLiteral("pandoc")));

  QCOMPARE(schemes[1].m_name, QStringLiteral("PNG"));
  QCOMPARE(schemes[1].m_targetSuffix, QStringLiteral("png"));
  QVERIFY(schemes[1].m_useHtmlInput);
  // %1/%5 are quoted by Exporter::evaluateCommand; the template must not add its own
  // quotes (double-quoting breaks paths with spaces).
  QCOMPARE(schemes[1].m_command, QStringLiteral("wkhtmltoimage %1 %5"));
  QVERIFY(!schemes[1].m_command.contains(QLatin1Char('"')));
}

void TestExportBuiltinSchemes::testBuiltinPlatformSeparator() {
  const auto schemes = ExportCustomOption::builtinSchemes();
#if defined(Q_OS_WIN)
  QCOMPARE(schemes[0].m_resourcePathSeparator, QStringLiteral(";"));
  QVERIFY(schemes[0].m_command.contains(QStringLiteral("--resource-path=.;")));
#else
  QCOMPARE(schemes[0].m_resourcePathSeparator, QStringLiteral(":"));
  QVERIFY(schemes[0].m_command.contains(QStringLiteral("--resource-path=.:")));
#endif
}

void TestExportBuiltinSchemes::testBuiltinFlaggedBuiltin() {
  for (const auto &opt : ExportCustomOption::builtinSchemes()) {
    QVERIFY(opt.m_builtin);
  }
}

void TestExportBuiltinSchemes::testBuiltinNotPersistedThroughJson() {
  // m_builtin must NOT survive a JSON round-trip (it is runtime-only).
  ExportCustomOption docx = ExportCustomOption::builtinSchemes()[0];
  QVERIFY(docx.m_builtin);

  ExportCustomOption restored;
  restored.fromJson(docx.toJson());
  QVERIFY(!restored.m_builtin);
}

void TestExportBuiltinSchemes::testFilterRemovesBuiltins() {
  ExportCustomOption user;
  user.m_name = QStringLiteral("MyScheme");
  user.m_builtin = false;

  // Exercise the production seeding used by ExportDialog2::loadConfig().
  QVector<ExportCustomOption> merged{user};
  ExportCustomOption::seedBuiltins(merged);
  // Built-ins prepended, user scheme retained.
  QCOMPARE(merged.size(), 3);

  // Exercise the production filter used by ExportDialog2::saveConfig().
  const auto persisted = ExportCustomOption::withoutBuiltins(merged);
  QCOMPARE(persisted.size(), 1);
  QCOMPARE(persisted[0].m_name, QStringLiteral("MyScheme"));
  for (const auto &opt : persisted) {
    QVERIFY(!opt.m_builtin);
  }
}

void TestExportBuiltinSchemes::testUserSchemeSuppressesBuiltin() {
  // A user scheme named "Docx" wins; the built-in of the same name is skipped.
  ExportCustomOption user;
  user.m_name = QStringLiteral("Docx");
  user.m_command = QStringLiteral("my-custom-docx %1 %5");
  user.m_builtin = false;

  QVector<ExportCustomOption> merged{user};
  ExportCustomOption::seedBuiltins(merged);

  int docxCount = 0;
  for (const auto &opt : merged) {
    if (opt.m_name == QStringLiteral("Docx")) {
      ++docxCount;
      QVERIFY(!opt.m_builtin);
      QCOMPARE(opt.m_command, QStringLiteral("my-custom-docx %1 %5"));
    }
  }
  QCOMPARE(docxCount, 1);

  // PNG built-in is still present.
  bool hasPng = false;
  for (const auto &opt : merged) {
    if (opt.m_name == QStringLiteral("PNG")) {
      hasPng = true;
      QVERIFY(opt.m_builtin);
    }
  }
  QVERIFY(hasPng);
}

} // namespace tests

QTEST_GUILESS_MAIN(tests::TestExportBuiltinSchemes)
#include "test_export_builtin_schemes.moc"
