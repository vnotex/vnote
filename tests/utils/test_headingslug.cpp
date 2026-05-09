// test_headingslug.cpp - Tests for vnotex::HeadingSlugger
#include <QtTest>

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <utils/headingslugger.h>

using namespace vnotex;

namespace tests {

class TestHeadingSlug : public QObject {
  Q_OBJECT

private slots:
  void initTestCase();

  void testSingleSlug_data();
  void testSingleSlug();

  void testDedup_data();
  void testDedup();

  void testReset();

private:
  QJsonObject m_vectors;
};

void TestHeadingSlug::initTestCase() {
  QString path = QFINDTESTDATA("../data/heading_slug_vectors.json");
  QVERIFY2(!path.isEmpty(), "Test vectors JSON not found");

  QFile file(path);
  QVERIFY(file.open(QIODevice::ReadOnly));
  QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
  QVERIFY(doc.isObject());
  m_vectors = doc.object();
  QVERIFY(m_vectors.contains(QStringLiteral("single")));
  QVERIFY(m_vectors.contains(QStringLiteral("dedup")));
}

void TestHeadingSlug::testSingleSlug_data() {
  QTest::addColumn<QString>("input");
  QTest::addColumn<QString>("expected");

  const QJsonArray arr = m_vectors[QStringLiteral("single")].toArray();
  for (const QJsonValue &val : arr) {
    QJsonObject obj = val.toObject();
    QByteArray note = obj[QStringLiteral("note")].toString().toUtf8();
    QString input = obj[QStringLiteral("input")].toString();
    QString expected = obj[QStringLiteral("expected")].toString();
    QTest::newRow(note.constData()) << input << expected;
  }
}

void TestHeadingSlug::testSingleSlug() {
  QFETCH(QString, input);
  QFETCH(QString, expected);

  QCOMPARE(HeadingSlugger::slugify(input), expected);
}

void TestHeadingSlug::testDedup_data() {
  QTest::addColumn<QStringList>("inputs");
  QTest::addColumn<QStringList>("expected");

  const QJsonArray arr = m_vectors[QStringLiteral("dedup")].toArray();
  for (const QJsonValue &val : arr) {
    QJsonObject obj = val.toObject();
    QByteArray note = obj[QStringLiteral("note")].toString().toUtf8();

    QStringList inputs;
    for (const QJsonValue &v : obj[QStringLiteral("inputs")].toArray()) {
      inputs.append(v.toString());
    }

    QStringList expectedList;
    for (const QJsonValue &v : obj[QStringLiteral("expected")].toArray()) {
      expectedList.append(v.toString());
    }

    QTest::newRow(note.constData()) << inputs << expectedList;
  }
}

void TestHeadingSlug::testDedup() {
  QFETCH(QStringList, inputs);
  QFETCH(QStringList, expected);

  HeadingSlugger slugger;
  for (int i = 0; i < inputs.size(); ++i) {
    QCOMPARE(slugger.slug(inputs[i]), expected[i]);
  }
}

void TestHeadingSlug::testReset() {
  HeadingSlugger slugger;

  // Generate a slug to populate state.
  slugger.slug(QStringLiteral("Test"));
  slugger.slug(QStringLiteral("Test"));

  // After reset, dedup counter should restart.
  slugger.reset();
  QCOMPARE(slugger.slug(QStringLiteral("Test")), QStringLiteral("test"));
  QCOMPARE(slugger.slug(QStringLiteral("Test")), QStringLiteral("test-1"));
}

} // namespace tests

QTEST_GUILESS_MAIN(tests::TestHeadingSlug)
#include "test_headingslug.moc"
