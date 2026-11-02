#include <QtTest>

#include <QDir>
#include <QFile>
#include <QLocale>
#include <QScopeGuard>
#include <QTemporaryDir>

#include <utils/docsutils.h>

using namespace vnotex;

namespace tests {
class TestDocsUtils : public QObject {
  Q_OBJECT

private slots:
  void initTestCase();
  void localizedDocument_data();
  void localizedDocument();
  void explicitApplicationLocale();
  void searchPathPriority();

private:
  QTemporaryDir m_docs;
};

void TestDocsUtils::initTestCase() {
  QVERIFY(m_docs.isValid());
  DocsUtils::addSearchPath(m_docs.path());
}

void TestDocsUtils::localizedDocument_data() {
  QTest::addColumn<QStringList>("uiLanguages");
  QTest::addColumn<QStringList>("availableLocales");
  QTest::addColumn<QString>("expectedLocale");

  const QStringList bilingual{QStringLiteral("zh_CN"), QStringLiteral("en_US")};
  QTest::newRow("chinese-ui-english-region")
      << QStringList{"zh-Hans-CN", "en-US"} << bilingual << QStringLiteral("zh_CN");
  QTest::newRow("english-preferred")
      << QStringList{"en-US", "zh-Hans-CN"} << bilingual << QStringLiteral("en_US");
  QTest::newRow("later-supported-language")
      << QStringList{"fr-FR", "zh-Hans-CN"} << bilingual << QStringLiteral("zh_CN");
  QTest::newRow("exact-script-before-canonical")
      << QStringList{"zh-Hans-CN", "en-US"} << QStringList{"zh_Hans_CN", "zh_CN", "zh", "en_US"}
      << QStringLiteral("zh_Hans_CN");
  QTest::newRow("canonical-before-language")
      << QStringList{"zh-Hans-CN", "en-US"} << QStringList{"zh_CN", "zh", "en_US"}
      << QStringLiteral("zh_CN");
  QTest::newRow("language-before-next-preference")
      << QStringList{"ja-Jpan-JP", "en-US"} << QStringList{"ja", "en_US"} << QStringLiteral("ja");
  QTest::newRow("untranslated-language")
      << QStringList{"ko-KR"} << bilingual << QStringLiteral("en_US");
  QTest::newRow("empty-preferences") << QStringList{} << bilingual << QStringLiteral("en_US");
  QTest::newRow("missing-document") << QStringList{"zh-CN"} << QStringList{} << QString();
}

void TestDocsUtils::localizedDocument() {
  QFETCH(QStringList, uiLanguages);
  QFETCH(QStringList, availableLocales);
  QFETCH(QString, expectedLocale);

  const QLocale previousLocale;
  const auto restore = qScopeGuard([&] { QLocale::setDefault(previousLocale); });
  QLocale::setDefault(QLocale(QStringLiteral("en_US")));
  const auto baseName = QString::fromLatin1(QTest::currentDataTag()) + QStringLiteral(".md");
  const QDir root(m_docs.path());
  for (const auto &locale : availableLocales) {
    QVERIFY(root.mkpath(locale));
    QFile file(root.filePath(locale + QLatin1Char('/') + baseName));
    QVERIFY(file.open(QIODevice::WriteOnly));
    QCOMPARE(file.write(locale.toUtf8()), locale.toUtf8().size());
  }

  const auto path = DocsUtils::getDocFile(baseName, uiLanguages);
  if (expectedLocale.isEmpty()) {
    QVERIFY(path.isEmpty());
  } else {
    QCOMPARE(path, root.filePath(expectedLocale + QLatin1Char('/') + baseName));
    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadOnly));
    QCOMPARE(file.readAll(), expectedLocale.toUtf8());
  }
}

void TestDocsUtils::explicitApplicationLocale() {
  const QLocale previousLocale;
  const auto restore = qScopeGuard([&] {
    QLocale::setDefault(previousLocale);
    DocsUtils::setLocale(QStringLiteral("en_US"));
  });
  const QDir root(m_docs.path());
  const QString baseName = QStringLiteral("explicit.md");
  for (const auto &locale : {QStringLiteral("zh_CN"), QStringLiteral("en_US")}) {
    QVERIFY(root.mkpath(locale));
    QFile file(root.filePath(locale + QLatin1Char('/') + baseName));
    QVERIFY(file.open(QIODevice::WriteOnly));
    QCOMPARE(file.write(locale.toUtf8()), locale.toUtf8().size());
  }

  // The loaded catalog can differ from both the region and its first UI preference.
  QLocale::setDefault(QLocale(QStringLiteral("en_US")));
  DocsUtils::setLocale(QStringLiteral("zh_CN"));
  QCOMPARE(DocsUtils::getDocFile(baseName), root.filePath(QStringLiteral("zh_CN/explicit.md")));
  QCOMPARE(DocsUtils::getDocText(baseName), QStringLiteral("zh_CN"));
  QLocale::setDefault(QLocale(QStringLiteral("zh_CN")));
  DocsUtils::setLocale(QStringLiteral("en_US"));
  QCOMPARE(DocsUtils::getDocFile(baseName), root.filePath(QStringLiteral("en_US/explicit.md")));
  QCOMPARE(DocsUtils::getDocText(baseName), QStringLiteral("en_US"));
}

void TestDocsUtils::searchPathPriority() {
  QTemporaryDir laterDocs;
  QVERIFY(laterDocs.isValid());
  DocsUtils::addSearchPath(laterDocs.path());
  const QDir first(m_docs.path());
  const QDir later(laterDocs.path());
  QVERIFY(first.mkpath(QStringLiteral("en_US")));
  QVERIFY(later.mkpath(QStringLiteral("zh_CN")));
  QFile english(first.filePath(QStringLiteral("en_US/priority.md")));
  QVERIFY(english.open(QIODevice::WriteOnly));
  english.close();
  QFile chinese(later.filePath(QStringLiteral("zh_CN/priority.md")));
  QVERIFY(chinese.open(QIODevice::WriteOnly));
  chinese.close();

  QCOMPARE(DocsUtils::getDocFile(QStringLiteral("priority.md"), {"zh-Hans-CN"}),
           english.fileName());
  QVERIFY(english.remove());
  QCOMPARE(DocsUtils::getDocFile(QStringLiteral("priority.md"), {"zh-Hans-CN"}),
           chinese.fileName());
}
} // namespace tests

QTEST_GUILESS_MAIN(tests::TestDocsUtils)
#include "test_docsutils.moc"
